#include "common.hlsli"

float4 FloorPS(float4 sp : SV_POSITION) : SV_TARGET
{
    float2 xy = float2(sp.x, viewSize.y - sp.y);
    float s = shadowAt(float3(xy, floorZ), float2(0.0, 0.0), 0.5, sp.xy);
    float a = s * shadowStrength;
    return float4(float3(0.035, 0.02, 0.01) * a, a);
}
