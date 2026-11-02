#pragma once
#include "SceneDocument.h"
#include "ValueBinary.h"

namespace PrefabDocument {
// Data is a complete single-root object document. Assigned object references
// must stay inside it; resource references keep their asset identities.
bool Validate(const Json::Value& data,std::string& error);
bool Encode(const Json::Value& data,std::vector<std::uint8_t>& bytes,std::string& error);
bool Decode(const std::vector<std::uint8_t>& bytes,Json::Value& data,std::string& error);
// Exact known identity fields only; script InstanceID is owner-local.
bool Remap(Json::Value& data,const std::map<std::uint64_t,std::uint64_t>& ids,std::string& error);
}
