/**
 * @file SoundSystem.h
 * @brief Low-level SDL sound device system with callback mixing
 */
#ifndef SOUND_SYSTEM_H
#define SOUND_SYSTEM_H

#include <string>
#include <vector>

#include <SDL.h>

#include "Sound.h"

/**
 * @brief Playback state for an active sound stream
 */
struct SoundState {
    const Uint8* cursor;
    Uint32 remaining;
};

/**
 * @brief Sound device manager with preload library and active playback queue
 */
class SoundSystem {
public:
    /**
     * @brief Opens audio device and starts callback stream
     */
    SoundSystem();

    /**
     * @brief Stops callback stream and cleans all audio resources
     */
    ~SoundSystem();

    SoundSystem(const SoundSystem&) = delete;
    SoundSystem& operator=(const SoundSystem&) = delete;

    /**
     * @brief Loads a WAV into preloaded sound library
     * @param path WAV filepath
     * @return True on success
     */
    bool loadSound(const std::string& path);

    /**
     * @brief Queues a sound by preloaded index
     * @param index Sound library index
     * @return True when queued
     */
    bool playSound(int index);

    /**
     * @brief Loads and queues one runtime WAV track
     * @param path WAV filepath
     * @return True when queued
     */
    bool playSound(const std::string& path);

    /**
     * @brief Reports whether audio device is open
     * @return True when device is ready
     */
    bool isReady() const;

    /**
     * @brief Audio callback implementation used by free callback bridge
     * @param stream Device output stream buffer
     * @param len Bytes available in output buffer
     */
    void mixToStream(Uint8* stream, int len);

private:
    /**
     * @brief Queues a sound object into playback
     * @param sound Sound object source
     * @return True when queued
     */
    bool queueSound(const Sound& sound);

    std::vector<Sound> sounds_;
    SDL_AudioDeviceID device_;
    SDL_AudioSpec obtained_spec_;
    std::vector<SoundState> playback_;
    Uint8* mix_;
    int mix_capacity_bytes_;
    Sound runtime_sound_;
    bool ready_;
};

#endif
