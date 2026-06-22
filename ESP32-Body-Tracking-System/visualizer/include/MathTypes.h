// MathTypes.h
//
// Minimal vec3 / quat / mat4 math used for forward kinematics and GPU
// skinning matrix construction. If your project already depends on GLM,
// you can delete this file and swap these types 1:1 for glm::vec3 /
// glm::quat / glm::mat4 -- the function names below mirror GLM's where
// practical to make that swap mechanical.

#pragma once

#include <cmath>
#include <cstring>

namespace mocap {

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

struct Quat {
    float w = 1, x = 0, y = 0, z = 0;

    Quat normalized() const {
        float len = std::sqrt(w * w + x * x + y * y + z * z);
        if (len < 1e-8f) return {1, 0, 0, 0};
        return {w / len, x / len, y / len, z / len};
    }

    Quat operator*(const Quat& o) const {
        // Hamilton product: this * o
        return {
            w * o.w - x * o.x - y * o.y - z * o.z,
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w
        };
    }

    Vec3 rotate(const Vec3& v) const {
        // v' = q * v * q^-1, optimized form
        Vec3 u{x, y, z};
        float s = w;
        float uDotV = u.x * v.x + u.y * v.y + u.z * v.z;
        float uDotU = u.x * u.x + u.y * u.y + u.z * u.z;
        Vec3 uCrossV{
            u.y * v.z - u.z * v.y,
            u.z * v.x - u.x * v.z,
            u.x * v.y - u.y * v.x
        };
        return u * (2.0f * uDotV) + v * (s * s - uDotU) + uCrossV * (2.0f * s);
    }
};

// Column-major 4x4, matches OpenGL's expected layout for glUniformMatrix4fv.
struct Mat4 {
    float m[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    static Mat4 identity() { return Mat4{}; }

    static Mat4 fromQuatTranslation(const Quat& q, const Vec3& t) {
        Mat4 result;
        float w = q.w, x = q.x, y = q.y, z = q.z;
        float xx = x*x, yy = y*y, zz = z*z;
        float xy = x*y, xz = x*z, yz = y*z;
        float wx = w*x, wy = w*y, wz = w*z;

        // Column-major fill
        result.m[0] = 1 - 2*(yy+zz); result.m[1] = 2*(xy+wz);     result.m[2] = 2*(xz-wy);     result.m[3] = 0;
        result.m[4] = 2*(xy-wz);     result.m[5] = 1 - 2*(xx+zz); result.m[6] = 2*(yz+wx);     result.m[7] = 0;
        result.m[8] = 2*(xz+wy);     result.m[9] = 2*(yz-wx);     result.m[10] = 1 - 2*(xx+yy);result.m[11] = 0;
        result.m[12] = t.x; result.m[13] = t.y; result.m[14] = t.z; result.m[15] = 1;
        return result;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k * 4 + row] * o.m[col * 4 + k];
                }
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }

    static Mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar) {
        Mat4 r;
        std::memset(r.m, 0, sizeof(r.m));
        float f = 1.0f / std::tan(fovYRadians / 2.0f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = center - eye;
        float fLen = std::sqrt(f.x*f.x + f.y*f.y + f.z*f.z);
        f = {f.x/fLen, f.y/fLen, f.z/fLen};

        Vec3 s{
            f.y*up.z - f.z*up.y,
            f.z*up.x - f.x*up.z,
            f.x*up.y - f.y*up.x
        };
        float sLen = std::sqrt(s.x*s.x + s.y*s.y + s.z*s.z);
        s = {s.x/sLen, s.y/sLen, s.z/sLen};

        Vec3 u{
            s.y*f.z - s.z*f.y,
            s.z*f.x - s.x*f.z,
            s.x*f.y - s.y*f.x
        };

        Mat4 r;
        r.m[0]=s.x; r.m[4]=s.y; r.m[8]=s.z;  r.m[12]=-(s.x*eye.x+s.y*eye.y+s.z*eye.z);
        r.m[1]=u.x; r.m[5]=u.y; r.m[9]=u.z;  r.m[13]=-(u.x*eye.x+u.y*eye.y+u.z*eye.z);
        r.m[2]=-f.x;r.m[6]=-f.y;r.m[10]=-f.z;r.m[14]=(f.x*eye.x+f.y*eye.y+f.z*eye.z);
        r.m[3]=0;   r.m[7]=0;   r.m[11]=0;   r.m[15]=1;
        return r;
    }
};

} // namespace mocap
