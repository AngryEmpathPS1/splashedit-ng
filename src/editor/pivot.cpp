#include "editor/pivot.hh"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

#include "editor/meter_fixes.hh"
#include "editor/project.hh"
#include "import.hh"

namespace fs = std::filesystem;

namespace editor {

namespace {

fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

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
    // A prefab instance's content cannot be edited here, so one drawing the
    // mesh (or under an object that does) could not be kept in place.
    for (const ObjectPath& p : doc.flatPaths()) {
        const splash::Object* d = doc.object(p);
        if (!d->mesh || d->mesh->mesh != t.mesh) continue;
        bool locked = doc.inInstance(p);
        for (size_t i = 0; !locked && i < d->children.size(); ++i) {
            ObjectPath c = p;
            c.push_back(int(i));
            locked = doc.inInstance(c);
        }
        if (locked) {
            const std::optional<ObjectPath> at = doc.instanceRoot(p);
            const splash::Object* inst = at ? doc.object(*at) : nullptr;
            return refuse("the prefab " + (inst ? inst->prefab : std::string("instance")) + " in this scene draws " + t.mesh +
                          " too and cannot be kept in place from here: unpack it or set the pivot in the prefab");
        }
    }
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

    // Every object drawing the mesh moves its origin onto the new pivot, and
    // every child of one moves back by it (an object can be both).
    std::map<ObjectPath, std::pair<bool, bool>> moves;  // path: (draws the mesh, under one that does)
    for (const ObjectPath& p : doc.flatPaths()) {
        const splash::Object* o = doc.object(p);
        if (o->mesh && o->mesh->mesh == t->mesh) {
            moves[p].first = true;
            for (size_t i = 0; i < o->children.size(); ++i) {
                ObjectPath c = p;
                c.push_back(int(i));
                moves[c].second = true;
            }
        }
    }
    std::vector<ObjectPath> paths;
    for (const auto& [p, m] : moves) paths.push_back(p);
    auto compensate = [&] {
        doc.editEach(paths, [&](const ObjectPath& p, splash::Object& o) {
            const auto& [draws, under] = moves.at(p);
            splash::Transform& tr = o.transform;
            if (draws) tr.position = tr.position + splash::rotate(tr.rotation, splash::scale(tr.scale, delta));
            if (under) tr.position = tr.position - delta;
        });
    };
    return reimportWithSettings(doc, t->model, settings, compensate, error);
}

}  // namespace editor
