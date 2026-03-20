/**
 * @file ShaderProgram.h
 * @brief RAII wrapper for OpenGL shader program compilation and linking
 */
#ifndef SHADER_PROGRAM_H
#define SHADER_PROGRAM_H

#include <string>

#include <GL/glew.h>

/**
 * @brief Owns a linked OpenGL shader program
 */
class ShaderProgram {
public:
    /**
     * @brief Constructs an empty shader program wrapper
     */
    ShaderProgram();

    /**
     * @brief Releases owned OpenGL program
     */
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    /**
     * @brief Compiles and links program from source files
     * @param vertex_path Vertex shader file path
     * @param fragment_path Fragment shader file path
     * @return True on successful compile and link
     */
    bool loadFromFiles(const std::string& vertex_path, const std::string& fragment_path);

    /**
     * @brief Returns OpenGL program id
     * @return Program id or 0
     */
    GLuint programId() const;

    /**
     * @brief Finds a uniform location in this program
     * @param name Uniform variable name
     * @return Uniform location or -1
     */
    GLint uniformLocation(const char* name) const;

private:
    /**
     * @brief Compiles one shader stage
     * @param stage OpenGL stage enum
     * @param source Shader source text
     * @return Shader id or 0
     */
    GLuint compileStage(GLenum stage, const std::string& source) const;

    /**
     * @brief Loads plain text shader file content
     * @param path File path
     * @return File content or empty string on error
     */
    static std::string loadTextFile(const std::string& path);

    GLuint program_id_;
};

#endif
