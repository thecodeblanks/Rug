#define RUG_EXPORTS
#include "rug.h"

#include "cloth.h"

#include <d3d11.h>
#include <objbase.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

// Shader bytecode compiled at build time by FxCompile from shaders\*.hlsl.
#include "cloth_ps.h"
#include "cloth_vs.h"
#include "floor_ps.h"
#include "floor_vs.h"
#include "shadow_ps.h"
#include "shadow_vs.h"

namespace {

constexpr float kLiftHeight = 60.0f;
constexpr float kClickLift = 100.0f;
constexpr float kLiftRamp = 0.3f;
constexpr int   kColumns = 44;
constexpr bool  kDesktopShadow = false;

// A cloth resting on icon bumps never reaches an exact fixed point: it keeps trembling around a mean pose
// without drifting, by up to 0.07 px after a drop at the screen centre (measured 2026-10-14 on 26 desktop
// icons). A threshold on raw movement either never fires on that tremble or freezes a cloth that is still
// creeping, so the cloth sleeps once the mean pose of a kSleepWindow-step window is less than kSleepDrift
// from the window before it. Measured on eight drops: asleep after 8-28 s, each pose inside the original's tremble.
constexpr int   kSleepWindow = 120;
constexpr float kSleepDrift = 0.005f;
// A corner balanced on a bump edge can creep for good: its window mean never settles below kSleepDrift. A drop
// that is going to settle does so within 8-28 s (measured on eight drops), i.e. well under kStuckWindows windows,
// so only a truly stuck cloth reaches this cap and is frozen at its mean to stop the tremble and let it sleep.
constexpr int   kStuckWindows = 20;

struct Vtx { float pos[3]; float nrm[3]; float uv[2]; };
struct Cb {
    float lightDir[4];
    float viewSize[2];
    float rugSize[2];
    float lightK[2];
    float camD;
    float shadowStrength;
    float floorZ;
    float pad[3];
};

struct State {
    int w = 0, h = 0;
    bool held = false;
    Camera camera;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Texture2D* rt = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* depth = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11Texture2D* staging = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11RasterizerState* rs = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11SamplerState* pointSampler = nullptr;
    ID3D11VertexShader* shadowVs = nullptr;
    ID3D11PixelShader* shadowPs = nullptr;
    ID3D11VertexShader* floorVs = nullptr;
    ID3D11PixelShader* floorPs = nullptr;
    ID3D11Texture2D* shadowTex = nullptr;
    ID3D11RenderTargetView* shadowRtv = nullptr;
    ID3D11Texture2D* shadowDepth = nullptr;
    ID3D11DepthStencilView* shadowDsv = nullptr;
    ID3D11ShaderResourceView* shadowSrv = nullptr;
    ID3D11BlendState* blendPremul = nullptr;
    ID3D11DepthStencilState* depthOn = nullptr;
    ID3D11DepthStencilState* depthOff = nullptr;
    ID3D11Texture2D* rugTex = nullptr;
    ID3D11ShaderResourceView* rugSrv = nullptr;
    int texW = 1, texH = 1;
    int ibCount = 0;

    std::unique_ptr<HeightField> field;
    std::unique_ptr<Cloth> cloth;
    std::vector<Vtx> verts;
    bool dragging = false;
    V3 dragFrom{}, dragTo{};
    V2 downScreen{};
    V2 grabBase{};
    float liftFrom = 0.0f, liftTo = 0.0f;
    float rampT = 0.0f;
    float accum = 0.0f;

