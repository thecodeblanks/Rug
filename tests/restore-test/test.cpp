// Checks that a saved drape is restored exactly and stays put. rug_set_points must put the saved vertices back
// (not leave the fresh drop), and the restored drape must rest on its bumps instead of slumping onto flat ground
// while the height field animates up. Written to pin two defects (2026-10-14): the settle/restore path had no
// GUI test, and a restore laid the folds onto a not-yet-risen field.
// Usage: test.exe <rug_native.dll> <rug picture>
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1200;
constexpr float kDt = 1.0f / 60.0f;

struct Api {
    HMODULE module = nullptr;
    int (*init)(int, int);
    int (*load_texture)(const wchar_t*);
    void (*rebuild)(float, float);
    void (*set_bumps)(const float*, int);
    void (*step)(float);
    int (*mouse_down)(float, float);
    void (*mouse_drag)(float, float);
    void (*mouse_up)();
    int (*hit_test)(float, float);
    void (*get_pose)(float*);
    int (*point_count)();
    int (*get_points)(float*, int);
    int (*set_points)(const float*, int);
    int (*is_settled)();
    void (*shutdown)();

    template <class F> void bind(F& fn, const char* name) {
        fn = reinterpret_cast<F>(GetProcAddress(module, name));
        if (!fn) { std::printf("missing export %s\n", name); std::exit(2); }
    }
    explicit Api(const wchar_t* path) {
        module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) { std::wprintf(L"cannot load %ls (%lu)\n", path, GetLastError()); std::exit(2); }
        bind(init, "rug_init"); bind(load_texture, "rug_load_texture"); bind(rebuild, "rug_rebuild");
        bind(set_bumps, "rug_set_bumps"); bind(step, "rug_step"); bind(mouse_down, "rug_mouse_down");
        bind(mouse_drag, "rug_mouse_drag"); bind(mouse_up, "rug_mouse_up"); bind(hit_test, "rug_hit_test");
        bind(get_pose, "rug_get_pose"); bind(point_count, "rug_point_count"); bind(get_points, "rug_get_points");
        bind(set_points, "rug_set_points"); bind(is_settled, "rug_is_settled"); bind(shutdown, "rug_shutdown");
    }
};

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// A patch of icon bumps across the centre so the rug settles with real folds over them.
std::vector<float> CentreBumps() {
    std::vector<float> b;
    for (int col = 0; col < 5; ++col)
        for (int row = 0; row < 4; ++row)
            b.insert(b.end(), { 760.0f + 100.0f * col, 440.0f + 100.0f * row, 30.0f, 30.0f, 22.0f });
    return b;
}

float MaxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return -1.0f;
    float m = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { std::printf("usage: test.exe <rug_native.dll> <rug picture>\n"); return 2; }
    Api api(argv[1]);
    if (!api.init(kWidth, kHeight)) { std::printf("rug_init failed\n"); return 2; }
    if (!api.load_texture(argv[2])) { std::printf("rug_load_texture failed\n"); return 2; }

    std::vector<float> bumps = CentreBumps();

    // A fresh drop over bumps that settles into real folds: this is the drape we save.
    api.rebuild(0.46f, 90.0f);
    api.set_bumps(bumps.data(), int(bumps.size() / 5));
    for (int i = 0; i < 1800; ++i) api.step(kDt);
    float pose[3] = {};
    api.get_pose(pose);
    std::printf("saved: pose=(%.1f, %.1f) settled=%d\n", pose[0], pose[1], api.is_settled());

    int n = api.point_count();
    std::vector<float> saved(static_cast<size_t>(n) * 3);
    check(api.get_points(saved.data(), n) == n, "get_points returns the full vertex count");

    // Restore the way a real restart does. A restart is a brand-new process, so re-init to get a fresh height
    // field (its heights start at zero and rise to the bumps over about 1.5 s), rebuild, set the bumps, then
    // restore. This is the case the field snap exists for: without it the folds slump onto flat ground first.
    if (!api.init(kWidth, kHeight)) { std::printf("re-init failed\n"); return 2; }
    if (!api.load_texture(argv[2])) { std::printf("re-load_texture failed\n"); return 2; }
    api.rebuild(0.46f, 90.0f);
    int n2 = api.point_count();
    check(n2 == n, "vertex count is stable across a restart");
    std::vector<float> freshDrop(static_cast<size_t>(n) * 3);
    api.get_points(freshDrop.data(), n);           // the initial drop, before any restore
    api.set_bumps(bumps.data(), int(bumps.size() / 5));
    int set = api.set_points(saved.data(), n);
    check(set == 1, "set_points accepts the saved drape");

    // set_points must write the saved folds, not leave the fresh drop.
    std::vector<float> restored(static_cast<size_t>(n) * 3);
    api.get_points(restored.data(), n);
    float vsSaved = MaxDiff(restored, saved), vsFresh = MaxDiff(restored, freshDrop);
    std::printf("after set_points: max diff vs saved=%.4f, vs fresh drop=%.1f\n", vsSaved, vsFresh);
    check(vsSaved < 0.001f, "restored vertices equal the saved drape");
    check(vsFresh > 1.0f, "restored drape differs from the fresh drop (set_points did something)");

    // The folds must rest on the bumps, not slump onto flat ground while the field rises.
    for (int i = 0; i < 90; ++i) api.step(kDt);
    std::vector<float> afterSteps(static_cast<size_t>(n) * 3);
    api.get_points(afterSteps.data(), n);
    float slump = MaxDiff(afterSteps, saved);
    std::printf("after 90 steps: max vertex move from the saved drape=%.2f px\n", slump);
    // Measured 2026-10-14: with the field snap a restored vertex moves 0.03 px over these 90 steps; without it,
    // while the ground rises from zero, it slumps 1.83 px. 0.5 px sits between the two, so this fails on a missing
    // snap but not on the ordinary settling jitter.
    check(slump < 0.5f, "restored drape stays on its bumps (no slump onto flat ground)");

    // The restored rug is still interactive.
    float rpose[3] = {};
    api.get_pose(rpose);
    check(api.hit_test(rpose[0], rpose[1]) != 0, "restored rug is hit-testable at its centre");
    int down = api.mouse_down(rpose[0], rpose[1]);
    check(down != 0, "restored rug can be grabbed (mouse_down)");
    for (int i = 0; i < 30; ++i) { api.mouse_drag(rpose[0] + 150.0f, rpose[1]); api.step(kDt); }
    api.mouse_up();
    float after[3] = {};
    api.get_pose(after);
    float moved = std::fabs(after[0] - rpose[0]);
    std::printf("after drag: pose=(%.1f, %.1f), centre moved %.1f px\n", after[0], after[1], moved);
    check(moved > 20.0f, "dragging the restored rug moves it");

    api.shutdown();
    std::printf("%s (%d failure(s))\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
