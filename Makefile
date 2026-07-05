################################################################################
# LivePi VideoGlitcher -- openFrameworks Makefile-based project
################################################################################
# Builds identically on the desktop (x86_64) and the Pi (aarch64); oF's own
# makefileCommon detects the architecture via `uname -m`. OF_ROOT defaults to
# a sibling directory (set up by scripts/setup-desktop.sh / scripts/setup-pi.sh)
# but can always be overridden: `make OF_ROOT=/path/to/openFrameworks`.
################################################################################

APPNAME = livepi-video-glitch

OF_ROOT ?= ../openFrameworks

include $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk
