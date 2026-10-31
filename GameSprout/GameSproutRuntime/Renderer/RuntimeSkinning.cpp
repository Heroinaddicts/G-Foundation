#include "RuntimeRenderer.h"
#include "RuntimeMath.h"

namespace GameSproutRuntime {
bool Renderer::SkinMatrices(IGameObject& object,const MeshGPU& mesh,std::vector<Mat4>& matrices) {
    matrices.clear();
    auto* skin=object.GetComponent<ISkinnedMeshRendererComponent>();
    if(!skin) return true;
    if(mesh.Data.Bones.empty() || mesh.Data.BoneIndicesOffset<0 || mesh.Data.BoneWeightsOffset<0 ||
       mesh.Skin.size()!=mesh.Positions.size()) {
        Diagnostic("Skinned mesh has no complete prepared bone bindings: "+object.GetName());return false;
    }
    if(mesh.Data.Bones.size()>MaxSkinBones) {
        Diagnostic("Skinned mesh exceeds the 128-bone palette limit: "+object.GetName());return false;
    }
    std::string error;
    if(!skin->GetSkinMatrices(mesh.Data.Bones,matrices,error) || matrices.size()!=mesh.Data.Bones.size()) {
        Diagnostic("Cannot bind skinned mesh '"+object.GetName()+"': "+error);matrices.clear();return false;
    }
    for(const auto& matrix:matrices) if(!RuntimeMath::IsFinite(matrix)) {
        Diagnostic("Skinned mesh has a nonfinite pose matrix: "+object.GetName());matrices.clear();return false;
    }
    return true;
}

bool Renderer::PosedPositions(IGameObject& object,const MeshGPU& mesh,std::vector<Vector3>& positions) {
    positions.clear();
    std::vector<Mat4> matrices;
    if(!SkinMatrices(object,mesh,matrices)) return false;
    if(matrices.empty()) {positions=mesh.Positions;return true;}
    positions.reserve(mesh.Positions.size());
    for(std::size_t index=0;index<mesh.Positions.size();++index) {
        const auto& vertex=mesh.Skin[index];
        const auto& source=mesh.Positions[index];
        Vector3 position;
        // Identical linear skinning to the vertex shader; only called for bounds/editor queries.
        // The shared Mesh cache always retains bind positions, never a particular instance pose.
        for(std::size_t influence=0;influence<4;++influence) {
            const auto value=matrices[vertex.Bones[influence]]*Vector4{source.X,source.Y,source.Z,1};
            position=position+Vector3{value.X,value.Y,value.Z}*vertex.Weights[influence];
        }
        if(!RuntimeMath::IsFinite(position)) {positions.clear();Diagnostic("Skinned vertex is outside finite range");return false;}
        positions.push_back(position);
    }
    return true;
}
}
