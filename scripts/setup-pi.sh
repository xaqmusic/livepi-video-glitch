#!/usr/bin/env bash
# Run this ON THE RASPBERRY PI (Raspberry Pi OS Lite, 64-bit, Trixie --
# see docs/deploy.md for why). Installs openFrameworks (linuxaarch64),
# Pisound's driver/software support, and the ofxMidi addon. Safe to re-run.
set -euo pipefail

OF_VERSION="0.12.1"
OF_PLATFORM="linuxaarch64"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OF_ROOT="${OF_ROOT:-$REPO_DIR/../openFrameworks}"

if [ -d "$OF_ROOT" ]; then
    echo "openFrameworks already present at $OF_ROOT, skipping download."
else
    TARBALL="of_v${OF_VERSION}_${OF_PLATFORM}_release.tar.gz"
    URL="https://github.com/openframeworks/openFrameworks/releases/download/${OF_VERSION}/${TARBALL}"
    echo "Downloading openFrameworks ${OF_VERSION} for ${OF_PLATFORM}..."
    curl -fL "$URL" -o "/tmp/${TARBALL}"
    mkdir -p "$(dirname "$OF_ROOT")"
    tar -xzf "/tmp/${TARBALL}" -C "$(dirname "$OF_ROOT")"
    mv "$(dirname "$OF_ROOT")/of_v${OF_VERSION}_${OF_PLATFORM}_release" "$OF_ROOT"
    rm "/tmp/${TARBALL}"
fi

# oF 0.12.1's own install_dependencies.sh scripts each have exactly one line
# that's stale on Debian 13 (Trixie) -- patched here (idempotent, safe to
# re-run) rather than worked around by hand every time this is run on a
# freshly-flashed Pi:
#   - debian variant: apt-get installs `libgconf-2-4`, a GNOME config
#     library removed from Debian years ago (unrelated to anything oF
#     actually needs to build).
#   - ubuntu variant (the fallback below): its distro-version detection
#     assumes it's always running on Ubuntu and parses /etc/os-release's
#     VERSION_ID as an Ubuntu release number. Debian's VERSION_ID="13"
#     lands in the "< 14" branch meant for old Ubuntu releases, requiring a
#     nonexistent `g++-4.9` package instead of just using the system's
#     already-modern default g++.
sed -i 's/ libgconf-2-4//' "$OF_ROOT/scripts/linux/debian/install_dependencies.sh"
sed -i 's/CXX_VER=-4\.9/CXX_VER=/g' "$OF_ROOT/scripts/linux/ubuntu/install_dependencies.sh"

# oF 0.12.1's "linuxaarch64" release (a generic 64-bit-ARM target, not
# Pi-specific the way the old 32-bit "linuxarmv6l"/"linuxarmv7l" releases
# were) never defines TARGET_OPENGLES at all: ofConstants.h's own platform
# detection checks the macro `__ARM__` (uppercase), which no real compiler
# defines on 64-bit ARM (GCC defines `__aarch64__`) -- so it silently falls
# through to assuming desktop GL. This Pi's Mesa v3d driver happens to also
# expose a partial desktop-GL-3.1-compatibility profile, which is just
# enough to *link* against but not enough to compile our shaders'
# `#version 150` (that profile only goes up to GLSL 1.40) -- and even if it
# were enough, it's not the GLES 2.0 this project deliberately targets
# uniformly across every Pi generation (docs/architecture.md, "GL / GLES
# portability"). See docs/architecture.md's "Pi 4 bring-up" section for the
# full empirical trail (a real GL context, then a segfault, then this)
# that found the five patches below -- all idempotent, safe to re-run.
python3 - "$OF_ROOT" << 'PATCH_OF_FOR_GLES'
import sys
of_root = sys.argv[1]

def patch(path, find, replace, label):
    with open(path) as f:
        content = f.read()
    if replace in content:
        print(f"{label}: already patched, skipping")
        return
    count = content.count(find)
    if count != 1:
        sys.exit(f"{label}: expected exactly 1 match in {path}, found {count} -- "
                  f"oF source may have changed, needs re-diagnosing")
    with open(path, "w") as f:
        f.write(content.replace(find, replace))
    print(f"{label}: patched")

