/**
 * @file SoundSystem.h
 * @brief low level sdl audio device wrapper with a tiny software mixer
 */
#ifndef SOUND_SYSTEM_H
#define SOUND_SYSTEM_H

#include <string>
#include <vector>

#include <SDL.h>

#include "Sound.h"

/**
 * @brief playback cursor for one currently active sound
 *
 * the sound bytes live inside a Sound object somewhere else
 * this struct only remembers where mixing should continue next
 */
struct SoundState {
    // current read position inside the converted mono sample buffer
    const Uint8* cursor;
    // number of mono sample bytes that still have not been consumed
    Uint32 remaining;
};

/**
 * @brief owns the sdl audio device and mixes queued sounds in the callback
 *
 * the flow is basically this
 * 1 sounds get loaded and converted into one shared playback format
 * 2 play requests append SoundState cursors into playback_
 * 3 sdl repeatedly asks for more audio through the callback
 * 4 mixToStream walks every active sound and adds samples together
 */
class SoundSystem {
public:
    /**
     * @brief opens the audio device and starts callback driven playback
     */
    SoundSystem();

    /**
     * @brief stops callbacks then frees buffers sounds and the device
     */
    ~SoundSystem();

    SoundSystem(const SoundSystem&) = delete;
    SoundSystem& operator=(const SoundSystem&) = delete;

    /**
     * @brief loads one wav into the persistent sound library
     * @param path wav file path
     * @return true when the file is loaded and converted
     */
    bool loadSound(const std::string& path);

    /**
     * @brief queues a previously loaded sound by library index
     * @param index index inside sounds_
     * @return true when the sound was successfully queued
     */
    bool playSound(int index);

    /**
     * @brief loads one wav into the reusable runtime slot and queues it
     * @param path wav file path
     * @return true when load and queue both succeed
     *
     * this path based overload is useful for one off sounds without growing the library forever
     */
    bool playSound(const std::string& path);

    /**
     * @brief reports whether the device is valid and usable
     * @return true when audio is ready
     */
    bool isReady() const;

    /**
     * @brief mixes all active sounds into the buffer sdl gave us
     * @param stream output byte buffer owned by sdl
     * @param len number of bytes the callback must fill
     */
    void mixToStream(Uint8* stream, int len);

private:
    /**
     * @brief creates a playback cursor for one sound and appends it to playback_
     * @param sound source sound object
     * @return true when the sound is valid and queued
     */
    bool queueSound(const Sound& sound);

    // preloaded sounds kept alive so indexed play requests always point at valid buffers
    std::vector<Sound> sounds_;
    // numeric handle returned by sdl for the opened playback device
    SDL_AudioDeviceID device_;
    // the format sdl actually opened which may differ from what we asked for
    SDL_AudioSpec obtained_spec_;
    // every currently active sound cursor that still needs to be mixed
    std::vector<SoundState> playback_;
    // scratch buffer where we build the mixed result before copying into the sdl stream
    Uint8* mix_;
    // how many bytes mix_ can currently hold
    int mix_capacity_bytes_;
    // reusable one shot sound for the path based overload
    Sound runtime_sound_;
    // easy status flag so callers can skip audio work when startup failed
    bool ready_;
};

#endif
