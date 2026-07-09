#pragma once

#include <string>

#include "ofVideoPlayer.h"

// Thin wrapper around ofVideoPlayer: loads a clip by path (relative to
// bin/data/), loops it, and exposes the current frame texture for
// ShaderChain. Kept separate from ShaderChain so a future frame-history need
// (beyond StutterBufferPass's own ring buffer) has somewhere obvious to live.
class ClipPlayer {
public:
    void load(const std::string& relativePath);
    void update();
    ofTexture& getTexture();
    bool isLoaded() const { return loaded; }
    float getPosition() const { return player.getPosition(); }
    float getDuration() const { return player.getDuration(); }

private:
    ofVideoPlayer player;
    bool loaded = false;
};
