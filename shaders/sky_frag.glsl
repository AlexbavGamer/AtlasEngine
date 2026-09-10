#version 450

// Procedural sky: vertical gradient (ground/horizon/zenith) + sun disk +
// halo, driven by the scene Sun direction. Push block MUST match
// Atlas::SkyPushConstants in src/renderer/renderer.h exactly (std430).
layout(push_constant) uniform SkyPC {
    mat4 invViewProj;
    vec4 sunDir;
    vec4 horizonColor;
    vec4 zenithColor;
    vec4 groundColor;
    vec4 sunColorSize;
    vec4 params;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 ndc = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 w = pc.invViewProj * ndc;
    vec3 dir = normalize(w.xyz / max(w.w, 1e-4));

    float h = dir.y;
    vec3 col = (h >= 0.0)
        ? mix(pc.horizonColor.rgb, pc.zenithColor.rgb, pow(clamp(h, 0.0, 1.0), 0.6))
        : mix(pc.horizonColor.rgb, pc.groundColor.rgb, pow(clamp(-h, 0.0, 1.0), 0.5));
    // Night cycle: fade the gradient to a deep blue-black as the sun drops
    // (full day above +0.25, full night below -0.15). Disk/glow below keep
    // their own gating so sunset lingers at the horizon.
    vec3 toSun = normalize(pc.sunDir.xyz);
    float dayness = smoothstep(-0.15, 0.25, toSun.y);
    col *= mix(vec3(0.025, 0.035, 0.07), vec3(1.0), dayness);

    float cosAng = dot(dir, toSun);
    float sizeRad = radians(clamp(pc.sunColorSize.a, 0.1, 30.0));
    // edges ordered (inner > outer in cos space is invalid for smoothstep).
    float disk = smoothstep(cos(sizeRad), cos(sizeRad * 0.7), cosAng);
    float glow = pow(max(cosAng, 0.0), 24.0) * pc.params.x;
    // Fade the disk below the horizon so sunset dips out of view.
    float sunUp = smoothstep(-0.08, 0.02, toSun.y);
    col += pc.sunColorSize.rgb * (disk * sunUp + glow);

    outColor = vec4(col, 1.0);
}
