#include "Sound.h"

#include <cstring>

Sound::Sound() : data_(nullptr), length_(0) {}

Sound::~Sound() {
    // destructor just forwards to clear so free logic lives in one place
    clear();
}

Sound::Sound(Sound&& other) noexcept : data_(other.data_), length_(other.length_) {
    // moving transfers ownership of the bytes to this object
    other.data_ = nullptr;
    other.length_ = 0;
}

Sound& Sound::operator=(Sound&& other) noexcept {
    if (this == &other) {
        // self move is weird but harmless if we just return
        return *this;
    }
    // free our old buffer before stealing the new one
    clear();
    data_ = other.data_;
    length_ = other.length_;
    // leave the moved from object empty so only one object owns the bytes
    other.data_ = nullptr;
    other.length_ = 0;
    return *this;
}

bool Sound::loadWavMonoS16(const std::string& path, int target_frequency) {
    // starting fresh avoids leaking an old buffer on reload
    clear();

    // source_spec describes whatever format the wav file was actually stored in
    SDL_AudioSpec source_spec{};
    Uint8* source_buffer = nullptr;
    Uint32 source_length = 0;
    if (SDL_LoadWAV(path.c_str(), &source_spec, &source_buffer, &source_length) == nullptr) {
        // file open parse or decode failed
        return false;
    }

    if (source_spec.channels != 1) {
        // this helper is intentionally strict and only accepts mono inputs
        SDL_FreeWAV(source_buffer);
        return false;
    }

    // ask sdl how to convert from the file format into the engine playback format
    SDL_AudioCVT converter{};
    const int build_result = SDL_BuildAudioCVT(
        &converter,
        source_spec.format, source_spec.channels, source_spec.freq,
        AUDIO_S16SYS, 1, target_frequency);
    if (build_result < 0) {
        SDL_FreeWAV(source_buffer);
        return false;
    }

    // converted will become the final owned buffer on success
    Uint8* converted = nullptr;
    Uint32 converted_length = 0;
    if (converter.needed != 0) {
        // converter needs a destination buffer large enough for worst case expansion
        converter.len = static_cast<int>(source_length);
        converter.buf = static_cast<Uint8*>(SDL_malloc(static_cast<size_t>(converter.len * converter.len_mult)));
        if (!converter.buf) {
            SDL_FreeWAV(source_buffer);
            return false;
        }
        // copy source bytes into the converter working buffer
        std::memcpy(converter.buf, source_buffer, source_length);
        if (SDL_ConvertAudio(&converter) < 0) {
            SDL_FreeWAV(source_buffer);
            SDL_free(converter.buf);
            return false;
        }
        // after conversion sdl tells us how many bytes are now valid
        converted = converter.buf;
        converted_length = static_cast<Uint32>(converter.len_cvt);
    } else {
        // if no conversion is needed we still copy into our own owned buffer
        converted = static_cast<Uint8*>(SDL_malloc(source_length));
        if (!converted) {
            SDL_FreeWAV(source_buffer);
            return false;
        }
        std::memcpy(converted, source_buffer, source_length);
        converted_length = source_length;
    }

    // the temporary wav buffer from sdl is no longer needed after the final copy or conversion
    SDL_FreeWAV(source_buffer);
    // adopt the converted buffer as this objects owned state
    data_ = converted;
    length_ = converted_length;
    return data_ != nullptr && length_ > 0;
}

const Uint8* Sound::data() const {
    // lets the mixer read but not modify the sound data
    return data_;
}

Uint32 Sound::length() const {
    // byte count is what the audio code advances and compares against
    return length_;
}

void Sound::clear() {
    if (data_) {
        // buffer was allocated with sdl so free it with sdl too
        SDL_free(data_);
        data_ = nullptr;
    }
    // zero length makes the object clearly empty
    length_ = 0;
}
