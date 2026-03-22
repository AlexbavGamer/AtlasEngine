#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 baseColor;
    vec4 emissiveFactor;
    float metallic;
    float roughness;
    float alphaCutoff;
    int albedoTexIndex;
    int normalTexIndex;
    int metallicRoughnessTexIndex;
    int aoTexIndex;
    int emissiveTexIndex;
    int flags;
} pc;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

#define MAX_LIGHTS 4
#define MAX_TEXTURES 64

struct Light {
    vec3 position;
    float intensity;
    vec3 color;
    float padding;
};

layout(set = 0, binding = 0) uniform LightBuffer {
    Light lights[MAX_LIGHTS];
    int lightCount;
    vec3 cameraPos;
    float padding;
} lightData;

layout(set = 0, binding = 1) uniform sampler2D textureSamplers[MAX_TEXTURES];

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invMax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invMax, B * invMax, N);
}

void main() {
    const int FLAG_HAS_ALBEDO = 1 << 0;
    const int FLAG_HAS_NORMAL = 1 << 1;
    const int FLAG_HAS_MR = 1 << 2;
    const int FLAG_HAS_AO = 1 << 3;
    const int FLAG_HAS_EMISSIVE = 1 << 4;
    const int FLAG_DOUBLE_SIDED = 1 << 5;

    const int ALPHA_MODE_SHIFT = 8;
    const int ALPHA_MODE_MASK = 3 << ALPHA_MODE_SHIFT;
    const int ALPHA_OPAQUE = 0;
    const int ALPHA_MASK = 1;
    const int ALPHA_BLEND = 2;

    vec4 albedoColor = pc.baseColor * vec4(fragColor, 1.0);
    if ((pc.flags & FLAG_HAS_ALBEDO) != 0) {
        albedoColor *= texture(textureSamplers[pc.albedoTexIndex], fragTexCoord);
    }

    int alphaMode = (pc.flags & ALPHA_MODE_MASK) >> ALPHA_MODE_SHIFT;
    float alpha = albedoColor.a;

    if (alphaMode == ALPHA_OPAQUE) {
        alpha = 1.0;
    } else if (alphaMode == ALPHA_MASK) {
        float cutoff = pc.alphaCutoff;
        if (cutoff <= 0.0) cutoff = 0.5;
        if (alpha < cutoff) {
            discard;
        }
        alpha = 1.0;
    } else {
        // ALPHA_BLEND: keep alpha
    }

    vec3 N = normalize(fragNormal);
    if ((pc.flags & FLAG_HAS_NORMAL) != 0) {
        vec3 mapN = texture(textureSamplers[pc.normalTexIndex], fragTexCoord).xyz * 2.0 - 1.0;
        mat3 TBN = cotangentFrame(N, fragWorldPos, fragTexCoord);
        N = normalize(TBN * mapN);
    }

    if ((pc.flags & FLAG_DOUBLE_SIDED) != 0 && !gl_FrontFacing) {
        N = -N;
    }

    float metallic = pc.metallic;
    float roughness = pc.roughness;
    if ((pc.flags & FLAG_HAS_MR) != 0) {
        vec4 mr = texture(textureSamplers[pc.metallicRoughnessTexIndex], fragTexCoord);
        // glTF convention: B=metallic, G=roughness
        metallic *= mr.b;
        roughness *= mr.g;
    }

    float ao = 1.0;
    if ((pc.flags & FLAG_HAS_AO) != 0) {
        ao = texture(textureSamplers[pc.aoTexIndex], fragTexCoord).r;
    }

    vec3 emissive = pc.emissiveFactor.rgb;
    if ((pc.flags & FLAG_HAS_EMISSIVE) != 0) {
        emissive *= texture(textureSamplers[pc.emissiveTexIndex], fragTexCoord).rgb;
    }

    vec3 V = normalize(lightData.cameraPos - fragWorldPos);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedoColor.rgb, metallic);

    vec3 Lo = vec3(0.0);

    for (int i = 0; i < lightData.lightCount; i++) {
        Light light = lightData.lights[i];

        vec3 L = normalize(light.position - fragWorldPos);
        vec3 H = normalize(V + L);

        float distance = length(light.position - fragWorldPos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = light.color * light.intensity * attenuation;

        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedoColor.rgb / PI + specular) * radiance * NdotL;
    }

    vec3 ambient = vec3(0.03) * albedoColor.rgb;
    vec3 color = (ambient + Lo) * ao + emissive;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, alpha);
}
