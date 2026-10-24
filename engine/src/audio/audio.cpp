#include <animal/audio/audio.h>

#include <animal/core/file.h>
#include <animal/core/log.h>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c> // the implementation is the animal_stb_vorbis library

namespace animal::audio {

namespace {

float limit(float x) {
    // Transparent up to 0.9, then a soft knee towards 1 (no hard clipping).
    const float a = std::fabs(x);
    if (a <= 0.9f) {
        return x;
    }
    const float y = 0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f);
    return x < 0.0f ? -y : y;
}

std::optional<Samples> convert(const SDL_AudioSpec& from, const void* data, int bytes, int rate, std::string* error) {
    const SDL_AudioSpec to{SDL_AUDIO_F32, 2, rate};
    Uint8* out = nullptr;
    int outBytes = 0;
    if (!SDL_ConvertAudioSamples(&from, static_cast<const Uint8*>(data), bytes, &to, &out, &outBytes)) {
        if (error) {
            *error = SDL_GetError();
        }
        return std::nullopt;
    }
    Samples samples;
    samples.data.resize(static_cast<size_t>(outBytes) / sizeof(float));
    std::memcpy(samples.data.data(), out, samples.data.size() * sizeof(float));
    SDL_free(out);
    return samples;
}

} // namespace

std::optional<Samples> decodeSound(const std::byte* bytes, size_t size, int rate, std::string* error) {
    const auto fail = [error](const std::string& message) -> std::optional<Samples> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    if (size >= 4 && std::memcmp(bytes, "OggS", 4) == 0) {
        int channels = 0;
        int sourceRate = 0;
        short* decoded = nullptr;
        const int frames = stb_vorbis_decode_memory(reinterpret_cast<const unsigned char*>(bytes),
                                                    static_cast<int>(size), &channels, &sourceRate, &decoded);
        if (frames <= 0 || !decoded || channels <= 0) {
            std::free(decoded);
            return fail("not a readable Ogg Vorbis stream");
        }
        const SDL_AudioSpec from{SDL_AUDIO_S16, channels, sourceRate};
        auto samples = convert(from, decoded, frames * channels * 2, rate, error);
        std::free(decoded);
        return samples;
    }
    SDL_IOStream* io = SDL_IOFromConstMem(bytes, size);
    SDL_AudioSpec spec{};
    Uint8* data = nullptr;
    Uint32 length = 0;
    if (!io || !SDL_LoadWAV_IO(io, true, &spec, &data, &length)) {
        return fail(std::string("not a readable WAV or Ogg Vorbis file (") + SDL_GetError() + ")");
    }
    auto samples = convert(spec, data, static_cast<int>(length), rate, error);
    SDL_free(data);
    return samples;
}

AudioSystem::AudioSystem() = default;

AudioSystem::~AudioSystem() {
    shutdown();
}

bool AudioSystem::init(bool device) {
    wantDevice_ = device;
    if (!device) {
        log::info("Audio: no device (silent)");
        return true;
    }
    openDevice();
    return true;
}

void AudioSystem::openDevice() {
    if (!subsystemInit_) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            log::warn("Audio: {}; continuing without sound", SDL_GetError());
            return;
        }
        subsystemInit_ = true;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kSampleRate};
    SDL_AudioStream* stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioSystem::callback, this);
    if (!stream) {
        log::warn("Audio: no playback device ({}); continuing without sound", SDL_GetError());
        return;
    }
    stream_ = stream;
    SDL_ResumeAudioStreamDevice(stream);
    log::info("Audio: {} Hz stereo on {}", kSampleRate, deviceName());
}

std::string AudioSystem::deviceName() const {
    if (!stream_) {
        return "no device";
    }
    const char* name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(static_cast<SDL_AudioStream*>(stream_)));
    return name ? name : "the default device";
}

void AudioSystem::devicesChanged() {
    if (!wantDevice_) {
        return;
    }
    if (!stream_) {
        openDevice();
        return;
    }
    log::info("Audio: playback devices changed; playing on {}", deviceName());
}

void AudioSystem::shutdown() {
    if (started_ > 0) {
        log::info("Audio: {} voice(s) played from {} sound(s)", started_, byPath_.size());
        started_ = 0;
    }
    if (stream_) {
        SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(stream_));
        stream_ = nullptr;
    }
    if (subsystemInit_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        subsystemInit_ = false;
    }
    wantDevice_ = false;
    const std::lock_guard lock(mutex_);
    for (VoiceSlot& slot : voices_) {
        slot.active = false;
        slot.voice = {};
    }
}

