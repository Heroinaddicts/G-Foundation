#pragma once
#include <JsonCpp/json.hpp>
#include <string>

namespace AssetSourceIO {
// Human-readable source files. Compact serialization used for hashes,
// database records and in-memory edit baselines stays separate.
inline std::string JsonText(const Json::Value& value) {
    Json::StreamWriterBuilder writer;
    writer["indentation"]="    ";
    writer["emitUTF8"]=true;
    return Json::writeString(writer,value)+"\n";
}
}
