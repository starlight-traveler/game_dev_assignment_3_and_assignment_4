#include "ShaderProgram.h"

#include <fstream>
#include <sstream>

ShaderProgram::ShaderProgram() : program_id_(0) {}

ShaderProgram::~ShaderProgram() {
    if (program_id_ != 0) {
        glDeleteProgram(program_id_);
        program_id_ = 0;
    }
}

bool ShaderProgram::loadFromFiles(const std::string& vertex_path, const std::string& fragment_path) {
    const std::string vertex_source = loadTextFile(vertex_path);
    const std::string fragment_source = loadTextFile(fragment_path);
    if (vertex_source.empty() || fragment_source.empty()) {
        return false;
    }

    const GLuint vertex_shader = compileStage(GL_VERTEX_SHADER, vertex_source);
    if (vertex_shader == 0) {
        return false;
    }
    const GLuint fragment_shader = compileStage(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        glDeleteProgram(program);
        return false;
    }

    if (program_id_ != 0) {
        glDeleteProgram(program_id_);
    }
    program_id_ = program;
    return true;
}

GLuint ShaderProgram::programId() const {
    return program_id_;
}

GLint ShaderProgram::uniformLocation(const char* name) const {
    if (program_id_ == 0 || name == nullptr) {
        return -1;
    }
    return glGetUniformLocation(program_id_, name);
}

GLuint ShaderProgram::compileStage(GLenum stage, const std::string& source) const {
    const char* source_ptr = source.c_str();
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source_ptr, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

std::string ShaderProgram::loadTextFile(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}
