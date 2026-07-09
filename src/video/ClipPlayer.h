#pragma once

#include <string>

#include "ofVideoPlayer.h"

// Thin wrapper around ofVideoPlayer: loads a clip by path (relative to
// bin/data/), loops it, and exposes the current frame for ShaderChain.
// Kept separate from ShaderChain so a future frame-history need (beyond
// StutterBufferPass's own ring buffer) has somewhere obvious to live.
class ClipPlayer {
public:
    // Pausing before the implicit close() in ofVideoPlayer's destructor
    // skips ofGstUtils' wait-for-EOS branch -- our clips loop forever and
    // never reach EOS, so destroying a still-playing player would otherwise
    // burn a full 5s timeout (same reason load() pauses before closing).
    // Matters now that the layered renderer destroys one player per clip
    // layer on every scene switch.
    ~ClipPlayer() { player.setPaused(true); }

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
    // True once at least one real decoded frame has arrived since load() --
    // the load-time texture is allocated immediately but holds zeroed (black)
    // planes, so isLoaded() alone can't tell "showing content" from "showing
    // black". SceneRenderer holds the previous scene's output on screen until
    // every layer reports this (freeze-frame instead of a black flash).
    bool hasReceivedFrame() const { return firstFrameSeen; }
    float getPosition() const { return player.getPosition(); }
    float getDuration() const { return player.getDuration(); }

private:
    ofVideoPlayer player;
    bool loaded = false;
    bool firstFrameSeen = false;
    bool backendConfigured = false;
};
