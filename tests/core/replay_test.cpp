#include "core/replay.hpp"

#include "test_harness.hpp"

using namespace fscore;

namespace {

Play play(int sequence, PlayKind kind)
{
	Play p;
	p.sequence = sequence;
	p.kind = kind;
	p.minute = std::to_string(sequence) + "'";
	return p;
}

// Foul 10, shot 20, substitution 25, goal 30, yellow 40, corner 50.
std::vector<Play> match_plays()
{
	return {play(10, PlayKind::Foul),  play(20, PlayKind::ShotOnTarget), play(25, PlayKind::Substitution),
		play(30, PlayKind::Goal),  play(40, PlayKind::YellowCard),   play(50, PlayKind::Corner)};
}

} // namespace

TEST_CASE(the_filters_pick_their_family)
{
	const auto plays = match_plays();
	CHECK(replay_list(plays, ReplayFilter::All) == (std::vector<int>{10, 20, 30, 40, 50}));
	CHECK(replay_list(plays, ReplayFilter::Goals) == (std::vector<int>{30}));
	CHECK(replay_list(plays, ReplayFilter::Shots) == (std::vector<int>{20, 30}));
	CHECK(replay_list(plays, ReplayFilter::Cards) == (std::vector<int>{40}));
}

TEST_CASE(the_buttons_walk_the_list)
{
	const auto plays = match_plays();
	const ReplayFilter all = ReplayFilter::All;
	CHECK_EQ(replay_step(plays, all, -1, ReplayStep::Previous), 50); // from live: the latest play
	CHECK_EQ(replay_step(plays, all, -1, ReplayStep::Repeat), 50);
	CHECK_EQ(replay_step(plays, all, -1, ReplayStep::Next), -1);
	CHECK_EQ(replay_step(plays, all, -1, ReplayStep::First), 10);
	CHECK_EQ(replay_step(plays, all, 30, ReplayStep::Previous), 20);
	CHECK_EQ(replay_step(plays, all, 30, ReplayStep::Next), 40);
	CHECK_EQ(replay_step(plays, all, 30, ReplayStep::Repeat), 30);
	CHECK_EQ(replay_step(plays, all, 10, ReplayStep::Previous), 10); // stays on the first
	CHECK_EQ(replay_step(plays, all, 50, ReplayStep::Next), -1);     // past the last: live
	CHECK_EQ(replay_step(plays, all, 30, ReplayStep::Live), -1);
}

TEST_CASE(a_play_the_filter_hides_still_has_neighbours)
{
	const auto plays = match_plays();
	// On the yellow card with "Goals": the goal before it, live after it.
	CHECK_EQ(replay_step(plays, ReplayFilter::Goals, 40, ReplayStep::Previous), 30);
	CHECK_EQ(replay_step(plays, ReplayFilter::Goals, 40, ReplayStep::Next), -1);
	CHECK_EQ(replay_step(plays, ReplayFilter::Goals, 10, ReplayStep::Next), 30);
}

TEST_CASE(nothing_to_replay_stays_live)
{
	const std::vector<Play> none = {play(5, PlayKind::Substitution)};
	CHECK_EQ(replay_step(none, ReplayFilter::All, -1, ReplayStep::Previous), -1);
	CHECK_EQ(replay_step(none, ReplayFilter::All, -1, ReplayStep::First), -1);
	CHECK_EQ(replay_step({}, ReplayFilter::Goals, -1, ReplayStep::Repeat), -1);
}

TEST_CASE(the_match_is_cut_right_after_the_play)
{
	MatchDetails details;
	details.plays = match_plays();
	for (const int sequence : {5, 10, 20, 25, 30, 35, 40, 50}) {
		CommentaryLine line;
		line.sequence = sequence;
		details.commentary.push_back(line);
	}
	details.home_name = "Getafe";
	const MatchDetails cut = details_until(details, 30);
	CHECK_EQ(cut.plays.size(), size_t(4));
	CHECK_EQ(cut.plays.back().sequence, 30);
	CHECK_EQ(cut.commentary.size(), size_t(5));
	CHECK_EQ(cut.commentary.back().sequence, 30);
	CHECK_EQ(cut.home_name, std::string("Getafe"));
	// An unknown play leaves the match whole.
	CHECK_EQ(details_until(details, 999).plays.size(), size_t(6));
}

TEST_CASE(a_cut_without_its_commentary_line_keeps_the_earlier_lines)
{
	MatchDetails details;
	details.plays = match_plays();
	for (const int sequence : {5, 15, 35}) {
		CommentaryLine line;
		line.sequence = sequence;
		details.commentary.push_back(line);
	}
	const MatchDetails cut = details_until(details, 20);
	CHECK_EQ(cut.commentary.size(), size_t(2));
}

TEST_CASE(a_new_goal_is_spotted_once)
{
	GoalReplayWatch watch;
	std::vector<Play> plays = match_plays();
	CHECK(!watch.update("m1", plays)); // learns the goal at 30
	CHECK(!watch.update("m1", plays));
	plays.push_back(play(60, PlayKind::ShotOffTarget));
	CHECK(!watch.update("m1", plays));
	plays.push_back(play(70, PlayKind::Goal));
	const auto goal = watch.update("m1", plays);
	CHECK(goal && *goal == 70);
	CHECK(!watch.update("m1", plays));
	// Another match: learns again, no replay of its old goals.
	CHECK(!watch.update("m2", plays));
}

TEST_CASE(the_first_goal_of_a_match_is_spotted)
{
	GoalReplayWatch watch;
	std::vector<Play> plays = {play(10, PlayKind::Foul)};
	CHECK(!watch.update("m1", plays));
	plays.push_back(play(20, PlayKind::Goal));
	const auto goal = watch.update("m1", plays);
	CHECK(goal && *goal == 20);
}

TEST_CASE(the_channel_bumps_the_revision_on_every_press)
{
	ReplayChannel channel;
	channel.show("m1", 30);
	const ReplayCursor a = channel.get();
	CHECK(a.replaying("m1"));
	CHECK(!a.replaying("m2"));
	channel.show("m1", 30); // "repeat"
	const ReplayCursor b = channel.get();
	CHECK(b.revision > a.revision);
	channel.set_options(2.0, false);
	const ReplayCursor c = channel.get();
	CHECK_EQ(c.revision, b.revision);
	CHECK_EQ(c.speed, 2.0);
	CHECK(!c.badge);
	channel.show("m1", -1);
	CHECK(!channel.get().replaying("m1"));
}
