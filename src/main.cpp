#include "ofApp.h"
#include "ofMain.h"

int main() {
    // Must happen before any texture/FBO is created: switches oF from its
    // default GL_TEXTURE_RECTANGLE_ARB (pixel-space coordinates) to plain
    // GL_TEXTURE_2D with normalized 0..1 coordinates. GLES (the Pi's driver)
    // doesn't support rectangle textures at all -- see
    // docs/shader-authoring.md.
    ofDisableArbTex();

    ofGLWindowSettings settings;
    settings.setSize(1280, 720);
    settings.windowMode = OF_WINDOW;

    auto window = ofCreateWindow(settings);
    auto app = std::make_shared<ofApp>();
    ofRunApp(window, app);
    return ofRunMainLoop();
}
