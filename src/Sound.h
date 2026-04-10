/**
 * @file Sound.h
 * @brief simple owned sound buffer for wav data after conversion
 */
#ifndef SOUND_H
#define SOUND_H

#include <string>

#include <SDL.h>

/**
 * @brief stores one converted sound buffer that the mixer can read from later
 *
 * this class is intentionally tiny
 * it just owns bytes and knows how long the byte buffer is
 * the audio system can then borrow that buffer without worrying about manual frees
 */
class Sound {
public:
    /**
     * @brief makes an empty sound with no loaded bytes
     */
    Sound();

    /**
     * @brief frees the owned sound buffer if one is loaded
     */
    ~Sound();

    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;
    Sound(Sound&& other) noexcept;
    Sound& operator=(Sound&& other) noexcept;

    /**
     * @brief loads a wav file and converts it into mono signed 16 bit samples
     * @param path wav file path on disk
     * @param target_frequency playback frequency the audio device wants
     * @return true when load and conversion both work
     *
     * the rest of the engine expects one simple mono buffer
     * later the sound system duplicates each mono sample into left and right channels
     */
    bool loadWavMonoS16(const std::string& path, int target_frequency);

    /**
     * @brief returns the first byte of the converted buffer
     * @return data pointer or nullptr when nothing is loaded
     */
    const Uint8* data() const;

    /**
     * @brief returns how many bytes exist in the converted buffer
     * @return byte count
     */
    Uint32 length() const;

    /**
     * @brief drops the current sound buffer and resets back to empty
     */
    void clear();

private:
    // raw owned bytes returned by sdl allocation helpers
    Uint8* data_;
    // total number of valid bytes inside data_
    Uint32 length_;
};

#endif
