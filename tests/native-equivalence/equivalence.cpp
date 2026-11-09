// Drives two builds of rug_native.dll with the same scripted input and compares the cloth simulation state
// (every vertex position) instead of rendered pixels, so a deliberate rendering change (the drop shadow) cannot
// mask a physics regression and cannot make the comparison fail on its own.
//   exact: every frame the candidate's vertex positions must be bit-identical to the reference's, together with
//          pose, bounds, corners and hit tests. The candidate is built with RUG_EXACT_STEPS, which keeps every
//          skip except the window-based sleep. Pressing Alt now holds the rug where it sits instead of
//          re-centring it, so that one step is asserted on its own and left out of the bit-exact run.
//   sleep: the cloth rests on icon bumps. The sleeping candidate freezes its vertices; it passes when it fell
//          asleep before the last 20 s and every frozen vertex lies within kPosSlack of the range the reference
//          still visited during those 20 s. The reference never sleeps, so it keeps trembling by a fraction of a
//          unit, and a single reference frame would be no yardstick.
// The reference dll is the original code with two read-only probes (rug_point_count, rug_get_points) added so the
// positions can be read; the probes do not touch the simulation or rendering.
// Usage: equivalence.exe exact|sleep <reference dll> <candidate dll> <rug picture> <second rug picture>
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1200;
constexpr float kFrameSeconds = 1.0f / 32.0f;
// The reference keeps settling very slowly for minutes, so a frozen candidate vertex lands slightly outside the
// range the reference visits during the measured 20 s window. Measured 2026-10-14 across the three rest
// scenarios: the reference's own tremble reaches 0.057 units and a frozen vertex sits at most 0.004 units
// outside the visited range. 0.1 world units (world units are screen pixels) is the accepted limit: ~25x the
// largest overflow seen, still far below one pixel, so a real physics shift above a tenth of a pixel fails.
constexpr float kPosSlack = 0.1f;
// The candidate's frozen frame is only meaningfully "within the reference's range" if that range is itself tight,
// i.e. the reference has all but stopped in the measured window. Measured 2026-10-14: the reference's spread over
// the window reached 0.053 units, so 0.5 is a ~10x margin that still catches a reference creeping by units (which
// would make kPosSlack meaningless and could hide a premature freeze).
constexpr float kRefRestBound = 0.5f;

struct Api {
    HMODULE module = nullptr;
    bool renderReportsChange = false;
    int (*init)(int, int);
    int (*load_texture)(const wchar_t*);
    void (*rebuild)(float, float);
    void (*set_bumps)(const float*, int);
    void (*step)(float);
    void* render;
    int (*mouse_down)(float, float);
    void (*mouse_drag)(float, float);
    void (*mouse_up)();
    int (*hit_test)(float, float);
    void (*rotate)(float);
    void (*resize)(float);
    void (*get_pose)(float*);
    void (*get_corners)(float*);
    void (*get_bounds)(float*);
    void (*hold_begin)();
    void (*hold_end)();
    void (*layout)(float, float, float, float);
    int (*point_count)();
    int (*get_points)(float*, int);
    void (*shutdown)();

    template <class F> void bind(F& fn, const char* name) {
        fn = reinterpret_cast<F>(GetProcAddress(module, name));
        if (!fn) { std::printf("missing export %s\n", name); std::exit(2); }
    }

