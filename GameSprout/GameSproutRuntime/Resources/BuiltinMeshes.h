#pragma once
#include "Api/RuntimeBuiltinResources.h"
#include "Api/RuntimeResourceApi.h"

namespace GameSproutRuntime::BuiltinMeshes {
// CPU prepared geometry, independent of Editor, source files and GPU backend.
bool Load(const ResourceReference&,MeshRenderData&,std::string& error);
}
