#pragma once
#include "GRHIApi.h"
namespace GameSproutRuntime {
enum class PreviewShader {MeshVertex,MeshNoUVVertex,MeshNoColorVertex,MeshNoUVNoColorVertex,MeshFragment,GridVertex,GridFragment,
    SkinnedMeshVertex,SkinnedMeshNoUVVertex,SkinnedMeshNoColorVertex,SkinnedMeshNoUVNoColorVertex,LitFragment,
    InstancedMeshVertex,InstancedMeshNoUVVertex,InstancedMeshNoColorVertex,InstancedMeshNoUVNoColorVertex,ShadowFragment,SkyVertex,SkyFragment};
GRHI::IShader* CreatePreviewShader(GRHI::IDevice& device,PreviewShader shader);
}
