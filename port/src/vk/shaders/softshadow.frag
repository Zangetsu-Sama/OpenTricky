#version 450
/* OpenTricky's soft shadows (xboxrecomp/src/d3d/d3d8_softshadow.c, same
 * passes): ONE the mask (1 where the title's stencil test passes), H / V the
 * depth-weighted Gaussian blur at half size, APPLY the scene times
 * mix(1, shade, mask). MS_DEPTH: the depth buffer is multisampled. */
layout(push_constant) uniform PC { vec4 size; vec4 prm; vec4 half_size; } pc;
layout(binding = 0) uniform sampler2D Src;
#ifdef MS_DEPTH
layout(binding = 1) uniform sampler2DMS Dep;
float depth_at(ivec2 c) { return texelFetch(Dep, c, 0).r; }
#else
layout(binding = 1) uniform sampler2D Dep;
float depth_at(ivec2 c) { return texelFetch(Dep, c, 0).r; }
#endif
layout(location = 0) out vec4 o;

ivec2 full_hi() { return ivec2(pc.size.xy) - 1; }
ivec2 half_hi() { return ivec2(pc.half_size.xy) - 1; }
float dweight(float z, float z0, float inv) { float dz = (z - z0) * inv; return 1.0 / (1.0 + dz * dz * pc.prm.w); }
float gweight(int i) { return exp2(-1.442695 * float(i * i) / (2.0 * pc.prm.z * pc.prm.z)); }
float src_load(ivec2 c) { return texelFetch(Src, clamp(c, ivec2(0), half_hi()), 0).r; }

void main()
{
#if defined(PASS_ONE)
    o = vec4(1.0);
#elif defined(PASS_H)
    ivec2 c = ivec2(gl_FragCoord.xy), f = c * 2;
    int r = int(pc.prm.y), i;
    float lo = 1.0, top = 0.0, m[33];
    for (i = -r; i <= r; i++) {
        vec2 uv = (vec2(f + ivec2(2 * i, 0)) + 1.0) * pc.size.zw;
        float v = textureLod(Src, uv, 0.0).r;
        m[i + r] = v; lo = min(lo, v); top = max(top, v);
    }
    if (top <= 0.0) { o = vec4(0.0); return; }
    if (lo >= 1.0) { o = vec4(1.0); return; }
    float z0 = depth_at(min(f, full_hi())), inv = 1.0 / max(1.0 - z0, 1e-5);
    float acc = 0.0, wsum = 0.0;
    for (i = -r; i <= r; i++) {
        ivec2 q = clamp(f + ivec2(2 * i, 0), ivec2(0), full_hi());
        float w = gweight(i) * dweight(depth_at(q), z0, inv);
        acc += w * m[i + r]; wsum += w;
    }
    o = vec4(acc / wsum);
#elif defined(PASS_V)
    ivec2 c = ivec2(gl_FragCoord.xy);
    int r = int(pc.prm.y), i;
    float lo = 1.0, top = 0.0, m[33];
    for (i = -r; i <= r; i++) {
        float v = src_load(c + ivec2(0, i));
        m[i + r] = v; lo = min(lo, v); top = max(top, v);
    }
    if (top <= 0.0) { o = vec4(0.0); return; }
    if (lo >= 1.0) { o = vec4(1.0); return; }
    float z0 = depth_at(min(c * 2, full_hi())), inv = 1.0 / max(1.0 - z0, 1e-5);
    float acc = 0.0, wsum = 0.0;
    for (i = -r; i <= r; i++) {
        ivec2 q = clamp(c + ivec2(0, i), ivec2(0), half_hi());
        float w = gweight(i) * dweight(depth_at(min(q * 2, full_hi())), z0, inv);
        acc += w * m[i + r]; wsum += w;
    }
    o = vec4(acc / wsum);
#else /* APPLY */
    vec2 hc = gl_FragCoord.xy * 0.5 - 0.5;
    ivec2 b = ivec2(floor(hc));
    vec2 t = hc - vec2(b);
    ivec2 off[4] = ivec2[4](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));
    float bw[4] = float[4]((1.0 - t.x) * (1.0 - t.y), t.x * (1.0 - t.y), (1.0 - t.x) * t.y, t.x * t.y);
    float v[4], lo = 1.0, top = 0.0, mask;
    int k;
    for (k = 0; k < 4; k++) { v[k] = src_load(b + off[k]); lo = min(lo, v[k]); top = max(top, v[k]); }
    if (top <= 0.0) { o = vec4(1.0); return; }
    if (lo >= 1.0) mask = 1.0;
    else {
        float z0 = depth_at(ivec2(gl_FragCoord.xy)), inv = 1.0 / max(1.0 - z0, 1e-5);
        float acc = 0.0, wsum = 1e-6;
        for (k = 0; k < 4; k++) {
            ivec2 q = clamp(b + off[k], ivec2(0), half_hi());
            float w = bw[k] * dweight(depth_at(min(q * 2, full_hi())), z0, inv);
            acc += w * v[k]; wsum += w;
        }
        mask = acc / wsum;
    }
    o = vec4(mix(1.0, pc.prm.x, mask));
#endif
}