    // The cloth is asleep: the last Cloth::step was a bit-exact no-op, or its mean pose moved less than
    // kSleepDrift between the last two kSleepWindow-step windows. Nothing has touched the cloth or the ground since.
    bool settled = false;
    bool derivedStale = false;
    bool frameDirty = true;
    std::vector<V3> snapPos, snapPrev, snapVel;
    int windowSteps = 0;
    int awakeWindows = 0;
    bool havePrevMean = false;
    // Positions are summed relative to the window's first pose, so float sums of ~1000 px values keep their precision.
    std::vector<V3> windowStart, windowSum, prevMean;
};

State S;

template <class T>
void Release(T*& ptr) {
    if (ptr) { ptr->Release(); ptr = nullptr; }
}

V2 ToV2(float x, float y) { return { x, y }; }

// Every export that changes the cloth, its invMass or the picture calls this.
void Touch() {
    S.settled = false;
    S.windowSteps = 0;
    S.awakeWindows = 0;
    S.havePrevMean = false;
    S.frameDirty = true;
}

template <class T>
bool SameBits(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0;
}

// Turns the window's sums into its mean pose, stores it in prevMean and returns whether it is within distance of
// the previous window's mean.
bool MeanStayedWithin(float distance) {
    const float limit = distance * distance;
    const float inv = 1.0f / float(kSleepWindow);
    const bool compare = S.havePrevMean && S.prevMean.size() == S.windowStart.size();
    S.prevMean.resize(S.windowStart.size());
    bool within = compare;
    for (size_t i = 0; i < S.windowStart.size(); ++i) {
        const V3 mean = S.windowStart[i] + S.windowSum[i] * inv;
        if (compare && !(len2(mean - S.prevMean[i]) < limit)) within = false;
        S.prevMean[i] = mean;
    }
    S.havePrevMean = true;
    return within;
}

}

extern "C" void rug_shutdown(void) {
    S.cloth.reset();
    S.field.reset();
    Release(S.rugSrv);
    Release(S.rugTex);
    Release(S.sampler);
    Release(S.pointSampler);
    Release(S.shadowVs);
    Release(S.shadowPs);
    Release(S.floorVs);
    Release(S.floorPs);
    Release(S.shadowSrv);
    Release(S.shadowDsv);
    Release(S.shadowDepth);
    Release(S.shadowRtv);
    Release(S.shadowTex);
    Release(S.blendPremul);
    Release(S.depthOn);
    Release(S.depthOff);
    Release(S.rs);
    Release(S.ib);
    Release(S.vb);
    Release(S.cb);
    Release(S.layout);
    Release(S.vs);
    Release(S.ps);
    Release(S.staging);
    Release(S.dsv);
    Release(S.depth);
    Release(S.rtv);
    Release(S.rt);
    Release(S.ctx);
    Release(S.dev);
    S.verts.clear();
    S.ibCount = 0;
    S.dragging = false;
    Touch();
}