    explicit Api(const wchar_t* path) {
        module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) { std::wprintf(L"cannot load %ls (%lu)\n", path, GetLastError()); std::exit(2); }
        bind(init, "rug_init"); bind(load_texture, "rug_load_texture"); bind(rebuild, "rug_rebuild");
        bind(set_bumps, "rug_set_bumps"); bind(step, "rug_step"); bind(render, "rug_render");
        bind(mouse_down, "rug_mouse_down"); bind(mouse_drag, "rug_mouse_drag"); bind(mouse_up, "rug_mouse_up");
        bind(hit_test, "rug_hit_test"); bind(rotate, "rug_rotate"); bind(resize, "rug_resize");
        bind(get_pose, "rug_get_pose"); bind(get_corners, "rug_get_corners"); bind(get_bounds, "rug_get_bounds");
        bind(hold_begin, "rug_hold_begin"); bind(hold_end, "rug_hold_end"); bind(layout, "rug_layout");
        bind(point_count, "rug_point_count"); bind(get_points, "rug_get_points");
        bind(shutdown, "rug_shutdown");
        // The candidate exports rug_invalidate and returns whether rug_render drew anything.
        renderReportsChange = GetProcAddress(module, "rug_invalidate") != nullptr;
    }

    // Returns true when the buffer may have changed.
    bool Render(std::vector<unsigned char>& dst, int* rect) {
        if (renderReportsChange)
            return reinterpret_cast<int (*)(unsigned char*, int, int*)>(render)(dst.data(), kWidth * 4, rect) != 0;
        reinterpret_cast<void (*)(unsigned char*, int, int*)>(render)(dst.data(), kWidth * 4, rect);
        return true;
    }

    // Every vertex position (3 floats each).
    std::vector<float> Points() {
        int n = point_count();
        std::vector<float> v(size_t(std::max(n, 0)) * 3);
        if (n > 0) get_points(v.data(), n);
        return v;
    }
};

struct Side {
    Api api;
    // rug_render writes here; the buffer is only a render target now that positions, not pixels, are compared.
    std::vector<unsigned char> pixels = std::vector<unsigned char>(size_t(kWidth) * kHeight * 4, 0);
    double seconds = 0;
    int drawn = 0;
    explicit Side(const wchar_t* path) : api(path) {}
};

int g_failures = 0;
int g_frame = 0;
bool g_bitExact = true;
std::string g_phase;
// The reference's vertex positions on the previous exact frame, to check the candidate redrew when they changed.
bool g_prevRefValid = false;
std::vector<float> g_prevRefPoints;

void Fail(const char* what) {
    if (++g_failures <= 20) std::printf("MISMATCH frame %d (%s): %s\n", g_frame, g_phase.c_str(), what);
}

void CompareFloats(const char* what, const float* a, const float* b, int n) {
    if (std::memcmp(a, b, sizeof(float) * n) != 0) Fail(what);
}

// Largest absolute coordinate difference between two position vectors, or -1 when the counts differ.
float MaxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return -1.0f;
    float m = 0.0f;
    for (size_t k = 0; k < a.size(); ++k) m = std::max(m, std::fabs(a[k] - b[k]));
    return m;
}

// One frame of the overlay loop: step, render, then read back what Overlay.cs reads.
void Frame(Side& ref, Side& cand, float cursorX, float cursorY) {
    ++g_frame;
    int rect[4];
    bool candDrew = false;
    for (Side* s : { &ref, &cand }) {
        auto t0 = std::chrono::steady_clock::now();
        s->api.step(kFrameSeconds);
        bool drew = s->api.Render(s->pixels, rect);
        if (drew) ++s->drawn;
        if (s == &cand) candDrew = drew;
        s->seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    if (!g_bitExact) { g_prevRefValid = false; return; }
    std::vector<float> pr = ref.api.Points(), pc = cand.api.Points();
    if (pr.size() != pc.size()) Fail("point count");
    else if (std::memcmp(pr.data(), pc.data(), pr.size() * sizeof(float)) != 0) Fail("point positions");
    // A moving rug must redraw. The candidate's rug_render returns 0 only when nothing changed, so if the
    // reference's vertices moved since the last frame, the candidate must have drawn this frame. This catches a
    // mutating path that forgets to mark the frame dirty (the pixel compare used to catch it; the position
    // compare would not).
    if (g_prevRefValid && pr.size() == g_prevRefPoints.size()) {
        bool refMoved = std::memcmp(pr.data(), g_prevRefPoints.data(), pr.size() * sizeof(float)) != 0;
        if (refMoved && !candDrew) Fail("candidate did not redraw after the rug moved");
    }
    g_prevRefPoints = pr;
    g_prevRefValid = true;
    float a[8], b[8];
    ref.api.get_pose(a); cand.api.get_pose(b); CompareFloats("pose", a, b, 3);
    ref.api.get_bounds(a); cand.api.get_bounds(b); CompareFloats("bounds", a, b, 4);
    ref.api.get_corners(a); cand.api.get_corners(b); CompareFloats("corners", a, b, 8);
    if (ref.api.hit_test(cursorX, cursorY) != cand.api.hit_test(cursorX, cursorY)) Fail("hit test");
}

template <class F> void Both(Side& ref, Side& cand, F&& f) { f(ref.api); f(cand.api); }

std::vector<float> IconBumps(float shiftX) {
    // Bumps in the layout Overlay.ApplyBumps sends: x, y (up), radius x, radius y, height.
    std::vector<float> b;
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 8; ++row) {
            float x = 47.0f + 95.0f * col + shiftX, y = kHeight - (37.0f + 125.0f * row);
            b.insert(b.end(), { x, y, 30.08f, 30.08f, 22.0f });
        }
    b.insert(b.end(), { 712.0f + shiftX, kHeight - 412.0f, 30.08f, 30.08f, 22.0f });
    b.insert(b.end(), { 960.0f, 600.0f, 30.08f, 30.08f, 22.0f });
    return b;
}

