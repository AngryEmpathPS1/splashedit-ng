#include "editor/vertex_ao.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>

#include "editor/gizmo.hh"

namespace editor {

namespace {

using splash::Vec3;

struct Tri {
    Vec3 v0, e1, e2;
};

// A median-split BVH over the occluders, for any-hit rays. A leaf has
// count > 0 and its triangles at first..first+count of the order; an inner
// node's children are first and first + 1.
struct Node {
    Vec3 lo, hi;
    int first = 0, count = 0;
};

struct Bvh {
    std::vector<Tri> tris;  // in leaf order
    std::vector<Node> nodes;
};

constexpr int kLeafSize = 4;

Bvh buildBvh(const std::vector<AoTriangle>& in) {
    Bvh b;
    if (in.empty()) return b;
    std::vector<Vec3> lo(in.size()), hi(in.size()), mid(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        lo[i] = splash::vmin(in[i].a, splash::vmin(in[i].b, in[i].c));
        hi[i] = splash::vmax(in[i].a, splash::vmax(in[i].b, in[i].c));
        mid[i] = (lo[i] + hi[i]) * 0.5f;
    }
    std::vector<int> order(in.size());
    std::iota(order.begin(), order.end(), 0);
    b.nodes.reserve(in.size() * 2);
    b.nodes.push_back({});
    struct Job {
        int node, begin, end;
    };
    std::vector<Job> jobs{{0, 0, int(in.size())}};
    while (!jobs.empty()) {
        const Job j = jobs.back();
        jobs.pop_back();
        Vec3 nlo = lo[size_t(order[size_t(j.begin)])], nhi = hi[size_t(order[size_t(j.begin)])];
        Vec3 clo = mid[size_t(order[size_t(j.begin)])], chi = clo;
        for (int k = j.begin + 1; k < j.end; k++) {
            const size_t t = size_t(order[size_t(k)]);
            nlo = splash::vmin(nlo, lo[t]), nhi = splash::vmax(nhi, hi[t]);
            clo = splash::vmin(clo, mid[t]), chi = splash::vmax(chi, mid[t]);
        }
        b.nodes[size_t(j.node)].lo = nlo;
        b.nodes[size_t(j.node)].hi = nhi;
        const Vec3 ext = chi - clo;
        const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
        if (j.end - j.begin <= kLeafSize || ext[axis] <= 0) {
            b.nodes[size_t(j.node)].first = j.begin;
            b.nodes[size_t(j.node)].count = j.end - j.begin;
            continue;
        }
        // Split at the median centroid on the longest axis (ties by index, so it is the same every time).
        const int m = (j.begin + j.end) / 2;
        std::nth_element(order.begin() + j.begin, order.begin() + m, order.begin() + j.end, [&](int x, int y) {
            const float a = mid[size_t(x)][axis], c = mid[size_t(y)][axis];
            return a < c || (a == c && x < y);
        });
        const int left = int(b.nodes.size());
        b.nodes[size_t(j.node)].first = left;
        b.nodes.push_back({});
        b.nodes.push_back({});
        jobs.push_back({left, j.begin, m});
        jobs.push_back({left + 1, m, j.end});
    }
    b.tris.reserve(in.size());
    for (int i : order) {
        const AoTriangle& t = in[size_t(i)];
        b.tris.push_back({t.a, t.b - t.a, t.c - t.a});
    }
    return b;
}

// Whether the ray o + d t, tmin < t < tmax, meets the box.
bool hitsBox(const Node& n, Vec3 o, Vec3 inv, float tmin, float tmax) {
    for (int a = 0; a < 3; a++) {
        float t0 = (n.lo[a] - o[a]) * inv[a], t1 = (n.hi[a] - o[a]) * inv[a];
        if (t0 > t1) std::swap(t0, t1);
        tmin = std::max(tmin, t0), tmax = std::min(tmax, t1);
        if (tmin > tmax) return false;
    }
    return true;
}

// Moller-Trumbore, both faces.
bool hitsTri(const Tri& t, Vec3 o, Vec3 d, float tmin, float tmax) {
    const Vec3 p = splash::cross(d, t.e2);
    const float det = splash::dot(t.e1, p);
    if (std::fabs(det) < 1e-20f) return false;
    const float inv = 1 / det;
    const Vec3 s = o - t.v0;
    const float u = splash::dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    const Vec3 q = splash::cross(s, t.e1);
    const float v = splash::dot(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    const float h = splash::dot(t.e2, q) * inv;
    return h > tmin && h < tmax;
}

bool occluded(const Bvh& b, Vec3 o, Vec3 d, float tmin, float tmax) {
    if (b.nodes.empty()) return false;
    auto safeInv = [](float c) { return 1 / (std::fabs(c) > 1e-12f ? c : std::copysign(1e-12f, c)); };
    const Vec3 inv{safeInv(d.x), safeInv(d.y), safeInv(d.z)};
    int stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& n = b.nodes[size_t(stack[--top])];
        if (!hitsBox(n, o, inv, tmin, tmax)) continue;
        if (n.count > 0) {
            for (int k = n.first; k < n.first + n.count; k++)
                if (hitsTri(b.tris[size_t(k)], o, d, tmin, tmax)) return true;
        } else if (top + 2 <= 64) {
            stack[top++] = n.first + 1;
            stack[top++] = n.first;
        }
    }
    return false;
}

// Van der Corput radical inverse in base 2: the Hammersley set's second coordinate.
float radicalInverse(uint32_t i) {
    i = (i << 16) | (i >> 16);
    i = ((i & 0x55555555u) << 1) | ((i & 0xAAAAAAAAu) >> 1);
    i = ((i & 0x33333333u) << 2) | ((i & 0xCCCCCCCCu) >> 2);
    i = ((i & 0x0F0F0F0Fu) << 4) | ((i & 0xF0F0F0F0u) >> 4);
    i = ((i & 0x00FF00FFu) << 8) | ((i & 0xFF00FF00u) >> 8);
    return float(double(i) / 4294967296.0);
}

// A turn in [0, 2 pi) from the position's bits (FNV-1a, then a final mix).
float turnOf(Vec3 p) {
    uint32_t h = 2166136261u;
    for (float c : {p.x, p.y, p.z}) {
        uint32_t bits;
        std::memcpy(&bits, &c, sizeof bits);
        if (c == 0) bits = 0;  // -0 and 0 are one place
        for (int k = 0; k < 4; k++) h = (h ^ ((bits >> (8 * k)) & 0xffu)) * 16777619u;
    }
    h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15;
    return float(double(h >> 8) / 16777216.0) * 2 * kPi;
}

}  // namespace

std::vector<float> ambientOcclusion(const std::vector<AoTriangle>& occluders, const std::vector<Vec3>& positions,
                                    const std::vector<Vec3>& normals, float distance, int rays) {
    std::vector<float> out(positions.size(), 1.f);
    if (rays <= 0 || distance <= 0 || occluders.empty()) return out;
    // How far a ray starts off its surface, and the nearest hit it counts:
    // 1e-3 of the diagonal of everything involved.
    Vec3 lo = occluders[0].a, hi = lo;
    for (const AoTriangle& t : occluders)
        lo = splash::vmin(lo, splash::vmin(t.a, splash::vmin(t.b, t.c))), hi = splash::vmax(hi, splash::vmax(t.a, splash::vmax(t.b, t.c)));
    for (const Vec3& p : positions) lo = splash::vmin(lo, p), hi = splash::vmax(hi, p);
    const float eps = std::max(1e-3f * splash::magnitude(hi - lo), 1e-6f);
    const Bvh bvh = buildBvh(occluders);
    // Cosine-weighted Hammersley directions about +z: radius sqrt(u1) on the disc, lifted onto the hemisphere.
    struct Dir {
        float r, phi, z;
    };
    std::vector<Dir> dirs;
    dirs.reserve(size_t(rays));
    for (int i = 0; i < rays; i++) {
        const float u1 = (float(i) + 0.5f) / float(rays);
        dirs.push_back({std::sqrt(u1), 2 * kPi * radicalInverse(uint32_t(i)), std::sqrt(1 - u1)});
    }
    for (size_t v = 0; v < positions.size() && v < normals.size(); v++) {
        const Vec3 n = splash::normalized(normals[v]);
        if (splash::sqrMagnitude(n) < 0.25f) continue;
        // An orthonormal frame about n (Duff et al. 2017).
        const float sign = std::copysign(1.f, n.z), a = -1 / (sign + n.z), b = n.x * n.y * a;
        const Vec3 tx{1 + sign * n.x * n.x * a, sign * b, -sign * n.x}, ty{b, sign + n.y * n.y * a, -n.y};
        const Vec3 o = positions[v] + n * eps;
        const float turn = turnOf(positions[v]);
        int open = 0;
        for (const Dir& s : dirs) {
            const float phi = s.phi + turn;
            const Vec3 d = tx * (s.r * std::cos(phi)) + ty * (s.r * std::sin(phi)) + n * s.z;
            open += !occluded(bvh, o, d, eps, distance);
        }
        out[v] = float(open) / float(rays);
    }
    return out;
}

}  // namespace editor
