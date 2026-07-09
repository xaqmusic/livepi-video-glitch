#pragma once

#include <map>
#include <string>

// One entry in the setlist: which clip to play and the effect-preset
// parameters active while this scene is selected. The button (or its mock
// keyboard stand-in) advances through the list configured in
// bin/data/config/app.json's "scenes" array.
//
// Params are a generic, namespaced ("hsync.intensity", "chromatic.intensity")
// string->float map rather than one struct field per effect -- a fixed
// field per effect stopped scaling once more than three effects were on the
// table (see docs/videosynth-effects.md's "Architecture implications").
// Booleans live in here too (0.0f/1.0f) rather than a second map, so every
// pass reads its own params the same way regardless of type. Each pass
// already knows its own defaults, so a param simply absent from a scene's
// config just falls back to whatever getParam() is called with -- the same
// "scene-configured value, sensible fallback if unset" shape stutterEnabled
// already had before this generalized.
struct Scene {
    std::string name;
    std::string clipPath;  // relative to bin/data/
    std::map<std::string, float> params;

    float getParam(const std::string& key, float fallback) const {
        auto it = params.find(key);
        return it != params.end() ? it->second : fallback;
    }
};
