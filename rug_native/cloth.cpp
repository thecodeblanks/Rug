#include "cloth.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>

float Camera::D() const { return std::max(size.x, size.y) * 1.15f; }
V2 Camera::center() const { return size * 0.5f; }

V2 Camera::project(V3 p) const {
    float w = (D() - p.z) / D();
    V2 c = center();
    return c + (V2{ p.x, p.y } - c) / w;
}

V3 Camera::unproject(V2 s, float z) const {
    float w = (D() - z) / D();
    V2 c = center();
    V2 xy = c + (s - c) * w;
    return { xy.x, xy.y, z };
}

HeightField::HeightField(V2 size) {
    w_ = int(size.x / cell) + 3;
    h_ = int(size.y / cell) + 3;
    data_.assign(size_t(w_) * h_, 0.0f);
    target_.assign(size_t(w_) * h_, 0.0f);
}

void HeightField::setBumps(const std::vector<Bump>& bumps) {
    std::fill(target_.begin(), target_.end(), 0.0f);
    for (const Bump& b : bumps) {
        int x0 = std::max(0, int((b.center.x - b.radius.x) / cell));
        int x1 = std::min(w_ - 1, int((b.center.x + b.radius.x) / cell) + 1);
        int y0 = std::max(0, int((b.center.y - b.radius.y) / cell));
        int y1 = std::min(h_ - 1, int((b.center.y + b.radius.y) / cell) + 1);
        if (x0 > x1 || y0 > y1) continue;
        for (int yy = y0; yy <= y1; ++yy)
            for (int xx = x0; xx <= x1; ++xx) {
                float dx = (float(xx) * cell - b.center.x) / b.radius.x;
                float dy = (float(yy) * cell - b.center.y) / b.radius.y;
                float q = dx * dx + dy * dy;
                if (q < 1.0f) target_[size_t(yy) * w_ + xx] += b.height * std::pow(1.0f - q, 0.6f);
            }
    }
    animating_ = true;
}

bool HeightField::animate(float dt) {
    if (!animating_) return false;
    float k = std::min(1.0f, dt * 4.0f);
    float maxDiff = 0;
    for (size_t i = 0; i < data_.size(); ++i) {
        float d = target_[i] - data_[i];
        if (d != 0.0f) {
            data_[i] += d * k;
            maxDiff = std::max(maxDiff, std::fabs(d));
        }
    }
    if (maxDiff < 0.05f) {
        data_ = target_;
        animating_ = false;
    }
    return true;
}

void HeightField::snap() {
    data_ = target_;
    animating_ = false;
}

