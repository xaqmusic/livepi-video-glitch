#include "ClipPlayer.h"

#include "ofLog.h"

void ClipPlayer::load(const std::string& relativePath) {
    // ofGstVideoPlayer::load() reuses the existing GStreamer pipeline (just
    // swaps the "uri" property) when a video is already loaded, rather than
    // tearing it down -- fine when clips share the same resolution/framerate,
    // but switching to a clip with different specs left the pipeline running
    // at the previous clip's rate on the Pi's GStreamer backend (found
    // switching between a 30fps and a 24fps clip). Explicitly closing first
    // forces a fresh pipeline per clip and avoids any stale negotiated state.
    //
    // close() blocks waiting for EOS before tearing down the pipeline, but
    // only if the player is still "playing" -- our clips loop forever
    // (OF_LOOP_NORMAL) and never reach EOS on their own, so that wait always
    // ran out its full 5s timeout on every scene switch. Pausing first skips
    // that wait entirely (see ofGstUtils::close()'s bPaused check).
    player.setPaused(true);
    player.close();
    loaded = player.load(relativePath);
    if (loaded) {
        player.setLoopState(OF_LOOP_NORMAL);
        player.play();
    } else {
        ofLogError("ClipPlayer") << "Failed to load clip: " << relativePath;
    }
}

void ClipPlayer::update() {
    if (loaded) player.update();
}

ofTexture& ClipPlayer::getTexture() {
    return player.getTexture();
}
