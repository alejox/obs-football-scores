#include "core/replay.hpp"

#include <algorithm>

namespace fscore {

bool replayable(const Play &play, ReplayFilter filter)
{
	switch (play.kind) {
	case PlayKind::Goal:
		return true;
	case PlayKind::ShotOnTarget:
	case PlayKind::ShotOffTarget:
	case PlayKind::ShotBlocked:
	case PlayKind::Woodwork:
		return filter == ReplayFilter::All || filter == ReplayFilter::Shots;
	case PlayKind::YellowCard:
	case PlayKind::RedCard:
		return filter == ReplayFilter::All || filter == ReplayFilter::Cards;
	case PlayKind::Corner:
	case PlayKind::Foul:
	case PlayKind::Offside:
		return filter == ReplayFilter::All;
	case PlayKind::Substitution:
	case PlayKind::Other:
		return false;
	}
	return false;
}

std::vector<int> replay_list(const std::vector<Play> &plays, ReplayFilter filter)
{
	std::vector<int> list;
	for (const Play &play : plays) {
		// Goals belong to every family but cards: a goal is also the shot that scored it.
		if (filter == ReplayFilter::Cards && play.kind == PlayKind::Goal)
			continue;
		if (replayable(play, filter))
			list.push_back(play.sequence);
	}
	return list;
}

int replay_step(const std::vector<Play> &plays, ReplayFilter filter, int current, ReplayStep step)
{
	const std::vector<int> list = replay_list(plays, filter);
	if (step == ReplayStep::Live || list.empty())
		return -1;
	// The plays are in match order; place `current` among them by that order, so a play
	// the filter hides still has a before and an after.
	auto position = [&plays](int sequence) -> long {
		for (size_t i = 0; i < plays.size(); ++i) {
			if (plays[i].sequence == sequence)
				return static_cast<long>(i);
		}
		return -1;
	};
	const long here = current >= 0 ? position(current) : -1;
	switch (step) {
	case ReplayStep::First:
		return list.front();
	case ReplayStep::Repeat:
		return current >= 0 && here >= 0 ? current : list.back();
	case ReplayStep::Previous: {
		if (current < 0 || here < 0)
			return list.back();
		int previous = -1;
		for (const int sequence : list) {
			if (position(sequence) < here)
				previous = sequence;
		}
		return previous >= 0 ? previous : current;
	}
	case ReplayStep::Next:
		if (current < 0 || here < 0)
			return -1;
		for (const int sequence : list) {
			if (position(sequence) > here)
				return sequence;
		}
		return -1;
	case ReplayStep::Live:
		break;
	}
	return -1;
}

const Play *find_play(const std::vector<Play> &plays, int sequence)
{
	for (const Play &play : plays) {
		if (play.sequence == sequence)
			return &play;
	}
	return nullptr;
}

MatchDetails details_until(const MatchDetails &details, int sequence)
{
	MatchDetails cut = details;
	const auto play = std::find_if(cut.plays.begin(), cut.plays.end(),
				       [sequence](const Play &p) { return p.sequence == sequence; });
	if (play == cut.plays.end())
		return cut;
	cut.plays.erase(play + 1, cut.plays.end());
	const auto line = std::find_if(cut.commentary.begin(), cut.commentary.end(),
				       [sequence](const CommentaryLine &l) { return l.sequence == sequence; });
	if (line != cut.commentary.end()) {
		cut.commentary.erase(line + 1, cut.commentary.end());
	} else {
		cut.commentary.erase(std::remove_if(cut.commentary.begin(), cut.commentary.end(),
						    [sequence](const CommentaryLine &l) { return l.sequence > sequence; }),
				     cut.commentary.end());
	}
	return cut;
}

std::optional<int> GoalReplayWatch::update(const std::string &event_id, const std::vector<Play> &plays)
{
	int newest = -1;
	for (const Play &play : plays) {
		if (play.kind == PlayKind::Goal)
			newest = play.sequence;
	}
	if (event_id != event_ || !primed_) {
		event_ = event_id;
		primed_ = !event_id.empty();
		newest_ = newest;
		return std::nullopt;
	}
	if (newest < 0 || newest == newest_)
		return std::nullopt;
	// A summary that lost its last plays (a correction) is not a new goal.
	const bool known = find_play(plays, newest_) != nullptr || newest_ < 0;
	const int before = newest_;
	newest_ = newest;
	if (!known && before >= 0)
		return std::nullopt;
	return newest;
}

ReplayCursor ReplayChannel::get() const
{
	std::lock_guard lock(mutex_);
	return cursor_;
}

void ReplayChannel::show(const std::string &event_id, int sequence)
{
	std::lock_guard lock(mutex_);
	cursor_.event_id = event_id;
	cursor_.sequence = sequence;
	++cursor_.revision;
}

void ReplayChannel::set_options(double speed, bool badge)
{
	std::lock_guard lock(mutex_);
	cursor_.speed = std::clamp(speed, 0.25, 4.0);
	cursor_.badge = badge;
}

ReplayChannel &replay_channel()
{
	static ReplayChannel channel;
	return channel;
}

} // namespace fscore
