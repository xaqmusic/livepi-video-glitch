#include "ofApp.h"
#include "ofMain.h"
#include "util/Config.h"

// Must come after ofMain.h: ofConstants.h is what decides between GLEW and
// the GLES headers and includes whichever one first -- glfw3.h pulls in the
// platform's own GL/gl.h if it's not already seen one, and either GL header
// coming first is a hard error ("gl.h included before glew.h").
#include <GLFW/glfw3.h>

int main() {
    // Must happen before any texture/FBO is created: switches oF from its
    // default GL_TEXTURE_RECTANGLE_ARB (pixel-space coordinates) to plain
    // GL_TEXTURE_2D with normalized 0..1 coordinates. GLES (the Pi's driver)
    // doesn't support rectangle textures at all -- see
    // docs/shader-authoring.md.
    ofDisableArbTex();

    // Loaded here (before the window exists) rather than in ofApp::setup()
    // so window.fullscreen/width/height can drive the actual window
    // creation -- ofApp reloads the same files for everything else.
    Config config;
    config.loadFromFile("config/app.json");
    config.mergeFromFile("config/app.local.json");

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
    int width = config.getInt("window.width", 1280);
    int height = config.getInt("window.height", 720);

    if (config.getBool("window.fullscreen", false)) {
        // Deliberately NOT using oF's OF_FULLSCREEN window mode: its
        // fullscreen transition is known-buggy on Raspberry Pi specifically
        // (ofAppGLFWWindow.cpp's own comment: "needed for rpi. as good
        // values don't come into resize_cb when coming out of fullscreen"),
        // and the workaround for that is gated behind TARGET_RASPBERRY_PI,
        // a macro this project deliberately doesn't define (see
        // docs/architecture.md's GL/GLES portability section -- enabling it
        // would silently drop TARGET_GLFW_WINDOW/audio defines this project
        // needs). Confirmed directly on this Pi 4: the real GLFW window
        // stayed at the originally-requested size while oF's own
        // ofGetWidth/Height() reported the monitor's actual (smaller) size,
        // so the render ended up clipped/vertically offset instead of
        // filling the screen.
        //
        // Sidestepped entirely: query the real monitor resolution ourselves
        // and create a plain OF_WINDOW sized and positioned to exactly
        // cover it. With no window manager running on the kiosk to add
        // borders/decoration, this is visually identical to fullscreen
        // without ever touching oF's fullscreen-mode-switch code path.
        glfwInit();
        if (auto* mode = glfwGetVideoMode(glfwGetPrimaryMonitor())) {
            width = mode->width;
            height = mode->height;
        }
        settings.setPosition(glm::vec2(0, 0));
    }
    settings.setSize(width, height);
    settings.windowMode = OF_WINDOW;

    auto window = ofCreateWindow(settings);
    auto app = std::make_shared<ofApp>();
    ofRunApp(window, app);
    return ofRunMainLoop();
}
