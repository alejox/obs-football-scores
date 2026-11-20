#include "audio/audio_clip.hpp"

#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#include <filesystem>
#endif

namespace fscore::audio {
namespace {

// The longest file read into memory: a 20 s WAV at 96 kHz stereo 32-bit is ~15 MB.
constexpr size_t kMaxFileBytes = 64 * 1024 * 1024;

void set_error(std::string *error, const char *text)
{
	if (error)
		*error = text;
}

// Interleaved float frames -> AudioClip (first two channels; mono duplicated).
std::optional<AudioClip> from_interleaved(const float *samples, uint64_t frames, unsigned channels, uint32_t rate,
					  std::string *error)
{
	if (!samples || frames == 0 || channels == 0 || rate == 0) {
		set_error(error, "empty or unreadable audio");
		return std::nullopt;
	}
	const uint64_t max_frames = static_cast<uint64_t>(kMaxClipSeconds * rate);
	frames = std::min(frames, max_frames);
	AudioClip clip;
	clip.sample_rate = rate;
	clip.left.resize(frames);
	clip.right.resize(frames);
	for (uint64_t i = 0; i < frames; ++i) {
		const float l = samples[i * channels];
		const float r = channels > 1 ? samples[i * channels + 1] : l;
		clip.left[i] = std::clamp(l, -1.0f, 1.0f);
		clip.right[i] = std::clamp(r, -1.0f, 1.0f);
	}
	return clip;
}

bool starts_with(const std::vector<uint8_t> &bytes, const char *magic, size_t offset = 0)
{
	const size_t n = std::strlen(magic);
	return bytes.size() >= offset + n && std::memcmp(bytes.data() + offset, magic, n) == 0;
}

} // namespace

std::optional<AudioClip> decode_clip(const std::vector<uint8_t> &bytes, std::string *error)
{
	if (bytes.empty()) {
		set_error(error, "empty file");
		return std::nullopt;
	}
	unsigned channels = 0;
	uint32_t rate = 0;

	if (starts_with(bytes, "RIFF") || starts_with(bytes, "RF64") || starts_with(bytes, "riff")) {
		unsigned int ch = 0, sr = 0;
		drwav_uint64 frames = 0;
		float *data = drwav_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &ch, &sr, &frames,
									nullptr);
		auto clip = from_interleaved(data, frames, ch, sr, error);
		drwav_free(data, nullptr);
		return clip;
	}
	if (starts_with(bytes, "fLaC")) {
		unsigned int ch = 0, sr = 0;
		drflac_uint64 frames = 0;
		float *data =
			drflac_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &ch, &sr, &frames, nullptr);
		auto clip = from_interleaved(data, frames, ch, sr, error);
		drflac_free(data, nullptr);
		return clip;
	}
	// MP3: an ID3 tag or a frame sync. Tried last because it has no fixed magic.
	drmp3_config config{};
	drmp3_uint64 frames = 0;
	float *data = drmp3_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &config, &frames, nullptr);
	if (data) {
		channels = config.channels;
		rate = config.sampleRate;
		auto clip = from_interleaved(data, frames, channels, rate, error);
		drmp3_free(data, nullptr);
		return clip;
	}
	set_error(error, "not a WAV, MP3 or FLAC file");
	return std::nullopt;
}

std::optional<AudioClip> load_clip(const std::string &path, std::string *error)
{
#ifdef _WIN32
	// The path is UTF-8; std::filesystem::u8path keeps non-ASCII names working on Windows.
	std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
#else
	std::ifstream file(path, std::ios::binary);
#endif
	if (!file) {
		set_error(error, "cannot open the file");
		return std::nullopt;
	}
	file.seekg(0, std::ios::end);
	const std::streamoff size = file.tellg();
	if (size <= 0 || static_cast<uint64_t>(size) > kMaxFileBytes) {
		set_error(error, size <= 0 ? "empty file" : "file too large");
		return std::nullopt;
	}
	file.seekg(0);
	std::vector<uint8_t> bytes(static_cast<size_t>(size));
	file.read(reinterpret_cast<char *>(bytes.data()), size);
	if (!file) {
		set_error(error, "cannot read the file");
		return std::nullopt;
	}
	return decode_clip(bytes, error);
}

void apply_gain(AudioClip &clip, float gain)
{
	gain = std::max(gain, 0.0f);
	for (std::vector<float> *channel : {&clip.left, &clip.right}) {
		for (float &sample : *channel)
			sample = std::clamp(sample * gain, -1.0f, 1.0f);
	}
}

float volume_to_gain(int volume)
{
	const float v = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f;
	return v * v * v;
}

} // namespace fscore::audio
