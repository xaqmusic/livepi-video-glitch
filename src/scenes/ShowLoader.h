#pragma once

#include <map>
#include <string>
#include <vector>

#include "Scene.h"

// Loads the active show: bin/data/shows/active.json names a show file in
// the same directory, which holds the setlist (docs/videosynth-backend.md's
// data model, schemaVersion 1). Clip layers reference clips by stable id;
// this loader resolves them to paths via bin/data/clips/library.json at
// parse time. The renderer never validates beyond "can I keep running" --
// a missing clip or unknown blend mode degrades (black layer, Normal blend)
// with a log line, never a crash; the backend rejects genuinely bad shows
// at save time before they ever land on disk.
//
// Parse failures keep the last successfully loaded scene list, so a torn
// or hand-mangled show file can't take down a running set. Hot reload
// (Phase A.4) builds on the same last-good behavior.
class ShowLoader {
public:
    // Reads active.json + the show it points at + the clip library.
    // Returns false (keeping any previous scenes) if nothing loadable.
    bool load();

    // Called every frame (a stat() is microseconds -- the cost of NOT
    // polling per frame is up to a second of dead wait in the editor's
    // save->see loop, which is a hard requirement). Detects changes to
    // active.json or the active show file by mtime+inode+size -- the
    // backend replaces files via atomic rename, which changes the inode,
    // so the path is re-stat()ed fresh every poll and no fd is ever held.
    // Returns true when a reload actually replaced the scene list.
    bool pollForChanges();

    const std::vector<Scene>& getScenes() const { return scenes; }
    const std::string& getActiveShowName() const { return activeShowName; }

private:
    struct FileSig {
        bool valid = false;
        unsigned long inode = 0;
        long long size = 0;
        long long mtimeSec = 0;
        long long mtimeNsec = 0;
        bool operator!=(const FileSig& other) const {
            return valid != other.valid || inode != other.inode || size != other.size
                || mtimeSec != other.mtimeSec || mtimeNsec != other.mtimeNsec;
        }
    };

    bool parseShowFile(const std::string& absPath);
    std::map<std::string, std::string> loadClipLibrary() const;
    FileSig statPath(const std::string& absPath) const;
    void captureSignatures();

    std::vector<Scene> scenes;
    std::string activeShowName;
    std::string pointedShowPath;  // abs path active.json currently points at
    FileSig activeSig, showSig, librarySig;
};
