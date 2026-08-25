#pragma once

#include <string>

namespace livepi {

// Which box this is running on, decided at RUNTIME.
//
// The app ships as one arm64 bundle for every Pi -- config.make sets no
// -mcpu/-mtune (generic aarch64) and GLES 2 is fixed at build time
// (main.cpp + setup-pi.sh's TARGET_OPENGLES patch) -- so a Pi 4 and a Pi 5
// run the byte-identical binary. Nothing that differs between them may be
// decided by the preprocessor; it all has to branch off this.
//
// Policy (docs/distribution.md, "Hardware support"): the tier is ADVISORY.
// It picks how hard to push and what to warn about; it must never change
// what a show MEANS, so a show authored on a Pi 5 still plays on a Pi 4.
enum class HardwareTier {
    Desktop,    // not a Pi at all -- the dev machine
    Pi3,        // VideoCore IV / vc4
    Pi4,        // VideoCore VI / v3d, hardware H.264 decode
    Pi5,        // VideoCore VII / v3d 7.1, HEVC decode only (H.264 is software)
    PiUnknown,  // a Pi we don't have a tier for -- treat as the Pi 4 baseline
};

struct PlatformInfo {
    HardwareTier tier = HardwareTier::Desktop;
    // Stable slug: "desktop" | "pi3" | "pi4" | "pi5" | "pi". Safe to put in
    // JSON and compare against the backend's own detection.
    std::string tierName = "desktop";
    // Raw /proc/device-tree/model ("Raspberry Pi 5 Model B Rev 1.0"), or
    // "desktop" where that file doesn't exist.
    std::string model = "desktop";
    // True when LIVEPI_HARDWARE_TIER forced the tier instead of detection --
    // worth logging so a forced run is never mistaken for a real reading.
    bool forced = false;

    bool isPi() const { return tier != HardwareTier::Desktop; }
};

// Detected once, then cached: the model string can't change under a running
// process. LIVEPI_HARDWARE_TIER (desktop|pi3|pi4|pi5|pi) overrides the
// detection so a tier-specific path can be exercised off the hardware --
// the same escape hatch LIVEPI_THERMAL_PATH gives ThermalGovernor.
const PlatformInfo& platformInfo();

}  // namespace livepi
