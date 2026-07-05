#include "ClipPlayer.h"

#include "ofLog.h"

void ClipPlayer::load(const std::string& relativePath) {
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
