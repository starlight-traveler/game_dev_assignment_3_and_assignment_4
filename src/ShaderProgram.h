/**
 * @file ShaderProgram.h
 * @brief small opengl shader program wrapper
 */
#ifndef SHADER_PROGRAM_H
#define SHADER_PROGRAM_H

#include <string>

#include <GL/glew.h>

/**
 * @brief owns one linked opengl program object
 *
 * the job of this class is
 * read shader text from files
 * compile both stages
 * link them into one program
 * then hold the final program id until destruction
 */
class ShaderProgram {
public:
    /**
     * @brief starts empty with no linked program
     */
    ShaderProgram();

    /**
     * @brief deletes the linked opengl program if one exists
     */
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    /**
     * @brief loads compiles and links a vertex plus fragment shader pair
     * @param vertex_path vertex shader file path
     * @param fragment_path fragment shader file path
     * @return true when every stage succeeds
     */
    bool loadFromFiles(const std::string& vertex_path, const std::string& fragment_path);

    /**
     * @brief returns the final linked program id
     * @return program id or 0 when not loaded
     */
    GLuint programId() const;

    /**
     * @brief looks up one uniform location in the linked program
     * @param name uniform variable name
     * @return location or negative one when unavailable
     */
    GLint uniformLocation(const char* name) const;

private:
    /**
     * @brief compiles one shader stage from source text
     * @param stage opengl shader stage enum
     * @param source shader source text
     * @return shader id or 0 on compile failure
     */
    GLuint compileStage(GLenum stage, const std::string& source) const;

    /**
     * @brief reads a text file into one string
     * @param path file path
     * @return file text or empty string on error
     */
    static std::string loadTextFile(const std::string& path);

    // opengl handle for the final linked program
    GLuint program_id_;
};

#endif
