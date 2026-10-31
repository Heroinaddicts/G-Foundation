#pragma once
#include "GRHIApi.h"
namespace GameSproutRuntime {
enum class PreviewShader {MeshVertex,MeshNoUVVertex,MeshNoColorVertex,MeshNoUVNoColorVertex,MeshFragment,GridVertex,GridFragment};
GRHI::IShader* CreatePreviewShader(GRHI::IDevice& device,PreviewShader shader);
}
