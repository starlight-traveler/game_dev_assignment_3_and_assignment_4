#include "Texture2D.h"

#include <array>

#include <SDL.h>

Texture2D::Texture2D() : texture_id_(0) {}

Texture2D::~Texture2D() {
    if (texture_id_ != 0) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }
}

bool Texture2D::loadFromBMP(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    SDL_Surface* surface = SDL_LoadBMP(path.c_str());
    if (!surface) {
        return false;
    }

    SDL_Surface* rgba_surface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface);
    if (!rgba_surface) {
        return false;
    }

    if (texture_id_ == 0) {
        glGenTextures(1, &texture_id_);
    }

    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba_surface->w, rgba_surface->h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_surface->pixels);
    applySamplerState();
    glBindTexture(GL_TEXTURE_2D, 0);

    SDL_FreeSurface(rgba_surface);
    return true;
}

void Texture2D::createFallbackChecker() {
    if (texture_id_ == 0) {
        glGenTextures(1, &texture_id_);
    }

    constexpr std::array<unsigned char, 4 * 4 * 4> kPixels = {
        210, 210, 210, 255, 40,  40,  40,  255, 210, 210, 210, 255, 40,  40,  40,  255,
        40,  40,  40,  255, 210, 210, 210, 255, 40,  40,  40,  255, 210, 210, 210, 255,
        210, 210, 210, 255, 40,  40,  40,  255, 210, 210, 210, 255, 40,  40,  40,  255,
        40,  40,  40,  255, 210, 210, 210, 255, 40,  40,  40,  255, 210, 210, 210, 255
    };

    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, kPixels.data());
    applySamplerState();
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::bind(GLenum texture_unit) const {
    if (texture_id_ == 0) {
        return;
    }
    glActiveTexture(texture_unit);
    glBindTexture(GL_TEXTURE_2D, texture_id_);
}

GLuint Texture2D::textureId() const {
    return texture_id_;
}

void Texture2D::applySamplerState() const {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
}
