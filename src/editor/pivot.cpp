#include "editor/pivot.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

#include "editor/file_swaps.hh"
#include "editor/meter_fixes.hh"
#include "editor/project.hh"
#include "import.hh"

namespace fs = std::filesystem;

namespace editor {

namespace {

fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string utf8(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

// What keeps a mesh in place when its origin moves: each object drawing it
// moves its origin onto the new pivot, and each child of one moves back by
// it (an object can be both). Path: (draws the mesh, under one that does).
using Moves = std::map<ObjectPath, std::pair<bool, bool>>;

void move(splash::Transform& tr, bool draws, bool under, splash::Vec3 delta) {
    if (draws) tr.position = tr.position + splash::rotate(tr.rotation, splash::scale(tr.scale, delta));
    if (under) tr.position = tr.position - delta;
}

// The objects of a document that move: the ones its own file holds. A prefab
// instance's content is its prefab file's (rewritten on its own), while the
// instance itself, whose transform the document holds, moves like any other
// object when the prefab's root draws the mesh.
Moves documentMoves(const Document& doc, const std::string& mesh) {
    Moves moves;
    for (const ObjectPath& p : doc.flatPaths()) {
        if (doc.inInstance(p)) continue;
        const splash::Object* o = doc.object(p);
        if (!o->mesh || o->mesh->mesh != mesh) continue;
        moves[p].first = true;
        for (size_t i = 0; i < o->children.size(); ++i) {
            ObjectPath c = p;
            c.push_back(int(i));
            if (!doc.inInstance(c)) moves[c].second = true;
        }
    }
    return moves;
}

// The same for a scene or prefab file: `stored` as it is saved, `filled` the
// same with its instances filled in (to tell which instances draw the mesh).
// Returns how many objects moved.
int moveStored(std::vector<splash::Object>& stored, const std::vector<splash::Object>& filled, const std::string& mesh,
               splash::Vec3 delta, bool parentDraws) {
    int n = 0;
    for (size_t i = 0; i < stored.size() && i < filled.size(); ++i) {
        splash::Object& s = stored[i];
        const splash::Object& f = filled[i];
        const bool draws = f.mesh && f.mesh->mesh == mesh;
        if (draws || parentDraws) {
            move(s.transform, draws, parentDraws, delta);
            ++n;
        }
        if (s.prefab.empty()) n += moveStored(s.children, f.children, mesh, delta, draws);
    }
    return n;
}

bool isPrefabFile(const fs::path& f) {
    std::string e = utf8(f.extension());
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e == ".prefab";
}

// Every scene and prefab file of the project, as the asset panel lists them
// (anything whose name starts with '.' left out), in a fixed order.
std::vector<fs::path> sceneAndPrefabFiles(const fs::path& root) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return out;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const std::string name = utf8(it->path().filename());
        std::error_code sec;
        const bool dir = it->is_directory(sec);
        if (name.empty() || name[0] == '.') {
            if (dir) it.disable_recursion_pending();
            continue;
        }
        if (dir) continue;
        std::string e = utf8(it->path().extension());
        for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (e == ".scene" || e == ".prefab") out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

fs::path sameFile(const fs::path& p) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : c;
}

// A scene or prefab file with every object in it drawing the mesh moved:
// its bytes before and after, written by the core's own writer. Nothing when
// no object in it draws the mesh. Throws, naming the file, when it cannot be
// read or written.
std::optional<FileSwapsCommand::File> moveFile(const fs::path& root, const fs::path& file, const std::string& mesh,
                                               splash::Vec3 delta) {
    const std::string rel = utf8(file.lexically_relative(root));
    FileSwapsCommand::File f;
    f.path = file;
    std::string before;
    if (!readBytes(file, &before)) throw std::runtime_error("could not read " + rel);
    const bool prefab = isPrefabFile(file);
    // A Unity prefab copied into the project (YAML) cannot place our meshes.
    if (prefab && before.find_first_not_of(" \t\r\n\xef\xbb\xbf") != std::string::npos &&
        before.compare(before.find_first_not_of(" \t\r\n\xef\xbb\xbf"), 5, "%YAML") == 0)
        return std::nullopt;
    splash::Scene stored;
    splash::ExtraKeys prefabExtra;
    try {
        if (prefab) {
            splash::Prefab p = splash::readPrefabText(before, rel);
            prefabExtra = std::move(p.extra);
            stored.objects.push_back(std::move(p.root));
        } else {
            stored = splash::loadScene(file);
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("could not read " + rel + ": " + e.what());
    }
    splash::Scene filled = stored;
    splash::resolvePrefabs(filled, root);
    if (moveStored(stored.objects, filled.objects, mesh, delta, false) == 0) return std::nullopt;
    std::string after;
    try {
        if (prefab) {
            after = splash::writePrefabText({stored.objects.at(0), prefabExtra});
        } else {
            const fs::path tmp = file.parent_path() / pathOf("." + utf8(file.filename()) + ".pivot");
            std::error_code ec;
            try {
                splash::saveScene(stored, tmp);
            } catch (...) {
                fs::remove(tmp, ec);
                throw;
            }
            const bool read = readBytes(tmp, &after);
            fs::remove(tmp, ec);
            if (!read) throw std::runtime_error("the file it wrote could not be read back");
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("could not write " + rel + ": " + e.what());
    }
    if (after == before) return std::nullopt;
    f.before = std::move(before);
    f.after = std::move(after);
    return f;
}

// What setPivot works from: the mesh, its model and the mesh as it is now.
struct Target {
    std::string mesh;  // project-relative, as MeshComponent::mesh holds it
    fs::path model;    // absolute
    splash::Mesh data;
};

std::optional<Target> target(const Document& doc, const ObjectPath& path, std::string* why) {
    auto refuse = [&](const std::string& w) {
        if (why) *why = w;
        return std::nullopt;
    };
    const splash::Object* o = doc.object(path);
    if (!o) return refuse("no object is selected");
    if (!o->mesh || o->mesh->mesh.empty()) return refuse("the object draws no mesh");
    Target t;
    t.mesh = o->mesh->mesh;
    const fs::path root = doc.projectRoot();
    t.model = modelOfMesh(root, t.mesh);
    if (t.model.empty()) return refuse(t.mesh + " was not imported from a model, so it has no import settings to keep a pivot in");
    try {
        t.data = splash::loadMesh(root / pathOf(t.mesh));
    } catch (const std::exception& e) {
        return refuse(e.what());
    }
    if (t.data.skin) return refuse("a skinned mesh keeps the skeleton's origin: its bones and animations are in that space");
    if (t.data.positions.empty()) return refuse(t.mesh + " has no vertices");
    // In prefab mode the scenes (or prefabs) waiting behind this one are in
    // memory, maybe with changes not saved: neither they nor their files can
    // be kept in place from here.
    for (const Document* d = doc.outerDocument(); d; d = d->outerDocument())
        if (!documentMoves(*d, t.mesh).empty())
            return refuse(d->sceneStem() + ", open behind this prefab, draws " + t.mesh +
                          " too and cannot be kept in place from here: close the prefab first");
    return t;
}

}  // namespace

std::string pivotBlocker(const Document& doc, const ObjectPath& path) {
    std::string why;
    target(doc, path, &why);
    return why;
}

bool setPivot(Document& doc, const ObjectPath& path, PivotMode mode, splash::Vec3 custom, std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    std::string why;
    std::optional<Target> t = target(doc, path, &why);
    if (!t) return fail(why);
    const splash::Bounds b = t->data.bounds();
    splash::Vec3 delta;
    switch (mode) {
        case PivotMode::Centre: delta = b.center; break;
        case PivotMode::BottomCentre: delta = {b.center.x, b.min().y, b.center.z}; break;
        case PivotMode::Custom: delta = custom; break;
    }
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z)) return fail("the pivot is not a number");
    // Already there, give or take float rounding of an earlier move.
    const splash::Vec3 size = b.size();
    const float tol = 1e-5f * std::max({1.f, std::fabs(size.x), std::fabs(size.y), std::fabs(size.z)});
    if (std::fabs(delta.x) <= tol && std::fabs(delta.y) <= tol && std::fabs(delta.z) <= tol)
        return fail("the pivot is already there");

