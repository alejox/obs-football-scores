#include "core/match_details.hpp"
#include "core/pitch.hpp"
#include "core/standings.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <set>
#include <string>

using namespace fscore;

TEST_CASE(parses_a_real_summary)
{
	const auto details = parse_summary(test_harness::read_fixture("esp1_summary.json"));
	REQUIRE(details.has_value());
	CHECK_EQ(details->home_name, std::string("Atlético Madrid"));
	CHECK_EQ(details->away_name, std::string("Barcelona"));
	CHECK_EQ(details->home_stats.at("possessionPct"), 37.0);
	CHECK_EQ(details->home_stats.at("totalShots"), 9.0);
	CHECK_EQ(details->home_lineup.formation, std::string("4-4-2"));
	CHECK_EQ(details->home_lineup.starters.size(), 11u);
	CHECK(!details->home_lineup.substitutes.empty());
	CHECK_EQ(details->home_lineup.starters[0].name, std::string("Jan Oblak"));
	CHECK_EQ(details->home_lineup.starters[0].position, std::string("G"));
	CHECK_EQ(details->away_lineup.starters.size(), 11u);

	int goals = 0, positioned = 0, home_plays = 0;
	for (const Play &play : details->plays) {
		goals += play.kind == PlayKind::Goal;
		positioned += play.has_position;
		home_plays += play.side && *play.side == TeamSide::Home;
	}
	CHECK_EQ(goals, 3);
	CHECK(positioned >= 90);
	CHECK(home_plays > 10);
	const Play *shot = nullptr;
	for (const Play &play : details->plays) {
		if (play.kind == PlayKind::ShotOffTarget) {
			shot = &play;
			break;
		}
	}
	REQUIRE(shot != nullptr);
	const Play &first_shot = *shot;
	CHECK(first_shot.kind == PlayKind::ShotOffTarget);
	CHECK(first_shot.side.has_value() && *first_shot.side == TeamSide::Home);
	CHECK_EQ(first_shot.minute, std::string("3'"));
	CHECK(first_shot.has_position);
}

TEST_CASE(rejects_what_is_not_a_summary)
{
	CHECK(!parse_summary(""));
	CHECK(!parse_summary("{\"code\":404}"));
	const auto bare = parse_summary("{\"header\":{}}");
	REQUIRE(bare.has_value());
	CHECK(bare->plays.empty());
	CHECK(bare->home_lineup.empty());
}

TEST_CASE(lineups_land_in_their_own_half_in_formation)
{
	const auto details = parse_summary(test_harness::read_fixture("esp1_summary.json"));
	REQUIRE(details.has_value());
	const auto home = place_lineup(details->home_lineup, TeamSide::Home);
	const auto away = place_lineup(details->away_lineup, TeamSide::Away);
	REQUIRE(home.size() == 11u);
	REQUIRE(away.size() == 11u);
	for (const auto &p : home)
		CHECK(p.at.x < 0.5);
	for (const auto &p : away)
		CHECK(p.at.x > 0.5);
	// Oblak in goal; the left back (LB) above the right back (RB) for the home side.
	CHECK_EQ(home[0].player.name, std::string("Jan Oblak"));
	CHECK_NEAR(home[0].at.x, 0.05, 1e-9);
	double lb = -1, rb = -1;
	std::set<double> lines;
	for (const auto &p : home) {
		if (p.player.position == "LB")
			lb = p.at.y;
		if (p.player.position == "RB")
			rb = p.at.y;
		lines.insert(p.at.x);
	}
	CHECK(lb >= 0 && rb >= 0 && lb < rb);
	CHECK_EQ(lines.size(), 4u); // keeper + 4-4-2
}

TEST_CASE(a_lineup_without_formation_groups_by_role)
{
	Lineup lineup;
	for (const char *pos : {"G", "CD", "CD", "LB", "RB", "CM", "CM", "AM", "LW", "RW", "F"}) {
		LineupPlayer p;
		p.name = pos;
		p.position = pos;
		lineup.starters.push_back(p);
	}
	const auto placed = place_lineup(lineup, TeamSide::Home);
	REQUIRE(placed.size() == 11u);
	std::set<double> lines;
	for (const auto &p : placed)
		lines.insert(p.at.x);
	CHECK_EQ(lines.size(), 4u); // keeper, defence, midfield, attack
}

