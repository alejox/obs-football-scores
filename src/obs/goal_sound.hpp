// OBS adapter: plays the goal sound through the scoreboard source's own audio, so it
// shows in the OBS mixer under the source's name and reaches the stream and recordings.
#pragma once

#include "audio/audio_clip.hpp"

#include <obs.h>

#include <cstdint>
#include <memory>
#include <string>

namespace fscore::obs {

enum class GoalSoundMode { None, BuiltIn, File };

GoalSoundMode goal_sound_mode_from_id(const std::string &id);

// Not thread-safe: the scoreboard source calls it under its own lock.
class GoalSound {
public:
	// Loads (or reuses) the clip for these settings. Logs and plays nothing when the
	// file cannot be read.
	void configure(GoalSoundMode mode, const std::string &file, int volume);

	// Starts the sound from the beginning (a goal, or the test button).
	void trigger();

	// Video thread, every tick: hands OBS the samples due in the next ~100 ms.
	void pump(obs_source_t *source);

private:
	GoalSoundMode mode_ = GoalSoundMode::None;
	std::string file_;
	int volume_ = -1;
	std::shared_ptr<const audio::AudioClip> clip_;

	bool playing_ = false;
	uint64_t start_ns_ = 0;   // timestamp of the clip's first frame
	size_t position_ = 0;     // next frame to hand over
	uint64_t timeline_end_ = 0; // end of the last audio handed over, to keep timestamps rising
};

} // namespace fscore::obs
