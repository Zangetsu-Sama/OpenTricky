#version 450
#define SMAA_INCLUDE_PS 0
#include "smaa_common.glsl"
#include "SMAA.hlsl"
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_off[3];
layout(location = 4) out vec2 v_pix;
void main()
{
    vec2 t = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec4 o[3];
    gl_Position = vec4(t * 2.0 - 1.0, 0.0, 1.0);
    v_uv = t;
    v_pix = vec2(0.0);
#if PASS == 0
    SMAAEdgeDetectionVS(t, o);
#elif PASS == 1
    SMAABlendingWeightCalculationVS(t, v_pix, o);
#else
    SMAANeighborhoodBlendingVS(t, o[0]);
    o[1] = o[2] = vec4(0.0);
#endif
    v_off[0] = o[0]; v_off[1] = o[1]; v_off[2] = o[2];
}
