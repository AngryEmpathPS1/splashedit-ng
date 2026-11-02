// Set pivot: a new origin for the mesh an object draws, an imported model's
// baked or node mesh, without anything in the scene moving. The model's
// sidecar keeps it (ImportSettings::pivots), so importing again keeps it too.
// No UI.
#pragma once

#include <string>

#include "editor/document.hh"
#include "scene.hh"

namespace editor {

enum class PivotMode {
    Centre,        // the mesh's bounds centre
    BottomCentre,  // the bounds centre in x and z, their lowest y (Y up)
    Custom,        // a point given in the mesh's own space as it is now
};

// Why setPivot would refuse the object at `path` whatever the mode, for the
// inspector to say: no object, no mesh, a mesh no imported model wrote (so no
// sidecar to keep the pivot in), a skinned mesh, or a prefab instance in the
// scene drawing the mesh too (its content is read-only here, so it would
// move). Reads the mesh file. Empty when it can.
std::string pivotBlocker(const Document& doc, const ObjectPath& path);

// Moves the origin of the mesh the object at `path` draws to the point
// `mode` names (`custom` with Custom) as one undo step: every object in the
// scene drawing that mesh moves by the same amount in its own space and its
// children back by it, so nothing moves; then the point goes into the
// model's sidecar and the model is imported again (see reimportWithSettings).
// False with `error` set, nothing changed, when pivotBlocker says why, when
// the pivot is already there (the change is zero), or the import fails.
bool setPivot(Document& doc, const ObjectPath& path, PivotMode mode, splash::Vec3 custom, std::string* error);

}  // namespace editor
