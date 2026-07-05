#pragma once

#include <string>
#include <vector>

#include "ofJson.h"
#include "scenes/Scene.h"

// Thin wrapper around oF's bundled JSON support (ofJson == nlohmann::json)
// for bin/data/config/app.json. loadFromFile() sets the base config;
// mergeFromFile() layers an optional, gitignored, machine-specific override
// (bin/data/config/app.local.json, copied from app.local.example.json) on
// top -- same pattern as .env.example -> .env at the repo root.
//
// Keys use dot notation ("midi.knobA_cc") regardless of nesting depth in the
// JSON file; getString/getFloat/getInt convert that to a JSON Pointer
// internally.
class Config {
public:
    bool loadFromFile(const std::string& relativePath);
    void mergeFromFile(const std::string& relativePath);

    std::string getString(const std::string& key, const std::string& fallback) const;
    float getFloat(const std::string& key, float fallback) const;
    int getInt(const std::string& key, int fallback) const;
    std::vector<Scene> getScenes() const;

private:
    ofJson json;
};
