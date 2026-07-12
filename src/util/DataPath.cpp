#include "util/DataPath.h"

#include <cstdlib>

#include "ofFileUtils.h"
#include "ofUtils.h"

namespace livepi {

std::string userDataPath(const std::string& relative) {
    // Cached once: LIVEPI_DATA_DIR is fixed for the process lifetime (set by
    // the systemd unit), and this sits on ShowLoader's per-frame poll path.
    static const std::string dataDir = [] {
        const char* env = std::getenv("LIVEPI_DATA_DIR");
        return std::string(env ? env : "");
    }();
    // Absolute either way, so oF's video/JSON loaders don't re-resolve it
    // against bin/data.
    if (!dataDir.empty()) return ofFilePath::join(dataDir, relative);
    return ofToDataPath(relative, true);
}

}  // namespace livepi
