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
// sidecar to keep the pivot in), a skinned mesh, or, in prefab mode, a scene
// (or prefab) waiting behind the open prefab that draws the mesh too (it is
// held in memory, so neither it nor its file can be kept in place). Reads
// the mesh file. Empty when it can.
std::string pivotBlocker(const Document& doc, const ObjectPath& path);

// Moves the origin of the mesh the object at `path` draws to the point
// `mode` names (`custom` with Custom) as one undo step, nothing moving:
// every object drawing that mesh moves by the same amount in its own space
// and its children back by it, in the document and in every other scene and
// prefab file of the project (the document's own file is left to its next
// save). A prefab instance moves where it is placed when its prefab's root
// draws the mesh; what it shows comes from its prefab file, moved there.
// Only files holding an object that moves are rewritten (by the core
// writer); undo puts them back byte for byte. Then the point goes into the
// model's sidecar and the model is imported again (see reimportWithSettings).
// False with `error` set, nothing changed, when pivotBlocker says why, when
// the pivot is already there (the change is zero), when a scene or prefab
// file cannot be read or written (the error names it), when one waiting
// behind the open prefab would have to move, or when the import fails.
bool setPivot(Document& doc, const ObjectPath& path, PivotMode mode, splash::Vec3 custom, std::string* error);

}  // namespace editor
