#include "common.hlsli"

float4 PSMain(VOut i, bool front : SV_IsFrontFace, float4 sp : SV_POSITION) : SV_TARGET
{
    float3 Ng = normalize(i.nrm);
    float3 N  = front ? Ng : -Ng;
    float3 col = rugTex.Sample(rugSamp, i.uv).rgb;

    float h = dot(col, float3(0.299, 0.587, 0.114));
    float2 g = float2(ddx(h), -ddy(h)) * (front ? 1.6 : 0.6);
    float3 Nd = normalize(N - float3(g, 0.0));

    if (!front)
    {
        float gray = dot(col, float3(0.299, 0.587, 0.114));
        col = lerp(col, float3(gray, gray, gray), 0.4) * 0.8 + float3(0.05, 0.04, 0.03);
        float2 k = frac(i.uv * float2(260.0, 175.0));
        float knot = smoothstep(0.0, 0.18, min(min(k.x, 1.0 - k.x), min(k.y, 1.0 - k.y)));
        col *= lerp(0.86, 1.0, knot);
    }

    float2 e2 = min(i.uv, 1.0 - i.uv) * rugSize;
    float e = min(e2.x, e2.y);
    col *= lerp(0.62, 1.0, smoothstep(0.4, 3.0, e));

    float3 L = normalize(lightDir.xyz);
    float amb = 0.28, kd = 0.95;
    float shade = (amb + kd * max(dot(Nd, L), 0.0)) / (amb + kd * L.z);

    float2 gp = float2(0.0, 0.0);
    float bias = 1.5;
    if (abs(Ng.z) > 0.2)
    {
        float2 gg = -Ng.xy / Ng.z;
        float den = 1.0 - dot(gg, lightK);
        if (abs(den) > 0.25) gp = clamp(gg / den, -3.0, 3.0);
        else bias = 5.0;
    }
    else
    {
        bias = 5.0;
    }
    float sh = shadowAt(i.world, gp, bias, sp.xy);
    shade *= 1.0 - 0.55 * sh;

    return float4(min(col * min(shade, 1.3), 1.0), 1.0);
}
