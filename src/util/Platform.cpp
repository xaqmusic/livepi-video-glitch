#include "util/Platform.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "ofLog.h"

namespace livepi {
namespace {

// The device-tree model node is a NUL-TERMINATED property, not a text file:
// read as a stream it comes back with a trailing '\0' (and sometimes a
// newline) that would poison every string compare and land raw in JSON.
std::string readModelFile() {
    std::ifstream f("/proc/device-tree/model");
    if (!f.good()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\0' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

HardwareTier tierFromModel(const std::string& model) {
    if (model.empty()) return HardwareTier::Desktop;
    if (!contains(model, "Raspberry Pi")) return HardwareTier::Desktop;
    // Compute Modules carry the generation after "Compute Module", so the
    // plain "Raspberry Pi <n>" test misses them -- check both spellings.
    if (contains(model, "Raspberry Pi 5") || contains(model, "Compute Module 5")) return HardwareTier::Pi5;
    if (contains(model, "Raspberry Pi 4") || contains(model, "Compute Module 4")) return HardwareTier::Pi4;
    if (contains(model, "Raspberry Pi 3") || contains(model, "Compute Module 3")) return HardwareTier::Pi3;
    return HardwareTier::PiUnknown;
}

std::string nameFromTier(HardwareTier tier) {
    switch (tier) {
        case HardwareTier::Pi3: return "pi3";
        case HardwareTier::Pi4: return "pi4";
        case HardwareTier::Pi5: return "pi5";
        case HardwareTier::PiUnknown: return "pi";
        case HardwareTier::Desktop: break;
    }
    return "desktop";
}

bool tierFromName(const std::string& name, HardwareTier& out) {
    if (name == "desktop") { out = HardwareTier::Desktop; return true; }
    if (name == "pi3") { out = HardwareTier::Pi3; return true; }
    if (name == "pi4") { out = HardwareTier::Pi4; return true; }
    if (name == "pi5") { out = HardwareTier::Pi5; return true; }
    if (name == "pi") { out = HardwareTier::PiUnknown; return true; }
    return false;
}

PlatformInfo detect() {
    PlatformInfo info;
    info.model = readModelFile();
    info.tier = tierFromModel(info.model);
    if (info.model.empty()) info.model = "desktop";

    if (const char* forced = std::getenv("LIVEPI_HARDWARE_TIER"); forced && *forced) {
        HardwareTier overridden = HardwareTier::Desktop;
        if (tierFromName(forced, overridden)) {
            info.tier = overridden;
            info.forced = true;
        } else {
            ofLogWarning("Platform") << "LIVEPI_HARDWARE_TIER=\"" << forced
                                     << "\" is not one of desktop|pi3|pi4|pi5|pi -- ignoring";
        }
    }
    info.tierName = nameFromTier(info.tier);
    return info;
}

}  // namespace

const PlatformInfo& platformInfo() {
    // Function-local static: detected on first use, thread-safe since C++11,
    // and never re-read (the model can't change under a running process).
    static const PlatformInfo info = detect();
    return info;
}

}  // namespace livepi