extern "C" int rug_init(int w, int h) {
    rug_shutdown();
    S.w = w;
    S.h = h;
    S.camera.size = { float(w), float(h) };
    S.field = std::make_unique<HeightField>(S.camera.size);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    D3D_FEATURE_LEVEL level;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &S.dev, &level, &S.ctx)))
        return 0;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(S.dev->CreateTexture2D(&td, nullptr, &S.rt))) return 0;
    if (FAILED(S.dev->CreateRenderTargetView(S.rt, nullptr, &S.rtv))) return 0;

    D3D11_TEXTURE2D_DESC dd = td;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(S.dev->CreateTexture2D(&dd, nullptr, &S.depth))) return 0;
    if (FAILED(S.dev->CreateDepthStencilView(S.depth, nullptr, &S.dsv))) return 0;

    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(S.dev->CreateTexture2D(&sd, nullptr, &S.staging))) return 0;

    if (FAILED(S.dev->CreateVertexShader(g_cloth_vs, sizeof(g_cloth_vs), nullptr, &S.vs))) return 0;
    if (FAILED(S.dev->CreatePixelShader(g_cloth_ps, sizeof(g_cloth_ps), nullptr, &S.ps))) return 0;
    if (FAILED(S.dev->CreateVertexShader(g_shadow_vs, sizeof(g_shadow_vs), nullptr, &S.shadowVs))) return 0;
    if (FAILED(S.dev->CreatePixelShader(g_shadow_ps, sizeof(g_shadow_ps), nullptr, &S.shadowPs))) return 0;
    if (FAILED(S.dev->CreateVertexShader(g_floor_vs, sizeof(g_floor_vs), nullptr, &S.floorVs))) return 0;
    if (FAILED(S.dev->CreatePixelShader(g_floor_ps, sizeof(g_floor_ps), nullptr, &S.floorPs))) return 0;

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(S.dev->CreateInputLayout(layoutDesc, 3, g_cloth_vs, sizeof(g_cloth_vs), &S.layout))) return 0;

    D3D11_TEXTURE2D_DESC shd{};
    shd.Width = w;
    shd.Height = h;
    shd.MipLevels = 1;
    shd.ArraySize = 1;
    shd.Format = DXGI_FORMAT_R32_FLOAT;
    shd.SampleDesc.Count = 1;
    shd.Usage = D3D11_USAGE_DEFAULT;
    shd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(S.dev->CreateTexture2D(&shd, nullptr, &S.shadowTex))) return 0;
    if (FAILED(S.dev->CreateRenderTargetView(S.shadowTex, nullptr, &S.shadowRtv))) return 0;
    if (FAILED(S.dev->CreateShaderResourceView(S.shadowTex, nullptr, &S.shadowSrv))) return 0;
    D3D11_TEXTURE2D_DESC sdd = shd;
    sdd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    sdd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(S.dev->CreateTexture2D(&sdd, nullptr, &S.shadowDepth))) return 0;
    if (FAILED(S.dev->CreateDepthStencilView(S.shadowDepth, nullptr, &S.shadowDsv))) return 0;

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(S.dev->CreateBlendState(&bd, &S.blendPremul))) return 0;

    D3D11_DEPTH_STENCIL_DESC dsOn{};
    dsOn.DepthEnable = TRUE;
    dsOn.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsOn.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(S.dev->CreateDepthStencilState(&dsOn, &S.depthOn))) return 0;
    D3D11_DEPTH_STENCIL_DESC dsOff{};
    dsOff.DepthEnable = FALSE;
    dsOff.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsOff.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(S.dev->CreateDepthStencilState(&dsOff, &S.depthOff))) return 0;

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(Cb);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(S.dev->CreateBuffer(&cbd, nullptr, &S.cb))) return 0;

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = TRUE;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = TRUE;
    if (FAILED(S.dev->CreateRasterizerState(&rd, &S.rs))) return 0;

    D3D11_SAMPLER_DESC samp{};
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samp.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(S.dev->CreateSamplerState(&samp, &S.sampler))) return 0;
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (FAILED(S.dev->CreateSamplerState(&samp, &S.pointSampler))) return 0;

    return 1;
}

extern "C" int rug_load_texture(const wchar_t* path) {
    if (!S.dev || !path) return 0;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    int ok = 0;
    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory))))
            break;
        if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder)))
            break;
        if (FAILED(decoder->GetFrame(0, &frame))) break;
        if (FAILED(factory->CreateFormatConverter(&converter))) break;
        if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                         nullptr, 0, WICBitmapPaletteTypeCustom)))
            break;

        UINT w = 0, h = 0;
        converter->GetSize(&w, &h);
        // A larger picture cannot become a Direct3D 11 texture, so it is refused before its pixels are allocated.
        if (w == 0 || h == 0 || w > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || h > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) break;
        std::vector<unsigned char> px;
        try { px.resize(size_t(w) * h * 4); }
        catch (const std::bad_alloc&) { break; }
        if (FAILED(converter->CopyPixels(nullptr, w * 4, UINT(px.size()), px.data()))) break;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = px.data();
        init.SysMemPitch = w * 4;
        // The current rug stays on screen unless the new picture fully loads.
        ID3D11Texture2D* tex = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        if (FAILED(S.dev->CreateTexture2D(&td, &init, &tex))) break;
        if (FAILED(S.dev->CreateShaderResourceView(tex, nullptr, &srv))) { tex->Release(); break; }
        Release(S.rugSrv);
        Release(S.rugTex);
        S.rugTex = tex;
        S.rugSrv = srv;
        S.texW = int(w);
        S.texH = int(h);
        Touch();
        ok = 1;
    } while (false);

    Release(converter);
    Release(frame);
    Release(decoder);
    Release(factory);
    return ok;
}

static float unitScale() { return float(S.w) / 2560.0f; }
static float liftH() { return kLiftHeight * unitScale(); }

