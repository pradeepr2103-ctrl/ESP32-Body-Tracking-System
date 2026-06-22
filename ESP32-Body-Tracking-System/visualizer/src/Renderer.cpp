// Renderer.cpp — fully self-contained, no Pose/SKELETON/NUM_SENSORS dependencies
// Matches Mat4 with flat m[16] array as used in your MathTypes.h

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include "Renderer.h"
#include "MotionRecorder.h"   // for Pose, Quat, NUM_SENSORS
#include "Skeleton.h"          // for SKELETON[], kNumSensors

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

namespace mocap {

// ─── Mat4 helpers using FLAT m[16] ───────────────────────────────────────────
// Column-major: m[col*4 + row]  i.e. m[0..3]=col0, m[4..7]=col1, etc.

static void mat4_identity(float* m) {
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_mul(const float* a, const float* b, float* out) {
    float tmp[16] = {};
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++)
                tmp[col*4+row] += a[k*4+row] * b[col*4+k];
    memcpy(out, tmp, 64);
}

static void mat4_perspective(float* m, float fovY, float aspect, float zn, float zf) {
    memset(m, 0, 64);
    float f = 1.0f / tanf(fovY * 0.5f);
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = (2.0f * zf * zn) / (zn - zf);
}

static void mat4_lookat(float* m,
    float ex, float ey, float ez,
    float cx, float cy, float cz,
    float ux, float uy, float uz)
{
    // forward
    float fx=cx-ex, fy=cy-ey, fz=cz-ez;
    float fl = sqrtf(fx*fx+fy*fy+fz*fz); fx/=fl; fy/=fl; fz/=fl;
    // right = forward x up
    float rx=fy*uz-fz*uy, ry=fz*ux-fx*uz, rz=fx*uy-fy*ux;
    float rl = sqrtf(rx*rx+ry*ry+rz*rz); rx/=rl; ry/=rl; rz/=rl;
    // up = right x forward
    float vx=ry*(-fz)-rz*(-fy), vy=rz*(-fx)-rx*(-fz), vz=rx*(-fy)-ry*(-fx);
    memset(m, 0, 64);
    m[0]=rx; m[1]=vx; m[2]=-fx;
    m[4]=ry; m[5]=vy; m[6]=-fy;
    m[8]=rz; m[9]=vz; m[10]=-fz;
    m[12]=-(rx*ex+ry*ey+rz*ez);
    m[13]=-(vx*ex+vy*ey+vz*ez);
    m[14]= (fx*ex+fy*ey+fz*ez);
    m[15]=1.0f;
}

static void mat4_translate(float* m, float tx, float ty, float tz) {
    mat4_identity(m);
    m[12]=tx; m[13]=ty; m[14]=tz;
}

// quaternion → column-major rotation matrix (flat[16])
static void quat_to_mat4(float* m, float qw, float qx, float qy, float qz) {
    memset(m, 0, 64);
    m[0]  = 1-2*(qy*qy+qz*qz);
    m[1]  =   2*(qx*qy+qw*qz);
    m[2]  =   2*(qx*qz-qw*qy);
    m[4]  =   2*(qx*qy-qw*qz);
    m[5]  = 1-2*(qx*qx+qz*qz);
    m[6]  =   2*(qy*qz+qw*qx);
    m[8]  =   2*(qx*qz+qw*qy);
    m[9]  =   2*(qy*qz-qw*qx);
    m[10] = 1-2*(qx*qx+qy*qy);
    m[15] = 1.0f;
}

// ─── GLSL shaders ─────────────────────────────────────────────────────────────

static const char* VERT = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in ivec4 aBoneIdx;
layout(location=4) in vec4  aBoneWeight;

uniform mat4 uMVP;
uniform mat4 uBones[128];
uniform bool uSkin;

out vec3 vN;

void main(){
    vec4 p = vec4(aPos,1.0);
    vec3 n = aNormal;
    if(uSkin){
        mat4 S =
            aBoneWeight.x * uBones[clamp(aBoneIdx.x,0,127)] +
            aBoneWeight.y * uBones[clamp(aBoneIdx.y,0,127)] +
            aBoneWeight.z * uBones[clamp(aBoneIdx.z,0,127)] +
            aBoneWeight.w * uBones[clamp(aBoneIdx.w,0,127)];
        float ws = aBoneWeight.x+aBoneWeight.y+aBoneWeight.z+aBoneWeight.w;
        if(ws>0.01){ p=S*p; n=mat3(S)*n; }
    }
    gl_Position = uMVP * p;
    vN = normalize(n);
}
)glsl";