# 1. Force TARGET_OPENGLES for this platform (the actual root cause above).
patch(
    f"{of_root}/libs/openFrameworksCompiled/project/linuxaarch64/config.linuxaarch64.default.mk",
    "PLATFORM_LDFLAGS += -no-pie",
    "PLATFORM_DEFINES += TARGET_OPENGLES\nPLATFORM_LDFLAGS += -no-pie",
    "TARGET_OPENGLES define",
)

# 2. ofConstants.h's non-ARM Linux branch (what we actually take, since we
#    force TARGET_OPENGLES without also being TARGET_LINUX_ARM -- flipping
#    that on instead would fix this but silently drop TARGET_GLFW_WINDOW
#    and the audio defines this project needs) still unconditionally
#    #included <GL/glew.h> -- a desktop GL loader with no GLES awareness.
#    Route to the real GLES headers instead when TARGET_OPENGLES is set,
#    matching exactly what the TARGET_LINUX_ARM branch above it already
#    does (both the ES2 core headers and the ES1 legacy header, since some
#    oF code -- ofVbo.cpp's dead-at-runtime fixed-function fallback path --
#    still references ES1-only fixed-function symbols like
#    glTexCoordPointer/GL_COLOR_ARRAY that only GLES2 headers alone don't
#    declare).
patch(
    f"{of_root}/libs/openFrameworks/utils/ofConstants.h",
    "\t\t#define __LINUX_OSS__\n\t\t#include <GL/glew.h>\n\t#endif",
    "\t\t#define __LINUX_OSS__\n"
    "\t\t#ifdef TARGET_OPENGLES\n"
    "\t\t\t#include <GLES/gl.h>\n"
    "\t\t\t#include <GLES/glext.h>\n"
    "\t\t\t#include <GLES2/gl2.h>\n"
    "\t\t\t#include <GLES2/gl2ext.h>\n"
    "\t\t#else\n"
    "\t\t\t#include <GL/glew.h>\n"
    "\t\t#endif\n"
    "\t#endif",
    "ofConstants.h GLES headers",
)

# 3. ofAppRunner.h/ofAppBaseWindow.h/ofAppGLFWWindow.h each declare
#    EGLDisplay/EGLContext/EGLSurface-returning functions under
#    `#if defined(TARGET_LINUX) && defined(TARGET_OPENGLES)`, but none of
#    them #include <EGL/egl.h> themselves on Linux (unlike ofAppEGLWindow.h,
#    oF's *other* GLES window backend, which does) -- this combination
#    (GLFW windowing + GLES) seems to have never been exercised upstream.
#    ofAppRunner.h's declarations are free functions (file scope), so the
#    include can go directly above them. ofAppBaseWindow.h's/
#    ofAppGLFWWindow.h's are class member declarations -- `extern "C" {`
#    (egl.h's first line) can't open mid-class, so their includes have to
#    go at file scope near the top instead, *after* ofConstants.h itself is
#    included (that's the file that actually defines TARGET_LINUX/
#    TARGET_OPENGLES -- including it before that point means the guard
#    condition is never true yet).
patch(
    f"{of_root}/libs/openFrameworks/app/ofAppRunner.h",
    "#if defined(TARGET_LINUX) && defined(TARGET_OPENGLES)\nEGLDisplay ofGetEGLDisplay();",
    "#if defined(TARGET_LINUX) && defined(TARGET_OPENGLES)\n#include <EGL/egl.h>\n#endif\n"
    "#if defined(TARGET_LINUX) && defined(TARGET_OPENGLES)\nEGLDisplay ofGetEGLDisplay();",
    "ofAppRunner.h EGL include",
)
patch(
    f"{of_root}/libs/openFrameworks/app/ofAppBaseWindow.h",
    '#pragma once\n\n#include "ofWindowSettings.h"\n// MARK: Target\n#include "ofConstants.h"\n',
    '#pragma once\n\n#include "ofWindowSettings.h"\n// MARK: Target\n#include "ofConstants.h"\n\n'
    "#if defined(TARGET_LINUX) && defined(TARGET_OPENGLES)\n#include <EGL/egl.h>\n#endif\n",
    "ofAppBaseWindow.h EGL include",
)
# ofAppGLFWWindow.h includes ofAppBaseWindow.h near its own top, so it picks
# up the include above transitively -- nothing further needed there as long
# as it doesn't also declare its own EGL types before that include resolves
# (it doesn't; its own EGL method declarations come later in the file).

