################################################################################
# LivePi VideoGlitcher -- project-specific build configuration.
################################################################################
# Left at openFrameworks' own defaults; see
# $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/config.shared.mk
# for everything this file can override if a future need comes up (extra
# include paths, compiler flags, etc).

PROJECT_CFLAGS =
# ofxMidi's addon_config.mk lists `jack` in ADDON_PKG_CONFIG_LIBRARIES for
# linux/linux64, but not linuxaarch64/linuxarmv7l/linuxarmv6l -- RtMidi.cpp
# still compiles in JACK support there (-D__UNIX_JACK__ gets set whenever
# libjack-dev is present, independent of that addon_config.mk list), so the
# link fails with "undefined reference to jack_port_name" on any ARM Linux
# build with libjack-dev installed. Link it directly here instead of patching
# the addon (which lives outside this repo and gets re-cloned from scratch
# on every machine) -- harmless no-op on desktop, where it's already linked
# via the addon's own linux64 config.
PROJECT_LDFLAGS = -ljack
PROJECT_DEFINES =

# Nothing excluded from the default src/ scan -- every .cpp under src/ builds.
PROJECT_EXCLUSIONS =