extern "C" void rug_rebuild(float scale, float drop) {
    if (!S.dev) return;
    float width = S.camera.size.x * scale;
    float aspect = float(S.texH) / float(S.texW);
    S.cloth = std::make_unique<Cloth>(width, width * aspect, kColumns);
    S.cloth->ground = S.field.get();
    S.cloth->unitScale = unitScale();
    S.cloth->layout(S.camera.center(), 0.0f, drop * unitScale());
    S.cloth->updateDerived(S.camera);
    S.dragging = false;
    S.derivedStale = false;
    Touch();

    Release(S.vb);
    Release(S.ib);
    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth = UINT(S.cloth->count * sizeof(Vtx));
    vbd.Usage = D3D11_USAGE_DYNAMIC;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    S.dev->CreateBuffer(&vbd, nullptr, &S.vb);

    D3D11_BUFFER_DESC ibd{};
    ibd.ByteWidth = UINT(S.cloth->indices.size() * sizeof(uint32_t));
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData{};
    ibData.pSysMem = S.cloth->indices.data();
    S.dev->CreateBuffer(&ibd, &ibData, &S.ib);
    S.ibCount = int(S.cloth->indices.size());
    S.verts.assign(S.cloth->count, Vtx{});
}

extern "C" void rug_set_bumps(const float* data, int count) {
    if (!data || count < 0) count = 0;
    std::vector<Bump> bumps;
    bumps.reserve(count);
    for (int i = 0; i < count; ++i) {
        const float* b = data + i * 5;
        bumps.push_back({ { b[0], b[1] }, { b[2], b[3] }, b[4] * unitScale() });
    }
    if (S.field) S.field->setBumps(bumps);
    S.settled = false;
    S.windowSteps = 0;
    S.havePrevMean = false;
    // Without this a cloth that has already drifted for kStuckWindows windows would freeze about one window
    // after the icons move, mid-fall onto the new bumps, instead of draping over them.
    S.awakeWindows = 0;
}

// Runs one Cloth::step and decides whether the cloth may sleep. RUG_EXACT_STEPS keeps only the bit-exact
// rule, so a test can compare every frame with the original code.
static void StepCloth(float dt) {
    Cloth& c = *S.cloth;
    const float speedBefore = c.maxSpeed;
    S.snapPos = c.pos;
    S.snapPrev = c.prev;
    S.snapVel = c.vel;
    c.step(dt);
    const bool posSame = SameBits(S.snapPos, c.pos);
    const bool unchanged = posSame && SameBits(S.snapPrev, c.prev) && SameBits(S.snapVel, c.vel) &&
                           std::memcmp(&speedBefore, &c.maxSpeed, sizeof(float)) == 0;
    // A held cloth is a bit-exact no-op every step (every invMass is 0), so it would otherwise count as settled
    // and the overlay would save the flat lifted sheet as the drape. Keep it awake while held.
    const bool free = !S.dragging && !c.isGrabbing() && !S.held;
    S.settled = unchanged && free;
    if (!posSame) S.derivedStale = true;

#ifndef RUG_EXACT_STEPS
    if (S.settled || !free) {
        S.windowSteps = 0;
        S.awakeWindows = 0;
        S.havePrevMean = false;
        return;
    }
    if (S.windowSteps == 0) {
        S.windowStart = c.pos;
        S.windowSum.assign(c.pos.size(), V3{});
    }
    for (size_t i = 0; i < c.pos.size(); ++i) S.windowSum[i] += c.pos[i] - S.windowStart[i];
    if (++S.windowSteps == kSleepWindow) {
        S.windowSteps = 0;
        if (MeanStayedWithin(kSleepDrift)) {
            S.settled = true;
            S.awakeWindows = 0;
        } else if (++S.awakeWindows >= kStuckWindows) {
            // Never reached a fixed point: freeze every vertex at the latest window mean so the tremble stops.
            for (size_t i = 0; i < c.pos.size(); ++i) {
                c.pos[i] = S.prevMean[i];
                c.prev[i] = S.prevMean[i];
                c.vel[i] = V3{};
            }
            S.settled = true;
            S.awakeWindows = 0;
            S.derivedStale = true;
        }
    }
#endif
}

