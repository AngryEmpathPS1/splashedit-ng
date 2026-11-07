// The inspector's file fields (a Mesh's Model and Texture, a Script's file,
// an Audio clip, ...): the file each shows, and Clear, which sets one back to
// None as one undo step.
#pragma once

#include <string>
#include <vector>

#include "editor/document.hh"

namespace editor {

enum class AssetField { Model, Texture, Lua, TriggerLua, Clip, SkinClips, SceneLua, MemCardIcon };

// The project-relative file the field at `target` shows, empty for None.
// `index`: the material for Texture (-1 is the first), the frame for
// MemCardIcon. SkinClips shows a list: its first clip.
std::string assetFieldFile(const Document& doc, AssetField f, const ObjectPath& target, int index = -1);

// Whether Clear is offered: not for a Mesh's Model (a mesh with no model
// stops the export) nor a skin's Clips (a skinned mesh with no clip is never
// drawn, and the export refuses it).
bool assetFieldClearable(AssetField f);

// Sets the field back to None as one undo step; false, and no step, when it
// already is or is not clearable. Texture clears material `index`'s image
// (the material stays, a flat colour); MemCardIcon takes frame `index` out,
// since a frame without an image cannot export. With `many` (Lua or Clip, the
// multi-object inspector) each of those objects is cleared in the one step.
bool clearAssetField(Document& doc, AssetField f, const ObjectPath& target, int index = -1,
                     const std::vector<ObjectPath>& many = {});

}  // namespace editor
