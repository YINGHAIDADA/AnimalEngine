// Audio decoding and mixing without a device (ADR-0033).

#include <animal/audio/audio.h>
#include <animal/core/file.h>

#include "test_harness.h"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_timer.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace animal;
using namespace animal::audio;
namespace fs = std::filesystem;

std::error_code cleanupError;

// A 16-bit PCM WAV file holding `frames` copies of `value` (mono).
std::vector<std::byte> wav(int rate, size_t frames, int16_t value) {
    const uint32_t dataBytes = static_cast<uint32_t>(frames * 2);
    std::vector<std::byte> out(44 + dataBytes);
    auto put = [&out](size_t at, const void* p, size_t n) { std::memcpy(out.data() + at, p, n); };
    const uint32_t riff = 36 + dataBytes;
    const uint32_t fmtSize = 16;
    const uint16_t pcm = 1;
    const uint16_t channels = 1;
    const auto sampleRate = static_cast<uint32_t>(rate);
    const uint32_t byteRate = sampleRate * 2;
    const uint16_t align = 2;
    const uint16_t bits = 16;
    put(0, "RIFF", 4);
    put(4, &riff, 4);
    put(8, "WAVEfmt ", 8);
    put(16, &fmtSize, 4);
    put(20, &pcm, 2);
    put(22, &channels, 2);
    put(24, &sampleRate, 4);
    put(28, &byteRate, 4);
    put(32, &align, 2);
    put(34, &bits, 2);
    put(36, "data", 4);
    put(40, &dataBytes, 4);
    for (size_t i = 0; i < frames; ++i) {
        put(44 + i * 2, &value, 2);
    }
    return out;
}

struct Rig {
    fs::path dir = fs::temp_directory_path() / "animal_audio_test";
    AudioSystem audio;
    Rig() {
        fs::create_directories(dir);
        audio.init(false);
    }
    ~Rig() { fs::remove_all(dir, cleanupError); }
    SoundHandle sound(const std::string& name, size_t frames, int16_t value, int rate = AudioSystem::kSampleRate) {
        const std::vector<std::byte> bytes = wav(rate, frames, value);
        const std::string path = (dir / name).generic_string();
        writeFileAtomic(path, bytes.data(), bytes.size());
        return audio.load(path);
    }
    std::vector<float> mix(size_t frames) {
        std::vector<float> out(frames * 2);
        audio.mix(out.data(), frames);
        return out;
    }
};

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-3f;
}

TEST(audio_decodes_wav_to_stereo_at_the_output_rate) {
    const std::vector<std::byte> same = wav(AudioSystem::kSampleRate, 480, 16384);
    std::string error;
    const auto a = decodeSound(same.data(), same.size(), AudioSystem::kSampleRate, &error);
    CHECK(a && a->frames() == 480 && near(a->data[0], 0.5f) && near(a->data[1], 0.5f));
    const std::vector<std::byte> half = wav(AudioSystem::kSampleRate / 2, 480, 16384);
    const auto b = decodeSound(half.data(), half.size(), AudioSystem::kSampleRate, &error);
    CHECK(b && b->frames() >= 940 && b->frames() <= 980); // resampled to twice the frames
    const char garbage[] = "definitely not audio";
    CHECK(!decodeSound(reinterpret_cast<const std::byte*>(garbage), sizeof(garbage), 48000, &error) && !error.empty());
    const char fakeOgg[] = "OggS but nothing else";
    CHECK(!decodeSound(reinterpret_cast<const std::byte*>(fakeOgg), sizeof(fakeOgg), 48000, &error));
}

