#include "Sound.h"

#include <cstring>

Sound::Sound() : data_(nullptr), length_(0) {}

Sound::~Sound() {
    clear();
}

Sound::Sound(Sound&& other) noexcept : data_(other.data_), length_(other.length_) {
    other.data_ = nullptr;
    other.length_ = 0;
}

Sound& Sound::operator=(Sound&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    clear();
    data_ = other.data_;
    length_ = other.length_;
    other.data_ = nullptr;
    other.length_ = 0;
    return *this;
}

bool Sound::loadWavMonoS8(const std::string& path, int target_frequency) {
    clear();

    SDL_AudioSpec source_spec{};
    Uint8* source_buffer = nullptr;
    Uint32 source_length = 0;
    if (SDL_LoadWAV(path.c_str(), &source_spec, &source_buffer, &source_length) == nullptr) {
        return false;
    }

    if (source_spec.channels != 1) {
        SDL_FreeWAV(source_buffer);
        return false;
    }

    SDL_AudioCVT converter{};
    const int build_result = SDL_BuildAudioCVT(
        &converter,
        source_spec.format, source_spec.channels, source_spec.freq,
        AUDIO_S8, 1, target_frequency);
    if (build_result < 0) {
        SDL_FreeWAV(source_buffer);
        return false;
    }

    Uint8* converted = nullptr;
    Uint32 converted_length = 0;
    if (converter.needed != 0) {
        converter.len = static_cast<int>(source_length);
        converter.buf = static_cast<Uint8*>(SDL_malloc(static_cast<size_t>(converter.len * converter.len_mult)));
        if (!converter.buf) {
            SDL_FreeWAV(source_buffer);
            return false;
        }
        std::memcpy(converter.buf, source_buffer, source_length);
        if (SDL_ConvertAudio(&converter) < 0) {
            SDL_FreeWAV(source_buffer);
            SDL_free(converter.buf);
            return false;
        }
        converted = converter.buf;
        converted_length = static_cast<Uint32>(converter.len_cvt);
    } else {
        converted = static_cast<Uint8*>(SDL_malloc(source_length));
        if (!converted) {
            SDL_FreeWAV(source_buffer);
            return false;
        }
        std::memcpy(converted, source_buffer, source_length);
        converted_length = source_length;
    }

    SDL_FreeWAV(source_buffer);
    data_ = converted;
    length_ = converted_length;
    return data_ != nullptr && length_ > 0;
}

const Uint8* Sound::data() const {
    return data_;
}

Uint32 Sound::length() const {
    return length_;
}

void Sound::clear() {
    if (data_) {
        SDL_free(data_);
        data_ = nullptr;
    }
    length_ = 0;
}
