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
 *
 * This class is intentionally small because its job is narrow:
 * it owns exactly one `GL_TEXTURE_2D` handle and provides a safe
 * interface for loading or generating texture data and binding it later
 *
 * The wrapper follows RAII so the OpenGL texture is deleted automatically
 * when the C++ object goes out of scope
 */
class Texture2D {
public:
    /**
     * @brief Constructs an empty texture wrapper
     *
     * Construction does not immediately create a GPU texture
     * The OpenGL object is created lazily only when real pixel data is uploaded
     */
    Texture2D();

    /**
     * @brief Releases owned OpenGL texture
     *
     * This prevents leaked GPU objects if a texture wrapper is destroyed
     * after asset loading, level shutdown, or engine shutdown
     */
    ~Texture2D();

    /**
     * @brief Disables copying because two wrappers cannot safely own the same OpenGL id
     */
    Texture2D(const Texture2D&) = delete;

    /**
     * @brief Disables copy assignment for the same ownership reason
     */
    Texture2D& operator=(const Texture2D&) = delete;

    /**
     * @brief Loads a texture from a BMP image file
     * @param path BMP file path
     * @return True on success
     *
     * The current engine keeps this loader simple by accepting BMP files through SDL
     * SDL handles the file parsing and gives us pixel data that we can normalize into
     * a consistent RGBA layout before sending it to OpenGL
     */
    bool loadFromBMP(const std::string& path);

    /**
     * @brief Creates a tiny fallback checkerboard texture
     *
     * This is used when an external texture is missing or when the engine wants
     * a guaranteed-valid texture for debugging and placeholder rendering
     */
    void createFallbackChecker();

    /**
     * @brief Binds this texture on a texture unit
     * @param texture_unit OpenGL texture unit enum
     *
     * The caller chooses the destination texture unit so this wrapper can be used
     * with shaders that expect textures on different sampler slots
     */
    void bind(GLenum texture_unit) const;

    /**
     * @brief Returns OpenGL texture id
     * @return Texture id or 0
     *
     * Returning the raw id is sometimes useful for diagnostics or for passing
     * the texture into lower-level rendering code that already speaks in OpenGL terms
     */
    GLuint textureId() const;

private:
    /**
     * @brief Applies default sampler parameters and mipmap generation
     *
     * After pixel data is uploaded, the texture still needs sampling rules:
     * wrap mode for out-of-range coordinates, minification and magnification filters,
     * and mipmaps for smaller on-screen representations
     */
    void applySamplerState() const;

    /**
     * @brief OpenGL object id for the owned 2D texture
     *
     * A value of zero means no GPU texture has been created yet
     */
    GLuint texture_id_;
};

#endif
