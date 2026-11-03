#include "RuntimeRenderer.h"
#include "RuntimeMath.h"
#include "Scene.h"
#include "Components/TransformComponent.h"
#include <algorithm>
#include <cmath>

namespace GameSproutRuntime {
bool Renderer::ExtractLights(IScene* scene,std::vector<DirectionalLight>& lights) {
    const auto sceneID=scene->GetInstanceID();
    const auto revision=static_cast<Scene*>(scene)->GetRenderRevision();
    if(_LightSnapshotKnown&&_LightSceneID==sceneID&&_LightSceneRevision==revision) {
        Diagnostic(_LightDiagnostic);return true;
    }
    _LightSnapshotKnown=false;lights.clear();
    const auto diagnosticBefore=_Error.size();
    ++_Statistics.LightSnapshotRebuilds;
    std::vector<IGameObject*> pending{scene};
    while(!pending.empty()) {
        auto* object=pending.back();pending.pop_back();
        for(std::size_t index=object->GetChildCount();index>0;--index)pending.push_back(object->GetChild(index-1));
        auto* light=object->GetComponent<ILightComponent>();
        if(!light||!object->IsActiveInHierarchy())continue;
        const auto& settings=light->GetSettings();
        if(!settings.Enabled||settings.Intensity==0||!settings.CullingMask)continue;
        if(settings.Type!=LightType::Directional){Diagnostic("Unsupported scene light type");continue;}
        // Unity light rays follow local +Z; surface-to-light is local -Z.
        // Compose authored rotations only: ancestor scale, including reflections
        // and zero scales, must never change the orientation of a directional light.
        Vector3 direction{0,0,-1};
        for(auto* node=object;node;node=node->Parent()) {
            auto* transform=node->GetTransform();
            Quaternion orientation;
            if(const auto* current=dynamic_cast<const TransformComponent*>(transform)) {
                if(!current->GetCurrentLocalRotation(orientation))return Fail("Invalid animated directional light orientation");
            } else orientation=transform->GetLocalRotation();
            const Mat4 rotation=RuntimeMath::ComposeTRS({},orientation,{1,1,1});
            const auto rotated=rotation*Vector4{direction.X,direction.Y,direction.Z,0};
            direction={rotated.X,rotated.Y,rotated.Z};
        }
        Color radiance{settings.Color.R*settings.Intensity,settings.Color.G*settings.Intensity,
            settings.Color.B*settings.Intensity,1};
        Vector3 normalized;
        if(!direction.TryNormalize(normalized)||!RuntimeMath::IsFinite(radiance)) {
            Diagnostic("Skipped nonfinite directional light radiance/orientation: "+object->GetName());continue;
        }
        lights.push_back({normalized,radiance,settings.CullingMask,settings.Shadows});
    }
    // Highest linear luminance first; hierarchy order resolves exact ties.
    std::stable_sort(lights.begin(),lights.end(),[](const DirectionalLight& left,const DirectionalLight& right){
        const auto luminance=[](Color color){return double(color.R)*.2126+double(color.G)*.7152+double(color.B)*.0722;};
        return luminance(left.Radiance)>luminance(right.Radiance);
    });
    _LightSceneID=sceneID;_LightSceneRevision=revision;
    _LightDiagnostic=_Error.substr(diagnosticBefore);
    if(!_LightDiagnostic.empty()&&_LightDiagnostic.front()=='\n')_LightDiagnostic.erase(0,1);
    _LightSnapshotKnown=true;
    return true;
}

Renderer::LitUniform Renderer::MakeLitUniform(const RenderView& view,const DrawObject& object,
    const MaterialRenderData& material,const std::vector<DirectionalLight>& lights,bool hasNormalMap) {
    LitUniform result{};
    result.CameraPosition={view.Camera.Position.X,view.Camera.Position.Y,view.Camera.Position.Z,material.SpecularHighlights?1.0f:0.0f};
    result.Parameters={material.Metallic,material.Roughness,material.NormalScale,hasNormalMap?1.0f:0.0f};
    result.Emission=material.EmissionColor;result.NormalTextureST=material.NormalTextureST;
    result.StyleParameters={material.Stylization,material.LightThreshold,material.LightSoftness,material.ColorSaturation};
    result.StylizedShadowTint=material.StylizedShadowTint;
    std::size_t count=0;
    for(const auto& light:lights) {
        const auto lightIndex=static_cast<std::size_t>(&light-lights.data());
        if(!(light.CullingMask&(std::uint32_t{1}<<object.Layer)))continue;
        if(count==MaxDirectionalLights){Diagnostic("Lit uses the four brightest matching directional lights per object");break;}
        result.Directions[count]={light.TowardsLight.X,light.TowardsLight.Y,light.TowardsLight.Z,lightIndex==_ShadowLightIndex&&_ShadowUniform.Info.X>0?2.0f:1.0f};
        result.Radiances[count]=light.Radiance;++count;
    }
    return result;
}
}
