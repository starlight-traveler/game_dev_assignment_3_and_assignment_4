#include "Texture2D.h"

#include <array>

#include <SDL.h>

// Start with no GPU resource
// The actual OpenGL texture object is created only once pixel data is ready to upload
Texture2D::Texture2D() : texture_id_(0) {}

Texture2D::~Texture2D() {
    // OpenGL texture ids are external GPU resources
    // If this wrapper owns one, delete it so shutdown or hot-reload does not leak video memory
    if (texture_id_ != 0) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }
}

bool Texture2D::loadFromBMP(const std::string& path) {
    // An empty path cannot possibly identify a real file
    if (path.empty()) {
        return false;
    }

    // Ask SDL to decode the BMP file into a CPU-side surface
    // At this point the pixels are not on the GPU yet
    SDL_Surface* surface = SDL_LoadBMP(path.c_str());
    if (!surface) {
        return false;
    }

    // Convert whatever SDL loaded into a predictable 32-bit RGBA layout
    // This keeps the OpenGL upload path simple because glTexImage2D can now always
    // be called with GL_RGBA and GL_UNSIGNED_BYTE instead of branching on source formats
    SDL_Surface* rgba_surface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);

    // The original decoded surface is no longer needed after conversion
    SDL_FreeSurface(surface);
    if (!rgba_surface) {
        return false;
    }

    // Lazily create the OpenGL texture object the first time this wrapper receives image data
    if (texture_id_ == 0) {
        glGenTextures(1, &texture_id_);
    }

    // Bind the texture so all following texture calls affect this specific object
    glBindTexture(GL_TEXTURE_2D, texture_id_);

    // Upload the CPU pixel buffer into GPU texture memory
    // Parameters in order:
    // - target: we are uploading a 2D texture
    // - level: base mip level 0
    // - internal format: store as 8-bit RGBA on the GPU
    // - width and height: taken from the SDL surface
    // - border: legacy parameter, always 0
    // - source format and type: incoming pixels are RGBA unsigned bytes
    // - data pointer: SDL surface pixel buffer
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba_surface->w, rgba_surface->h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_surface->pixels);

    // Configure wrapping, filtering, and generate mipmaps now that the base image exists
    applySamplerState();

    // Unbind to avoid accidental modification from unrelated texture code later
    glBindTexture(GL_TEXTURE_2D, 0);

    // CPU-side staging data is no longer needed after the upload completes
    SDL_FreeSurface(rgba_surface);
    return true;
}

void Texture2D::createFallbackChecker() {
    // Ensure a GPU texture object exists even when no external asset was loaded
    if (texture_id_ == 0) {
        glGenTextures(1, &texture_id_);
    }

    // Hard-coded 4x4 checkerboard pixels in RGBA byte order
    // Alternating light and dark texels make UV issues easy to spot during rendering
    constexpr std::array<unsigned char, 4 * 4 * 4> kPixels = {
        210, 210, 210, 255, 40,  40,  40,  255, 210, 210, 210, 255, 40,  40,  40,  255,
        40,  40,  40,  255, 210, 210, 210, 255, 40,  40,  40,  255, 210, 210, 210, 255,
        210, 210, 210, 255, 40,  40,  40,  255, 210, 210, 210, 255, 40,  40,  40,  255,
        40,  40,  40,  255, 210, 210, 210, 255, 40,  40,  40,  255, 210, 210, 210, 255
    };

    // Bind the owned texture so the upload targets this object
    glBindTexture(GL_TEXTURE_2D, texture_id_);

    // Upload the small debug texture directly from the static array
    // Even placeholder textures should use the same sampler and mipmap path as normal assets
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, kPixels.data());
    applySamplerState();

    // Return OpenGL state to an unbound texture on this target
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::bind(GLenum texture_unit) const {
    // If no texture was created, leave the OpenGL state unchanged
    if (texture_id_ == 0) {
        return;
    }

    // Select the destination sampler slot first
    // For example, GL_TEXTURE0 is usually the first texture unit in a draw call setup
    glActiveTexture(texture_unit);

    // Then bind this texture object onto the 2D target for that unit
    glBindTexture(GL_TEXTURE_2D, texture_id_);
}

GLuint Texture2D::textureId() const {
    return texture_id_;
}

void Texture2D::applySamplerState() const {
    // Repeat texture coordinates outside the [0, 1] range
    // This is a sensible default for tiled surfaces like floors or walls
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Use trilinear filtering for minification so distant textures sample from mipmaps smoothly
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    // Use linear filtering when magnifying the texture on screen
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Build the mip chain from the base level that was just uploaded
    // This reduces aliasing and improves texture quality when geometry appears small on screen
    glGenerateMipmap(GL_TEXTURE_2D);
}
