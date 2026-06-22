// Shader.cpp
#include "Shader.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace mocap {

Shader::~Shader() {
    if (programId_) glDeleteProgram(programId_);
}

std::string Shader::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Shader: failed to open " << path << "\n";
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint Shader::compile(GLenum type, const std::string& source, const std::string& debugName) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(logLen > 0 ? logLen : 1);
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        std::cerr << "Shader compile error [" << debugName << "]:\n"
                  << log.data() << "\n";
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::loadFromFiles(const std::string& vertPath, const std::string& fragPath) {
    std::string vertSrc = readFile(vertPath);
    std::string fragSrc = readFile(fragPath);
    if (vertSrc.empty() || fragSrc.empty()) return false;

    GLuint vs = compile(GL_VERTEX_SHADER, vertSrc, vertPath);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragSrc, fragPath);
    if (!vs || !fs) return false;

    programId_ = glCreateProgram();
    glAttachShader(programId_, vs);
    glAttachShader(programId_, fs);
    glLinkProgram(programId_);

    GLint linked = 0;
    glGetProgramiv(programId_, GL_LINK_STATUS, &linked);

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!linked) {
        GLint logLen = 0;
        glGetProgramiv(programId_, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(logLen > 0 ? logLen : 1);
        glGetProgramInfoLog(programId_, logLen, nullptr, log.data());
        std::cerr << "Shader link error [" << vertPath << " + " << fragPath << "]:\n"
                  << log.data() << "\n";
        glDeleteProgram(programId_);
        programId_ = 0;
        return false;
    }

    return true;
}

void Shader::use() const {
    glUseProgram(programId_);
}

void Shader::setMat4(const std::string& name, const Mat4& m) const {
    GLint loc = glGetUniformLocation(programId_, name.c_str());
    glUniformMatrix4fv(loc, 1, GL_FALSE, m.m);
}

void Shader::setVec3(const std::string& name, const Vec3& v) const {
    GLint loc = glGetUniformLocation(programId_, name.c_str());
    glUniform3f(loc, v.x, v.y, v.z);
}

void Shader::setFloat(const std::string& name, float v) const {
    GLint loc = glGetUniformLocation(programId_, name.c_str());
    glUniform1f(loc, v);
}

void Shader::setInt(const std::string& name, int v) const {
    GLint loc = glGetUniformLocation(programId_, name.c_str());
    glUniform1i(loc, v);
}

void Shader::setMat4Array(const std::string& name, const Mat4* matrices, int count) const {
    GLint loc = glGetUniformLocation(programId_, name.c_str());
    glUniformMatrix4fv(loc, count, GL_FALSE, matrices[0].m);
}

} // namespace mocap