# 4. ofFbo.cpp's checkStatus() logs GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS,
#    which core GLES 2 doesn't define at all (desktop-GL-only enum) --
#    every other legacy/desktop-only case in this same switch is already
#    guarded (e.g. GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER right below it);
#    this one case was just missed.
patch(
    f"{of_root}/libs/openFrameworks/gl/ofFbo.cpp",
    "\t\tcase GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:\n"
    '\t\t\tofLogError("ofFbo") << "FRAMEBUFFER_INCOMPLETE_DIMENSIONS";\n'
    "\t\t\tbreak;\n",
    "#ifndef TARGET_OPENGLES\n"
    "\t\tcase GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:\n"
    '\t\t\tofLogError("ofFbo") << "FRAMEBUFFER_INCOMPLETE_DIMENSIONS";\n'
    "\t\t\tbreak;\n"
    "#endif\n",
    "ofFbo.cpp GLES guard",
)

# 5. ofPixels sizes its allocation by w*h*channels, but planar YUV formats
#    (what ClipPlayer negotiates from the hardware decoder via
#    OF_PIXELS_NATIVE) pack more than one byte per pixel per "channel" --
#    I420 is 12 bits/pixel across 3 planes. The Y plane fits exactly; both
#    chroma planes land past the end of the allocation. Everything that
#    *consumes* planar pixels (getTotalBytes, copyFrom, setFromPixels)
#    already uses the format-aware bytesFromPixelFormat(), so copies are
#    heap-overflow writes and plane texture uploads read unowned memory:
#    segfaults inside the GL driver on the Pi (crashed the kiosk 3s after
#    boot), silent garbage chroma on desktop where the stray reads happen
#    to hit mapped memory. Same fix in both the owning allocate() and the
#    non-owning setFromExternalPixels().
patch(
    f"{of_root}/libs/openFrameworks/graphics/ofPixels.cpp",
    "\tpixelsSize = w * h * getNumChannels();\n"
    "\n"
    "\t// we have some incongruence here, if we use PixelType\n"
    "\t// we are not able to use RGB565 format\n"
    "\tpixels = new PixelType[pixelsSize];",
    "\t// planar formats (I420/NV12/...) carry more data than w*h*channels --\n"
    "\t// size by the format-aware byte count like getTotalBytes() does, or the\n"
    "\t// chroma planes overflow the allocation\n"
    "\tpixelsSize = bytesFromPixelFormat(w,h,format) / sizeof(PixelType);\n"
    "\n"
    "\t// we have some incongruence here, if we use PixelType\n"
    "\t// we are not able to use RGB565 format\n"
    "\tpixels = new PixelType[pixelsSize];",
    "ofPixels.cpp planar allocate",
)
patch(
    f"{of_root}/libs/openFrameworks/graphics/ofPixels.cpp",
    "\tpixelsSize = w * h * getNumChannels();\n"
    "\n"
    "\tpixels = newPixels;",
    "\t// same planar-format sizing fix as in allocate() above\n"
    "\tpixelsSize = bytesFromPixelFormat(w,h,_pixelFormat) / sizeof(PixelType);\n"
    "\n"
    "\tpixels = newPixels;",
    "ofPixels.cpp planar external view",
)

