// Renderer.cpp — safe version: shows bind pose when no sensors connected,
// no segfault, handles any bone count returned by the GLB loader.

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include "Renderer.h"
#include "Skeleton.h"

#include <cmath>
#include <cstdio>
#include <algorithm>

namespace mocap {

// ─── tiny inline math helpers ────────────────────────────────────────────────

static Mat4 identity() {
    Mat4 m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    return m;
}

static Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    float f = 1.0f / std::tan(fovY * 0.5f);
    Mat4 m{};
    m.m[0][0] = f / aspect;
    m.m[1][1] = f;
    m.m[2][2] = (zf + zn) / (zn - zf);
    m.m[2][3] = -1.0f;
    m.m[3][2] = (2.0f * zf * zn) / (zn - zf);
    return m;
}

static Vec3 normalize(Vec3 v) {
    float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (len < 1e-6f) return {0,1,0};
    return {v.x/len, v.y/len, v.z/len};
}

static Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}

static float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = normalize({center.x-eye.x, center.y-eye.y, center.z-eye.z});
    Vec3 r = normalize(cross(f, up));
    Vec3 u = cross(r, f);
    Mat4 m{};
    m.m[0][0]=r.x; m.m[1][0]=r.y; m.m[2][0]=r.z;
    m.m[0][1]=u.x; m.m[1][1]=u.y; m.m[2][1]=u.z;
    m.m[0][2]=-f.x;m.m[1][2]=-f.y;m.m[2][2]=-f.z;
    m.m[3][0]=-dot(r,eye);
    m.m[3][1]=-dot(u,eye);
    m.m[3][2]= dot(f,eye);
    m.m[3][3]=1.0f;
    return m;
}

static Mat4 translate(float tx, float ty, float tz) {
    Mat4 m = identity();
    m.m[3][0] = tx; m.m[3][1] = ty; m.m[3][2] = tz;
    return m;
}

static Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i=0;i<4;i++)
        for (int j=0;j<4;j++)
            for (int k=0;k<4;k++)
                r.m[i][j] += a.m[i][k]*b.m[k][j];
    return r;
}

// Convert quaternion to 4×4 rotation matrix
static Mat4 quatToMat4(const Quat& q) {
    float w=q.w, x=q.x, y=q.y, z=q.z;
    Mat4 m{};
    m.m[0][0]=1-2*(y*y+z*z); m.m[1][0]=  2*(x*y-w*z); m.m[2][0]=  2*(x*z+w*y);
    m.m[0][1]=  2*(x*y+w*z); m.m[1][1]=1-2*(x*x+z*z); m.m[2][1]=  2*(y*z-w*x);
    m.m[0][2]=  2*(x*z-w*y); m.m[1][2]=  2*(y*z+w*x); m.m[2][2]=1-2*(x*x+y*y);
    m.m[3][3]=1.0f;
    return m;
}

// ─── GLSL shaders ────────────────────────────────────────────────────────────

static const char* VERT_SRC = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in ivec4 aBoneIdx;
layout(location=4) in vec4 aBoneWeight;

uniform mat4 uMVP;
uniform mat4 uBoneMatrices[128];
uniform bool uSkinningEnabled;

out vec3 vNormal;
out vec3 vPos;

void main() {
    vec4 pos = vec4(aPos, 1.0);
    vec3 norm = aNormal;

    if (uSkinningEnabled) {
        // Clamp bone indices to safe range (avoid OOB if model has more bones than expected)
        mat4 skin =
            aBoneWeight.x * uBoneMatrices[clamp(aBoneIdx.x, 0, 127)] +
            aBoneWeight.y * uBoneMatrices[clamp(aBoneIdx.y, 0, 127)] +
            aBoneWeight.z * uBoneMatrices[clamp(aBoneIdx.z, 0, 127)] +
            aBoneWeight.w * uBoneMatrices[clamp(aBoneIdx.w, 0, 127)];

        // Only apply if weights sum to something reasonable
        float wsum = aBoneWeight.x + aBoneWeight.y + aBoneWeight.z + aBoneWeight.w;
        if (wsum > 0.01) {
            pos  = skin * pos;
            norm = mat3(skin) * norm;
        }
    }

    gl_Position = uMVP * pos;
    vNormal = normalize(norm);
    vPos    = pos.xyz;
}
)glsl";

static const char* FRAG_SRC = R"glsl(
#version 330 core
in vec3 vNormal;
in vec3 vPos;

uniform vec3 uAlbedo;

