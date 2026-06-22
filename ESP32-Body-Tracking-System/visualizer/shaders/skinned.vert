#version 330 core

// GPU skinning vertex shader. Each vertex carries up to 4 bone indices
// and weights (standard glTF skinning attributes); the actual bone
// transform is looked up from a uniform array uploaded once per frame
// by the CPU-side FK pass (Renderer::computeBoneMatrices in Renderer.cpp).

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in ivec4 aBoneIndices;
layout(location = 4) in vec4 aBoneWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

// Final (parent-relative * skin-offset) matrices, one per bone, computed
// on the CPU each frame from the live/recorded quaternion pose.
uniform mat4 uBoneMatrices[64];

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    mat4 skinMatrix =
        aBoneWeights.x * uBoneMatrices[aBoneIndices.x] +
        aBoneWeights.y * uBoneMatrices[aBoneIndices.y] +
        aBoneWeights.z * uBoneMatrices[aBoneIndices.z] +
        aBoneWeights.w * uBoneMatrices[aBoneIndices.w];

    // Vertices with no bone influence (weights all zero, e.g. unrigged
    // accessory geometry) fall back to identity skinning.
    float weightSum = aBoneWeights.x + aBoneWeights.y + aBoneWeights.z + aBoneWeights.w;
    if (weightSum < 0.001) {
        skinMatrix = mat4(1.0);
    }

    vec4 skinnedPos = skinMatrix * vec4(aPosition, 1.0);
    vec4 worldPos = uModel * skinnedPos;

    mat3 normalMatrix = mat3(transpose(inverse(uModel * skinMatrix)));
    vNormal = normalize(normalMatrix * aNormal);

    vWorldPos = worldPos.xyz;
    vTexCoord = aTexCoord;

    gl_Position = uProjection * uView * worldPos;
}