extern "C" void rug_step(float dt) {
    if (!S.cloth) return;

    const float fixed = 1.0f / 60.0f;
    S.accum += std::clamp(dt, 0.0f, 0.25f);
    int due = 0;
    for (float a = S.accum; a >= fixed && due < 4; a -= fixed) ++due;
    int steps = 0;
    while (S.accum >= fixed && steps < 4) {
        if (S.dragging) {

            float t = float(steps + 1) / float(std::max(due, 1));
            V3 g = S.dragFrom + (S.dragTo - S.dragFrom) * t;

            S.rampT = std::min(kLiftRamp, S.rampT + fixed);
            g.z = S.liftFrom + (S.liftTo - S.liftFrom) * (S.rampT / kLiftRamp);
            S.cloth->grabTarget = g;
        }
        const bool groundMoved = S.field && S.field->animate(fixed);
        if (groundMoved || !S.settled || S.dragging || S.cloth->isGrabbing())
            StepCloth(fixed);
        S.accum -= fixed;
        ++steps;
    }
    if (steps == 4) S.accum = 0.0f;
    if (steps > 0 && S.dragging) S.dragFrom = S.dragTo;

    if (S.held && !S.cloth->pos.empty() && std::fabs(S.cloth->pos[0].z - liftH()) > 0.5f) {
        S.cloth->setHeld(true, liftH());
        S.settled = false;
        S.derivedStale = true;
    }
    // updateDerived depends only on pos, so it is skipped while pos is bit-identical to its last input.
    if (steps > 0 && S.derivedStale) {
        S.cloth->updateDerived(S.camera);
        S.derivedStale = false;
        S.frameDirty = true;
    }
}

static void activeRect(int out[4]) {
    int left = 0, top = 0, right = S.w, bottom = S.h;
    if (S.cloth) {
        const float pad = 96.0f;
        const V2 lo = S.cloth->bboxMin, hi = S.cloth->bboxMax;
        left   = std::clamp(int(std::floor(lo.x - pad)), 0, S.w);
        right  = std::clamp(int(std::ceil(hi.x + pad)), 0, S.w);
        top    = std::clamp(int(std::floor(S.h - hi.y - pad)), 0, S.h);
        bottom = std::clamp(int(std::ceil(S.h - lo.y + pad)), 0, S.h);
    }
    out[0] = left; out[1] = top; out[2] = std::max(0, right - left); out[3] = std::max(0, bottom - top);
}

