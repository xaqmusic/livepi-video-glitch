# Shader authoring

Shader body files in `bin/data/shaders/*.vert` / `*.frag` are loaded through
`ShaderLoader::load()` (`src/util/ShaderLoader.h`), never directly by
`ofShader::load()`. This is what makes the same shader source run on the
desktop's real GL 4.6 and the Pi's GLES-only driver (see "GL / GLES
portability" in `architecture.md`).

## Rules for every shader body file

1. **No `#version` line.** `ShaderLoader` prepends one for you:
   - Desktop: `#version 150`
   - Pi (`TARGET_OPENGLES` defined): `#version 100` + `precision mediump
     float;` (GLES 2.0/GLSL ES 1.00 -- targeted uniformly across every Pi
     generation, not just whichever one is currently plugged in. Pi 3's
     Mesa `vc4` driver has GLES 2.0 as its solid baseline, with GLES 3.x
     support added later and less complete; Pi 4/5's `v3d` driver handles
     GLES 3.1 fine, but our effects don't use anything from GLES 3 that
     GLES 2 lacks, so there's no reason to target it and then have to
     special-case older hardware.)

2. **Use `in`/`out`, not `attribute`/`varying` -- write it as if targeting
   GLSL 150 / GLSL ES 300.** Vertex shaders declare `in vec4 position;
   in vec2 texcoord; out vec2 texCoordVarying;`. Fragment shaders declare
   `in vec2 texCoordVarying; out vec4 fragColor;` and write to `fragColor`,
   not `gl_FragColor`. **On the Pi, `ShaderLoader` mechanically rewrites this
   to GLSL ES 1.00's `attribute`/`varying`/`gl_FragColor`** (see
   `toGles2Dialect()` in `ShaderLoader.cpp`) -- it depends on these exact
   variable names (`position`, `texcoord`, `texCoordVarying`, `fragColor`),
   so don't rename them without updating that function too.

3. **Use `texture(sampler, uv)`, not `texture2D(sampler, uv)`** -- write it
   as `texture()` regardless of target; `ShaderLoader` rewrites it to
   `texture2D()` on the Pi (GLSL ES 1.00 doesn't have `texture()`).

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
