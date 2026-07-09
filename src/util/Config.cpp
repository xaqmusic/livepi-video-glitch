#include "util/Config.h"

#include <algorithm>

#include "ofFileUtils.h"
#include "ofLog.h"
#include "ofUtils.h"

namespace {

ofJson::json_pointer toPointer(const std::string& dottedKey) {
    std::string path = "/" + dottedKey;
    std::replace(path.begin(), path.end(), '.', '/');
    return ofJson::json_pointer(path);
}

}  // namespace

bool Config::loadFromFile(const std::string& relativePath) {
    std::string path = ofToDataPath(relativePath, true);
    if (!ofFile::doesFileExist(path)) {
        ofLogWarning("Config") << "Config file not found: " << path << " -- using defaults.";
        json = ofJson::object();
        return false;
    }
    json = ofLoadJson(path);
    return true;
}

void Config::mergeFromFile(const std::string& relativePath) {
    std::string path = ofToDataPath(relativePath, true);
    if (!ofFile::doesFileExist(path)) return;
    json.merge_patch(ofLoadJson(path));
}

std::string Config::getString(const std::string& key, const std::string& fallback) const {
    return json.value(toPointer(key), fallback);
}

float Config::getFloat(const std::string& key, float fallback) const {
    return json.value(toPointer(key), fallback);
}

int Config::getInt(const std::string& key, int fallback) const {
    return json.value(toPointer(key), fallback);
}

bool Config::getBool(const std::string& key, bool fallback) const {
    return json.value(toPointer(key), fallback);
}