    splash::ImportSettings settings;
    try {
        settings = splash::loadImportSettings(t->model);
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    settings.pivots[t->mesh] = settings.pivots[t->mesh] + delta;

    // Every object drawing the mesh, in the document and in every other scene
    // and prefab file of the project, moves its origin onto the new pivot,
    // and its children back by it. The files are worked out first: one that
    // cannot be read or written stops it all, nothing changed.
    const fs::path root = doc.projectRoot();
    std::vector<fs::path> skip{sameFile(doc.scenePath())};
    std::vector<fs::path> behind;
    for (const Document* d = doc.outerDocument(); d; d = d->outerDocument()) behind.push_back(sameFile(d->scenePath()));
    auto swaps = std::make_unique<FileSwapsCommand>();
    bool prefabs = false;
    try {
        for (const fs::path& file : sceneAndPrefabFiles(root)) {
            const fs::path same = sameFile(file);
            if (same == skip[0]) continue;
            std::optional<FileSwapsCommand::File> f = moveFile(root, file, t->mesh, delta);
            if (!f) continue;
            if (std::find(behind.begin(), behind.end(), same) != behind.end())
                return fail(utf8(file.lexically_relative(root)) + ", open behind this prefab, draws " + t->mesh +
                            " too and cannot be kept in place from here: close the prefab first");
            prefabs |= isPrefabFile(file);
            swaps->files.push_back(std::move(*f));
        }
    } catch (const std::exception& e) {
        return fail(e.what());
    }

    const Moves moves = documentMoves(doc, t->mesh);
    std::vector<ObjectPath> paths;
    for (const auto& [p, m] : moves) paths.push_back(p);
    auto compensate = [&]() -> std::string {
        if (!paths.empty())
            doc.editEach(paths, [&](const ObjectPath& p, splash::Object& o) {
                const auto& [draws, under] = moves.at(p);
                move(o.transform, draws, under, delta);
            });
        if (swaps->files.empty()) return {};
        for (size_t i = 0; i < swaps->files.size(); ++i) {
            const FileSwapsCommand::File& f = swaps->files[i];
            if (writeBytes(f.path, *f.after)) continue;
            for (size_t j = 0; j < i; ++j) writeBytes(swaps->files[j].path, *swaps->files[j].before);
            return "could not write " + utf8(f.path.lexically_relative(root));
        }
        doc.execute(std::move(swaps));
        // Instances show their prefabs' files as they are now.
        if (prefabs) doc.resolvePrefabs();
        return {};
    };
    return reimportWithSettings(doc, t->model, settings, compensate, error);
}

}  // namespace editor
