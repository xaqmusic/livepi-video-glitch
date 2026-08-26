#pragma once

#include <map>
#include <string>

// Every param's valid [min, max], read from backend/effects_manifest.json --
// the SAME file the backend validates against and the web UI builds its
// controls from, so a param's range is asserted in exactly one place
// (CLAUDE.md: "adding a param is one line in effects_manifest.json").
//
// Why the renderer needs this at all: mapping contributions have to be kept
// inside the param's domain, and the renderer previously had no way to know
// what that domain was. It guessed 0..1, which was right when every param was
// 0..1 and silently broke every signed param added later -- an audio-mapped
// transform.x had its negative baseline stamped back to 0 on every frame. See
// docs/tech-debt.md.
//
// Scope matters: color.brightness/contrast/saturation exist as BOTH a
// per-layer and a post param, so a bare name is ambiguous even though those
// three currently happen to share a range. Layer scope covers layerEffects
// plus every generator's own params (which don't collide with each other).
//
// A missing or unparseable manifest is NOT fatal: lookups simply report
// "unknown" and callers fall back to not clamping, exactly as before.
class ParamDomains {
public:
    // Idempotent. Resolves the manifest relative to the oF data path
    // (bin/data/../../backend/effects_manifest.json).
    void load();

    // True when the domain is known; fills outMin/outMax only then.
    // layerScope=false means a scene-scope (post) param.
    bool get(bool layerScope, const std::string& param, float& outMin, float& outMax) const;

    size_t count() const { return layerDomains.size() + postDomains.size(); }

private:
    struct Range { float min = 0.0f; float max = 1.0f; };
    std::map<std::string, Range> layerDomains;
    std::map<std::string, Range> postDomains;
    bool loaded = false;
};
