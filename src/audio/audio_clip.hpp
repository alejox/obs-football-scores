// Audio adapter: plain C++ (dr_libs), no OBS or Qt. Decodes the goal sounds.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fscore::audio {

// Sounds longer than this are cut: a goal sound is a few seconds.
inline constexpr double kMaxClipSeconds = 20.0;

// Stereo, float samples in [-1, 1], at the file's own sample rate (OBS resamples).
struct AudioClip {
	uint32_t sample_rate = 0;
	std::vector<float> left;
	std::vector<float> right; // a mono file has right == left

	size_t frames() const { return left.size(); }
	bool empty() const { return left.empty() || sample_rate == 0; }
};

// Decodes a WAV, MP3 or FLAC file, picked by its contents rather than its name. Returns
// nullopt (with `error` set) for unreadable or unsupported files. `path` is UTF-8.
std::optional<AudioClip> load_clip(const std::string &path, std::string *error = nullptr);

// Same, from bytes in memory.
std::optional<AudioClip> decode_clip(const std::vector<uint8_t> &bytes, std::string *error = nullptr);

// Multiplies every sample by `gain` (0..1+) and keeps the result in [-1, 1].
void apply_gain(AudioClip &clip, float gain);

// Volume 0-100 as a gain that sounds even across the range (perceptual, roughly cubic).
float volume_to_gain(int volume);

} // namespace fscore::audio