float HeightField::sample(float x, float y) const {
    float fx = x / cell, fy = y / cell;
    // Reject out of range as float, before the int cast: a huge finite coordinate (for example from a damaged
    // restored drape) would overflow int(fx) to a negative index and slip past the bound checks, reading out of
    // bounds. The !(fx >= 0) form also rejects a NaN that reached here.
    if (!(fx >= 0) || !(fy >= 0) || fx >= float(w_ - 1) || fy >= float(h_ - 1)) return 0;
    int ix = int(fx), iy = int(fy);
    float tx = fx - ix, ty = fy - iy;
    size_t i = size_t(iy) * w_ + ix;
    float a = data_[i], b = data_[i + 1], c = data_[i + w_], d = data_[i + w_ + 1];
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

SpatialHash::SpatialHash(float spacing, int maxObjects)
    : spacing_(spacing), maxObjects_(maxObjects), tableSize_(2 * maxObjects) {
    cellStart_.assign(tableSize_ + 1, 0);
    cellEntries_.assign(maxObjects, 0);
    queryIds_.assign(maxObjects, 0);
    firstAdj.assign(maxObjects + 1, 0);
    adj.assign(size_t(16) * maxObjects, 0);
}

int32_t SpatialHash::coord(float c) const { return int32_t(std::floor(c / spacing_)); }

int SpatialHash::hashCoords(int32_t x, int32_t y, int32_t z) const {

    uint32_t hu = (uint32_t(x) * 92837111u) ^ (uint32_t(y) * 689287499u) ^ (uint32_t(z) * 283923481u);
    int32_t hs = int32_t(hu);
    uint32_t mag = hs < 0 ? uint32_t(-int64_t(hs)) : uint32_t(hs);
    return int(mag % uint32_t(tableSize_));
}

int SpatialHash::hashPos(V3 p) const { return hashCoords(coord(p.x), coord(p.y), coord(p.z)); }

void SpatialHash::create(const std::vector<V3>& pos, int n) {
    std::fill(cellStart_.begin(), cellStart_.end(), 0);
    for (int i = 0; i < n; ++i) cellStart_[hashPos(pos[i])] += 1;
    int32_t start = 0;
    for (int i = 0; i < tableSize_; ++i) {
        start += cellStart_[i];
        cellStart_[i] = start;
    }
    cellStart_[tableSize_] = start;
    for (int i = 0; i < n; ++i) {
        int h = hashPos(pos[i]);
        cellStart_[h] -= 1;
        cellEntries_[cellStart_[h]] = i;
    }
}

void SpatialHash::query(V3 p, float maxDist) {
    int32_t x0 = coord(p.x - maxDist), y0 = coord(p.y - maxDist), z0 = coord(p.z - maxDist);
    int32_t x1 = coord(p.x + maxDist), y1 = coord(p.y + maxDist), z1 = coord(p.z + maxDist);
    querySize_ = 0;
    for (int32_t xi = x0; xi <= x1; ++xi)
        for (int32_t yi = y0; yi <= y1; ++yi)
            for (int32_t zi = z0; zi <= z1; ++zi) {
                int h = hashCoords(xi, yi, zi);
                int s = cellStart_[h], e = cellStart_[h + 1];
                for (int i = s; i < e && querySize_ < maxObjects_; ++i) queryIds_[querySize_++] = cellEntries_[i];
            }
}

void SpatialHash::queryAll(const std::vector<V3>& pos, int n, float maxDist) {
    int num = 0;
    float md2 = maxDist * maxDist;
    for (int i = 0; i < n; ++i) {
        firstAdj[i] = num;
        query(pos[i], maxDist);
        for (int q = 0; q < querySize_; ++q) {
            int j = queryIds_[q];
            if (j >= i) continue;
            if (len2(pos[j] - pos[i]) > md2) continue;
            if (size_t(num) >= adj.size()) adj.resize(adj.size() * 2);
            adj[num++] = j;
        }
    }
    firstAdj[n] = num;
}

Cloth::Cloth(float w, float h, int columns) : nx(columns) {
    width = w;
    spacing = w / float(columns - 1);
    ny = std::max(3, int(std::round(h / spacing)) + 1);
    count = nx * ny;
    height = spacing * float(ny - 1);
    thickness = spacing * 1.1f;

    pos.assign(count, V3{});
    prev.assign(count, V3{});
    vel.assign(count, V3{});
    rest.assign(count, V3{});
    nrm.assign(count, V3{ 0, 0, 1 });
    proj.assign(count, V2{});
    uvs.assign(count, V2{});
    invMass.assign(count, 1.0f);
    hash_ = std::make_unique<SpatialHash>(thickness, count);

    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            int k = idx(i, j);
            rest[k] = { float(i) * spacing - width / 2, float(j) * spacing - height / 2, 0 };
            pos[k] = rest[k];
            prev[k] = rest[k];
            uvs[k] = { float(i) / float(nx - 1), 1 - float(j) / float(ny - 1) };
        }

    for (int j = 0; j < ny - 1; ++j)
        for (int i = 0; i < nx - 1; ++i) {
            uint32_t a = idx(i, j), b = idx(i + 1, j), c = idx(i, j + 1), d = idx(i + 1, j + 1);
            if ((i + j) % 2 == 0) {
                indices.insert(indices.end(), { a, b, d, a, d, c });
            } else {
                indices.insert(indices.end(), { a, b, c, b, d, c });
            }
        }

    const ClothParams& p = params;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            if (i + 1 < nx) addCon(idx(i, j), idx(i + 1, j), 0);
            if (j + 1 < ny) addCon(idx(i, j), idx(i, j + 1), 0);
            if (i + 1 < nx && j + 1 < ny) {
                addCon(idx(i, j), idx(i + 1, j + 1), p.shearCompliance);
                addCon(idx(i + 1, j), idx(i, j + 1), p.shearCompliance);
            }
            if (i + 2 < nx) addCon(idx(i, j), idx(i + 2, j), p.bendCompliance);
            if (j + 2 < ny) addCon(idx(i, j), idx(i, j + 2), p.bendCompliance);
        }
}