void AudioSystem::callback(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<AudioSystem*>(user);
    const size_t frames = static_cast<size_t>(additional) / (2 * sizeof(float));
    if (frames == 0) {
        return;
    }
    self->scratch_.resize(frames * 2);
    self->mix(self->scratch_.data(), frames);
    SDL_PutAudioStreamData(stream, self->scratch_.data(),
                           static_cast<int>(frames * 2 * sizeof(float)));
}

SoundHandle AudioSystem::load(const std::string& path) {
    {
        const std::lock_guard lock(mutex_);
        for (const auto& [p, handle] : byPath_) {
            if (p == path) {
                return handle;
            }
        }
    }
    std::string error;
    const auto bytes = readFile(path, &error);
    std::optional<Samples> samples =
        bytes ? decodeSound(bytes->data(), bytes->size(), kSampleRate, &error) : std::nullopt;
    if (!samples) {
        log::error("Cannot load sound '{}': {}", path, error);
        return {};
    }
    const std::lock_guard lock(mutex_);
    const SoundHandle handle = sounds_.insert({path, std::make_shared<const Samples>(std::move(*samples))});
    byPath_.emplace_back(path, handle);
    return handle;
}

bool AudioSystem::reload(const std::string& path) {
    SoundHandle handle;
    {
        const std::lock_guard lock(mutex_);
        for (const auto& [p, h] : byPath_) {
            if (p == path) {
                handle = h;
            }
        }
    }
    if (!handle) {
        return false;
    }
    std::string error;
    const auto bytes = readFile(path, &error);
    std::optional<Samples> samples =
        bytes ? decodeSound(bytes->data(), bytes->size(), kSampleRate, &error) : std::nullopt;
    if (!samples) {
        log::error("Cannot reload sound '{}': {}", path, error);
        return false;
    }
    const std::lock_guard lock(mutex_);
    if (Sound* sound = sounds_.get(handle)) {
        sound->samples = std::make_shared<const Samples>(std::move(*samples));
    }
    log::info("Reloaded sound '{}'", path);
    return true;
}

AudioSystem::Voice* AudioSystem::find(VoiceHandle handle) {
    if (handle.index >= voices_.size()) {
        return nullptr;
    }
    VoiceSlot& slot = voices_[handle.index];
    return slot.active && slot.generation == handle.generation ? &slot.voice : nullptr;
}

const AudioSystem::Voice* AudioSystem::find(VoiceHandle handle) const {
    return const_cast<AudioSystem*>(this)->find(handle);
}

VoiceHandle AudioSystem::play(SoundHandle soundHandle, const PlayParams& params) {
    const std::lock_guard lock(mutex_);
    const Sound* sound = sounds_.get(soundHandle);
    if (!sound || !sound->samples || sound->samples->frames() == 0) {
        return {};
    }
    // A free slot, else the quietest voice gives way.
    size_t index = voices_.size();
    float quietest = 1.0e9f;
    for (size_t i = 0; i < voices_.size(); ++i) {
        if (!voices_[i].active) {
            index = i;
            break;
        }
        const float level = voices_[i].voice.volume * buses_[static_cast<size_t>(voices_[i].voice.bus)];
        if (level < quietest) {
            quietest = level;
            index = i;
        }
    }
    VoiceSlot& slot = voices_[index];
    ++slot.generation;
    ++started_;
    slot.active = true;
    Voice& v = slot.voice;
    v = {};
    v.samples = sound->samples;
    v.rate = std::max(0.01f, params.rate);
    v.pan = std::clamp(params.pan, -1.0f, 1.0f);
    v.loop = params.loop;
    v.bus = params.bus;
    v.target = std::max(0.0f, params.volume);
    if (params.fadeIn > 0.0f) {
        v.volume = 0.0f;
        v.step = v.target / (params.fadeIn * kSampleRate);
    } else {
        v.volume = v.target;
    }
    return {static_cast<uint32_t>(index), slot.generation};
}

void AudioSystem::stop(VoiceHandle voice, float fadeOut) {
    const std::lock_guard lock(mutex_);
    if (Voice* v = find(voice)) {
        if (fadeOut <= 0.0f || v->volume <= 0.0f) {
            voices_[voice.index].active = false;
            voices_[voice.index].voice = {};
            return;
        }
        v->target = 0.0f;
        v->step = -v->volume / (fadeOut * kSampleRate);
        v->stopping = true;
    }
}

