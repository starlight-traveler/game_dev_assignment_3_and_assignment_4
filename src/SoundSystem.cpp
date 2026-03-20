#include "SoundSystem.h"

#include <algorithm>
#include <cstddef>

namespace {
/**
 * @brief Clamps mixed signed byte sample to valid range
 * @param value Mixed integer sample
 * @return Clamped signed byte sample
 */
Sint8 clamp_s8(int value) {
    return static_cast<Sint8>(std::max(-128, std::min(127, value)));
}

/**
 * @brief Free callback bridge for SDL audio device
 * @param userdata SoundSystem pointer
 * @param stream Device output stream
 * @param len Byte count available in stream
 */
void sound_callback(void* userdata, Uint8* stream, int len) {
    if (!userdata) {
        return;
    }
    auto* sound_system = static_cast<SoundSystem*>(userdata);
    sound_system->mixToStream(stream, len);
}
}  // namespace

SoundSystem::SoundSystem()
    : sounds_(),
      device_(0),
      obtained_spec_{},
      playback_(),
      mix_(nullptr),
      mix_capacity_bytes_(0),
      runtime_sound_(),
      ready_(false) {
    SDL_AudioSpec desired{};
    desired.freq = 48000;
    desired.format = AUDIO_S8;
    desired.channels = 2;
    desired.samples = 2048;
    desired.callback = sound_callback;
    desired.userdata = this;

    device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained_spec_, 0);
    if (device_ == 0) {
        return;
    }

    mix_capacity_bytes_ = static_cast<int>(obtained_spec_.size);
    if (mix_capacity_bytes_ <= 0) {
        mix_capacity_bytes_ = static_cast<int>(obtained_spec_.samples) *
                              static_cast<int>(obtained_spec_.channels);
    }
    if (mix_capacity_bytes_ <= 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        return;
    }

    mix_ = static_cast<Uint8*>(SDL_malloc(static_cast<size_t>(mix_capacity_bytes_)));
    if (!mix_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        mix_capacity_bytes_ = 0;
        return;
    }

    SDL_PauseAudioDevice(device_, 0);
    ready_ = true;
}

SoundSystem::~SoundSystem() {
    if (device_ != 0) {
        SDL_PauseAudioDevice(device_, 1);
        SDL_LockAudioDevice(device_);
        playback_.clear();
        SDL_UnlockAudioDevice(device_);
    }

    runtime_sound_.clear();
    sounds_.clear();

    if (mix_) {
        SDL_free(mix_);
        mix_ = nullptr;
        mix_capacity_bytes_ = 0;
    }

    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }

    ready_ = false;
}

bool SoundSystem::loadSound(const std::string& path) {
    if (!ready_) {
        return false;
    }

    Sound sound{};
    if (!sound.loadWavMonoS8(path, obtained_spec_.freq)) {
        return false;
    }
    sounds_.push_back(std::move(sound));
    return true;
}

bool SoundSystem::playSound(int index) {
    if (!ready_ || index < 0 || static_cast<std::size_t>(index) >= sounds_.size()) {
        return false;
    }
    return queueSound(sounds_[static_cast<std::size_t>(index)]);
}

bool SoundSystem::playSound(const std::string& path) {
    if (!ready_) {
        return false;
    }

    // remove active states that reference the old runtime-loaded sound buffer
    if (runtime_sound_.data() != nullptr && runtime_sound_.length() > 0) {
        const Uint8* start = runtime_sound_.data();
        const Uint8* end = start + runtime_sound_.length();
        SDL_LockAudioDevice(device_);
        playback_.erase(
            std::remove_if(playback_.begin(), playback_.end(),
                           [start, end](const SoundState& state) {
                               return state.cursor >= start && state.cursor < end;
                           }),
            playback_.end());
        SDL_UnlockAudioDevice(device_);
    }

    if (!runtime_sound_.loadWavMonoS8(path, obtained_spec_.freq)) {
        return false;
    }
    return queueSound(runtime_sound_);
}

bool SoundSystem::isReady() const {
    return ready_;
}

void SoundSystem::mixToStream(Uint8* stream, int len) {
    if (!stream || len <= 0) {
        return;
    }

    if (!mix_ || len > mix_capacity_bytes_) {
        Uint8* resized = static_cast<Uint8*>(SDL_realloc(mix_, static_cast<size_t>(len)));
        if (!resized) {
            SDL_memset(stream, 0, static_cast<size_t>(len));
            return;
        }
        mix_ = resized;
        mix_capacity_bytes_ = len;
    }

    SDL_memset(mix_, 0, static_cast<size_t>(len));
    auto* mix_samples = reinterpret_cast<Sint8*>(mix_);
    const int output_frames = len / 2;

    for (std::size_t i = 0; i < playback_.size();) {
        SoundState& state = playback_[i];
        if (!state.cursor || state.remaining == 0) {
            playback_.erase(playback_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        const Uint32 frames_to_mix = std::min(static_cast<Uint32>(output_frames), state.remaining);
        const auto* mono_samples = reinterpret_cast<const Sint8*>(state.cursor);
        for (Uint32 frame = 0; frame < frames_to_mix; ++frame) {
            const int out_index = static_cast<int>(frame * 2);
            const int mono_value = static_cast<int>(mono_samples[frame]);
            const int left = static_cast<int>(mix_samples[out_index]) + mono_value;
            const int right = static_cast<int>(mix_samples[out_index + 1]) + mono_value;
            mix_samples[out_index] = clamp_s8(left);
            mix_samples[out_index + 1] = clamp_s8(right);
        }

        state.cursor += frames_to_mix;
        state.remaining -= frames_to_mix;
        if (state.remaining == 0) {
            playback_.erase(playback_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }

    SDL_memcpy(stream, mix_, static_cast<size_t>(len));
}

bool SoundSystem::queueSound(const Sound& sound) {
    if (!ready_ || sound.data() == nullptr || sound.length() == 0) {
        return false;
    }

    SDL_LockAudioDevice(device_);
    SoundState state{};
    state.cursor = sound.data();
    state.remaining = sound.length();
    playback_.push_back(state);
    SDL_UnlockAudioDevice(device_);
    return true;
}
