cbuffer Cb : register(b0)
{
    float4 lightDir;
    float2 viewSize;
    float2 rugSize;
    float2 lightK;
    float  camD;
    float  shadowStrength;
    float  floorZ;
    float3 _pad;
};

Texture2D    rugTex    : register(t0);
Texture2D    shadowMap : register(t1);
SamplerState rugSamp   : register(s0);
SamplerState pointSamp : register(s1);

static const float2 poisson[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432),  float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845),  float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554),  float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507),  float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367),   float2(0.14383161, -0.14100790)
};

float depthFromZ(float z) { return saturate(0.5 - z / 4000.0); }

float2 toTC(float2 f) { return float2(f.x / viewSize.x, 1.0 - f.y / viewSize.y); }

float ign(float2 px) { return frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715)))); }

float shadowAt(float3 p, float2 gp, float bias, float2 px)
{
    float2 f = p.xy - lightK * p.z;
    float a = ign(px) * 6.2831853;
    float2x2 R = float2x2(cos(a), -sin(a), sin(a), cos(a));

    float sumD = 0.0, nB = 0.0;
    [unroll] for (int i = 0; i < 16; i++)
    {
        float2 o = mul(R, poisson[i]) * 34.0;
        float zr = p.z + dot(gp, o);
        float occ = shadowMap.SampleLevel(pointSamp, toTC(f + o), 0).r;
        if (occ > zr + bias) { sumD += occ - zr; nB += 1.0; }
    }
    if (nB < 0.5) return 0.0;

    float r = clamp(2.5 + (sumD / nB) * 0.2, 2.5, 30.0);
    float sh = 0.0;
    [unroll] for (int j = 0; j < 16; j++)
    {
        float2 o = mul(R, poisson[j]) * r;
        float zr = p.z + dot(gp, o);
        float occ = shadowMap.SampleLevel(pointSamp, toTC(f + o), 0).r;
        sh += (occ > zr + bias) ? 1.0 : 0.0;
    }
    return sh / 16.0;
}

struct SIn  { float3 pos : POSITION; };
struct SOut { float4 sv : SV_POSITION; float z : TEXCOORD0; };

struct VIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
struct VOut { float4 sv : SV_POSITION; float3 world : TEXCOORD1; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