static const char* FRAG = R"glsl(
#version 330 core
in  vec3 vN;
out vec4 FC;
uniform vec3 uColor;
void main(){
    float d = max(dot(normalize(vN), normalize(vec3(0.5,1.0,0.8))), 0.0);
    FC = vec4(uColor*(0.25+0.75*d), 1.0);
}
)glsl";

// ─── helpers ──────────────────────────────────────────────────────────────────

static GLuint compile(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[512]; glGetShaderInfoLog(s,512,nullptr,log); fprintf(stderr,"Shader: %s\n",log); }
    return s;
}

// ─── init ─────────────────────────────────────────────────────────────────────

bool Renderer::init(const std::string& title, int w, int h){
    if(!glfwInit()){ fprintf(stderr,"GLFW failed\n"); return false; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window_ = glfwCreateWindow(w,h,title.c_str(),nullptr,nullptr);
    if(!window_){ fprintf(stderr,"Window failed\n"); glfwTerminate(); return false; }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    if(!gladLoadGL((GLADloadfunc)glfwGetProcAddress)){ fprintf(stderr,"GLAD failed\n"); return false; }
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f,0.1f,0.13f,1.0f);

    GLuint vs=compile(GL_VERTEX_SHADER,VERT);
    GLuint fs=compile(GL_FRAGMENT_SHADER,FRAG);
    prog_ = glCreateProgram();
    glAttachShader(prog_,vs); glAttachShader(prog_,fs);
    glLinkProgram(prog_);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(prog_,GL_LINK_STATUS,&ok);
    if(!ok){ char log[512]; glGetProgramInfoLog(prog_,512,nullptr,log); fprintf(stderr,"Link: %s\n",log); return false; }

    locMVP_   = glGetUniformLocation(prog_,"uMVP");
    locBones_ = glGetUniformLocation(prog_,"uBones");
    locColor_ = glGetUniformLocation(prog_,"uColor");
    locSkin_  = glGetUniformLocation(prog_,"uSkin");
    return true;
}

// ─── detect scale from inverse bind matrices ──────────────────────────────────

static float detectScale(const std::vector<BoneInfo>& bones){
    float maxY = 0;
    for(auto& b : bones){
        // column-major flat: translation is at m[12],m[13],m[14]
        float y = fabsf(b.inverseBindMatrix.m[13]);
        if(y > maxY) maxY = y;
    }
    return (maxY > 10.0f) ? 100.0f : 1.0f;
}

// ─── check if any sensor has real data ───────────────────────────────────────

static bool anyLive(const Pose& pose){
    for(int i=0;i<NUM_SENSORS;i++){
        const Quat& q = pose.q[i];
        if(fabsf(q.w-1.0f)>0.01f || fabsf(q.x)>0.01f ||
           fabsf(q.y)>0.01f      || fabsf(q.z)>0.01f) return true;
    }
    return false;
}

// ─── upload identity bones (show bind pose) ──────────────────────────────────

static void uploadIdentity(GLint loc, int count){
    static float buf[128*16];
    static bool init=false;
    if(!init){
        init=true;
        memset(buf,0,sizeof(buf));
        for(int i=0;i<128;i++){ float* p=buf+i*16; p[0]=p[5]=p[10]=p[15]=1.0f; }
    }
    glUniformMatrix4fv(loc, std::min(count,128), GL_FALSE, buf);
}

// ─── forward kinematics ───────────────────────────────────────────────────────

