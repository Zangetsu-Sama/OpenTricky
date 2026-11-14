#version 450
#define SMAA_INCLUDE_VS 0
#include "smaa_common.glsl"
#include "SMAA.hlsl"
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_off[3];
layout(location = 4) in vec2 v_pix;
layout(location = 0) out vec4 o_col;
layout(binding = 0) uniform sampler2D t0;   /* colour | edges | colour */
layout(binding = 1) uniform sampler2D t1;   /*   -    | area  | weights */
layout(binding = 2) uniform sampler2D t2;   /*   -    | search */
void main()
{
    vec4 o[3] = vec4[3](v_off[0], v_off[1], v_off[2]);
#if PASS == 0
    o_col = vec4(SMAALumaEdgeDetectionPS(v_uv, o, t0), 0.0, 0.0);
#elif PASS == 1
    o_col = SMAABlendingWeightCalculationPS(v_uv, v_pix, o, t0, t1, t2, vec4(0.0));
#else
    o_col = SMAANeighborhoodBlendingPS(v_uv, o[0], t0, t1);
#endif
}
