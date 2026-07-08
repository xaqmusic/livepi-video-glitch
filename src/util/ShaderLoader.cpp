#include "util/ShaderLoader.h"

#include <sstream>

#include "ofFileUtils.h"
#include "ofGraphics.h"
#include "ofLog.h"
#include "ofUtils.h"

namespace {

std::string platformHeader() {
#if defined(TARGET_OPENGLES)
    return "#version 100\nprecision mediump float;\n";
#else
    return "#version 150\n";
#endif
}

#if defined(TARGET_OPENGLES)
// Shader bodies are authored once, in the modern in/out/texture() dialect
// (see docs/shader-authoring.md), which works unchanged on desktop (GLSL
// 150 core) and would also work unchanged on a Pi 4/5 (GLES 3.x). GLES 2 is
// targeted for every Pi generation instead -- guaranteed to run on the
// Pi 3's older Mesa vc4 driver too, and our effects don't need anything
// GLES 3 offers over it -- so this downgrades to GLSL ES 1.00 (attribute/
// varying/texture2D()/gl_FragColor) mechanically, rather than maintaining a
// second hand-written shader dialect. Relies on the naming conventions
// shader-authoring.md already mandates: `position`/`texcoord` are always
// vertex attributes, `texCoordVarying` is always the shared interpolant,
// `fragColor` is always the fragment output.
std::string toGles2Dialect(std::string source) {
    auto replaceAll = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = source.find(from, pos)) != std::string::npos) {
            source.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replaceAll("in vec4 position;", "attribute vec4 position;");
    replaceAll("in vec2 texcoord;", "attribute vec2 texcoord;");
    replaceAll("in vec2 texCoordVarying;", "varying vec2 texCoordVarying;");
    replaceAll("out vec2 texCoordVarying;", "varying vec2 texCoordVarying;");
    replaceAll("out vec4 fragColor;\n", "");
    replaceAll("fragColor =", "gl_FragColor =");
    replaceAll("texture(", "texture2D(");
    return source;
}
#endif

// Resolves `#pragma include "name.glslinc"` lines against files in the same
// directory as `path`. Not a real GLSL feature -- just a small textual
// preprocessing step so common helpers (see common.glslinc) aren't
// duplicated across shader files.
std::string resolveIncludes(const std::string& source, const std::string& baseDir) {
    std::istringstream in(source);
    std::ostringstream out;
    std::string line;

    while (std::getline(in, line)) {
        auto pragmaPos = line.find("#pragma include");
        if (pragmaPos != std::string::npos) {
            auto firstQuote = line.find('"');
            auto lastQuote = line.rfind('"');
            if (firstQuote != std::string::npos && lastQuote > firstQuote) {
                std::string includeName = line.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                out << ofBufferFromFile(baseDir + "/" + includeName).getText() << "\n";
                continue;
            }
        }
        out << line << "\n";
    }
    return out.str();
}

std::string loadBodyWithHeader(const std::string& path) {
    ofBuffer buffer = ofBufferFromFile(path);
    if (buffer.size() == 0) {
        ofLogError("ShaderLoader") << "Could not read shader file: " << path;
    }

    std::string baseDir = ofFilePath::getEnclosingDirectory(path, false);
    std::string body = resolveIncludes(buffer.getText(), baseDir);
#if defined(TARGET_OPENGLES)
    body = toGles2Dialect(body);
#endif
    return platformHeader() + body;
}

}  // namespace

bool ShaderLoader::load(ofShader& shader, const std::string& vertPath, const std::string& fragPath) {
    std::string vertSrc = loadBodyWithHeader(vertPath);
    std::string fragSrc = loadBodyWithHeader(fragPath);

    bool ok = shader.setupShaderFromSource(GL_VERTEX_SHADER, vertSrc);
    ok &= shader.setupShaderFromSource(GL_FRAGMENT_SHADER, fragSrc);
    ok &= shader.bindDefaults();
    ok &= shader.linkProgram();

    if (!ok) {
        ofLogError("ShaderLoader") << "Failed to build shader from " << vertPath << " + " << fragPath;
    }
    return ok;
}

void ShaderLoader::bindMvp(ofShader& shader) {
    glm::mat4 mvp = ofGetCurrentMatrix(OF_MATRIX_PROJECTION) * ofGetCurrentMatrix(OF_MATRIX_MODELVIEW);
    shader.setUniformMatrix4f("modelViewProjectionMatrix", mvp);
}

void ShaderLoader::drawFullscreenQuad(float width, float height) {
    // Bypasses oF's shared internal VBO (ofGLProgrammableRenderer::meshVbo,
    // also used by ofMesh::draw()) entirely -- own VAO/VBO, own explicit
    // glVertexAttribPointer calls, so nothing else touching that shared
    // state (oF's default shaders, the GStreamer/NVDEC GL interop used for
    // hardware video decode) can leave the texcoord attribute disabled out
    // from under a custom shader's draw call.
    static GLuint vao = 0;
    static GLuint vbo = 0;
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(ofShader::POSITION_ATTRIBUTE);
        glVertexAttribPointer(ofShader::POSITION_ATTRIBUTE, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)0);
        glEnableVertexAttribArray(ofShader::TEXCOORD_ATTRIBUTE);
        glVertexAttribPointer(
            ofShader::TEXCOORD_ATTRIBUTE, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));
        glBindVertexArray(0);
    }

    float verts[16] = {
        0,     0,      0, 0,
        width, 0,      1, 0,
        width, height, 1, 1,
        0,     height, 0, 1,
    };
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}