void Cloth::addCon(int a, int b, float compliance) {
    cons_.push_back({ a, b, len(rest[a] - rest[b]), compliance });
}

void Cloth::layout(V2 center, float angle, float drop) {
    endGrab();
    float ca = std::cos(angle), sa = std::sin(angle);
    std::uniform_real_distribution<float> jitter(-1.5f, 1.5f);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            int k = idx(i, j);
            V3 r = rest[k];
            float u = float(i) / float(nx - 1);
            float z = drop > 0 ? drop * (0.55f + 0.45f * u) + jitter(rng_) : 0.0f;
            pos[k] = { center.x + r.x * ca - r.y * sa, center.y + r.x * sa + r.y * ca, z };
            prev[k] = pos[k];
            vel[k] = {};
        }
    maxSpeed = 1000;
}

void Cloth::pose(V2& c, float& angle) const {
    V2 s{};
    for (int i = 0; i < count; ++i) s = s + V2{ pos[i].x, pos[i].y };
    c = s / float(count);
    int j = ny / 2;
    V3 a = pos[idx(0, j)], b = pos[idx(nx - 1, j)];
    angle = std::atan2(b.y - a.y, b.x - a.x);
}

void Cloth::rotate(float angle) {
    V2 c;
    float unused;
    pose(c, unused);
    float ca = std::cos(angle), sa = std::sin(angle);
    auto rot = [&](V3 p) {
        V2 d = V2{ p.x, p.y } - c;
        return V3{ c.x + d.x * ca - d.y * sa, c.y + d.x * sa + d.y * ca, p.z };
    };
    for (int i = 0; i < count; ++i) {
        pos[i] = rot(pos[i]);
        prev[i] = rot(prev[i]);
    }
    if (isGrabbing()) {
        grabPos_ = rot(grabPos_);
        grabTarget = rot(grabTarget);
    }
}

void Cloth::beginGrab(int v, V3 target) {
    endGrab();
    int vi = v % nx, vj = v / nx;
    V3 center = pos[v];
    for (int j = std::max(0, vj - 1); j <= std::min(ny - 1, vj + 1); ++j)
        for (int i = std::max(0, vi - 1); i <= std::min(nx - 1, vi + 1); ++i) {
            int k = idx(i, j);
            grabbedIds_.push_back(k);
            V3 o = pos[k] - center;
            o.z = 0;
            grabOffsets_.push_back(o);
            invMass[k] = 0;
        }
    grabPos_ = center;
    grabTarget = target;

    tetherRest_.resize(count);
    for (int i = 0; i < count; ++i) tetherRest_[i] = len(rest[i] - rest[v]) * 1.01f;
    maxSpeed = 1000;
}

void Cloth::endGrab() {
    const float cap = 1400;
    for (int k : grabbedIds_) {
        invMass[k] = 1;
        float s = len(vel[k]);
        if (s > cap) vel[k] = vel[k] * (cap / s);
    }
    grabbedIds_.clear();
    grabOffsets_.clear();
}

void Cloth::setHeld(bool held, float z) {
    endGrab();
    for (int k = 0; k < count; ++k) {
        if (held) {
            invMass[k] = 0;
            pos[k] = { pos[k].x, pos[k].y, z };
        } else {
            invMass[k] = 1;
        }
        prev[k] = pos[k];
        vel[k] = {};
    }
}