# 6. V4L2 decoders pad plane heights to macroblock alignment (1080 -> 1088
#    rows), making the buffer bigger than the tight frame size while the
#    row stride still equals the width. ofGstUtils treats exactly that
#    combination as fatal and returns GST_FLOW_ERROR, killing the whole
#    pipeline ("Internal data stream error" from qtdemux) -- 720p clips
#    dodge it only because 720 is already a multiple of 16. And oF's
#    fallback strided-I420 copy assumes each plane starts right after the
#    previous plane's visible rows, so it would read chroma from the wrong
#    (unpadded) offsets anyway. Both fixed: the bail-out now only applies
#    to single-plane formats, and the I420 copy honors the per-plane
#    offsets AND strides GStreamer actually reports. 1080p verified at
#    1.0x real-time on the Pi 4 after this.
patch(
    f"{of_root}/libs/openFrameworks/video/ofGstUtils.cpp",
    "\t\tstride = v_info.stride[0];\n"
    "\n"
    "\t\tif(stride == (pixels.getWidth() * pixels.getBytesPerPixel())) {\n"
    "\t\t\tofLogError(\"ofGstVideoUtils\") << \"buffer_cb(): error on new buffer, buffer size: \" << size << \"!= init size: \" << pixels.getTotalBytes();\n"
    "\t\t\treturn GST_FLOW_ERROR;\n"
    "\t\t}",
    "\t\tstride = v_info.stride[0];\n"
    "\n"
    "\t\t// planar formats can be bigger than the tight frame size with the\n"
    "\t\t// row stride still equal to the width: v4l2 decoders pad the plane\n"
    "\t\t// HEIGHT to macroblock alignment (1080 -> 1088), which moves the\n"
    "\t\t// chroma plane offsets instead of widening rows. Only single-plane\n"
    "\t\t// formats make this combination inexplicable.\n"
    "\t\tif(pixels.getNumPlanes() <= 1 && stride == (pixels.getWidth() * pixels.getBytesPerPixel())) {\n"
    "\t\t\tofLogError(\"ofGstVideoUtils\") << \"buffer_cb(): error on new buffer, buffer size: \" << size << \"!= init size: \" << pixels.getTotalBytes();\n"
    "\t\t\treturn GST_FLOW_ERROR;\n"
    "\t\t}",
    "ofGstUtils.cpp planar size-mismatch bail",
)
patch(
    f"{of_root}/libs/openFrameworks/video/ofGstUtils.cpp",
    "\t\t\tif(pixels.getPixelFormat() == OF_PIXELS_I420){\n"
    "\t\t\t\tGstVideoInfo v_info = getVideoInfo(sample.get());\n"
    "\t\t\t\tstd::vector<size_t> strides{size_t(v_info.stride[0]),size_t(v_info.stride[1]),size_t(v_info.stride[2])};\n"
    "\t\t\t\tbackPixels.setFromAlignedPixels(mapinfo.data,pixels.getWidth(),pixels.getHeight(),pixels.getPixelFormat(),strides);\n"
    "\t\t\t} else {",
    "\t\t\tif(pixels.getPixelFormat() == OF_PIXELS_I420){\n"
    "\t\t\t\t// copy honoring BOTH per-plane strides and per-plane OFFSETS:\n"
    "\t\t\t\t// v4l2 decoders pad the plane height (1080 -> 1088), so the\n"
    "\t\t\t\t// chroma planes do NOT start where width*height would put them\n"
    "\t\t\t\t// (which is what setFromAlignedPixels assumes).\n"
    "\t\t\t\tGstVideoInfo v_info = getVideoInfo(sample.get());\n"
    "\t\t\t\tbackPixels.allocate(pixels.getWidth(),pixels.getHeight(),OF_PIXELS_I420);\n"
    "\t\t\t\tunsigned char * dst = backPixels.getData();\n"
    "\t\t\t\tfor(size_t p=0;p<3;p++){\n"
    "\t\t\t\t\tsize_t pw = p==0 ? size_t(pixels.getWidth()) : size_t(pixels.getWidth())/2;\n"
    "\t\t\t\t\tsize_t ph = p==0 ? size_t(pixels.getHeight()) : size_t(pixels.getHeight())/2;\n"
    "\t\t\t\t\tconst unsigned char * src = ((const unsigned char*)mapinfo.data) + GST_VIDEO_INFO_PLANE_OFFSET(&v_info,p);\n"
    "\t\t\t\t\tsize_t srcStride = GST_VIDEO_INFO_PLANE_STRIDE(&v_info,p);\n"
    "\t\t\t\t\tfor(size_t y=0;y<ph;y++){\n"
    "\t\t\t\t\t\tmemcpy(dst, src + y*srcStride, pw);\n"
    "\t\t\t\t\t\tdst += pw;\n"
    "\t\t\t\t\t}\n"
    "\t\t\t\t}\n"
    "\t\t\t} else {",
    "ofGstUtils.cpp I420 plane-offset copy",
)

