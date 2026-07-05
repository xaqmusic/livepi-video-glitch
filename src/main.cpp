#include "ofApp.h"
#include "ofMain.h"

int main() {
    // Must happen before any texture/FBO is created: switches oF from its
    // default GL_TEXTURE_RECTANGLE_ARB (pixel-space coordinates) to plain
    // GL_TEXTURE_2D with normalized 0..1 coordinates. GLES (the Pi's driver)
    // doesn't support rectangle textures at all -- see
    // docs/shader-authoring.md.
    ofDisableArbTex();

    // ofGLWindowSettings requests GLFW_OPENGL_API (desktop GL) unconditionally
    // -- on the Pi, where only GLES is available (Mesa vc4/v3d have no desktop
    // GL profile), that fails to create a context at all. GLES 2 is targeted
    // uniformly across every Pi generation -- see docs/shader-authoring.md for
    // why -- so this never needs touching when moving from a Pi 3 to a 4/5.
#if defined(TARGET_OPENGLES)
    ofGLESWindowSettings settings;
    settings.glesVersion = 2;
#else
    ofGLWindowSettings settings;
#endif
    settings.setSize(1280, 720);
    settings.windowMode = OF_WINDOW;

    auto window = ofCreateWindow(settings);
    auto app = std::make_shared<ofApp>();
    ofRunApp(window, app);
    return ofRunMainLoop();
}
