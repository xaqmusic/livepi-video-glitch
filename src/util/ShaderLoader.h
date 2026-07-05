#pragma once

#include <string>

#include "ofShader.h"

// Loads a vertex+fragment shader pair, prepending the correct #version (and,
// on GLES, a precision qualifier) for whichever platform we're compiled for,
// and textually resolving any `#pragma include "file.glslinc"` line against
// a shared helper file. Shader body files under bin/data/shaders/ deliberately
// contain no #version line of their own -- see docs/shader-authoring.md for
// the exact GLSL subset this depends on, chosen so the same body compiles
// under both desktop GLSL 150 and GLSL ES 300 (what the Pi 4/5's Mesa V3D
// driver speaks -- see docs/architecture.md).
namespace ShaderLoader {
bool load(ofShader& shader, const std::string& vertPath, const std::string& fragPath);
}
