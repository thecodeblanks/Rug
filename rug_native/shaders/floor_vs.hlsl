#include "common.hlsli"

float4 FloorVS(uint id : SV_VertexID) : SV_POSITION
{
    float x = (id == 1) ? 3.0 : -1.0;
    float y = (id == 2) ? 3.0 : -1.0;
    return float4(x, y, 0.0, 1.0);
}
