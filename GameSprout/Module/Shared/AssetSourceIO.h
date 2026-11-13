#pragma once
#include <JsonCpp/json.hpp>
#include <string>

namespace AssetSourceIO {
// Human-readable source files. Compact serialization used for hashes,
// database records and in-memory edit baselines stays separate.
inline std::string JsonText(const Json::Value& value) {
    static const Json::StreamWriterBuilder writer=[] {
        Json::StreamWriterBuilder settings;
        settings["indentation"]="    ";
        settings["emitUTF8"]=true;
        return settings;
    }();
    return Json::writeString(writer,value)+"\n";
}
}
