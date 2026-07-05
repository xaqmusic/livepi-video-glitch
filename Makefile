################################################################################
# LivePi VideoGlitcher -- openFrameworks Makefile-based project
################################################################################
# Builds identically on the desktop (x86_64) and the Pi (aarch64); oF's own
# makefileCommon detects the architecture via `uname -m`. OF_ROOT defaults to
# a sibling directory (set up by scripts/setup-desktop.sh / scripts/setup-pi.sh)
# but can always be overridden: `make OF_ROOT=/path/to/openFrameworks`.
################################################################################

APPNAME = livepi-video-glitch

# config.make was never actually wired in here -- PROJECT_CFLAGS/LDFLAGS/
# DEFINES/EXCLUSIONS in that file have been silent no-ops since the original
# scaffold. This is oF's own canonical pattern (see e.g.
# examples/threads/threadedImageLoaderExample/Makefile).
ifneq ($(wildcard config.make),)
	include config.make
endif

OF_ROOT ?= ../openFrameworks

include $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk
