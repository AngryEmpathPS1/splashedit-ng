#include "editor/asset_fields.hh"

#include <algorithm>

namespace editor {

namespace {

// The field's file on one object, or null when the object lacks the component.
const std::string* objectFile(const splash::Object& o, AssetField f, int index) {
    switch (f) {
        case AssetField::Model: return o.mesh ? &o.mesh->mesh : nullptr;
        case AssetField::Texture: {
            const size_t i = size_t(std::max(index, 0));
            return o.mesh && i < o.mesh->materials.size() ? &o.mesh->materials[i].texture : nullptr;
        }
        case AssetField::Lua: return o.script ? &o.script->lua : nullptr;
        case AssetField::TriggerLua: return o.trigger ? &o.trigger->lua : nullptr;
        case AssetField::Clip: return o.audio ? &o.audio->clip : nullptr;
        case AssetField::SkinClips: return o.skin && !o.skin->clips.empty() ? &o.skin->clips.front() : nullptr;
        case AssetField::SceneLua:
        case AssetField::MemCardIcon: break;
    }
    return nullptr;
}

void clearObject(splash::Object& o, AssetField f, int index) {
    switch (f) {
        case AssetField::Texture:
            if (o.mesh && size_t(std::max(index, 0)) < o.mesh->materials.size())
                o.mesh->materials[size_t(std::max(index, 0))].texture.clear();
            break;
        case AssetField::Lua:
            if (o.script) o.script->lua.clear();
            break;
        case AssetField::TriggerLua:
            if (o.trigger) o.trigger->lua.clear();
            break;
        case AssetField::Clip:
            if (o.audio) o.audio->clip.clear();
            break;
        case AssetField::Model:
        case AssetField::SkinClips:
        case AssetField::SceneLua:
        case AssetField::MemCardIcon: break;
    }
}

}  // namespace

std::string assetFieldFile(const Document& doc, AssetField f, const ObjectPath& target, int index) {
    const splash::SceneSettings& s = doc.scene().settings;
    if (f == AssetField::SceneLua) return s.script;
    if (f == AssetField::MemCardIcon)
        return index >= 0 && size_t(index) < s.memCard.icons.size() ? s.memCard.icons[size_t(index)] : std::string();
    const splash::Object* o = doc.object(target);
    const std::string* file = o ? objectFile(*o, f, index) : nullptr;
    return file ? *file : std::string();
}

bool assetFieldClearable(AssetField f) { return f != AssetField::Model && f != AssetField::SkinClips; }

bool clearAssetField(Document& doc, AssetField f, const ObjectPath& target, int index, const std::vector<ObjectPath>& many) {
    if (!assetFieldClearable(f)) return false;
    if (f == AssetField::SceneLua) {
        if (doc.scene().settings.script.empty()) return false;
        doc.editSettings([](splash::SceneSettings& s) { s.script.clear(); });
        doc.endMerge();
        return true;
    }
    if (f == AssetField::MemCardIcon) {
        if (index < 0 || size_t(index) >= doc.scene().settings.memCard.icons.size()) return false;
        doc.editSettings([index](splash::SceneSettings& s) { s.memCard.icons.erase(s.memCard.icons.begin() + index); });
        doc.endMerge();
        return true;
    }
    std::vector<ObjectPath> paths = many.empty() ? std::vector<ObjectPath>{target} : many;
    // Only the objects that show a file: no step when none does.
    paths.erase(std::remove_if(paths.begin(), paths.end(),
                               [&](const ObjectPath& p) {
                                   const splash::Object* o = doc.object(p);
                                   const std::string* file = o ? objectFile(*o, f, index) : nullptr;
                                   return !file || file->empty();
                               }),
                paths.end());
    if (paths.empty()) return false;
    const auto clear = [&](splash::Object& o) { clearObject(o, f, index); };
    const bool done = paths.size() == 1 ? doc.edit(paths.front(), clear) : doc.editMany(paths, clear) > 0;
    doc.endMerge();
    return done;
}

}  // namespace editor
