#pragma once

#include <string>

#include "ofVideoPlayer.h"

// Thin wrapper around ofVideoPlayer: loads a clip by path (relative to
// bin/data/), loops it, and exposes the current frame for ShaderChain.
// Kept separate from ShaderChain so a future frame-history need (beyond
// StutterBufferPass's own ring buffer) has somewhere obvious to live.
class ClipPlayer {
public:
    void load(const std::string& relativePath);
    void update();
    ofTexture& getTexture();
    // ShaderChain seeds its first FBO by *drawing* this rather than sampling
    // getTexture(): drawing routes through the renderer, which is what binds
    // oF's built-in YUV->RGB conversion shader when the decoder hands us
    // planar frames (see the OF_PIXELS_NATIVE comment in load()).
    // getTexture() on a planar frame would return only the Y plane.
    const ofBaseDraws& getDrawable() const { return player; }
    std::string getPixelFormatName() const;
    bool isLoaded() const { return loaded; }
    float getPosition() const { return player.getPosition(); }
    float getDuration() const { return player.getDuration(); }

private:
    ofVideoPlayer player;
    bool loaded = false;
    bool backendConfigured = false;
};
