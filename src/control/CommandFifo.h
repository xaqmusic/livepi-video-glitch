#pragma once

#include <string>
#include <vector>

// The backend's one-way path into the renderer: newline-delimited text
// commands on a named pipe (see docs/videosynth-backend.md's IPC section).
// The renderer is the non-blocking reader -- works identically under
// mock/midi/pisound control sources, which is why this lives in ofApp
// rather than any one ControlSource. The existing pisound.button_fifo
// stays separate (the physical button's bridge script never contends with
// the backend).
//
// Protocol (one command per line):
//   click                                   -- advance scene (mirrors button)
//   hold                                    -- jump to scene 1
//   goto <sceneId>                          -- jump directly (Live mode Back)
//   cc <number> <value01>                   -- inject a CC, indistinguishable
//                                              from the same knob over MIDI
//   note <number> <value01>                 -- inject a note (velocity;
//                                              0 = release)
//   param <sceneId> <targetPath> <value>    -- pin one param; targetPath is
//                                              postEffects.<key> or
//                                              layer.<layerId>.<key>; ignored
//                                              unless <sceneId> is current
class CommandFifo {
public:
    struct Command {
        enum class Type { Click, Hold, Goto, Cc, Note, Param };
        Type type;
        std::string sceneId;   // Goto / Param
        int ccNumber = 0;      // Cc / Note
        float value = 0.0f;    // Cc / Param
        std::string layerId;   // Param ("" = scene scope)
        std::string param;     // Param
    };

    ~CommandFifo();

    void setup(const std::string& fifoPath);
    // Non-blocking; returns whatever complete lines have arrived.
    std::vector<Command> poll();

private:
    int fd = -1;
    std::string pending;  // partial line carried across polls
};
