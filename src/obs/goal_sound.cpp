#include "obs/goal_sound.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <algorithm>

namespace fscore::obs {
namespace {

// How far ahead of real time the samples are handed to OBS.
constexpr uint64_t kLookaheadNs = 100'000'000;

std::string built_in_path()
{
	char *path = obs_module_file("sounds/goal.wav");
	const std::string result = path ? path : "";
	bfree(path);
	return result;
}

} // namespace

GoalSoundMode goal_sound_mode_from_id(const std::string &id)
{
	if (id == "none")
		return GoalSoundMode::None;
	if (id == "file")
		return GoalSoundMode::File;
	return GoalSoundMode::BuiltIn;
}

void GoalSound::configure(GoalSoundMode mode, const std::string &file, int volume)
{
	if (mode == mode_ && volume == volume_ && (mode != GoalSoundMode::File || file == file_))
		return;
	mode_ = mode;
	file_ = file;
	volume_ = volume;
	clip_.reset();
	playing_ = false;

	if (mode == GoalSoundMode::None)
		return;
	const std::string path = mode == GoalSoundMode::BuiltIn ? built_in_path() : file;
	if (path.empty()) {
		if (mode == GoalSoundMode::BuiltIn)
			obs_log(LOG_WARNING, "built-in goal sound not found");
		return;
	}
	std::string error;
	auto clip = audio::load_clip(path, &error);
	if (!clip) {
		obs_log(LOG_WARNING, "goal sound %s not loaded: %s", path.c_str(), error.c_str());
		return;
	}
	audio::apply_gain(*clip, audio::volume_to_gain(volume));
	clip_ = std::make_shared<const audio::AudioClip>(std::move(*clip));
}

void GoalSound::trigger()
{
	if (!clip_ || clip_->empty())
		return;
	// Timestamps must keep rising: a replay while the sound plays starts right after it.
	start_ns_ = std::max(os_gettime_ns(), timeline_end_);
	position_ = 0;
	playing_ = true;
}

void GoalSound::pump(obs_source_t *source)
{
	if (!playing_ || !clip_)
		return;
	const audio::AudioClip &clip = *clip_;
	const uint64_t now = os_gettime_ns();
	const uint64_t horizon = now + kLookaheadNs;
	if (horizon <= start_ns_)
		return;
	const size_t due = static_cast<size_t>((horizon - start_ns_) * clip.sample_rate / 1'000'000'000ull);
	const size_t end = std::min(due, clip.frames());
	if (end <= position_) {
		playing_ = position_ < clip.frames();
		return;
	}

	obs_source_audio audio = {};
	audio.data[0] = reinterpret_cast<const uint8_t *>(clip.left.data() + position_);
	audio.data[1] = reinterpret_cast<const uint8_t *>(clip.right.data() + position_);
	audio.frames = static_cast<uint32_t>(end - position_);
	audio.speakers = SPEAKERS_STEREO;
	audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
	audio.samples_per_sec = clip.sample_rate;
	audio.timestamp = start_ns_ + position_ * 1'000'000'000ull / clip.sample_rate;
	obs_source_output_audio(source, &audio);

	position_ = end;
	timeline_end_ = start_ns_ + position_ * 1'000'000'000ull / clip.sample_rate;
	if (position_ >= clip.frames())
		playing_ = false;
}

} // namespace fscore::obs
