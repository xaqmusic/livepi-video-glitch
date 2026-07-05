#include "util/ShaderLoader.h"

#include <sstream>

#include "ofFileUtils.h"
#include "ofLog.h"
#include "ofUtils.h"

namespace {

std::string platformHeader() {
#if defined(TARGET_OPENGLES)
    return "#version 300 es\nprecision mediump float;\n";
#else
    return "#version 150\n";
#endif
}

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
