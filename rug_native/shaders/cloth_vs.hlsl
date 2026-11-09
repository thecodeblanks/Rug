#include "common.hlsli"

VOut VSMain(VIn i)
{
    float2 c = viewSize * 0.5;
    float w = (camD - i.pos.z) / camD;
    VOut o;
    o.sv    = float4((i.pos.xy - c) / c, depthFromZ(i.pos.z) * w, w);
    o.world = i.pos;
    o.nrm   = i.nrm;
    o.uv    = i.uv;
    return o;
}
