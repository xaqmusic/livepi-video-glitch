#!/usr/bin/env bash
# Apply openFrameworks video-path patches that are PLATFORM-NEUTRAL --
# the subset of setup-pi.sh's oF patches that fix planar-YUV (NV12/I420)
# handling everywhere, not just GLES/Pi quirks. Run once per oF tree:
#
#   ./scripts/patch-of-video.sh [OF_ROOT]     (default ~/openFrameworks)
#
# Why the desktop needs this too: ClipPlayer negotiates OF_PIXELS_NATIVE
# (planar YUV straight from the decoder, GPU color conversion) on every
# platform. Without patch 5 the chroma planes land past the end of
# ofPixels' undersized allocation -- a segfaulting heap overflow on the
# Pi, silently corrupt/lost chroma (grayscale video) on the desktop.
# setup-pi.sh applies these same edits (among its GLES-only ones) on the
# Pi; keep the two in sync if oF is ever upgraded past 0.12.1.
set -euo pipefail

OF_ROOT="${1:-$HOME/openFrameworks}"

python3 - "$OF_ROOT" << 'PATCH_OF_VIDEO'
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

# ofPixels sizes its allocation by w*h*channels, but planar YUV formats
# pack more than one byte per pixel per "channel" -- I420/NV12 are 12
# bits/pixel across multiple planes. The Y plane fits exactly; the chroma
# planes land past the end of the allocation. Same fix in both the owning
# allocate() and the non-owning setFromExternalPixels().
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

# Decoders may pad plane heights to macroblock alignment (1080 -> 1088
# rows), making the buffer bigger than the tight frame size while the row
# stride still equals the width. ofGstUtils treats exactly that
# combination as fatal for ANY format; only single-plane formats make it
# inexplicable. And the strided-I420 fallback copy must honor the
# per-plane OFFSETS GStreamer reports, not assume tight packing.
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

print("Video-path patches applied.")
PATCH_OF_VIDEO