TEST_CASE(plays_turn_around_for_the_away_side)
{
	Play play;
	play.x = 90;
	play.y = 30;
	play.side = TeamSide::Home;
	CHECK_NEAR(play_point(play).x, 0.9, 1e-9);
	CHECK_NEAR(play_point(play).y, 0.3, 1e-9);
	play.side = TeamSide::Away;
	CHECK_NEAR(play_point(play).x, 0.1, 1e-9);
	CHECK_NEAR(play_point(play).y, 0.7, 1e-9);
}

TEST_CASE(pressure_follows_the_recent_chances)
{
	auto make = [](PlayKind kind, TeamSide side, double seconds) {
		Play p;
		p.kind = kind;
		p.side = side;
		p.clock_seconds = seconds;
		return p;
	};
	CHECK_NEAR(pressure({}), 0.0, 1e-9);
	const std::vector<Play> home_push = {make(PlayKind::ShotOnTarget, TeamSide::Home, 1000),
					     make(PlayKind::Corner, TeamSide::Home, 1060),
					     make(PlayKind::ShotOffTarget, TeamSide::Home, 1100)};
	CHECK(pressure(home_push) > 0.5);
	// Old chances fade: the same away push 20 minutes ago counts for nothing.
	std::vector<Play> mixed = home_push;
	mixed.push_back(make(PlayKind::Goal, TeamSide::Away, -100));
	CHECK_NEAR(pressure(mixed, 1100), pressure(home_push, 1100), 1e-9);
	mixed.push_back(make(PlayKind::Goal, TeamSide::Away, 1090));
	CHECK(pressure(mixed, 1100) < pressure(home_push, 1100));
	// Fouls and substitutions do not move it.
	CHECK_NEAR(pressure({make(PlayKind::Foul, TeamSide::Home, 10)}), 0.0, 1e-9);
}

TEST_CASE(parses_standings_and_groups)
{
	const auto liga = parse_standings(test_harness::read_fixture("esp1_standings.json"));
	REQUIRE(liga.has_value());
	REQUIRE(liga->size() == 1u);
	const StandingsGroup &table = liga->front();
	REQUIRE(table.rows.size() == 20u);
	CHECK_EQ(table.rows[0].rank, 1);
	CHECK_EQ(table.rows[0].name, std::string("Barcelona"));
	CHECK_EQ(table.rows[0].points, 36);
	CHECK_EQ(table.rows[0].played, 13);
	CHECK_EQ(table.rows[0].goal_difference, 36);
	CHECK(table.rows[0].zone_color.has_value());
	CHECK(!table.rows[0].logo_url.empty());

	const auto world = parse_standings(test_harness::read_fixture("world_standings.json"));
	REQUIRE(world.has_value());
	REQUIRE(world->size() == 3u);
	CHECK_EQ(world->at(0).name, std::string("Group A"));
	const std::string team_in_b = world->at(1).rows.at(2).team_id;
	CHECK(group_for_teams(*world, "nope", team_in_b) == &world->at(1));
	CHECK(group_for_teams(*world, "x", "y") == &world->at(0));
	CHECK(!parse_standings("not json"));
}

TEST_CASE(the_table_window_keeps_the_match_teams)
{
	const auto liga = parse_standings(test_harness::read_fixture("esp1_standings.json"));
	REQUIRE(liga.has_value());
	const StandingsGroup &table = liga->front();
	CHECK_EQ(visible_rows(table, 0, "", "").size(), 20u);
	const auto top = visible_rows(table, 8, table.rows[1].team_id, "");
	REQUIRE(top.size() == 8u);
	CHECK_EQ(top[0].rank, 1);
	const auto bottom = visible_rows(table, 8, table.rows[18].team_id, table.rows[16].team_id);
	REQUIRE(bottom.size() == 8u);
	bool has18 = false, has16 = false;
	for (const auto &row : bottom) {
		has18 |= row.team_id == table.rows[18].team_id;
		has16 |= row.team_id == table.rows[16].team_id;
	}
	CHECK(has18 && has16);
	// Second and eleventh with ten rows: both stay in (the Brighton / Hull case).
	const auto both = visible_rows(table, 10, table.rows[1].team_id, table.rows[10].team_id);
	REQUIRE(both.size() == 10u);
	CHECK_EQ(both.front().rank, 2);
	CHECK_EQ(both.back().rank, 11);
	// Fourth and thirteenth in twelve rows: both, plus one row of context below.
	const auto context = visible_rows(table, 12, table.rows[3].team_id, table.rows[12].team_id);
	REQUIRE(context.size() == 12u);
	CHECK_EQ(context.front().rank, 3);
	CHECK_EQ(context.back().rank, 14);
}