void Cloth::step(float frameDt) {
    int n = params.substeps;
    float sdt = frameDt / float(n);
    float maxVel = 0.2f * thickness / sdt;
    float travel = std::min(std::max(maxSpeed * frameDt * 1.5f, thickness), thickness * 4.0f);

    hash_->create(pos, count);
    hash_->queryAll(pos, count, travel);

    // rest[] is fixed for the whole step, so each pair's rest distance is computed once here instead of on every
    // substep inside solveSelfCollisions (which runs 2 * substeps times).
    int pairs = hash_->firstAdj[count];
    if (int(selfRest2_.size()) < pairs) selfRest2_.resize(hash_->adj.size());
    for (int i = 0; i < count; ++i)
        for (int k = hash_->firstAdj[i]; k < hash_->firstAdj[i + 1]; ++k)
            selfRest2_[k] = len2(rest[i] - rest[hash_->adj[k]]);

    for (int s = 0; s < n; ++s) {
        integrate(sdt, maxVel);
        moveGrabbed(sdt);
        solveConstraints(sdt);
        solveTethers();
        solveSelfCollisions();
        solveSelfCollisions();
        solveGround();
        float inv = 1 / sdt;
        for (int i = 0; i < count; ++i) vel[i] = (pos[i] - prev[i]) * inv;
    }

    float m = 0;
    for (int i = 0; i < count; ++i)
        if (invMass[i] > 0) m = std::max(m, len2(vel[i]));
    maxSpeed = std::sqrt(m);

    if (!std::isfinite(maxSpeed)) {

        V3 p = prev[count / 2];
        V2 c = { p.x, p.y };
        if (!std::isfinite(c.x) || !std::isfinite(c.y)) c = { width, height };
        layout(c, 0, 60);
    }
}

void Cloth::integrate(float sdt, float maxVel) {
    float g = params.gravity * unitScale * sdt;
    float damp = std::max(0.0f, 1 - params.airDamping * sdt);
    float mv2 = maxVel * maxVel;
    for (int i = 0; i < count; ++i) {
        if (invMass[i] == 0) continue;
        V3 v = vel[i];
        v.z -= g;
        v = v * damp;
        float s2 = len2(v);
        if (s2 > mv2) v = v * (maxVel / std::sqrt(s2));
        vel[i] = v;
        prev[i] = pos[i];
        pos[i] += v * sdt;
    }
}

void Cloth::moveGrabbed(float sdt) {
    if (!isGrabbing()) return;
    V3 d = (grabTarget - grabPos_) * std::min(1.0f, sdt * 30);
    float maxStep = 0.3f * thickness;
    float l = len(d);
    if (l > maxStep) d = d * (maxStep / l);
    grabPos_ += d;
    for (size_t n = 0; n < grabbedIds_.size(); ++n) {
        int k = grabbedIds_[n];
        prev[k] = pos[k];
        pos[k] = grabPos_ + grabOffsets_[n];
    }
}

void Cloth::solveConstraints(float sdt) {
    float inv = 1 / (sdt * sdt);
    for (const Con& c : cons_) {
        int a = c.a, b = c.b;
        float wa = invMass[a], wb = invMass[b];
        float w = wa + wb;
        if (w == 0) continue;
        V3 d = pos[b] - pos[a];
        float l = len(d);
        if (l < 1e-6f) continue;
        float s = -(l - c.rest) / (w + c.compliance * inv);
        V3 nd = d / l;
        pos[a] -= nd * (s * wa);
        pos[b] += nd * (s * wb);
    }
}

void Cloth::solveTethers() {
    if (!isGrabbing() || int(tetherRest_.size()) != count) return;
    V3 g = grabPos_;
    for (int i = 0; i < count; ++i) {
        if (invMass[i] == 0) continue;
        V3 d = pos[i] - g;
        float l = len(d);
        float m = tetherRest_[i];
        if (l > m) pos[i] = g + d * (m / l);
    }
}