# NOTE: OF_USE_GST_GL (routing GStreamer video decode straight into a GL
# texture) was tried as the first fix for slow video playback -- the V4L2
# hardware decoder keeps up fine, but oF's default demand for RGB at the
# appsink makes playbin auto-plug a software videoconvert that pegs an
# entire Pi 4 core and plays every clip at roughly 40% of real speed.
# USE_GST_GL got past three separate latent oF bugs in that
# never-before-exercised code path (a missing ofTexture.h include, missing
# EGL/iostream includes, unqualified cout/endl), but the resulting texture
# never allocates (blank screen, clip 0x0) -- a fourth, deeper bug not
# worth chasing. Reverted. The actual fix ships in the app instead:
# ClipPlayer requests OF_PIXELS_NATIVE (so the pipeline stays in the
# decoder's own YUV format, no CPU conversion) and draws the player through
# oF's built-in GPU YUV->RGB video shaders. Patch 5 above is what makes
# that path survive on the Pi.

print("All GLES patches applied. If oF is ever upgraded past 0.12.1, "
      "re-check these against docs/architecture.md's \"Pi 4 bring-up\" section.")
PATCH_OF_FOR_GLES

echo "Installing Linux dependencies (requires sudo)..."
sudo bash "$OF_ROOT/scripts/linux/debian/install_dependencies.sh" 2>/dev/null || \
    sudo bash "$OF_ROOT/scripts/linux/ubuntu/install_dependencies.sh"

echo "Cloning ofxMidi addon..."
ADDON_DIR="$OF_ROOT/addons/ofxMidi"
if [ -d "$ADDON_DIR" ]; then
    echo "ofxMidi already present, skipping clone."
else
    git clone --depth 1 https://github.com/danomatika/ofxMidi.git "$ADDON_DIR"
fi

echo "Installing backend dependencies (python venv + ffmpeg)..."
sudo apt-get install -y python3-venv ffmpeg
if [ ! -d "$REPO_DIR/backend/.venv" ]; then
    python3 -m venv "$REPO_DIR/backend/.venv"
fi
"$REPO_DIR/backend/.venv/bin/pip" install -q -r "$REPO_DIR/backend/requirements.txt"

echo "Installing Pisound driver/software support..."
# On a genuinely fresh Raspberry Pi OS flash, unattended-upgrades or the
# apt run above can still be holding the dpkg lock for a few seconds after
# it returns -- confirmed empirically via a from-scratch reinstall test,
# where the Pisound installer's own `apt-get install` intermittently hit
# "Could not get lock /var/lib/dpkg/lock-frontend". Wait for it to clear
# rather than let that apt-get fail partway through.
for i in $(seq 1 30); do
    sudo fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1 || break
    sleep 1
done
curl https://blokas.io/pisound/install.sh | sh

echo ""
echo "Next steps:"
echo "  sudo pisound-config     -- confirm MIDI/audio are recognized, wire up the button (docs/pisound-hardware-notes.md)"
echo "  make OF_ROOT=\"$OF_ROOT\"  -- build"
echo "See docs/deploy.md for the autostart/kiosk systemd setup."