out vec4 FragColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.8));
    float diff = max(dot(normalize(vNormal), lightDir), 0.0);
    vec3 col = uAlbedo * (0.25 + 0.75 * diff);
    FragColor = vec4(col, 1.0);
}
)glsl";

// ─── Renderer impl ───────────────────────────────────────────────────────────

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
        fprintf(stderr, "Shader error: %s\n", log);
    }
    return s;
}

bool Renderer::init(const std::string& title, int w, int h) {
    if (!glfwInit()) { fprintf(stderr,"GLFW init failed\n"); return false; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(w, h, title.c_str(), nullptr, nullptr);
    if (!window_) { fprintf(stderr,"Window creation failed\n"); glfwTerminate(); return false; }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        fprintf(stderr,"GLAD init failed\n"); return false;
    }

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

    // Compile shaders
    GLuint vs = compileShader(GL_VERTEX_SHADER, VERT_SRC);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAG_SRC);
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs); glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    glDeleteShader(vs); glDeleteShader(fs);

    GLint ok; glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) { char log[512]; glGetProgramInfoLog(prog_,512,nullptr,log); fprintf(stderr,"Link: %s\n",log); return false; }

    // Cache uniform locations
    locMVP_            = glGetUniformLocation(prog_, "uMVP");
    locBones_          = glGetUniformLocation(prog_, "uBoneMatrices");
    locAlbedo_         = glGetUniformLocation(prog_, "uAlbedo");
    locSkinEnabled_    = glGetUniformLocation(prog_, "uSkinningEnabled");

    return true;
}

bool Renderer::shouldClose() const {
    return window_ && glfwWindowShouldClose(window_);
}

void Renderer::pollEvents() { glfwPollEvents(); }

bool Renderer::isKeyPressed(int glfwKey) const {
    return window_ && (glfwGetKey(window_, glfwKey) == GLFW_PRESS);
}

// ─── detect GLB model scale from its bone positions ──────────────────────────
// Returns approximate scale: 1.0 for metre-scale, 100.0 for centimetre-scale

static float detectModelScale(const std::vector<BoneInfo>& bones) {
    // Find the "head" or topmost bone Y in inverse bind matrix translation
    float maxY = 0.0f;
    for (auto& b : bones) {
        // inverseBindMatrix column 3 = world position of bind pose (negated)
        float y = std::abs(b.inverseBindMatrix.m[3][1]);
        if (y > maxY) maxY = y;
    }
    // If max Y > 10, model is in centimetres
    return (maxY > 10.0f) ? 100.0f : 1.0f;
}

// ─── upload bone matrices (identity = show bind pose) ────────────────────────

static void uploadIdentityBones(GLint loc, int count) {
    // Upload identity matrices — GPU skinning has zero effect, shows bind pose
    static float identData[128 * 16];
    static bool filled = false;
    if (!filled) {
        filled = true;
        for (int i = 0; i < 128; i++) {
            float* p = identData + i * 16;
            for (int j = 0; j < 16; j++) p[j] = 0.0f;
            p[0] = p[5] = p[10] = p[15] = 1.0f; // identity diagonal
        }
    }
    int safe = std::min(count, 128);
    glUniformMatrix4fv(loc, safe, GL_FALSE, identData);
}

// ─── forward kinematics: compute world bone matrices from sensor quaternions ──

static void computeBoneMatrices(
    const Pose& pose,
    const std::vector<BoneInfo>& bones,
    float scale,
    std::vector<Mat4>& out)
{
    int n = (int)bones.size();
    out.resize(n);

    // world transforms
    std::vector<Mat4> world(n, identity());

    // Map sensor IDs to bone indices in the GLB
    // sensorToBone[sensorId] = bone index in model
    int sensorToBoneIdx[NUM_SENSORS];
    for (int s = 0; s < NUM_SENSORS; s++) sensorToBoneIdx[s] = -1;

    for (int bi = 0; bi < n; bi++) {
        for (int s = 0; s < NUM_SENSORS; s++) {
            if (bones[bi].name == SKELETON[s].boneName) {
                sensorToBoneIdx[s] = bi;
                break;
            }
        }
    }

    // Process bones in order (parent before child guaranteed by Assimp traversal)
    for (int bi = 0; bi < n; bi++) {
        // Find if any sensor drives this bone
        Quat q = {1.0f, 0.0f, 0.0f, 0.0f}; // identity
        for (int s = 0; s < NUM_SENSORS; s++) {
            if (sensorToBoneIdx[s] == bi) { q = pose.q[s]; break; }
        }

        Mat4 localRot = quatToMat4(q);

        int parent = bones[bi].parentBoneIndex;
        if (parent < 0) {
            // Root: place at origin
            world[bi] = localRot;
            world[bi].m[3][1] = 0.0f; // keep at ground
        } else {
            world[bi] = mul(world[parent], localRot);
        }

        // Skinning matrix = world * inverseBindMatrix
        out[bi] = mul(world[bi], bones[bi].inverseBindMatrix);
    }
}

