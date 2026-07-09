#include "ClipPlayer.h"

#include "ofGstVideoPlayer.h"
#include "ofLog.h"
#include "ofPixels.h"

void ClipPlayer::load(const std::string& relativePath) {
    if (!backendConfigured) {
        // Install our own GStreamer backend before the first load so
        // copy-pixels mode is on before the first frame ever arrives.
        // Without it, ofGstUtils' process_sample() points its pixels
        // straight into a GstBuffer it maps and then immediately unmaps --
        // which happens to keep working for the malloc-backed buffers
        // software decoders produce, but segfaults in the texture upload on
        // the Pi, whose v4l2h264dec hands over V4L2 mmap'd memory that
        // genuinely goes away on unmap. oF knows: setCopyPixels' own doc
        // comment calls it a fix for a v4l2 bug (GNOME #737427).
        //
        // A bool flag, NOT `if (!player.getPlayer())`: getPlayer() lazily
        // creates and installs a default backend instead of returning null,
        // so that test can never pass.
        auto gstPlayer = std::make_shared<ofGstVideoPlayer>();
        gstPlayer->getGstVideoUtils()->setCopyPixels(true);
        player.setPlayer(gstPlayer);
        backendConfigured = true;
    }
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
    firstFrameSeen = false;
    // OF_PIXELS_NATIVE accepts whatever pixel format the decoder already
    // produces (NV12/I420 for H.264) instead of oF's default of demanding
    // RGB at the appsink. Demanding RGB makes playbin auto-plug a software
    // videoconvert that converts every frame on the CPU -- on the Pi 4 that
    // single element pegs an entire core and drops playback to ~40% of
    // real-time speed. With the native format the conversion moves to the
    // GPU: drawing the player binds oF's per-format video shader
    // (ofGLProgrammableRenderer's NV12/I420 fragment shaders, which have
    // explicit GLES2 variants).
    player.setPixelFormat(OF_PIXELS_NATIVE);
    loaded = player.load(relativePath);
    if (loaded) {
        player.setLoopState(OF_LOOP_NORMAL);
        player.play();
        ofLogNotice("ClipPlayer") << "Loaded " << relativePath << " (" << getPixelFormatName() << ")";
    } else {
        ofLogError("ClipPlayer") << "Failed to load clip: " << relativePath;
    }
}

std::string ClipPlayer::getPixelFormatName() const {
    // player.getPixelFormat() reports the format the pipeline actually
    // negotiated (ofGstVideoPlayer::allocate() overwrites the requested
    // OF_PIXELS_NATIVE with the real one once caps are known) -- so this
    // names what the decoder is really handing over.
    switch (player.getPixelFormat()) {
        case OF_PIXELS_RGB: return "RGB";
        case OF_PIXELS_RGBA: return "RGBA";
        case OF_PIXELS_BGRA: return "BGRA";
        case OF_PIXELS_NV12: return "NV12";
        case OF_PIXELS_NV21: return "NV21";
        case OF_PIXELS_I420: return "I420";
        case OF_PIXELS_YV12: return "YV12";
        case OF_PIXELS_YUY2: return "YUY2";
        case OF_PIXELS_GRAY: return "GRAY";
        default: return "other(" + std::to_string(player.getPixelFormat()) + ")";
    }
}

void ClipPlayer::update() {
    if (!loaded) return;
    player.update();
    if (player.isFrameNew()) firstFrameSeen = true;
}

ofTexture& ClipPlayer::getTexture() {
    return player.getTexture();
}