TEST_CASE(the_tracker_keeps_every_shot_and_the_latest_plays)
{
	const auto details = parse_summary(test_harness::read_fixture("esp1_summary.json"));
	REQUIRE(details.has_value());
	const auto tracked = tracked_plays(details->plays, 6);
	int shots = 0, total_shots = 0;
	for (const Play &play : details->plays)
		total_shots += play.has_position && is_shot(play.kind);
	for (const TrackedPlay &t : tracked) {
		CHECK(t.play->has_position);
		shots += is_shot(t.play->kind);
		if (!is_shot(t.play->kind))
			CHECK(t.recency < 6);
	}
	CHECK_EQ(shots, total_shots);
	REQUIRE(!tracked.empty());
	CHECK_EQ(tracked.front().recency, 0);
	CHECK(tracked.front().play == &*std::find_if(details->plays.rbegin(), details->plays.rend(),
						     [](const Play &p) { return p.has_position; }));
	CHECK(tracked_plays({}, 6).empty());
}

TEST_CASE(momentum_rises_with_each_side_chances)
{
	auto make = [](PlayKind kind, TeamSide side, double minute) {
		Play p;
		p.kind = kind;
		p.side = side;
		p.clock_seconds = minute * 60;
		return p;
	};
	const auto flat = momentum_series({}, 90);
	REQUIRE(flat.size() == 91u);
	for (double v : flat)
		CHECK_NEAR(v, 0.0, 1e-12);
	const auto series = momentum_series({make(PlayKind::Goal, TeamSide::Home, 10), make(PlayKind::ShotOnTarget, TeamSide::Away, 60)}, 90);
	CHECK(series[10] > 0.5);
	CHECK(series[60] < -0.3);
	CHECK_NEAR(series[35], 0.0, 1e-9);
	// Added time stretches the series.
	CHECK_EQ(momentum_series({make(PlayKind::Corner, TeamSide::Home, 95)}, 90).size(), 96u);
	// Real match: Atlético scored first (10'), Barcelona around 45' and 66'.
	const auto details = parse_summary(test_harness::read_fixture("esp1_summary.json"));
	REQUIRE(details.has_value());
	const auto real = momentum_series(details->plays);
	CHECK(real[10] > 0);
	CHECK(real[66] < 0);
}

TEST_CASE(summary_extras_form_meetings_venue_and_commentary)
{
	const auto details = parse_summary(test_harness::read_fixture("esp1_summary.json"));
	REQUIRE(details.has_value());
	CHECK_EQ(details->home_form.size(), 5u);
	CHECK_EQ(details->away_form.size(), 5u);
	for (const FormGame &g : details->home_form)
		CHECK(g.result == 'W' || g.result == 'D' || g.result == 'L');
	CHECK(!details->home_form[0].opponent.empty());
	CHECK_EQ(details->head_to_head, std::string("BAR leads series 3-2"));
	CHECK(!details->meetings.empty());
	CHECK_EQ(details->venue, std::string("Riyadh Air Metropolitano"));
	CHECK(details->attendance > 0);
	REQUIRE(details->commentary.size() > details->plays.size());
	CHECK_EQ(details->commentary.front().text, std::string("Lineups are announced and players are warming up."));
	CHECK(details->commentary.front().minute.empty());
}

TEST_CASE(ball_route_joins_the_last_positioned_plays_oldest_first)
{
	std::vector<Play> plays(4);
	plays[0].sequence = 1; plays[0].has_position = true; plays[0].x = 0.1; plays[0].y = 0.2; plays[0].side = TeamSide::Home;
	plays[1].sequence = 2; // no position: skipped
	plays[2].sequence = 3; plays[2].has_position = true; plays[2].x = 0.5; plays[2].y = 0.5; plays[2].side = TeamSide::Home;
	plays[3].sequence = 4; plays[3].has_position = true; plays[3].x = 0.9; plays[3].y = 0.4; plays[3].side = TeamSide::Home;
	const BallRoute all = ball_route(plays);
	CHECK_EQ(all.points.size(), size_t{3});
	CHECK_EQ(all.latest_sequence, 4);
	CHECK(all.points.front().x == play_point(plays[0]).x);
	CHECK(all.points.back().x == play_point(plays[3]).x);
	const BallRoute two = ball_route(plays, 2);
	CHECK_EQ(two.points.size(), size_t{2});
	CHECK(two.points.front().x == play_point(plays[2]).x);
	CHECK_EQ(ball_route({}).latest_sequence, -1);
}
