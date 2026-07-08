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

// oF's own per-bind modelViewProjectionMatrix upload (ofShader::begin() ->
// ofGLProgrammableRenderer::bind() -> uploadMatrices()) does not reliably
// reach a freshly-linked custom shader in this renderer/version -- confirmed
// empirically: the uniform is present (glGetActiveUniform finds it at a
// valid location) but reads back as all-zero after begin(), collapsing
// gl_Position and rendering nothing. Every ShaderPass must call this right
// after shader.begin() to set it explicitly instead of relying on oF's
// automatic upload.
void bindMvp(ofShader& shader);

// oF's shared internal VBO (used by ofFbo::draw()/ofTexture::draw() for every
// textured quad) leaves the texcoord vertex attribute disabled when a custom
// shader draws through it here -- confirmed empirically via
// glGetVertexAttribiv: the array is disabled and GL falls back to the
// generic constant (1,1,1,1), so every fragment reads the same texCoordVarying
// instead of an interpolated 0..1 gradient. Draw full-screen passes through
// this dedicated quad (its own mesh/VBO) instead of fbo.draw()/tex.draw() to
// sidestep it.
void drawFullscreenQuad(float width, float height);
}
