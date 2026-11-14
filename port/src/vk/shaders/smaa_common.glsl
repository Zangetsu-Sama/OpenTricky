/* SMAA 1x for the Vulkan renderer (vkr.c smaa_run): the reference shader
 * (xboxrecomp/src/d3d/smaa/SMAA.hlsl, as OpenTricky's D3D11 post uses it) in
 * its GLSL 4 mode. Built once per pass, stage and preset (CMakeLists.txt):
 * PASS 0 edge detection (luma), 1 blending weights, 2 neighbourhood blending;
 * PRESET 0..3 = low, medium, high, ultra. */
#extension GL_GOOGLE_include_directive : require
layout(push_constant) uniform PC { vec4 rt; } pc;   /* 1/w, 1/h, w, h */
#define SMAA_RT_METRICS pc.rt
#define SMAA_GLSL_4 1
#if PRESET == 0
#define SMAA_PRESET_LOW 1
#elif PRESET == 1
#define SMAA_PRESET_MEDIUM 1
#elif PRESET == 2
#define SMAA_PRESET_HIGH 1
#else
#define SMAA_PRESET_ULTRA 1
#endif
