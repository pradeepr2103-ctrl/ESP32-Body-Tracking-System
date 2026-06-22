// Shader.h
//
// Minimal GLSL program wrapper: load vertex+fragment source from disk,
// compile, link, report errors with the actual GLSL compiler log (not
// just a generic "shader failed").

#pragma once

#include <glad/gl.h>
#include <string>

#include "MathTypes.h"

namespace mocap {

class Shader {
public:
    Shader() = default;
    ~Shader();

    // Loads, compiles, and links. Returns false (and prints the GLSL
    // compiler's error log) on failure.
    bool loadFromFiles(const std::string& vertPath, const std::string& fragPath);

    void use() const;
    GLuint id() const { return programId_; }

    void setMat4(const std::string& name, const Mat4& m) const;
    void setVec3(const std::string& name, const Vec3& v) const;
    void setFloat(const std::string& name, float v) const;
    void setInt(const std::string& name, int v) const;
    // For GPU skinning: uploads an array of bone matrices in one call.
    void setMat4Array(const std::string& name, const Mat4* matrices, int count) const;

private:
    GLuint programId_ = 0;

    static std::string readFile(const std::string& path);
    static GLuint compile(GLenum type, const std::string& source, const std::string& debugName);
};

} // namespace mocap