void Idle(Side& ref, Side& cand, int frames, const char* phase) {
    g_phase = phase;
    int drawnBefore = cand.drawn, lastDrawn = -1;
    for (int i = 0; i < frames; ++i) {
        int before = cand.drawn;
        Frame(ref, cand, 960.0f, 600.0f);
        if (cand.drawn != before) lastDrawn = i;
    }
    std::printf("  %-26s candidate drew %3d of %3d frames, last at %d\n", phase, cand.drawn - drawnBefore, frames, lastDrawn);
}

}

// 50 s of rest after whatever the caller just did. Returns whether the candidate passed.
bool Rest(Side& ref, Side& cand, const char* name) {
    std::printf("-- %s\n", name);
    g_bitExact = false;
    const int frames = 1600, measureFrom = 960;
    std::vector<float> lo, hi, windowStart, previous;
    int lastDrawn = -1, selfChangedFrames = 0;
    float worstSelf = 0, worstSpread = 0;
    double refSeconds = 0, candSeconds = 0;
    for (int i = 0; i < frames; ++i) {
        double r0 = ref.seconds, c0 = cand.seconds;
        int before = cand.drawn;
        Frame(ref, cand, 960.0f, 600.0f);
        if (cand.drawn != before) lastDrawn = i;
        std::vector<float> rp = ref.api.Points();
        if (i < measureFrom) { previous = rp; continue; }
        if (i == measureFrom) { lo = hi = windowStart = rp; }
        for (size_t k = 0; k < rp.size(); ++k) {
            lo[k] = std::min(lo[k], rp[k]);
            hi[k] = std::max(hi[k], rp[k]);
        }
        float self = MaxDiff(previous, rp), spread = MaxDiff(windowStart, rp);
        if (self > 0) ++selfChangedFrames;
        worstSelf = std::max(worstSelf, self);
        worstSpread = std::max(worstSpread, spread);
        refSeconds += ref.seconds - r0;
        candSeconds += cand.seconds - c0;
        previous = rp;
    }
    int measured = frames - measureFrom;
    std::vector<float> cp = cand.api.Points();
    int outside = 0;
    float beyond = 0;
    if (cp.size() == lo.size())
        for (size_t k = 0; k < cp.size(); ++k) {
            float over = std::max(lo[k] - cp[k], cp[k] - hi[k]);
            if (over > 0) { ++outside; beyond = std::max(beyond, over); }
        }
    else
        beyond = -1.0f;
    std::printf("candidate last drew at frame %d (%.1f s after the drop)\n", lastDrawn, (lastDrawn + 1) * kFrameSeconds);
    std::printf("last %d frames (%.0f s):\n", measured, measured * kFrameSeconds);
    std::printf("  reference vs its previous frame: changed in %d frames, worst %.4f units\n", selfChangedFrames, worstSelf);
    std::printf("  reference vs its window start:   worst %.4f units\n", worstSpread);
    std::printf("  candidate outside the reference's range: %d coords, by up to %.4f units\n", outside, beyond);
    std::printf("  time per frame: reference %.2f ms, candidate %.3f ms\n", refSeconds * 1000 / measured, candSeconds * 1000 / measured);
    const bool asleep = lastDrawn < measureFrom;
    const bool within = beyond >= 0.0f && beyond <= kPosSlack;
    const bool refRested = worstSpread <= kRefRestBound;
    if (!asleep) std::printf("FAIL: the candidate was still drawing during the measured window\n");
    if (beyond < 0.0f) std::printf("FAIL: the candidate and reference report a different vertex count\n");
    else if (!within) std::printf("FAIL: a frozen vertex is more than %.2f units outside what the reference showed\n", kPosSlack);
    if (!refRested) std::printf("FAIL: the reference was still moving (spread %.4f > %.2f units) in the measured window, so its range is not a resting one\n", worstSpread, kRefRestBound);
    std::fflush(stdout);
    return asleep && within && refRested;
}