// ─── check if any sensor has moved from identity ──────────────────────────────

static bool anyNonIdentity(const Pose& pose) {
    for (int i = 0; i < NUM_SENSORS; i++) {
        const Quat& q = pose.q[i];
        if (std::abs(q.w - 1.0f) > 0.01f ||
            std::abs(q.x) > 0.01f ||
            std::abs(q.y) > 0.01f ||
            std::abs(q.z) > 0.01f) return true;
    }
    return false;
}

// ─── render one skeleton ──────────────────────────────────────────────────────

void Renderer::drawSkeleton(const Model& model, const Pose& pose,
                             float xOffset, Vec3 albedo)
{
    if (!model.vao_ && model.vao_ == 0) return; // model not loaded

    int boneCount = model.boneCount();
    if (boneCount == 0) {
        // No bones — just draw mesh with identity
        glUniform1i(locSkinEnabled_, 0);
    } else {
        glUniform1i(locSkinEnabled_, 1);

        if (!anyNonIdentity(pose)) {
            // No sensors sending — show bind pose (identity bones)
            uploadIdentityBones(locBones_, boneCount);
        } else {
            float scale = detectModelScale(model.bones());
            std::vector<Mat4> boneMatrices;
            computeBoneMatrices(pose, model.bones(), scale, boneMatrices);

            int safe = std::min(boneCount, 128);
            // Upload as column-major flat array
            std::vector<float> flat(safe * 16);
            for (int i = 0; i < safe; i++) {
                const float* src = &boneMatrices[i].m[0][0];
                std::copy(src, src + 16, flat.data() + i * 16);
            }
            glUniformMatrix4fv(locBones_, safe, GL_FALSE, flat.data());
        }
    }

    // Model matrix: translate by xOffset
    int w, h; glfwGetFramebufferSize(window_, &w, &h);
    float aspect = (h > 0) ? float(w)/float(h) : 1.0f;

    float scale = detectModelScale(model.bones());
    float camY  = 0.85f * scale;
    float camZ  = 3.5f  * scale;

    Mat4 view = lookAt({xOffset, camY, camZ},
                       {xOffset, 0.8f * scale, 0.0f},
                       {0.0f, 1.0f, 0.0f});
    Mat4 proj = perspective(50.0f * 3.14159265f / 180.0f, aspect, 0.1f * scale, 500.0f * scale);
    Mat4 model_mat = translate(xOffset, 0.0f, 0.0f);
    Mat4 mvp = mul(proj, mul(view, model_mat));

    glUniformMatrix4fv(locMVP_, 1, GL_FALSE, &mvp.m[0][0]);
    glUniform3f(locAlbedo_, albedo.x, albedo.y, albedo.z);

    model.draw();
}

// ─── public render: single live view ─────────────────────────────────────────

void Renderer::render(const Model& model, const Pose& pose) {
    if (!window_) return;

    int w, h; glfwGetFramebufferSize(window_, &w, &h);
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(prog_);
    drawSkeleton(model, pose, 0.0f, {0.7f, 0.75f, 0.9f});

    glfwSwapBuffers(window_);
}

// ─── public renderCompare: two skeletons side by side ────────────────────────

void Renderer::renderCompare(const Model& model,
                              const Pose& poseA, const Pose& poseB)
{
    if (!window_) return;

    int w, h; glfwGetFramebufferSize(window_, &w, &h);
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(prog_);

    float scale = (model.boneCount() > 0)
        ? detectModelScale(model.bones()) : 1.0f;
    float sep = 1.5f * scale; // separation between the two

    // Left: reference (blue tint)
    drawSkeleton(model, poseA, -sep, {0.5f, 0.65f, 1.0f});
    // Right: student (orange tint)
    drawSkeleton(model, poseB,  sep, {1.0f, 0.7f, 0.4f});

    glfwSwapBuffers(window_);
}

void Renderer::shutdown() {
    if (prog_)   { glDeleteProgram(prog_); prog_ = 0; }
    if (window_) { glfwDestroyWindow(window_); window_ = nullptr; }
    glfwTerminate();
}

} // namespace mocap
