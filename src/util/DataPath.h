#pragma once

#include <string>

// Resolves a path to Pi-authored USER data -- shows/ and clips/ (and
// everything under them: library.json, the baked .pingpong boomerangs, the
// clip video files themselves). On the appliance the backend owns this under
// $LIVEPI_DATA_DIR, a separate WRITABLE /data partition kept apart from the
// read-only app tree (docs/architecture.md "Data model & read-only root").
// In desktop/dev, LIVEPI_DATA_DIR is unset and this falls back to oF's
// bin/data, which the dev backend and renderer share.
//
// APP-owned assets -- shaders and config/app.json -- are NOT user data: they
// ship and update *with* the app, so they stay on the app tree via oF's own
// ofToDataPath (shader.load()/ofLoadShader() resolve there internally). Only
// route shows/clips through here.
namespace livepi {
std::string userDataPath(const std::string& relative);
}
