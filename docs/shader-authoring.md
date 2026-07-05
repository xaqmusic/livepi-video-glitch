# Shader authoring

Shader body files in `bin/data/shaders/*.vert` / `*.frag` are loaded through
`ShaderLoader::load()` (`src/util/ShaderLoader.h`), never directly by
`ofShader::load()`. This is what makes the same shader source run on the
desktop's real GL 4.6 and the Pi's GLES-only driver (see "GL / GLES
portability" in `architecture.md`).

## Rules for every shader body file

1. **No `#version` line.** `ShaderLoader` prepends one for you:
   - Desktop: `#version 150`
   - Pi (`TARGET_OPENGLES` defined): `#version 300 es` + `precision mediump
     float;`

   Both targets support the same modern-ish syntax below, so the shader body
   itself never needs to know which platform it's on.

2. **Use `in`/`out`, not `attribute`/`varying`.** Vertex shaders declare
   `in vec4 position; in vec2 texcoord; out vec2 texCoordVarying;`. Fragment
   shaders declare `in vec2 texCoordVarying; out vec4 fragColor;` and write
   to `fragColor`, not `gl_FragColor`.

3. **Use `texture(sampler, uv)`, not `texture2D(sampler, uv)`** -- the
   former is what both GLSL 150 and GLSL ES 300 call it.

4. **Texture coordinates are already normalized 0..1.** `main.cpp` calls
   `ofDisableArbTex()` before any texture is created, which switches oF from
   its default `GL_TEXTURE_RECTANGLE_ARB` (pixel-space coordinates, and
   *not supported by GLES at all*) to plain `GL_TEXTURE_2D`. Don't divide by
   `textureSize()` -- `texCoordVarying` is already in the right range.

5. **Shared helpers go in `common.glslinc`**, pulled in with
   `#pragma include "common.glslinc"` on its own line (resolved textually by
   `ShaderLoader` relative to the including file's directory -- this is a
   tiny custom preprocessor step, not a real GLSL feature). Don't put a
   `#version`/`precision` line in an include file; those are only added once,
   to the fully-assembled source.

## Adding a new pass

1. Write `bin/data/shaders/my_effect.frag` following the rules above.
   Reuse `bin/data/shaders/passthrough.vert` unless the effect needs
   per-vertex work.
2. Add a `MyEffectPass : public ShaderPass` in `src/fx/` (see
   `HSyncTearPass` for the shortest example): `setup()` calls
   `ShaderLoader::load()`, `apply()` binds uniforms from `ControlState` /
   `Scene` and draws `src` into `dst` inside `shader.begin()`/`end()`.
3. Register it in `ofApp::setup()`'s `shaderChain.addPass(...)` calls.