extern "C" int rug_render(unsigned char* dst, int stride, int* rect) {
    if (!S.ctx || !dst) return 0;
    // Nothing that feeds the picture changed since the last call, so dst already holds this frame.
    if (!S.frameDirty) return 0;
    S.frameDirty = false;

    static int prevRc[4] = { 0, 0, 0, 0 };
    int rc[4] = { 0, 0, 0, 0 };
    activeRect(rc);
    int drawRc[4] = { rc[0], rc[1], rc[2], rc[3] };
    if (prevRc[2] > 0 && prevRc[3] > 0) {
        const int l = std::min(rc[0], prevRc[0]);
        const int t = std::min(rc[1], prevRc[1]);
        const int r = std::max(rc[0] + rc[2], prevRc[0] + prevRc[2]);
        const int b = std::max(rc[1] + rc[3], prevRc[1] + prevRc[3]);
        drawRc[0] = l; drawRc[1] = t; drawRc[2] = r - l; drawRc[3] = b - t;
    }
    std::memcpy(prevRc, rc, sizeof(rc));
    if (rect) std::memcpy(rect, drawRc, sizeof(drawRc));

    if (S.cloth && S.vb) {
        const Cloth& c = *S.cloth;
        for (int i = 0; i < c.count; ++i) {
            Vtx& v = S.verts[i];
            v.pos[0] = c.pos[i].x; v.pos[1] = c.pos[i].y; v.pos[2] = c.pos[i].z;
            v.nrm[0] = c.nrm[i].x; v.nrm[1] = c.nrm[i].y; v.nrm[2] = c.nrm[i].z;
            v.uv[0] = c.uvs[i].x;  v.uv[1] = c.uvs[i].y;
        }
        D3D11_MAPPED_SUBRESOURCE m{};
        S.ctx->Map(S.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        std::memcpy(m.pData, S.verts.data(), S.verts.size() * sizeof(Vtx));
        S.ctx->Unmap(S.vb, 0);
    }

    Cb cb{};
    {
        float lx = -0.5f, ly = 0.58f, lz = 0.64f;
        float l = std::sqrt(lx * lx + ly * ly + lz * lz);
        cb.lightDir[0] = lx / l; cb.lightDir[1] = ly / l; cb.lightDir[2] = lz / l; cb.lightDir[3] = 0.0f;
        // The shadow-casting light sits lower than the shading light (smaller z), so even a gentle bump over an
        // icon throws a soft shadow and reads as relief away from the rug's edge, not only at the silhouette.
        float sx = -0.3f, sy = 0.38f, sz = 0.55f;
        float s = std::sqrt(sx * sx + sy * sy + sz * sz);
        cb.lightK[0] = (sx / s) / (sz / s);
        cb.lightK[1] = (sy / s) / (sz / s);
    }
    cb.viewSize[0] = S.camera.size.x; cb.viewSize[1] = S.camera.size.y;
    cb.camD = S.camera.D();
    cb.shadowStrength = 0.0f;
    cb.floorZ = -4.0f;
    if (S.cloth) { cb.rugSize[0] = S.cloth->width; cb.rugSize[1] = S.cloth->height; }
    D3D11_MAPPED_SUBRESOURCE mc{};
    S.ctx->Map(S.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc);
    std::memcpy(mc.pData, &cb, sizeof(cb));
    S.ctx->Unmap(S.cb, 0);

    D3D11_VIEWPORT vp{ 0.0f, 0.0f, float(S.w), float(S.h), 0.0f, 1.0f };
    S.ctx->RSSetViewports(1, &vp);
    S.ctx->RSSetState(S.rs);

    RECT scissor{ rc[0], rc[1], rc[0] + rc[2], rc[1] + rc[3] };
    S.ctx->RSSetScissorRects(1, &scissor);
    S.ctx->VSSetConstantBuffers(0, 1, &S.cb);
    S.ctx->PSSetConstantBuffers(0, 1, &S.cb);
    S.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const bool hasCloth = S.cloth && S.vb && S.ib && S.rugSrv;
    UINT vstride = sizeof(Vtx), offset = 0;

    ID3D11ShaderResourceView* nullSrv[2] = { nullptr, nullptr };
    S.ctx->PSSetShaderResources(0, 2, nullSrv);
    const float zero[4] = { 0, 0, 0, 0 };
    S.ctx->ClearRenderTargetView(S.shadowRtv, zero);
    S.ctx->ClearDepthStencilView(S.shadowDsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    S.ctx->OMSetRenderTargets(1, &S.shadowRtv, S.shadowDsv);
    if (hasCloth) {
        S.ctx->IASetInputLayout(S.layout);
        S.ctx->IASetVertexBuffers(0, 1, &S.vb, &vstride, &offset);
        S.ctx->IASetIndexBuffer(S.ib, DXGI_FORMAT_R32_UINT, 0);
        S.ctx->VSSetShader(S.shadowVs, nullptr, 0);
        S.ctx->PSSetShader(S.shadowPs, nullptr, 0);
        S.ctx->DrawIndexed(UINT(S.ibCount), 0, 0);
    }

    const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    S.ctx->ClearRenderTargetView(S.rtv, clear);
    S.ctx->ClearDepthStencilView(S.dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    S.ctx->OMSetRenderTargets(1, &S.rtv, S.dsv);

    float blendFactor[4] = { 0, 0, 0, 0 };
    S.ctx->OMSetBlendState(S.blendPremul, blendFactor, 0xFFFFFFFF);
    S.ctx->OMSetDepthStencilState(S.depthOff, 0);
    S.ctx->IASetInputLayout(nullptr);
    S.ctx->VSSetShader(S.floorVs, nullptr, 0);
    S.ctx->PSSetShader(S.floorPs, nullptr, 0);
    if (kDesktopShadow) S.ctx->Draw(3, 0);

    S.ctx->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    S.ctx->OMSetDepthStencilState(S.depthOn, 0);
    if (hasCloth) {
        ID3D11ShaderResourceView* srvs[2] = { S.rugSrv, S.shadowSrv };
        ID3D11SamplerState* samplers[2] = { S.sampler, S.pointSampler };
        S.ctx->IASetInputLayout(S.layout);
        S.ctx->IASetVertexBuffers(0, 1, &S.vb, &vstride, &offset);
        S.ctx->IASetIndexBuffer(S.ib, DXGI_FORMAT_R32_UINT, 0);
        S.ctx->VSSetShader(S.vs, nullptr, 0);
        S.ctx->PSSetShader(S.ps, nullptr, 0);
        S.ctx->PSSetShaderResources(0, 2, srvs);
        S.ctx->PSSetSamplers(0, 2, samplers);
        S.ctx->DrawIndexed(UINT(S.ibCount), 0, 0);
    }
    S.ctx->PSSetShaderResources(0, 2, nullSrv);

    if (drawRc[2] > 0 && drawRc[3] > 0) {
        const D3D11_BOX box{ UINT(drawRc[0]), UINT(drawRc[1]), 0u, UINT(drawRc[0] + drawRc[2]), UINT(drawRc[1] + drawRc[3]), 1u };
        S.ctx->CopySubresourceRegion(S.staging, 0, UINT(drawRc[0]), UINT(drawRc[1]), 0, S.rt, 0, &box);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(S.ctx->Map(S.staging, 0, D3D11_MAP_READ, 0, &m))) {
            const unsigned char* src = static_cast<const unsigned char*>(m.pData);
            for (int y = drawRc[1]; y < drawRc[1] + drawRc[3]; ++y)
                std::memcpy(dst + size_t(y) * stride + size_t(drawRc[0]) * 4,
                            src + size_t(y) * m.RowPitch + size_t(drawRc[0]) * 4,
                            size_t(drawRc[2]) * 4);
            S.ctx->Unmap(S.staging, 0);
        } else {
            S.frameDirty = true;
            return 0;
        }
    }
    return 1;
}

extern "C" int rug_mouse_down(float x, float y) {
    if (!S.cloth) return 0;
    V2 s = ToV2(x, y);
    auto hit = S.cloth->pick(s, true);
    if (!hit) return 0;
    auto [vertex, z] = *hit;
    S.dragging = true;

    V3 p = S.cloth->pos[vertex];
    V3 target{ p.x, p.y, p.z };
    S.downScreen = s;
    S.grabBase = V2{ target.x, target.y };
    S.liftFrom = p.z;
    S.liftTo = std::max(z, 0.0f) + kClickLift * unitScale();
    S.rampT = 0.0f;
    S.cloth->beginGrab(vertex, target);
    S.dragFrom = S.dragTo = target;
    Touch();
    return 1;
}

extern "C" void rug_mouse_drag(float x, float y) {
    if (!S.cloth || !S.dragging) return;

    V3 from = S.camera.unproject(S.downScreen, S.dragTo.z);
    V3 now = S.camera.unproject(ToV2(x, y), S.dragTo.z);
    S.dragTo = { S.grabBase.x + (now.x - from.x), S.grabBase.y + (now.y - from.y), S.dragTo.z };
}

extern "C" void rug_mouse_up(void) {
    if (!S.cloth || !S.dragging) return;
    S.dragging = false;
    S.cloth->endGrab();
    Touch();
}

extern "C" int rug_hit_test(float x, float y) {
    if (!S.cloth) return 0;
    return S.cloth->pick(ToV2(x, y), false).has_value() ? 1 : 0;
}

extern "C" void rug_get_bounds(float* out) {
    if (!S.cloth || !out) return;
    out[0] = S.cloth->bboxMin.x; out[1] = S.cloth->bboxMin.y;
    out[2] = S.cloth->bboxMax.x; out[3] = S.cloth->bboxMax.y;
}

extern "C" void rug_rotate(float angle) {
    if (!S.cloth) return;
    S.cloth->rotate(angle);
    S.cloth->updateDerived(S.camera);
    S.derivedStale = false;
    Touch();
}

extern "C" void rug_get_pose(float* out) {
    if (!S.cloth || !out) return;
    V2 c{};
    float a = 0.0f;
    S.cloth->pose(c, a);
    out[0] = c.x; out[1] = c.y; out[2] = a;
}

extern "C" void rug_get_corners(float* out) {
    if (!S.cloth || !out) return;
    const Cloth& c = *S.cloth;
    const int idxs[4] = { 0, c.nx - 1, c.count - c.nx, c.count - 1 };
    for (int i = 0; i < 4; ++i) {
        V2 p = c.proj[idxs[i]];
        out[i * 2] = p.x;
        out[i * 2 + 1] = p.y;
    }
}

extern "C" int rug_point_count(void) {
    return S.cloth ? S.cloth->count : 0;
}

extern "C" int rug_get_points(float* out, int maxVertices) {
    if (!S.cloth || !out) return 0;
    const int n = S.cloth->count;
    if (maxVertices < n) return 0;
    for (int i = 0; i < n; ++i) {
        out[i * 3 + 0] = S.cloth->pos[i].x;
        out[i * 3 + 1] = S.cloth->pos[i].y;
        out[i * 3 + 2] = S.cloth->pos[i].z;
    }
    return n;
}

extern "C" int rug_set_points(const float* in, int count) {
    if (!S.cloth || !in || count != S.cloth->count) return 0;
    // A damaged drape must not poison the cloth: step()'s non-finite guard reads maxSpeed via std::max, which
    // drops a NaN, so a NaN here would spread and never be caught. Reject the whole restore instead.
    for (int i = 0; i < count * 3; ++i)
        if (!std::isfinite(in[i])) return 0;
    for (int i = 0; i < count; ++i) {
        const V3 p{ in[i * 3 + 0], in[i * 3 + 1], in[i * 3 + 2] };
        S.cloth->pos[i] = p;
        S.cloth->prev[i] = p;   // prev == pos leaves the cloth at rest, so it holds the saved folds
        S.cloth->vel[i] = {};
        S.cloth->invMass[i] = 1.0f;
    }
    // The caller sets the bumps just before restoring, but the height field rises to them over about 1.5 s. Snap
    // it to the bumps now so the restored folds rest on them instead of slumping onto flat ground first.
    if (S.field) S.field->snap();
    S.cloth->endGrab();
    S.dragging = false;
    S.held = false;
    S.cloth->updateDerived(S.camera);
    S.derivedStale = false;
    Touch();
    return 1;
}

extern "C" int rug_is_settled(void) {
    return S.settled ? 1 : 0;
}

extern "C" void rug_resize(float scale) {
    if (!S.cloth) return;
    V2 pc{};
    float pa = 0.0f;
    S.cloth->pose(pc, pa);
    bool wasHeld = S.held;
    rug_rebuild(scale, 0.0f);
    if (!S.cloth) return;
    S.cloth->layout(pc, pa, 0.0f);
    if (wasHeld) S.cloth->setHeld(true, liftH());
    S.cloth->updateDerived(S.camera);
    S.derivedStale = false;
    Touch();
}

extern "C" void rug_hold_begin(void) {
    if (!S.cloth) return;
    S.dragging = false;
    // Lift the cloth straight up where it already sits, keeping its centre and angle. The old path re-laid it out
    // at the screen centre first, so pressing Alt to resize snapped the rug back to a fresh centred drop.
    S.held = true;
    S.cloth->setHeld(true, liftH());
    S.cloth->updateDerived(S.camera);
    S.derivedStale = false;
    Touch();
}

extern "C" void rug_hold_end(void) {
    if (!S.cloth) return;
    S.held = false;
    S.cloth->setHeld(false, 0.0f);
    Touch();
}

extern "C" void rug_layout(float cx, float cy, float angle, float drop) {
    if (!S.cloth) return;
    S.dragging = false;
    S.cloth->layout(V2{ cx, cy }, angle, drop * unitScale());
    S.cloth->updateDerived(S.camera);
    S.derivedStale = false;
    Touch();
}

extern "C" void rug_invalidate(void) {
    S.frameDirty = true;
}
