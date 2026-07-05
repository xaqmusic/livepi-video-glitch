#pragma once

#include <string>

// One entry in the setlist: which clip to play and the effect-preset
// intensities active while this scene is selected. The button (or its mock
// keyboard stand-in) advances through the list configured in
// bin/data/config/app.json's "scenes" array.
struct Scene {
    std::string name;
    std::string clipPath;  // relative to bin/data/
    float hSyncIntensity = 0.5f;
    float chromaticIntensity = 0.5f;
    bool stutterEnabled = true;
};