void Cloth::solveSelfCollisions() {
    float t = thickness, t2 = t * t;
    float fr = params.selfFriction;
    for (int i = 0; i < count; ++i) {
        int first = hash_->firstAdj[i], last = hash_->firstAdj[i + 1];
        if (first == last) continue;
        float wi = invMass[i];
        for (int k = first; k < last; ++k) {
            int j = hash_->adj[k];
            float wj = invMass[j];
            float w = wi + wj;
            if (w == 0) continue;
            V3 d = pos[j] - pos[i];
            float dist2 = len2(d);
            if (dist2 > t2 || dist2 == 0) continue;
            float restDist2 = selfRest2_[k];
            if (dist2 > restDist2) continue;
            float minDist = t;
            if (restDist2 < t2) minDist = std::sqrt(restDist2);
            float dist = std::sqrt(dist2);
            d = d * ((minDist - dist) / (dist * w));
            pos[i] -= d * wi;
            pos[j] += d * wj;
            V3 v0 = pos[i] - prev[i], v1 = pos[j] - prev[j];
            V3 avg = (v0 * wj + v1 * wi) / w;
            pos[i] += (avg - v0) * (fr * wi / w * 2);
            pos[j] += (avg - v1) * (fr * wj / w * 2);
        }
    }
}

void Cloth::solveGround() {
    float ms = params.groundStatic;
    float mk = params.groundKinetic;
    for (int i = 0; i < count; ++i) {
        if (invMass[i] == 0) continue;
        V3 p = pos[i];
        float h = ground ? ground->sample(p.x, p.y) : 0.0f;
        if (p.z >= h) continue;
        float pen = h - p.z;
        p.z = h;
        V3 q = prev[i];
        float dx = p.x - q.x, dy = p.y - q.y;
        float l = std::sqrt(dx * dx + dy * dy);
        if (l < ms * pen) {
            p.x = q.x;
            p.y = q.y;
        } else if (l > 0) {
            float k = std::min(mk * pen / l, 1.0f);
            p.x -= dx * k;
            p.y -= dy * k;
        }
        pos[i] = p;
    }
}

void Cloth::updateDerived(const Camera& cam) {
    for (int i = 0; i < count; ++i) nrm[i] = {};
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
        V3 n = cross(pos[b] - pos[a], pos[c] - pos[a]);
        nrm[a] += n;
        nrm[b] += n;
        nrm[c] += n;
    }
    V2 lo{ FLT_MAX, FLT_MAX }, hi{ -FLT_MAX, -FLT_MAX };
    for (int i = 0; i < count; ++i) {
        float l = len(nrm[i]);
        nrm[i] = l > 0 ? nrm[i] / l : V3{ 0, 0, 1 };
        V2 s = cam.project(pos[i]);
        proj[i] = s;
        lo = { std::min(lo.x, s.x), std::min(lo.y, s.y) };
        hi = { std::max(hi.x, s.x), std::max(hi.y, s.y) };
    }
    bboxMin = lo;
    bboxMax = hi;
}

std::optional<std::pair<int, float>> Cloth::pick(V2 s, bool needVertex) const {
    if (s.x < bboxMin.x || s.y < bboxMin.y || s.x > bboxMax.x || s.y > bboxMax.y) return std::nullopt;
    float bestZ = -FLT_MAX;
    int bestV = -1;
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
        V2 pa = proj[a], pb = proj[b], pc = proj[c];
        if (s.x < std::min({ pa.x, pb.x, pc.x }) || s.x > std::max({ pa.x, pb.x, pc.x })) continue;
        if (s.y < std::min({ pa.y, pb.y, pc.y }) || s.y > std::max({ pa.y, pb.y, pc.y })) continue;
        V2 v0 = pb - pa, v1 = pc - pa, v2 = s - pa;
        float den = v0.x * v1.y - v1.x * v0.y;
        if (std::fabs(den) < 1e-6f) continue;
        float u = (v2.x * v1.y - v1.x * v2.y) / den;
        float v = (v0.x * v2.y - v2.x * v0.y) / den;
        if (u < 0 || v < 0 || u + v > 1) continue;
        float w = 1 - u - v;
        float z = pos[a].z * w + pos[b].z * u + pos[c].z * v;
        if (z > bestZ) {
            bestZ = z;
            bestV = (w >= u && w >= v) ? int(a) : (u >= v ? int(b) : int(c));
            if (!needVertex) break;
        }
    }
    if (bestV < 0) return std::nullopt;
    return std::make_pair(bestV, bestZ);
}
