#include "common.hlsli"

SOut ShadowVS(SIn i)
{
    float2 f = i.pos.xy - lightK * i.pos.z;
    float2 c = viewSize * 0.5;
    SOut o;
    o.sv = float4((f - c) / c, depthFromZ(i.pos.z), 1.0);
    o.z = i.pos.z;
    return o;
}