TEST(audio_mixes_volume_pan_buses_and_ends_voices) {
    Rig rig;
    const SoundHandle half = rig.sound("half.wav", 1000, 16384);
    CHECK(half.valid());
    CHECK(rig.audio.load((rig.dir / "half.wav").generic_string()) == half); // loaded once per path

    VoiceHandle v = rig.audio.play(half);
    auto out = rig.mix(4);
    CHECK(near(out[0], 0.5f) && near(out[1], 0.5f));
    rig.audio.setPan(v, 1.0f);
    out = rig.mix(4);
    CHECK(near(out[0], 0.0f) && near(out[1], 0.5f));
    rig.audio.setPan(v, 0.0f);
    rig.audio.setBusVolume(Bus::Effects, 0.5f);
    out = rig.mix(4);
    CHECK(near(out[0], 0.25f));
    rig.audio.setBusVolume(Bus::Effects, 1.0f);

    rig.mix(2000); // past the end: the voice ends
    CHECK(!rig.audio.playing(v));
    CHECK(rig.audio.stats().voices == 0);

    v = rig.audio.play(half, {1.0f, 0.0f, 2.0f, false, Bus::Effects, 0.0f}); // twice the speed
    rig.mix(490);
    CHECK(rig.audio.playing(v));
    rig.mix(20);
    CHECK(!rig.audio.playing(v));

    v = rig.audio.play(half, {1.0f, 0.0f, 1.0f, true, Bus::Ambience, 0.0f}); // looping
    rig.mix(5000);
    CHECK(rig.audio.playing(v));
    rig.audio.stop(v, 0.01f); // 480 frames of fade
    out = rig.mix(200);
    CHECK(rig.audio.playing(v) && out[2 * 199] < 0.5f && out[2 * 199] > 0.0f);
    rig.mix(400);
    CHECK(!rig.audio.playing(v));
}

TEST(audio_fades_limits_and_steals_voices) {
    Rig rig;
    const SoundHandle half = rig.sound("half.wav", 48000, 16384);
    const VoiceHandle fading = rig.audio.play(half, {1.0f, 0.0f, 1.0f, false, Bus::Effects, 0.01f});
    auto out = rig.mix(480);
    CHECK(out[0] < 0.01f && near(out[2 * 479], 0.5f)); // ramps up over 10 ms
    rig.audio.stop(fading);

    for (int i = 0; i < 4; ++i) {
        rig.audio.play(half);
    }
    out = rig.mix(16);
    CHECK(out[0] <= 1.0f && out[0] > 0.9f); // 2.0 summed, softly limited below full scale
    CHECK(rig.audio.stats().peak <= 1.0f);

    for (size_t i = 0; i < AudioSystem::kMaxVoices + 5; ++i) {
        rig.audio.play(half);
    }
    CHECK(rig.audio.stats().voices == AudioSystem::kMaxVoices);
    rig.audio.stopAll(Bus::Effects);
    CHECK(rig.audio.stats().voices == 0);
}

TEST(audio_reload_keeps_playing_voices_and_refuses_bad_files) {
    Rig rig;
    const SoundHandle s = rig.sound("tone.wav", 48000, 16384);
    const std::string path = (rig.dir / "tone.wav").generic_string();
    const VoiceHandle old = rig.audio.play(s);
    const std::vector<std::byte> quieter = wav(AudioSystem::kSampleRate, 48000, 8192);
    writeFileAtomic(path, quieter.data(), quieter.size());
    CHECK(rig.audio.reload(path));
    auto out = rig.mix(4);
    CHECK(near(out[0], 0.5f)); // the voice already playing keeps its samples
    rig.audio.stop(old);
    rig.audio.play(s);
    out = rig.mix(4);
    CHECK(near(out[0], 0.25f)); // new voices use the reloaded sound
    writeFileAtomic(path, "junk", 4);
    CHECK(!rig.audio.reload(path));
    CHECK(!rig.audio.reload((rig.dir / "never_loaded.wav").generic_string()));
    CHECK(!rig.audio.load((rig.dir / "missing.wav").generic_string()).valid());
}

} // namespace

namespace {

// The device path (an SDL audio stream fed by the mixer on SDL's audio thread) through SDL's dummy
// driver, which consumes audio in real time without hardware: a short sound must play to its end.
TEST(audio_device_stream_plays_through_the_dummy_driver) {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    {
        animal::audio::AudioSystem audio;
        CHECK(audio.init(true) && audio.hasDevice());
        const auto sound = audio.load(std::string(ANIMAL_TEST_ASSETS_DIR) + "/sounds/grab.wav"); // 50 ms
        const auto voice = audio.play(sound);
        bool finished = false;
        for (int i = 0; i < 100 && !finished; ++i) { // up to a second
            SDL_Delay(10);
            finished = !audio.playing(voice);
        }
        CHECK(finished);
        audio.devicesChanged(); // with a device: keeps it
        CHECK(audio.hasDevice());
    }
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
}

} // namespace
