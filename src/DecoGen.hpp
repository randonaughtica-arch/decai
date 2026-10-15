#pragma once

#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <map>
#include <string>
#include <vector>

namespace deco {

// Everything the AI needs to know about the current selection.
struct SelectionContext {
    float originX = 0.f; // bottom-left of the selection bounding box (object centers)
    float originY = 0.f;
    float width = 0.f;
    float height = 0.f;
    size_t totalObjects = 0;
    std::vector<matjson::Value> objects;      // relative, possibly truncated
    std::map<int, int> idCounts;              // object ID -> how many are selected
    std::map<int, std::string> extraPalette;  // object ID -> description (from settings)

    bool isAllowedID(int id) const {
        return idCounts.contains(id) || extraPalette.contains(id);
    }
};

SelectionContext describeSelection(cocos2d::CCArray* objects, size_t maxObjects);

std::string buildSystemPrompt(size_t maxGenerated);
std::string buildUserPrompt(SelectionContext const& ctx, std::string const& request);

struct ObjectStringResult {
    std::string objectString;
    int created = 0;
    int skipped = 0;
};

// Turns the AI's JSON reply into a GD object string ready for createObjectsFromString.
geode::Result<ObjectStringResult> buildObjectString(
    std::string const& aiText, SelectionContext const& ctx, int editorLayer, size_t maxGenerated
);

}