void AudioSystem::stopAll(Bus bus, float fadeOut) {
    for (uint32_t i = 0; i < voices_.size(); ++i) {
        VoiceHandle handle;
        {
            const std::lock_guard lock(mutex_);
            if (!voices_[i].active || voices_[i].voice.bus != bus) {
                continue;
            }
            handle = {i, voices_[i].generation};
        }
        stop(handle, fadeOut);
    }
}

void AudioSystem::setVolume(VoiceHandle voice, float volume, float seconds) {
    const std::lock_guard lock(mutex_);
    if (Voice* v = find(voice); v && !v->stopping) {
        v->target = std::max(0.0f, volume);
        if (seconds <= 0.0f) {
            v->volume = v->target;
            v->step = 0.0f;
        } else {
            v->step = (v->target - v->volume) / (seconds * kSampleRate);
        }
    }
}

void AudioSystem::setPan(VoiceHandle voice, float pan) {
    const std::lock_guard lock(mutex_);
    if (Voice* v = find(voice)) {
        v->pan = std::clamp(pan, -1.0f, 1.0f);
    }
}

bool AudioSystem::playing(VoiceHandle voice) const {
    const std::lock_guard lock(mutex_);
    return find(voice) != nullptr;
}

void AudioSystem::setBusVolume(Bus bus, float volume) {
    const std::lock_guard lock(mutex_);
    buses_[static_cast<size_t>(bus)] = std::max(0.0f, volume);
}

float AudioSystem::busVolume(Bus bus) const {
    const std::lock_guard lock(mutex_);
    return buses_[static_cast<size_t>(bus)];
}

void AudioSystem::setMasterVolume(float volume) {
    const std::lock_guard lock(mutex_);
    master_ = std::max(0.0f, volume);
}

void AudioSystem::mix(float* out, size_t frames) {
    std::fill(out, out + frames * 2, 0.0f);
    const std::lock_guard lock(mutex_);
    for (VoiceSlot& slot : voices_) {
        if (!slot.active) {
            continue;
        }
        Voice& v = slot.voice;
        const Samples& s = *v.samples;
        const size_t length = s.frames();
        const float bus = buses_[static_cast<size_t>(v.bus)] * master_;
        const float left = std::min(1.0f, 1.0f - v.pan) * bus;
        const float right = std::min(1.0f, 1.0f + v.pan) * bus;
        bool finished = false;
        for (size_t f = 0; f < frames; ++f) {
            if (v.step != 0.0f) {
                v.volume += v.step;
                if ((v.step > 0.0f && v.volume >= v.target) || (v.step < 0.0f && v.volume <= v.target)) {
                    v.volume = v.target;
                    v.step = 0.0f;
                    if (v.stopping) {
                        finished = true;
                        break;
                    }
                }
            }
            const auto i = static_cast<size_t>(v.position);
            if (i >= length) {
                finished = true;
                break;
            }
            const auto frac = static_cast<float>(v.position - static_cast<double>(i));
            const size_t j = i + 1 < length ? i + 1 : (v.loop ? 0 : i);
            const float l = s.data[i * 2] + (s.data[j * 2] - s.data[i * 2]) * frac;
            const float r = s.data[i * 2 + 1] + (s.data[j * 2 + 1] - s.data[i * 2 + 1]) * frac;
            out[f * 2] += l * v.volume * left;
            out[f * 2 + 1] += r * v.volume * right;
            v.position += v.rate;
            if (v.loop && v.position >= static_cast<double>(length)) {
                v.position -= static_cast<double>(length);
            }
        }
        if (finished) {
            slot.active = false;
            slot.voice = {};
        }
    }
    float peak = 0.0f;
    for (size_t k = 0; k < frames * 2; ++k) {
        out[k] = limit(out[k]);
        peak = std::max(peak, std::fabs(out[k]));
    }
    peak_ = peak;
}

AudioSystem::Stats AudioSystem::stats() const {
    const std::lock_guard lock(mutex_);
    Stats s;
    for (const VoiceSlot& slot : voices_) {
        s.voices += slot.active ? 1u : 0u;
    }
    s.sounds = static_cast<uint32_t>(byPath_.size());
    s.started = started_;
    s.peak = peak_;
    return s;
}

} // namespace animal::audio