static void computeSkinMatrices(const Pose& pose,
                                 const std::vector<BoneInfo>& bones,
                                 std::vector<float>& flatOut)
{
    int n = (int)bones.size();
    if(n==0) return;
    int safe = std::min(n,128);

    // world transforms (flat 16 each)
    std::vector<float> world(n*16);
    for(int i=0;i<n;i++) mat4_identity(world.data()+i*16);

    // map sensor to bone index
    int s2b[NUM_SENSORS];
    for(int s=0;s<NUM_SENSORS;s++) s2b[s]=-1;
    for(int bi=0;bi<safe;bi++){
        for(int s=0;s<NUM_SENSORS;s++){
            if(bones[bi].name == SKELETON[s].boneName){
                s2b[s]=bi; break;
            }
        }
    }

    for(int bi=0;bi<safe;bi++){
        // find quaternion for this bone
        float qw=1,qx=0,qy=0,qz=0;
        for(int s=0;s<NUM_SENSORS;s++){
            if(s2b[s]==bi){
                qw=pose.q[s].w; qx=pose.q[s].x;
                qy=pose.q[s].y; qz=pose.q[s].z;
                break;
            }
        }
        float rot[16]; quat_to_mat4(rot, qw,qx,qy,qz);

        int parent = bones[bi].parentBoneIndex;
        float* dst = world.data()+bi*16;
        if(parent<0 || parent>=safe){
            memcpy(dst, rot, 64);
        } else {
            mat4_mul(world.data()+parent*16, rot, dst);
        }
    }

    // skinning = world * inverseBindMatrix
    flatOut.resize(safe*16);
    for(int i=0;i<safe;i++){
        mat4_mul(world.data()+i*16,
                 bones[i].inverseBindMatrix.m,
                 flatOut.data()+i*16);
    }
}

// ─── drawSkeleton ─────────────────────────────────────────────────────────────

void Renderer::drawSkeleton(const Model& model, const Pose& pose,
                             float xOff, float r, float g, float b)
{
    int boneCount = model.boneCount();
    if(boneCount==0){
        glUniform1i(locSkin_, 0);
    } else {
        glUniform1i(locSkin_, 1);
        if(!anyLive(pose)){
            uploadIdentity(locBones_, boneCount);
        } else {
            std::vector<float> flat;
            computeSkinMatrices(pose, model.bones(), flat);
            int safe = std::min(boneCount,128);
            glUniformMatrix4fv(locBones_, safe, GL_FALSE, flat.data());
        }
    }

    // camera
    int vw,vh; glfwGetFramebufferSize(window_,&vw,&vh);
    float aspect = (vh>0)?(float)vw/vh:1.0f;
    float scale  = (boneCount>0) ? detectScale(model.bones()) : 1.0f;
    float camY   = 0.85f*scale;
    float camZ   = 3.5f*scale;

    float view[16], proj[16], modl[16], vp[16], mvp[16];
    mat4_lookat(view, xOff, camY, camZ,
                      xOff, 0.8f*scale, 0.0f,
                      0,1,0);
    mat4_perspective(proj, 50.0f*3.14159265f/180.0f, aspect, 0.01f*scale, 500.0f*scale);
    mat4_translate(modl, xOff, 0.0f, 0.0f);
    mat4_mul(proj, view, vp);
    mat4_mul(vp, modl, mvp);

    glUniformMatrix4fv(locMVP_, 1, GL_FALSE, mvp);
    glUniform3f(locColor_, r, g, b);

    model.draw();
}

// ─── public render ────────────────────────────────────────────────────────────

void Renderer::render(const Model& model, const Pose& pose){
    if(!window_) return;
    int w,h; glfwGetFramebufferSize(window_,&w,&h);
    glViewport(0,0,w,h);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glUseProgram(prog_);
    drawSkeleton(model, pose, 0.0f, 0.7f,0.75f,0.95f);
    glfwSwapBuffers(window_);
}

void Renderer::renderCompare(const Model& model, const Pose& poseA, const Pose& poseB){
    if(!window_) return;
    int w,h; glfwGetFramebufferSize(window_,&w,&h);
    glViewport(0,0,w,h);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glUseProgram(prog_);
    float scale = (model.boneCount()>0) ? detectScale(model.bones()) : 1.0f;
    float sep   = 1.5f*scale;
    drawSkeleton(model, poseA, -sep, 0.5f,0.7f,1.0f);   // blue = reference
    drawSkeleton(model, poseB,  sep, 1.0f,0.65f,0.3f);  // orange = student
    glfwSwapBuffers(window_);
}

// ─── misc ─────────────────────────────────────────────────────────────────────

bool Renderer::shouldClose() const { return window_ && glfwWindowShouldClose(window_); }
void Renderer::pollEvents()        { glfwPollEvents(); }
bool Renderer::isKeyPressed(int k) const { return window_ && glfwGetKey(window_,k)==GLFW_PRESS; }

void Renderer::shutdown(){
    if(prog_)   { glDeleteProgram(prog_); prog_=0; }
    if(window_) { glfwDestroyWindow(window_); window_=nullptr; }
    glfwTerminate();
}

} // namespace mocap
