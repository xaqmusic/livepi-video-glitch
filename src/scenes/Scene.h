#pragma once

#include <map>
#include <string>
#include <vector>

// The show data model, mirroring docs/videosynth-backend.md's schema
// (schemaVersion 1). Everything cross-references by stable string ID --
// never array position, never raw file path -- so reordering layers or
// renaming clips can't silently repoint mappings at the wrong thing.
// IDs are opaque to the renderer; the backend generates them (the
// hand-written starter show uses readable ones like "scene-default").

// Values match composite_blend.frag's blendMode uniform -- keep in sync.
enum class BlendMode {
    Normal = 0,
    Add = 1,
    Screen = 2,
    Multiply = 3,
};

enum class LayerKind {
    Clip,
    Generator,
};

// One element of a scene's bottom-to-top layer stack: a looping clip or a
// procedural generator, with its own effect params, blended over what's
// below it. Generators parse and composite like any layer but render a
// black placeholder until the demoscene generator passes exist
// (docs/videosynth-effects.md).
struct Layer {
    std::string id;
    LayerKind kind = LayerKind::Clip;
    std::string source;        // clipId (kind==Clip) or generator name (kind==Generator)
    std::string resolvedPath;  // clip path resolved from clips/library.json at
                               // load time; empty means the clipId didn't
                               // resolve and the layer renders black
    BlendMode blendMode = BlendMode::Normal;
    float opacity = 1.0f;
    std::map<std::string, float> layerEffects;  // per-layer warp passes (future)
    std::map<std::string, float> params;        // generator-specific (future)
};

enum class TriggerType {
    CC,        // absolute: the resolved value IS the target's value
    Note,      // absolute like CC: velocity on press, 0 on release (momentary)
    AudioBand  // additive: summed onto the target's current value, then clamped
};

enum class AudioBandChoice {
    Low,
    Mid,
    High,
};

struct MappingTrigger {
    TriggerType type = TriggerType::CC;
    int number = 0;                                  // CC number or note number
    AudioBandChoice band = AudioBandChoice::Low;     // when type == AudioBand
};

// Where a trigger's value lands. layerId empty = scene scope (a postEffects
// param; the JSON's "postEffects." prefix is stripped at parse time so
// `param` matches the keys passes already read, e.g. "hsync.intensity").
struct MappingTarget {
    std::string layerId;
    std::string param;
    float min = 0.0f;
    float max = 1.0f;
};

// One knob (or audio band) fanning out to any number of targets. Lives on
// the Scene, not the device: switching scenes swaps the whole table
// atomically, which is what makes per-song remapping seamless.
struct Mapping {
    MappingTrigger trigger;
    std::vector<MappingTarget> targets;
};

// One entry in the setlist. `params` holds the scene's static post-effect
// baseline (JSON key "postEffects"), namespaced like "hsync.intensity" --
// a generic string->float map rather than one field per effect, with bools
// as 0.0f/1.0f (see docs/videosynth-effects.md "Architecture implications").
// Passes read it through LiveParams, which overlays the mapping resolver's
// live values on top of these baselines; getParam() is the static fallback.
// How a scene is ENTERED: the outgoing frame ramps into an effect peak
// (masking the decoder spin-up freeze), then the new scene ramps out of
// it. Styles map to post passes: fade dips to black, tear is the h-sync
// shred at full, shatter fractures to void and reassembles.
enum class TransitionStyle { None, Fade, Tear, Shatter };

struct TransitionSpec {
    TransitionStyle style = TransitionStyle::None;
    float duration = 0.8f;  // seconds, total out+in (hold excluded)
};

struct Scene {
    std::string id;
    std::string name;
    std::vector<Layer> layers;
    std::vector<Mapping> mappings;
    std::map<std::string, float> params;
    TransitionSpec transition;

    // Transitional (removed with the layered SceneRenderer in Phase A.2):
    // the first clip layer's resolved path, so the current single-clip
    // render path keeps working between the loader landing and the
    // compositor landing.
    std::string clipPath;

    float getParam(const std::string& key, float fallback) const {
        auto it = params.find(key);
        return it != params.end() ? it->second : fallback;
    }
};
