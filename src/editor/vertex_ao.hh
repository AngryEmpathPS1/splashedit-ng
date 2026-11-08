// Ambient occlusion for baking into vertex colours (#305): how much of the
// hemisphere above each vertex is open sky within a distance, by rays
// against the scene's triangles. Headless and deterministic (the same input
// gives the same output, no random numbers), so editor_tests checks it; the
// Paint tool's Bake occlusion (editor/vertex_paint.hh) multiplies it in.
#pragma once

#include <vector>

#include "scene.hh"
#include "unitymath.hh"

namespace editor {

struct AoTriangle {
    splash::Vec3 a, b, c;  // world space, either face occludes
};

// Per vertex the unoccluded fraction, 0..1, of `rays` cosine-weighted
// directions over the hemisphere about normals[i] (unit length), each
// traced from positions[i] lifted off the surface by 1e-3 of the diagonal
// of the box around the occluders and the positions, out to `distance`
// metres. A zero normal or no rays gives 1. The directions are a fixed
// Hammersley set turned about the normal by an angle hashed from the
// position, so vertices at one place agree and nothing bands in step.
std::vector<float> ambientOcclusion(const std::vector<AoTriangle>& occluders, const std::vector<splash::Vec3>& positions,
                                    const std::vector<splash::Vec3>& normals, float distance, int rays = 128);

}  // namespace editor
