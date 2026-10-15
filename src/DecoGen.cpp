#include "DecoGen.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

using namespace geode::prelude;

namespace deco {

namespace {

float round1(float v) {
    return std::round(v * 10.f) / 10.f;
}

// GD's z layer values, as named in the editor.
std::string zLayerName(int z) {
    switch (z) {
        case -5: return "B5";
        case -3: return "B4";
        case -1: return "B3";
        case 1: return "B2";
        case 3: return "B1";
        case 5: return "T1";
        case 7: return "T2";
        case 9: return "T3";
        case 11: return "T4";
        default: return "";
    }
}

std::optional<int> zLayerValue(std::string const& name) {
    static std::map<std::string, int> const values = {
        {"B5", -5}, {"B4", -3}, {"B3", -1}, {"B2", 1}, {"B1", 3},
        {"T1", 5}, {"T2", 7}, {"T3", 9}, {"T4", 11},
    };
    if (auto it = values.find(name); it != values.end()) return it->second;
    return std::nullopt;
}

int effectiveColor(GJSpriteColor* color) {
    if (!color) return 0;
    return color->m_colorID != 0 ? color->m_colorID : color->m_defaultColorID;
}

std::map<int, std::string> parseExtraPalette(std::string const& setting) {
    std::map<int, std::string> palette;
    for (auto& entry : utils::string::split(setting, ",")) {
        auto parts = utils::string::split(entry, ":");
        if (parts.empty()) continue;
        auto id = utils::numFromString<int>(utils::string::trim(parts[0]));
        if (id.isErr() || id.unwrap() <= 0) continue;
        palette[id.unwrap()] = parts.size() > 1 ? utils::string::trim(parts[1]) : "";
    }
    return palette;
}

double getNumber(matjson::Value const& obj, std::string_view key, double fallback) {
    if (!obj.contains(key)) return fallback;
    auto value = obj[key].asDouble();
    return value.isOk() ? value.unwrap() : fallback;
}

bool getBool(matjson::Value const& obj, std::string_view key) {
    if (!obj.contains(key)) return false;
    auto value = obj[key].asBool();
    return value.isOk() && value.unwrap();
}

std::string getString(matjson::Value const& obj, std::string_view key) {
    if (!obj.contains(key)) return "";
    return obj[key].asString().unwrapOr("");
}

}

SelectionContext describeSelection(CCArray* objects, size_t maxObjects) {
    SelectionContext ctx;
    ctx.extraPalette = parseExtraPalette(Mod::get()->getSettingValue<std::string>("extra-palette"));

    float minX = std::numeric_limits<float>::max(), minY = minX;
    float maxX = std::numeric_limits<float>::lowest(), maxY = maxX;

    for (auto obj : CCArrayExt<GameObject*>(objects)) {
        auto pos = obj->getPosition();
        minX = std::min(minX, pos.x);
        minY = std::min(minY, pos.y);
        maxX = std::max(maxX, pos.x);
        maxY = std::max(maxY, pos.y);
        ctx.idCounts[obj->m_objectID]++;
        ctx.totalObjects++;
    }
    if (ctx.totalObjects == 0) return ctx;

    ctx.originX = minX;
    ctx.originY = minY;
    ctx.width = maxX - minX;
    ctx.height = maxY - minY;

    for (auto obj : CCArrayExt<GameObject*>(objects)) {
        if (ctx.objects.size() >= maxObjects) break;
        auto pos = obj->getPosition();

        auto desc = matjson::makeObject({
            {"id", obj->m_objectID},
            {"x", round1(pos.x - minX)},
            {"y", round1(pos.y - minY)},
        });
        if (auto rot = round1(obj->getRotation()); rot != 0.f) desc["rot"] = rot;
        if (std::abs(obj->m_scaleX - 1.f) > 0.01f) desc["scaleX"] = round1(obj->m_scaleX);
        if (std::abs(obj->m_scaleY - 1.f) > 0.01f) desc["scaleY"] = round1(obj->m_scaleY);
        if (obj->isFlipX()) desc["flipX"] = true;
        if (obj->isFlipY()) desc["flipY"] = true;
        if (auto c = effectiveColor(obj->m_baseColor)) desc["color"] = c;
        if (auto c = effectiveColor(obj->m_detailColor)) desc["detail"] = c;
        if (auto z = zLayerName(static_cast<int>(obj->m_zLayer)); !z.empty()) desc["zLayer"] = z;
        if (obj->m_zOrder != 0) desc["zOrder"] = obj->m_zOrder;

        ctx.objects.push_back(desc);
    }
    return ctx;
}

std::string buildSystemPrompt(size_t maxGenerated) {
    return fmt::format(R"(You are a Geometry Dash 2.2 level decorator working inside the level editor. The user has selected a group of objects and wants you to add decoration to it.

Coordinate system:
- 30 units = 1 grid block. x grows to the right, y grows upward.
- Every position you receive and return is an object's CENTER, relative to (0,0) = the bottom-left of the selection's bounding box.
- A 1x1 block is 30x30 units, so adjacent blocks are 30 units apart. A half-block offset is 15 units.

Rules:
- Use ONLY object IDs from the palette in the user's message. Never invent IDs; any other ID is discarded.
- Study how the selection is built (structure shape, which objects look like deco, colors, layers) and decorate in a matching style: outlines, edge details, patterns, fills, scattered accents, whatever the request asks for.
- Keep decoration tidy and aligned to the grid or to the existing structure unless the request asks for something organic.
- Don't stack an object exactly on top of an existing object with the same ID.
- color / detail are color channel IDs. 1-999 are the level's custom channels. Special channels: 1000 BG, 1001 Ground 1, 1009 Ground 2, 1002 Line, 1003 3DL, 1004 Obj, 1005 P1, 1006 P2, 1007 LBG, 1010 Black, 1011 White, 1012 Lighter. Prefer channels already used in the selection. Omit to keep the object's default color.
- zLayer is one of B5, B4, B3, B2, B1 (behind the player, B5 furthest back) or T1, T2, T3, T4 (in front). Omit for the object's default.
- Create at most {} objects.

Reply with ONLY a JSON object, no prose and no code fences, in this shape:
{{"objects":[{{"id":1,"x":0,"y":0,"rot":0,"scale":1,"flipX":false,"flipY":false,"color":1,"detail":2,"zLayer":"B1","zOrder":0}}]}}
Only id, x and y are required; leave out fields you don't need.)", maxGenerated);
}

std::string buildUserPrompt(SelectionContext const& ctx, std::string const& request) {
    std::string palette;
    for (auto& [id, count] : ctx.idCounts) {
        palette += fmt::format("- {} (appears {}x in the selection)\n", id, count);
    }
    for (auto& [id, desc] : ctx.extraPalette) {
        if (ctx.idCounts.contains(id)) continue;
        palette += fmt::format("- {}{}\n", id, desc.empty() ? "" : ": " + desc);
    }

    auto objects = matjson::Value(ctx.objects).dump(matjson::NO_INDENTATION);
    auto truncated = ctx.objects.size() < ctx.totalObjects
        ? fmt::format(" (showing the first {})", ctx.objects.size())
        : std::string();

    return fmt::format(
        "Request: {}\n\n"
        "Selection bounding box: {:.0f} x {:.0f} units (about {:.1f} x {:.1f} blocks).\n\n"
        "Palette (allowed object IDs):\n{}\n"
        "Selected objects: {}{}:\n{}",
        request,
        ctx.width, ctx.height, ctx.width / 30.f, ctx.height / 30.f,
        palette,
        ctx.totalObjects, truncated, objects
    );
}

Result<ObjectStringResult> buildObjectString(
    std::string const& aiText, SelectionContext const& ctx, int editorLayer, size_t maxGenerated
) {
    // Tolerate stray prose or code fences around the JSON.
    auto start = aiText.find('{');
    auto end = aiText.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end < start) {
        return Err("The AI didn't return any JSON.");
    }

    auto parsed = matjson::parse(aiText.substr(start, end - start + 1));
    if (parsed.isErr()) {
        return Err("The AI returned invalid JSON.");
    }
    auto root = parsed.unwrap();
    if (!root.contains("objects") || !root["objects"].isArray()) {
        return Err("The AI's reply has no \"objects\" list.");
    }

    // Keep generated deco near the selection even if the AI goes wild.
    float margin = std::max({ctx.width, ctx.height, 300.f});

    ObjectStringResult result;
    for (auto const& obj : root["objects"].asArray().unwrap()) {
        if (static_cast<size_t>(result.created) >= maxGenerated) {
            result.skipped++;
            continue;
        }
        if (!obj.isObject()) {
            result.skipped++;
            continue;
        }

        int id = static_cast<int>(getNumber(obj, "id", 0));
        double x = getNumber(obj, "x", std::nan(""));
        double y = getNumber(obj, "y", std::nan(""));
        if (!ctx.isAllowedID(id) || std::isnan(x) || std::isnan(y)
            || x < -margin || x > ctx.width + margin || y < -margin || y > ctx.height + margin) {
            result.skipped++;
            continue;
        }

        double scale = std::clamp(getNumber(obj, "scale", 1.0), 0.05, 4.0);
        double rot = std::fmod(getNumber(obj, "rot", 0.0), 360.0);
        int color = static_cast<int>(getNumber(obj, "color", 0));
        int detail = static_cast<int>(getNumber(obj, "detail", 0));
        int zOrder = std::clamp(static_cast<int>(getNumber(obj, "zOrder", 0)), -100, 100);
        auto zLayer = zLayerValue(getString(obj, "zLayer"));

        // GD object string keys: 1 id, 2 x, 3 y, 4 flipX, 5 flipY, 6 rotation, 20 editor layer,
        // 21 main color, 22 detail color, 24 z layer, 25 z order, 32 scale, 128/129 scale x/y.
        auto str = fmt::format("1,{},2,{:.2f},3,{:.2f}", id, ctx.originX + x, ctx.originY + y);
        if (getBool(obj, "flipX")) str += ",4,1";
        if (getBool(obj, "flipY")) str += ",5,1";
        if (rot != 0.0) str += fmt::format(",6,{:.2f}", rot);
        if (editorLayer > 0) str += fmt::format(",20,{}", editorLayer);
        if (color > 0 && color <= 1012) str += fmt::format(",21,{}", color);
        if (detail > 0 && detail <= 1012) str += fmt::format(",22,{}", detail);
        if (zLayer) str += fmt::format(",24,{}", *zLayer);
        if (zOrder != 0) str += fmt::format(",25,{}", zOrder);
        if (scale != 1.0) str += fmt::format(",32,{0:.2f},128,{0:.2f},129,{0:.2f}", scale);

        result.objectString += str + ";";
        result.created++;
    }

    if (result.created == 0) {
        return Err("The AI didn't produce any usable objects. Try a more specific prompt, or include deco objects in your selection.");
    }
    return Ok(result);
}

}
