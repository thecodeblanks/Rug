#include "common.hlsli"

float ShadowPS(SOut i) : SV_TARGET { return i.z; }
