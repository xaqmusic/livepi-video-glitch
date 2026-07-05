#include "ControlSource.h"

#include "MidiControlSource.h"
#include "MockControlSource.h"
#include "PisoundControlSource.h"
#include "util/Config.h"

std::unique_ptr<ControlSource> createControlSource(const Config& config) {
    std::string kind = config.getString("control_source", "mock");
    if (kind == "pisound") {
        return std::make_unique<PisoundControlSource>();
    }
    if (kind == "midi") {
        return std::make_unique<MidiControlSource>();
    }
    return std::make_unique<MockControlSource>();
}
