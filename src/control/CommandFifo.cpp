#include "CommandFifo.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sstream>

#include "ofFileUtils.h"
#include "ofLog.h"
#include "ofUtils.h"

CommandFifo::~CommandFifo() {
    if (fd >= 0) close(fd);
}

void CommandFifo::setup(const std::string& fifoPath) {
    ofDirectory::createDirectory(ofFilePath::getEnclosingDirectory(fifoPath, false), false, true);

    struct stat st{};
    if (stat(fifoPath.c_str(), &st) == 0 && !S_ISFIFO(st.st_mode)) {
        // A stale regular file at the path would silently break the channel.
        unlink(fifoPath.c_str());
    }
    if (stat(fifoPath.c_str(), &st) != 0) {
        if (mkfifo(fifoPath.c_str(), 0666) != 0) {
            ofLogError("CommandFifo") << "mkfifo(" << fifoPath << ") failed -- browser commands disabled.";
            return;
        }
    }

    // O_NONBLOCK: opening a FIFO read-side succeeds with no writer, and
    // read() returns EAGAIN instead of blocking the render loop.
    fd = open(fifoPath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        ofLogError("CommandFifo") << "open(" << fifoPath << ") failed -- browser commands disabled.";
    } else {
        ofLogNotice("CommandFifo") << "Listening on " << fifoPath;
    }
}

std::vector<CommandFifo::Command> CommandFifo::poll() {
    std::vector<Command> commands;
    if (fd < 0) return commands;

    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        pending.append(buf, static_cast<size_t>(n));
    }

    size_t newline;
    while ((newline = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, newline);
        pending.erase(0, newline + 1);
        if (line.empty()) continue;

        std::istringstream in(line);
        std::string verb;
        in >> verb;
        Command cmd;
        if (verb == "click") {
            cmd.type = Command::Type::Click;
        } else if (verb == "hold") {
            cmd.type = Command::Type::Hold;
        } else if (verb == "goto") {
            cmd.type = Command::Type::Goto;
            in >> cmd.sceneId;
        } else if (verb == "cc") {
            cmd.type = Command::Type::Cc;
            in >> cmd.ccNumber >> cmd.value;
        } else if (verb == "param") {
            cmd.type = Command::Type::Param;
            std::string targetPath;
            in >> cmd.sceneId >> targetPath >> cmd.value;
            const std::string postPrefix = "postEffects.";
            const std::string layerPrefix = "layer.";
            if (targetPath.rfind(postPrefix, 0) == 0) {
                cmd.param = targetPath.substr(postPrefix.size());
            } else if (targetPath.rfind(layerPrefix, 0) == 0) {
                std::string rest = targetPath.substr(layerPrefix.size());
                size_t dot = rest.find('.');
                if (dot == std::string::npos) {
                    ofLogWarning("CommandFifo") << "Bad param target: " << targetPath;
                    continue;
                }
                cmd.layerId = rest.substr(0, dot);
                cmd.param = rest.substr(dot + 1);
            } else {
                ofLogWarning("CommandFifo") << "Bad param target: " << targetPath;
                continue;
            }
        } else {
            ofLogWarning("CommandFifo") << "Unknown command: " << line;
            continue;
        }
        if (in.fail()) {
            ofLogWarning("CommandFifo") << "Malformed command: " << line;
            continue;
        }
        commands.push_back(std::move(cmd));
    }
    return commands;
}
