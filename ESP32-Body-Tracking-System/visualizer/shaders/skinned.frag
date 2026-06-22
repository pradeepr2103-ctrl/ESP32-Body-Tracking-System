#version 330 core

// Blinn-Phong lighting with a roughness/metalness-influenced specular
// term -- a practical middle ground between classic Blinn-Phong and full
// PBR (Cook-Torrance) that looks good on a humanoid mesh without needing
// an HDR environment map / IBL setup.

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform vec3 uViewPos;
uniform vec3 uLightDir;       // directional "sun" light, pointing FROM the light
uniform vec3 uLightColor;
uniform vec3 uAlbedo;
uniform float uRoughness;     // 0 = mirror-smooth highlight, 1 = very broad/soft
uniform float uMetalness;     // 0 = dielectric (skin/fabric), 1 = metal
uniform float uAmbientStrength;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 H = normalize(L + V);

    // Ambient
    vec3 ambient = uAmbientStrength * uAlbedo;

    // Diffuse (Lambertian) -- metals have ~no diffuse term
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = (1.0 - uMetalness) * NdotL * uAlbedo;

    // Specular (Blinn-Phong, shininess derived from roughness)
    float shininess = mix(256.0, 8.0, uRoughness);
    float NdotH = max(dot(N, H), 0.0);
    float specPower = pow(NdotH, shininess);

    // Metals tint specular by albedo; dielectrics get a neutral highlight.
    vec3 specColor = mix(vec3(0.04), uAlbedo, uMetalness);
    vec3 specular = specColor * specPower;

    vec3 color = ambient + (diffuse + specular) * uLightColor;

    // Simple Reinhard tonemap + gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
