// Model import: a glTF 2.0 (.gltf, .glb), FBX, OBJ (with its .mtl) or .blend
// file turned into a project .mesh plus the PNG textures its materials use.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "scene.hh"

namespace splash {

// One node of the model's hierarchy. The transform is local to the parent
// node (to the model's root for a top-level node) and already in the .mesh
// space: X mirrored in the position, the rotation (x, y, z, w) taken to
// (x, -y, -z, w), the scale as it is. Composing it down the tree over `mesh`
// gives the node's part of the baked mesh.
struct ImportedNode {
    std::string name;
    int parent = -1;  // index into ImportedModel::nodes, -1 for a top-level node
    Transform transform;
    std::string mesh;                 // project-relative .mesh in the node's own space, empty for none
    std::vector<Material> materials;  // one per submesh of `mesh`
    std::vector<std::string> materialNames;  // beside materials: each one's name in the file
    // `mesh` carries a skeleton. Its space is then the baked mesh's: the
    // transform puts the node at the model's root, as glTF ignores a skinned
    // node's own transform.
    bool skinned = false;
};

struct ImportedModel {
    std::string mesh;                 // project-relative path of the written .mesh
    std::vector<Material> materials;  // one per submesh, textures project-relative
    std::vector<std::string> materialNames;  // beside materials: each one's name in the file
    std::vector<ImportedNode> nodes;  // the node tree, every parent before its children
    std::vector<std::string> textures;  // every PNG written, project-relative
    bool skinned = false;               // the baked mesh carries a skeleton
    std::vector<std::string> clips;     // every .anim written, project-relative, in file order
    std::vector<std::string> clipTakes; // beside clips: each one's name in the file (its take)
    int triangles = 0;        // in the baked mesh as written
    int sourceTriangles = 0;  // in the baked mesh before targetTriangles applied
    std::vector<std::string> warnings;
};

// How a model is imported, kept per model file in a sidecar beside it (see
// importSettingsPath), the way Unity keeps a model's import settings.
enum class UpAxis : uint8_t { File, YUp, ZUp };
enum class Normals : uint8_t { Import, Recompute };
struct ImportSettings {
    float scale = 1;  // multiplies every position and node translation
    // An FBX's own unit turned into metres, on top of `scale`, as Unity's
    // Convert Units does: into every position and node translation, node
    // scales as written. Off: the file's numbers are taken as metres.
    bool convertUnits = true;
    // File: the axes the file declares. YUp, ZUp: the data is taken as
    // right-handed Y up (+Z front) or Z up (-Y front) whatever the file says.
    UpAxis up = UpAxis::File;
    // Recompute: every corner gets the average of the face normals around its
    // position that are within smoothingAngle degrees of its own face's, so a
    // sharper edge stays hard. Import: the file's normals, flat where it has none.
    Normals normals = Normals::Import;
    float smoothingAngle = 60;
    // Above 0: the model is simplified toward this many triangles (see
    // simplifyToTriangles), the baked mesh to all of them and each node mesh
    // to its share, in proportion to its own count (at least 4, at most what
    // it has). Every simplified mesh then gets recomputed normals, at
    // smoothingAngle, whichever `normals` says. 0 keeps every triangle.
    int targetTriangles = 0;
    // The baked mesh's triangles before targetTriangles, as the editor last
    // imported it (for the inspector; saved only with a target). 0: unknown.
    int sourceTriangles = 0;
    // Objects the editor makes from the model: one object drawing the baked
    // mesh instead of the node tree, and these on each mesh component.
    bool mergeMeshes = false;
    BitDepth bitDepth = MeshComponent{}.bitDepth;
    DynamicLighting lighting = MeshComponent{}.dynamicLighting;
    // Absolute path of the file the model was copied into the project from;
    // empty when it was in the project already.
    std::string source;
    // Names given to the model's clips in the editor, by the clip's file
    // under models/<name>/ without .anim ("Walk", "clips/run_fbx/mixamo_com"):
    // the import writes that name into the .anim, the file keeping the one it
    // makes from the take, so a rename survives importing again. Saved only
    // when there are any.
    std::map<std::string, std::string> clipNames;
    // A material's normal map and occlusion map baked into the base colour
    // texture written (see bakeSurfaceDetail): psxsplash lights per vertex,
    // so the texture is the only place that detail can stay. Saved only on.
    bool bakeDetail = false;
    // With a targetTriangles: the shading the simplified mesh lost against
    // the full one baked into each base colour texture (see bakeLostDetail).
    // Saved only on.
    bool bakeLost = false;
    // A mesh's pivot moved in the editor (Set pivot), by the project-relative
    // .mesh the import writes, as MeshComponent::mesh holds it
    // ("models/<stem>.mesh", "models/<stem>/<node>.mesh"): where the new
    // origin is in that mesh's space after scale and up axis, summed over
    // every move. The import takes it off the mesh's positions and, for a
    // node mesh, puts it on the node's translation and off its children's, so
    // nothing moves. A skinned mesh ignores it (it stays in root space).
    // Saved only when there are any.
    std::map<std::string, Vec3> pivots;
    ExtraKeys extra;
};

// An 8-bit RGBA image, rows top-down as stb_image reads them.
struct RgbaImage {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
};
// A PNG or JPEG decoded; nothing when it is neither.
std::optional<RgbaImage> decodeRgba(const std::vector<unsigned char>& bytes);
// The maps bakeSurfaceDetail shades a base colour image by. A normal map is
// tangent space, green up (OpenGL); occlusion reads one channel of its image.
struct DetailMaps {
    const RgbaImage* normal = nullptr;
    float normalScale = 1;
    const RgbaImage* occlusion = nullptr;
    int occlusionChannel = 0;  // 0 red (glTF, FBX), 1 green (Unity)
    float occlusionStrength = 1;
};
// Each texel of `base` times the light a fixed lamp up and to the left
// (-0.4, 0.5, 1) gives its normal, relative to a flat one (clamped to 0 to
// 1.6), times 1 + strength * (occlusion - 1). Maps of another size are
// resampled to the base's. A flat normal (x and y encoded 127 or 128, z 255)
// and full occlusion leave the texel exactly as it was; alpha is kept.
void bakeSurfaceDetail(RgbaImage& base, const DetailMaps& maps);
// The same on files: `base` read, the maps that decode baked into it (one
// that does not is a warning, left out), written to `out` as PNG. Throws
// std::runtime_error when `base` cannot be read or `out` written.
struct DetailFiles {
    std::filesystem::path normal, occlusion;  // empty for none
    float normalScale = 1;
    int occlusionChannel = 0;
    float occlusionStrength = 1;
};
void bakeSurfaceDetailFile(const std::filesystem::path& base, const DetailFiles& maps, const std::filesystem::path& out,
                           std::vector<std::string>& warnings);

// What bakeLostDetail did to one texture.
struct LostDetailStats {
    int baked = 0;       // texels given a shade (exactly 1 included)
    int covered = 0;     // texels at least one `lo` triangle covers
    int overlapped = 0;  // texels more than one `lo` triangle covers, left as they were
    int skippedUv = 0;   // triangles (of `hi` and `lo`) with a UV outside 0 to 1, left out
};
// The detail simplifying `hi` into `lo` removed, baked into `px`, the base
// colour texture of `submeshes` (indices into both meshes' submeshes, which
// simplifyToTriangles keeps in order). Both meshes' triangles are drawn into
// the texture at its size (texel centres, UV bottom-left origin, rows top-down
// as stb_image reads them, shared edges drawn once) for the interpolated
// normal of each; a texel is multiplied by 1 + (n_hi.L - n_lo.L) / L.z
// (clamped to 0 to 1.6), L the lamp up and to the left in front of the
// model: (0.4, 0.5, 1) in the .mesh's left-handed space, which is glTF's
// (-0.4, 0.5, 1) with X mirrored. Alpha is kept. A texel no `hi` triangle
// covers, or that not exactly one `lo` triangle covers (mirrored or tiled
// UVs), stays as it was; so does every texel when hi == lo. Triangles with a
// UV outside 0 to 1 are left out.
LostDetailStats bakeLostDetail(RgbaImage& px, const Mesh& hi, const Mesh& lo, const std::vector<int>& submeshes);
// Every corner's normal from the faces around its position within `degrees`
// of its own face's: what importing does with Normals::Recompute and to a
// simplified mesh.
void recomputeNormals(Mesh& mesh, float degrees);

// models/crate.fbx -> models/crate.fbx.import.json
std::filesystem::path importSettingsPath(const std::filesystem::path& model);
// The settings in `model`'s sidecar, the defaults when it has none. Throws
// std::runtime_error when the sidecar cannot be read.
ImportSettings loadImportSettings(const std::filesystem::path& model);
// Writes `model`'s sidecar. Throws std::runtime_error on failure.
void saveImportSettings(const std::filesystem::path& model, const ImportSettings& settings);

// File extensions importModel reads, lower case with the dot.
bool canImportModel(const std::filesystem::path& file);

// The name importModel writes `file`'s outputs under: models/<name>.mesh.
std::string importName(const std::filesystem::path& file);

// Reads `source` and writes models/<stem>.mesh and models/<stem>_<n>.png into
// `project`, replacing earlier imports of the same name. Every mesh node of
// the file (the default scene for glTF) is baked into one mesh in the file's
// root space, one submesh per material. The node tree is kept too: every node
// with a mesh also gets models/<stem>/<node>.mesh in its own space (names made
// file-safe and unique). glTF is read through cgltf, FBX and
// OBJ through ufbx, which first converts their axes and units to glTF's
// (right-handed, Y up, +Z front, metres). From there all three take the same
// route to the left-handed space a .mesh uses: X mirrored, winding reversed
// (again for each mirrored node). UVs end up with a bottom-left origin: glTF's
// are flipped, FBX and OBJ already have it. Base colour textures come from the
// glTF buffers or files; for FBX and OBJ from embedded data, the path in the
// file, then the bare name in the model's folder and its textures/tex/
// materials folders (one not found is a warning, not an error). Textures
// larger than 256 texels on a side are scaled down to fit.
// A skinned mesh (glTF skin, FBX skin deformer) keeps its skeleton: the joints
// of every skin in one list in tree order, each named after its node (made
// unique). Its geometry stays in root space whatever the skinned node's
// transform, and each vertex keeps its four heaviest influences, normalised.
// Every animation in the file becomes models/<stem>/<clip>.anim, channels by
// joint name in the .mesh space and keys in seconds: glTF keys as they are
// (cubic-spline ones by their values), FBX baked at 30 Hz.
// `settings` then apply in the .mesh space: an up axis other than File turns
// the model (Z up: -90 degrees about X, so +Z becomes +Y), scale multiplies
// every position and node translation, and Recompute replaces the normals.
// Then targetTriangles simplifies the meshes, with a warning when seams stop
// it above the target and when it replaced normals the file had.
// The outputs are named after `as` (see importName) when it is given, so a
// model read from where it was dropped is named after its copy in the project.
// With `progress`, importModel reports how far it is (`fraction`, 0 to 1, as
// it parses, converts each mesh and writes the outputs) and checks `cancel`
// between those steps (and every few thousand triangles of a large mesh):
// once it is set, importModel throws ImportCancelled, leaving what it wrote
// so far for the caller to remove. Either may be read or set from another
// thread while it runs. `checks` counts the points where it looked at
// `cancel` and went on: the work done after a cancel, in those units, is what
// a test can hold to a bound whatever the machine's load.
// A .blend is first exported as a GLB by Blender run in the background (see
// blend.hh), into a temporary folder, and that GLB is imported as above.
// Throws std::runtime_error with a message for the user.
struct ImportProgress {
    std::atomic<bool> cancel{false};
    std::atomic<float> fraction{0};
    std::atomic<unsigned> checks{0};
};
struct ImportCancelled : std::runtime_error {
    ImportCancelled() : std::runtime_error("the import was cancelled") {}
};
ImportedModel importModel(const std::filesystem::path& source, const std::filesystem::path& project,
                          const ImportSettings& settings = {}, const std::filesystem::path& as = {},
                          ImportProgress* progress = nullptr);

// Animations from another file for a skinned model's skeleton (Mixamo writes
// one animation per file, all on the same joint names).
struct ClipTarget {
    std::vector<std::string> joints;  // the skeleton's joint names, as in its .mesh
    std::string model;                // the skinned model's importName: the clips go under models/<model>/
    std::string folder;               // under that: clipFolder(source file)
    std::set<std::string> taken;      // clip names, lower case, the skin's other clips have
};
struct ImportedClips {
    std::vector<std::string> clips;      // every .anim written, project-relative, in file order
    std::vector<std::string> clipTakes;  // beside clips: each one's name in the file
    int droppedJoints = 0;               // animated nodes not in the skeleton, left out
    std::vector<std::string> warnings;
};
// "clips/<file name, extension too, made file-safe>": models/run.fbx -> clips/run_fbx.
std::string clipFolder(const std::filesystem::path& source);
// Reads only the animations of `source` and writes each one that moves a
// joint of `target` as models/<model>/<folder>/<clip>.anim, after clearing
// that folder's clips. The file's nodes named like a joint of the skeleton
// are its joints, so each key lands in the same joint space importModel puts
// the model's own clips in, and `settings` (the skinned model's: scale, up
// axis, units) apply as they do there; channels on other nodes are left out
// with one warning giving their count. The file name is the take made
// file-safe and unique in the file, as importModel names clips; the clip's
// name is settings.clipNames["<folder>/<file stem>"] when there is one, else
// the file name, numbered (_2, at most 24 characters) away from `taken`.
// Throws std::runtime_error when the file shares no joint name with the
// skeleton or has no animation that moves one.
ImportedClips importClips(const std::filesystem::path& source, const std::filesystem::path& project, const ImportSettings& settings,
                          const ClipTarget& target, ImportProgress* progress = nullptr);

}  // namespace splash
