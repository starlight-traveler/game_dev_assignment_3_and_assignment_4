#include "ShaderProgram.h"

#include <fstream>
#include <sstream>

ShaderProgram::ShaderProgram() : program_id_(0) {}

ShaderProgram::~ShaderProgram() {
    if (program_id_ != 0) {
        // release the currently owned linked program
        glDeleteProgram(program_id_);
        program_id_ = 0;
    }
}

bool ShaderProgram::loadFromFiles(const std::string& vertex_path, const std::string& fragment_path) {
    // first read both files fully from disk
    const std::string vertex_source = loadTextFile(vertex_path);
    const std::string fragment_source = loadTextFile(fragment_path);
    if (vertex_source.empty() || fragment_source.empty()) {
        // empty text here means read failure or empty file
        return false;
    }

    // compile each stage separately because opengl links compiled stage objects into a program
    const GLuint vertex_shader = compileStage(GL_VERTEX_SHADER, vertex_source);
    if (vertex_shader == 0) {
        return false;
    }
    const GLuint fragment_shader = compileStage(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return false;
    }

    // once both stage objects exist we can create the final program object
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    // shaders are only needed for linking
    // after link we can delete the stage objects whether link worked or not
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        // failed link means the program object is useless
        glDeleteProgram(program);
        return false;
    }

    if (program_id_ != 0) {
        // replace any older linked program we were already holding
        glDeleteProgram(program_id_);
    }
    program_id_ = program;
    return true;
}

GLuint ShaderProgram::programId() const {
    // renderer uses this for glUseProgram
    return program_id_;
}

GLint ShaderProgram::uniformLocation(const char* name) const {
    if (program_id_ == 0 || name == nullptr) {
        // no valid program or no valid name means no lookup
        return -1;
    }
    return glGetUniformLocation(program_id_, name);
}

GLuint ShaderProgram::compileStage(GLenum stage, const std::string& source) const {
    // opengl wants a raw c string pointer to the shader text
    const char* source_ptr = source.c_str();
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source_ptr, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        // throw away the stage object if compilation failed
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

std::string ShaderProgram::loadTextFile(const std::string& path) {
    // plain ifstream read because shader files are just text
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }
    std::ostringstream stream;
    // dump the whole file into one string for glShaderSource
    stream << input.rdbuf();
    return stream.str();
}
