#pragma once

// Audio (ADR-0033): sounds decoded once into float stereo at the output rate, mixed by the engine
// (voices with volume, pan, rate, fades and loops on three buses, then a limiter) and handed to an
// SDL3 audio stream. Without a device (headless runs, tests) nothing is played but every call works,
// and mix() can be driven directly.

#include <animal/core/handle.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace animal::audio {

struct SoundTag;
struct VoiceTag;
using SoundHandle = Handle<SoundTag>;
using VoiceHandle = Handle<VoiceTag>;

enum class Bus : uint8_t {
    Effects,
    Ambience,
    Music,
    Count,
};

struct PlayParams {
    float volume = 1.0f;
    float pan = 0.0f;  // -1 left .. 1 right (balance: the far side is turned down, the centre is full)
    float rate = 1.0f; // playback speed (pitch); 1 = as recorded
    bool loop = false;
    Bus bus = Bus::Effects;
    float fadeIn = 0.0f; // seconds
};

// Decoded sound: interleaved stereo float at AudioSystem::kSampleRate.
struct Samples {
    std::vector<float> data;
    size_t frames() const { return data.size() / 2; }
};

// WAV (any format SDL reads) or OGG Vorbis bytes -> stereo float at `rate`.
std::optional<Samples> decodeSound(const std::byte* bytes, size_t size, int rate, std::string* error = nullptr);

class AudioSystem {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr size_t kMaxVoices = 32; // the quietest voice gives way beyond this

    AudioSystem();
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Opens the default playback device; `device` false (or no device available) runs silently.
    bool init(bool device = true);
    void shutdown();
    bool hasDevice() const { return stream_ != nullptr; }
    // After playback devices change: the stream follows the system default by itself (SDL moves
    // streams opened on the default device); this opens one if there was no device so far, and
    // logs the device in use.
    void devicesChanged();
    std::string deviceName() const; // of the device playing, or "no device"

    // Loads through the file layer (resource packs included); the same path loads once.
    SoundHandle load(const std::string& path);
    // Hot reload: decodes the file again; voices already playing keep the old sound. False (logged)
    // if the file is unusable or was never loaded; the previous sound stays.
    bool reload(const std::string& path);

    VoiceHandle play(SoundHandle sound, const PlayParams& params = {});
    void stop(VoiceHandle voice, float fadeOut = 0.0f);
    void stopAll(Bus bus, float fadeOut = 0.0f);
    void setVolume(VoiceHandle voice, float volume, float seconds = 0.0f);
    void setPan(VoiceHandle voice, float pan);
    bool playing(VoiceHandle voice) const;

    void setBusVolume(Bus bus, float volume);
    float busVolume(Bus bus) const;
    void setMasterVolume(float volume);

    // Mixes `frames` stereo frames (interleaved float) and advances every voice. Called by the device
    // thread; public for tests and headless use.
    void mix(float* out, size_t frames);

    struct Stats {
        uint32_t voices = 0;
        uint32_t sounds = 0;
        uint64_t started = 0; // voices started since init
        float peak = 0.0f;    // of the last mixed block, after the limiter
    };
    Stats stats() const;

private:
    struct Sound {
        std::string path;
        std::shared_ptr<const Samples> samples;
    };
    struct Voice {
        std::shared_ptr<const Samples> samples; // kept alive while playing, even across a reload
        double position = 0.0;
        float rate = 1.0f;
        float pan = 0.0f;
        float volume = 1.0f; // current
        float target = 1.0f;
        float step = 0.0f;   // volume change per frame (fades)
        bool loop = false;
        bool stopping = false; // remove when the fade reaches zero
        Bus bus = Bus::Effects;
    };

    static void callback(void* user, SDL_AudioStream* stream, int additional, int total);

    void* stream_ = nullptr; // SDL_AudioStream
    bool wantDevice_ = false; // init(true): keep trying to open one when devices change
    bool subsystemInit_ = false;
    void openDevice();
    mutable std::mutex mutex_;
    struct VoiceSlot {
        Voice voice;
        uint32_t generation = 1;
        bool active = false;
    };
    Voice* find(VoiceHandle handle);
    const Voice* find(VoiceHandle handle) const;

    HandlePool<SoundTag, Sound> sounds_;
    std::vector<std::pair<std::string, SoundHandle>> byPath_;
    std::array<VoiceSlot, kMaxVoices> voices_{};
    std::array<float, static_cast<size_t>(Bus::Count)> buses_{1.0f, 1.0f, 1.0f};
    float master_ = 1.0f;
    float peak_ = 0.0f;
    uint64_t started_ = 0;
    std::vector<float> scratch_;
};

} // namespace animal::audio
