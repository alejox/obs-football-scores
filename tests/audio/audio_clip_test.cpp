#include "audio/audio_clip.hpp"

#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace fscore::audio;

namespace {

void put16(std::vector<uint8_t> &out, uint16_t v)
{
	out.push_back(v & 0xFF);
	out.push_back(v >> 8);
}

void put32(std::vector<uint8_t> &out, uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		out.push_back((v >> (8 * i)) & 0xFF);
}

// A 16-bit PCM WAV with `frames` frames of a ramp.
std::vector<uint8_t> make_wav(uint16_t channels, uint32_t rate, uint32_t frames)
{
	std::vector<uint8_t> out;
	const uint32_t data_size = frames * channels * 2;
	out.insert(out.end(), {'R', 'I', 'F', 'F'});
	put32(out, 36 + data_size);
	out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
	put32(out, 16);
	put16(out, 1); // PCM
	put16(out, channels);
	put32(out, rate);
	put32(out, rate * channels * 2);
	put16(out, channels * 2);
	put16(out, 16);
	out.insert(out.end(), {'d', 'a', 't', 'a'});
	put32(out, data_size);
	for (uint32_t i = 0; i < frames; ++i) {
		for (uint16_t c = 0; c < channels; ++c)
			put16(out, static_cast<uint16_t>(static_cast<int16_t>(c == 0 ? 16384 : -16384)));
	}
	return out;
}

} // namespace

TEST_CASE(decodes_a_stereo_wav)
{
	const auto clip = decode_clip(make_wav(2, 44100, 441));
	REQUIRE(clip.has_value());
	CHECK_EQ(clip->sample_rate, 44100u);
	CHECK_EQ(clip->frames(), 441u);
	CHECK_NEAR(clip->left[10], 0.5, 0.001);
	CHECK_NEAR(clip->right[10], -0.5, 0.001);
}

TEST_CASE(mono_plays_on_both_sides)
{
	const auto clip = decode_clip(make_wav(1, 48000, 100));
	REQUIRE(clip.has_value());
	CHECK_EQ(clip->frames(), 100u);
	CHECK_NEAR(clip->right[5], clip->left[5], 1e-6);
}

TEST_CASE(long_files_are_cut)
{
	const auto clip = decode_clip(make_wav(1, 8000, 8000 * 30));
	REQUIRE(clip.has_value());
	CHECK_EQ(clip->frames(), static_cast<size_t>(kMaxClipSeconds * 8000));
}

TEST_CASE(rejects_what_is_not_audio)
{
	std::string error;
	CHECK(!decode_clip({}, &error));
	CHECK(!error.empty());
	const std::vector<uint8_t> text = {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};
	CHECK(!decode_clip(text, &error));
	CHECK(!load_clip("/no/such/file.wav", &error));
}

TEST_CASE(gain_and_volume)
{
	auto clip = decode_clip(make_wav(2, 48000, 10));
	REQUIRE(clip.has_value());
	apply_gain(*clip, 0.5f);
	CHECK_NEAR(clip->left[0], 0.25, 0.001);
	apply_gain(*clip, 100.0f);
	CHECK_NEAR(clip->left[0], 1.0, 1e-6); // clamped
	CHECK_NEAR(volume_to_gain(0), 0.0, 1e-9);
	CHECK_NEAR(volume_to_gain(100), 1.0, 1e-9);
	CHECK_NEAR(volume_to_gain(50), 0.125, 1e-6);
	CHECK_NEAR(volume_to_gain(400), 1.0, 1e-9);
}

TEST_CASE(the_built_in_goal_sound_loads)
{
	std::string error;
	const auto clip = load_clip(GOAL_WAV, &error);
	REQUIRE(clip.has_value());
	CHECK_EQ(clip->sample_rate, 48000u);
	CHECK(clip->frames() > 48000u * 3);
	float peak = 0;
	for (float s : clip->left)
		peak = std::max(peak, std::fabs(s));
	CHECK(peak > 0.5f && peak <= 1.0f);
}