int RestScenario(Side& ref, Side& cand) {
    // The user's desktop: three columns of icons and one icon under the dropped rug.
    std::vector<float> bumps;
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 8; ++row)
            bumps.insert(bumps.end(), { 47.0f + 95.0f * col, kHeight - (37.0f + 125.0f * row), 30.08f, 30.08f, 22.0f });
    bumps.insert(bumps.end(), { 712.0f, kHeight - 412.0f, 30.08f, 30.08f, 22.0f });
    Both(ref, cand, [&](Api& a) { a.set_bumps(bumps.data(), int(bumps.size() / 5)); });
    bool passed = Rest(ref, cand, "first drop");

    // Both layouts reset every particle, so the frozen candidate and the reference start identical again.
    Both(ref, cand, [](Api& a) { a.layout(kWidth / 2.0f, kHeight / 2.0f, 0.0f, 60.0f); });
    passed = Rest(ref, cand, "after Center") && passed;

    // A re-drop off the centre: both builds lay the rug out identically, so their frozen frames stay comparable.
    // hold_begin now holds the rug in place instead of re-centring it, so it no longer drops the rug at the centre
    // and cannot be measured against the re-centring reference here; the off-centre layout keeps them aligned.
    Both(ref, cand, [](Api& a) { a.layout(kWidth / 2.0f + 200.0f, kHeight / 2.0f - 100.0f, 0.3f, 60.0f); });
    passed = Rest(ref, cand, "after an off-centre drop") && passed;
    return passed ? 0 : 1;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 6) { std::printf("usage: equivalence exact|sleep <reference dll> <candidate dll> <rug> <second rug>\n"); return 2; }
    const bool sleepMode = std::wstring(argv[1]) == L"sleep";
    Side ref(argv[2]), cand(argv[3]);
    if (!ref.api.init(kWidth, kHeight) || !cand.api.init(kWidth, kHeight)) { std::printf("init failed\n"); return 2; }
    if (!ref.api.load_texture(argv[4]) || !cand.api.load_texture(argv[4])) { std::printf("texture failed\n"); return 2; }
    Both(ref, cand, [](Api& a) { a.rebuild(0.46f, 90.0f); });
    if (sleepMode) return RestScenario(ref, cand);
    auto bumps = IconBumps(0);
    Both(ref, cand, [&](Api& a) { a.set_bumps(bumps.data(), int(bumps.size() / 5)); });

    Idle(ref, cand, 240, "drop and settle");

    g_phase = "drag";
    float pose[3];
    ref.api.get_pose(pose);
    float gx = pose[0] + 300.0f, gy = pose[1] + 120.0f;
    int downRef = ref.api.mouse_down(gx, gy), downCand = cand.api.mouse_down(gx, gy);
    if (downRef != downCand) Fail("mouse down");
    for (int i = 0; i < 90; ++i) {
        float t = i / 90.0f * 6.2831853f;
        float x = gx + 220.0f * std::sin(t), y = gy + 140.0f * (1.0f - std::cos(t));
        Both(ref, cand, [&](Api& a) { a.mouse_drag(x, y); });
        Frame(ref, cand, x, y);
    }
    Both(ref, cand, [](Api& a) { a.mouse_up(); });
    Idle(ref, cand, 150, "after drag");

    g_phase = "wheel rotate";
    for (int i = 0; i < 12; ++i) { Both(ref, cand, [](Api& a) { a.rotate(0.08f); }); Frame(ref, cand, 960, 600); }
    Idle(ref, cand, 120, "after rotate");

    g_phase = "resize";
    Both(ref, cand, [](Api& a) { a.resize(0.56f); });
    Idle(ref, cand, 150, "after resize");

    g_phase = "icons moved";
    bumps = IconBumps(380.0f);
    Both(ref, cand, [&](Api& a) { a.set_bumps(bumps.data(), int(bumps.size() / 5)); });
    Idle(ref, cand, 160, "icons moved");
    for (int i = 0; i < 3; ++i) {
        Both(ref, cand, [&](Api& a) { a.set_bumps(bumps.data(), int(bumps.size() / 5)); });
        Idle(ref, cand, 64, "same icons again");
    }

    g_phase = "drop again";
    ref.api.get_pose(pose);
    Both(ref, cand, [&](Api& a) { a.layout(pose[0], pose[1], pose[2], 110.0f); });
    Idle(ref, cand, 240, "after drop again");

    g_phase = "center";
    Both(ref, cand, [](Api& a) { a.layout(kWidth / 2.0f, kHeight / 2.0f, 0.0f, 60.0f); });
    Idle(ref, cand, 200, "after center");

    g_phase = "change rug";
    if (!ref.api.load_texture(argv[5]) || !cand.api.load_texture(argv[5])) Fail("second texture");
    Both(ref, cand, [](Api& a) { a.resize(0.46f); });
    Idle(ref, cand, 200, "after change rug");

    g_phase = "missing picture";
    if (ref.api.load_texture(L"C:\\no\\such\\picture.jpg") != cand.api.load_texture(L"C:\\no\\such\\picture.jpg"))
        Fail("missing picture result");
    Idle(ref, cand, 30, "after missing picture");

    // Pressing Alt now lifts the rug where it sits instead of re-centring it. The old reference re-lays the cloth
    // out at the screen centre, which advances its per-vertex jitter RNG, while the new build touches neither the
    // pose nor the RNG. That RNG desync would spoil any bit-exact frame that followed, so this deliberately
    // divergent step is the last one, and the in-place lift is asserted on its own rather than by comparison.
    g_phase = "alt hold";
    g_bitExact = false;
    // Move the rug well off-centre and off-angle first. At the screen centre the in-place hold and the old
    // re-centring hold look identical, so the check would pass either way; off-centre, a regression to re-centring
    // moves the rug by hundreds of px and resets the angle, and the asserts below catch it.
    cand.api.layout(kWidth / 2.0f + 400.0f, kHeight / 2.0f - 250.0f, 0.5f, 60.0f);
    Idle(ref, cand, 30, "settle off-centre before hold");
    float beforeHold[3];
    cand.api.get_pose(beforeHold);
    cand.api.hold_begin();
    float held[3];
    cand.api.get_pose(held);
    float movedByHold = std::hypot(held[0] - beforeHold[0], held[1] - beforeHold[1]);
    float turnedByHold = std::fabs(held[2] - beforeHold[2]);
    if (movedByHold > 5.0f || turnedByHold > 0.02f) {
        std::printf("MISMATCH: hold_begin moved the rug %.1f px / %.3f rad instead of holding it in place\n", movedByHold, turnedByHold);
        ++g_failures;
    } else {
        std::printf("alt hold kept the rug within %.2f px and %.3f rad of where it sat\n", movedByHold, turnedByHold);
    }
    Idle(ref, cand, 40, "held");
    // hold_end drops the lifted rug: it falls in z but its centre must stay put, not snap back to the middle.
    float beforeEnd[3];
    cand.api.get_pose(beforeEnd);
    cand.api.hold_end();
    Idle(ref, cand, 60, "after hold");
    float afterEnd[3];
    cand.api.get_pose(afterEnd);
    float movedByEnd = std::hypot(afterEnd[0] - beforeEnd[0], afterEnd[1] - beforeEnd[1]);
    if (movedByEnd > 60.0f) {
        std::printf("MISMATCH: after hold_end the rug's centre moved %.1f px (it should settle near where it was held)\n", movedByEnd);
        ++g_failures;
    } else {
        std::printf("hold_end dropped the rug within %.1f px of where it was held\n", movedByEnd);
    }

    Both(ref, cand, [](Api& a) { a.shutdown(); });
    std::printf("frames %d, mismatches %d\n", g_frame, g_failures);
    std::printf("reference: %.2f ms/frame, drew %d frames\n", ref.seconds * 1000 / g_frame, ref.drawn);
    std::printf("candidate: %.2f ms/frame, drew %d frames\n", cand.seconds * 1000 / g_frame, cand.drawn);
    return g_failures == 0 ? 0 : 1;
}
