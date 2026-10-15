#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <utility>
#include <vector>

struct V2 { float x = 0, y = 0; };
struct V3 { float x = 0, y = 0, z = 0; };

inline V2 operator+(V2 a, V2 b) { return { a.x + b.x, a.y + b.y }; }
inline V2 operator-(V2 a, V2 b) { return { a.x - b.x, a.y - b.y }; }
inline V2 operator*(V2 a, float s) { return { a.x * s, a.y * s }; }
inline V2 operator/(V2 a, float s) { return { a.x / s, a.y / s }; }

inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline V3 operator/(V3 a, float s) { return { a.x / s, a.y / s, a.z / s }; }
inline V3& operator+=(V3& a, V3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline V3& operator-=(V3& a, V3 b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline float len2(V3 a) { return a.x * a.x + a.y * a.y + a.z * a.z; }
inline float len(V3 a) { return std::sqrt(len2(a)); }
inline float len2(V2 a) { return a.x * a.x + a.y * a.y; }

struct Camera {
    V2 size{};
    float D() const;
    V2 center() const;
    V2 project(V3 p) const;
    V3 unproject(V2 s, float z) const;
};

struct Bump {
    V2 center{};
    V2 radius{};
    float height = 0;
};

class HeightField {
public:
    explicit HeightField(V2 size);
    void setBumps(const std::vector<Bump>& bumps);
    bool animate(float dt);
    // Jump straight to the target heights, skipping the rise animation, so a restored drape sits on its bumps at once.
    void snap();
    float sample(float x, float y) const;

private:
    static constexpr float cell = 4.0f;
    int w_, h_;
    std::vector<float> data_, target_;
    bool animating_ = false;
};

class SpatialHash {
public:
    SpatialHash(float spacing, int maxObjects);
    void create(const std::vector<V3>& pos, int n);
    void queryAll(const std::vector<V3>& pos, int n, float maxDist);

    std::vector<int> firstAdj;
    std::vector<int> adj;

private:
    int32_t coord(float c) const;
    int hashCoords(int32_t x, int32_t y, int32_t z) const;
    int hashPos(V3 p) const;
    void query(V3 p, float maxDist);

    float spacing_;
    int maxObjects_;
    int tableSize_;
    std::vector<int> cellStart_, cellEntries_, queryIds_;
    int querySize_ = 0;
};

struct ClothParams {
    float gravity = 5000;
    int substeps = 10;
    float shearCompliance = 2e-7f;
    float bendCompliance = 3e-7f;
    float airDamping = 1.3f;
    float groundStatic = 2;
    float groundKinetic = 1.3f;
    float selfFriction = 0.3f;
};

class Cloth {
public:
    Cloth(float width, float height, int columns);

    int nx = 0, ny = 0, count = 0;
    float width = 0, height = 0, spacing = 0, thickness = 0;
    ClothParams params;

    std::vector<V3> pos, prev, vel, rest, nrm;
    std::vector<V2> proj, uvs;
    std::vector<float> invMass;
    std::vector<uint32_t> indices;

    V2 bboxMin{}, bboxMax{};
    float maxSpeed = 0;
    HeightField* ground = nullptr;
    V3 grabTarget{};

    float unitScale = 1.0f;

    bool isGrabbing() const { return !grabbedIds_.empty(); }
    void layout(V2 center, float angle, float drop);
    void pose(V2& center, float& angle) const;
    void rotate(float angle);
    void beginGrab(int vertex, V3 target);
    void endGrab();

    void setHeld(bool held, float z);
    void step(float dt);
    void updateDerived(const Camera& cam);

    std::optional<std::pair<int, float>> pick(V2 s, bool needVertex) const;

private:
    struct Con { int a, b; float rest, compliance; };

    int idx(int i, int j) const { return j * nx + i; }
    void addCon(int a, int b, float compliance);
    void integrate(float sdt, float maxVel);
    void moveGrabbed(float sdt);
    void solveConstraints(float sdt);
    void solveTethers();
    void solveSelfCollisions();
    void solveGround();
    std::vector<Con> cons_;
    // len2(rest[i] - rest[j]) for each self-collision pair, filled once per step and read on every substep.
    std::vector<float> selfRest2_;
    std::vector<int> grabbedIds_;
    std::vector<V3> grabOffsets_;
    std::vector<float> tetherRest_;
    V3 grabPos_{};
    std::unique_ptr<SpatialHash> hash_;
    std::mt19937 rng_{ 12345u };
};
