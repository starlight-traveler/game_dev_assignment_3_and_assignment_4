/**
 * @file Sound.h
 * @brief RAII sound object for mono WAV sample data
 */
#ifndef SOUND_H
#define SOUND_H

#include <string>

#include <SDL.h>

/**
 * @brief Owns converted mono sound bytes for playback
 */
class Sound {
public:
    /**
     * @brief Constructs an empty sound object
     */
    Sound();

    /**
     * @brief Releases owned sound memory
     */
    ~Sound();

    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;
    Sound(Sound&& other) noexcept;
    Sound& operator=(Sound&& other) noexcept;

    /**
     * @brief Loads and converts WAV into signed 8-bit mono data
     * @param path WAV file path
     * @param target_frequency Target playback frequency
     * @return True on success
     */
    bool loadWavMonoS8(const std::string& path, int target_frequency);

    /**
     * @brief Returns pointer to converted audio bytes
     * @return Audio data pointer or nullptr
     */
    const Uint8* data() const;

    /**
     * @brief Returns number of bytes in converted audio data
     * @return Byte length
     */
    Uint32 length() const;

    /**
     * @brief Clears loaded sound data
     */
    void clear();

private:
    Uint8* data_;
    Uint32 length_;
};

#endif
