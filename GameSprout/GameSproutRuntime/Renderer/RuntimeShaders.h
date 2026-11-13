#pragma once
#include "GRHIApi.h"
namespace GameSproutRuntime {
enum class PreviewShader {MeshVertex,MeshNoUVVertex,MeshNoColorVertex,MeshNoUVNoColorVertex,MeshFragment,GridVertex,GridFragment,
    SkinnedMeshVertex,SkinnedMeshNoUVVertex,SkinnedMeshNoColorVertex,SkinnedMeshNoUVNoColorVertex,LitFragment,
    InstancedMeshVertex,InstancedMeshNoUVVertex,InstancedMeshNoColorVertex,InstancedMeshNoUVNoColorVertex,ShadowFragment,SkyVertex,SkyFragment,
    ShadowVertex,InstancedShadowVertex,SkinnedShadowVertex,UIVertex,UIFragment};
inline constexpr unsigned CachedPreviewShaderCount=static_cast<unsigned>(PreviewShader::UIVertex);
GRHI::IShader* CreatePreviewShader(GRHI::IDevice& device,PreviewShader shader);
}
