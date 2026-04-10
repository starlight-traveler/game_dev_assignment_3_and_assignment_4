#include "SoundSystem.h"

#include <algorithm>
#include <cstddef>

namespace {
/**
 * @brief clamps a mixed integer sample into signed 16 bit range
 * @param value mixed sample before clamping
 * @return legal signed 16 bit sample
 */
Sint16 clamp_s16(int value) {
    // audio samples are stored as signed 16-bit values, so clamp into that legal range
    return static_cast<Sint16>(std::max(-32768, std::min(32767, value)));
}

/**
 * @brief free function bridge that forwards sdl callback work into the class
 * @param userdata SoundSystem pointer stored during device creation
 * @param stream device output stream
 * @param len bytes available in stream
 */
void sound_callback(void* userdata, Uint8* stream, int len) {
    if (!userdata) {
        // without the owning SoundSystem object there is nothing valid to mix
        return;
    }
    // SDL hands back the userdata pointer that was stored during device creation
    auto* sound_system = static_cast<SoundSystem*>(userdata);
    // forward the real work into the class method so the callback stays tiny
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
    // describe the format we want the device to use so all loaded sounds can target one format
    SDL_AudioSpec desired{};
    // 48 khz is a common modern sample rate
    desired.freq = 48000;
    // use signed 16 bit integer samples because they are simple to mix in software
    desired.format = AUDIO_S16SYS;
    // output is stereo even though source sounds are mono
    desired.channels = 2;
    // larger callback blocks reduce callback frequency but increase latency
    desired.samples = 2048;
    // sdl calls this whenever the hardware needs more samples
    desired.callback = sound_callback;
    // store this object pointer so the callback can reach mixToStream
    desired.userdata = this;

    // ask for the default playback device
    // obtained_spec_ comes back filled with the real opened format
    device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained_spec_, 0);
    if (device_ == 0) {
        // if device creation fails, leave ready_ false and let the app continue silently
        SDL_Log("SoundSystem: SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return;
    }
    SDL_Log("SoundSystem: opened audio device freq=%d format=0x%x channels=%u samples=%u",
            obtained_spec_.freq,
            static_cast<unsigned int>(obtained_spec_.format),
            static_cast<unsigned int>(obtained_spec_.channels),
            static_cast<unsigned int>(obtained_spec_.samples));

    // sdl often tells us the actual byte size for one callback buffer
    mix_capacity_bytes_ = static_cast<int>(obtained_spec_.size);
    if (mix_capacity_bytes_ <= 0) {
        // if not we estimate bytes from frames channels and sample width
        mix_capacity_bytes_ = static_cast<int>(obtained_spec_.samples) *
                              static_cast<int>(obtained_spec_.channels) *
                              static_cast<int>(sizeof(Sint16));
    }
    if (mix_capacity_bytes_ <= 0) {
        // a non-positive mix size means we cannot safely allocate the scratch buffer
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        return;
    }

    // allocate a scratch buffer for mixed output
    // we mix here first so we do not write partial intermediate values straight into sdl memory
    mix_ = static_cast<Uint8*>(SDL_malloc(static_cast<size_t>(mix_capacity_bytes_)));
    if (!mix_) {
        // if scratch allocation fails, fully tear down the device and stay unavailable
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        mix_capacity_bytes_ = 0;
        return;
    }

    // unpause means audio playback actually begins and callbacks can start arriving
    SDL_PauseAudioDevice(device_, 0);
    // audio is now fully usable
    ready_ = true;
}

SoundSystem::~SoundSystem() {
    if (device_ != 0) {
        // stop future callback work before touching shared playback state
        SDL_PauseAudioDevice(device_, 1);
        // lock so the callback and destructor do not touch playback_ at the same time
        SDL_LockAudioDevice(device_);
        // drop all active play cursors
        playback_.clear();
        // release the device lock once shared state is safe
        SDL_UnlockAudioDevice(device_);
    }

    // release the optional one-shot runtime sound buffer
    runtime_sound_.clear();
    // release all preloaded sounds
    sounds_.clear();

    if (mix_) {
        // free the scratch mix buffer used by the callback
        SDL_free(mix_);
        mix_ = nullptr;
        mix_capacity_bytes_ = 0;
    }

    if (device_ != 0) {
        // finally close the SDL device itself
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }

    // advertise that the audio system is no longer operational
    ready_ = false;
}

bool SoundSystem::loadSound(const std::string& path) {
    if (!ready_) {
        // refuse to load if the audio device never initialized
        return false;
    }

    // use a temporary sound first so a failed load does not partially change sounds_
    Sound sound{};
    if (!sound.loadWavMonoS16(path, obtained_spec_.freq)) {
        // loading or conversion failed
        return false;
    }
    // move the successfully loaded sound into the persistent library
    sounds_.push_back(std::move(sound));
    return true;
}

bool SoundSystem::playSound(int index) {
    if (!ready_ || index < 0 || static_cast<std::size_t>(index) >= sounds_.size()) {
        // invalid device state or invalid library index means no playback request
        return false;
    }
    // queue the already loaded sound so the callback will start reading from byte zero
    return queueSound(sounds_[static_cast<std::size_t>(index)]);
}

bool SoundSystem::playSound(const std::string& path) {
    if (!ready_) {
        // path-based playback also depends on a valid device
        return false;
    }

    // runtime_sound_ is reused
    // that means any old playback cursor pointing into its old buffer must be removed first
    if (runtime_sound_.data() != nullptr && runtime_sound_.length() > 0) {
        // remember the address range of the current runtime sound buffer
        const Uint8* start = runtime_sound_.data();
        const Uint8* end = start + runtime_sound_.length();
        // playback_ is shared with the callback so lock before editing it
        SDL_LockAudioDevice(device_);
        playback_.erase(
            std::remove_if(playback_.begin(), playback_.end(),
                           [start, end](const SoundState& state) {
                               // remove any playback cursor still pointing into the old runtime buffer
                               return state.cursor >= start && state.cursor < end;
                           }),
            playback_.end());
        // unlock so the callback can resume
        SDL_UnlockAudioDevice(device_);
    }

    // load the requested file into the reusable runtime sound object
    if (!runtime_sound_.loadWavMonoS16(path, obtained_spec_.freq)) {
        return false;
    }
    // queue the newly loaded runtime sound just like a preloaded sound
    return queueSound(runtime_sound_);
}

bool SoundSystem::isReady() const {
    // tiny getter used by callers before attempting loads or playback
    return ready_;
}

void SoundSystem::mixToStream(Uint8* stream, int len) {
    if (!stream || len <= 0) {
        // SDL should not call this with an invalid buffer, but guard anyway
        return;
    }

    if (!mix_ || len > mix_capacity_bytes_) {
        // callbacks are usually a stable size
        // this path handles the rare case where sdl asks for a bigger block later
        Uint8* resized = static_cast<Uint8*>(SDL_realloc(mix_, static_cast<size_t>(len)));
        if (!resized) {
            // on allocation failure output silence rather than garbage
            SDL_memset(stream, 0, static_cast<size_t>(len));
            return;
        }
        // adopt the newly resized buffer and remember the new capacity
        mix_ = resized;
        mix_capacity_bytes_ = len;
    }

    // start from silence then add each sound on top
    SDL_memset(mix_, 0, static_cast<size_t>(len));
    // reinterpret raw bytes as 16 bit samples because that is the opened output format
    auto* mix_samples = reinterpret_cast<Sint16*>(mix_);
    // each stereo frame contains left then right 16 bit samples
    const int output_frames = len / static_cast<int>(sizeof(Sint16) * 2);

    // visit every currently active sound
    // we erase finished sounds in place so the loop only increments when the current item survives
    for (std::size_t i = 0; i < playback_.size();) {
        // take the current sound state by reference because we will update its cursor
        SoundState& state = playback_[i];
        if (!state.cursor || state.remaining == 0) {
            // drop invalid or exhausted sounds immediately
            playback_.erase(playback_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        // we can only mix the overlap between how much output space exists
        // and how many source samples remain in this sound
        const Uint32 source_frames_remaining = state.remaining / static_cast<Uint32>(sizeof(Sint16));
        const Uint32 frames_to_mix = std::min(static_cast<Uint32>(output_frames), source_frames_remaining);
        // source sound is mono so each frame has one sample only
        const auto* mono_samples = reinterpret_cast<const Sint16*>(state.cursor);
        for (Uint32 frame = 0; frame < frames_to_mix; ++frame) {
            // stereo output reserves two slots per frame
            const int out_index = static_cast<int>(frame * 2);
            // read one mono sample from the source
            const int mono_value = static_cast<int>(mono_samples[frame]);
            // duplicate mono into left
            const int left = static_cast<int>(mix_samples[out_index]) + mono_value;
            // duplicate mono into right
            const int right = static_cast<int>(mix_samples[out_index + 1]) + mono_value;
            // clamp after addition so loud overlaps do not overflow
            mix_samples[out_index] = clamp_s16(left);
            mix_samples[out_index + 1] = clamp_s16(right);
        }

        // move the source cursor forward by the amount we just mixed
        const Uint32 bytes_consumed = frames_to_mix * static_cast<Uint32>(sizeof(Sint16));
        state.cursor += bytes_consumed;
        // reduce the number of bytes remaining for this sound
        state.remaining -= bytes_consumed;
        if (state.remaining == 0) {
            // remove the sound once all of its samples have been consumed
            playback_.erase(playback_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            // otherwise keep it for the next callback chunk
            ++i;
        }
    }

    // copy the completed mixed buffer into SDL's output stream
    SDL_memcpy(stream, mix_, static_cast<size_t>(len));
}

bool SoundSystem::queueSound(const Sound& sound) {
    if (!ready_ || sound.data() == nullptr || sound.length() == 0) {
        // cannot queue invalid or empty sounds
        return false;
    }

    // queue insertion must be protected because playback_ is shared with the callback thread
    SDL_LockAudioDevice(device_);
    // start reading from the first byte of the source buffer
    SoundState state{};
    state.cursor = sound.data();
    // total remaining mono sample bytes for this sound
    state.remaining = sound.length();
    // append to the active playback list so future callbacks will mix it
    playback_.push_back(state);
    // release the lock so the callback can see the new sound
    SDL_UnlockAudioDevice(device_);
    return true;
}
