/**
 * @file Texture2D.h
 * @brief RAII wrapper for OpenGL 2D textures
 */
#ifndef TEXTURE2D_H
#define TEXTURE2D_H

#include <string>

#include <GL/glew.h>

/**
 * @brief Owns a single OpenGL texture object
 */
class Texture2D {
public:
    /**
     * @brief Constructs an empty texture wrapper
     */
    Texture2D();

    /**
     * @brief Releases owned OpenGL texture
     */
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    /**
     * @brief Loads a texture from a BMP image file
     * @param path BMP file path
     * @return True on success
     */
    bool loadFromBMP(const std::string& path);

    /**
     * @brief Creates a tiny fallback checkerboard texture
     */
    void createFallbackChecker();

    /**
     * @brief Binds this texture on a texture unit
     * @param texture_unit OpenGL texture unit enum
     */
    void bind(GLenum texture_unit) const;

    /**
     * @brief Returns OpenGL texture id
     * @return Texture id or 0
     */
    GLuint textureId() const;

private:
    /**
     * @brief Applies default sampler parameters and mipmap generation
     */
    void applySamplerState() const;

    GLuint texture_id_;
};

#endif
