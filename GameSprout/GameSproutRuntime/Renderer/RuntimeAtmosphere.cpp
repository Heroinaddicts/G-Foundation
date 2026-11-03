#include "RuntimeRenderer.h"
#include "RuntimeRenderResourceCache.h"
#include "RuntimeMath.h"
#include <cmath>

namespace GameSproutRuntime {
bool Renderer::DrawSky(GRHI::IRenderPass& pass,const RenderView& view,GRHI::TextureFormat format,
    float aspect,const std::vector<DirectionalLight>& lights) {
    const auto& settings=view.Scene->GetEnvironmentLighting();
    if(!settings.Atmosphere.Enabled||!settings.Atmosphere.DrawSky)return true;
    if(!IsValidEnvironmentLighting(settings))return true;
    for(const auto index:{17u,18u})if(!_Shaders[index]) {
        _Shaders[index]=CreatePreviewShader(_Device,static_cast<PreviewShader>(index));
        if(!_Shaders[index])return Fail(DeviceError("Create Runtime atmosphere shader"));
    }
    auto& pipelines=_ResourceCache.SkyPipelines;
    auto found=pipelines.find(format);
    if(found==pipelines.end()) {
        GRHI::GraphicsPipelineDesc description;
        description.VertexShader=_Shaders[17];description.FragmentShader=_Shaders[18];
        description.ColorFormat=format;description.DepthFormat=GRHI::TextureFormat::D32_Float;
        description.Cull=GRHI::CullMode::None;description.DepthTest=false;description.DepthWrite=false;
        auto* pipeline=_Device.CreateGraphicsPipeline(description);
        if(!pipeline)return Fail(DeviceError("Create Runtime atmosphere pipeline"));
        try {found=pipelines.emplace(format,pipeline).first;}
        catch(...){delete pipeline;throw;}
    }
    struct VertexUniform {Vector4 Right,Up,Forward,Projection;} vertex{};
    struct FragmentUniform {Color Zenith,Horizon;Vector4 SunDirection;Color SunRadiance;} fragment{};
    Mat4 inverseView;
    // GPU cameras use an RH view basis internally even though authored world
    // coordinates use Unity's +Z-forward convention. Use the actual matrices.
    const auto& camera=view.Camera.Projection;
    if(!RuntimeMath::TryInverseAffine(view.Camera.View,inverseView))return Fail("Cannot reconstruct Runtime sky camera rays");
    vertex.Right=inverseView*Vector4{1,0,0,0};vertex.Up=inverseView*Vector4{0,1,0,0};
    vertex.Forward=inverseView*Vector4{0,0,-1,0};
    if(camera.Projection==CameraProjection::Perspective) {
        const float halfHeight=std::tan(camera.VerticalFovRadians*.5f);
        vertex.Projection={halfHeight*aspect,halfHeight,0,0};
    }
    fragment.Zenith=settings.Atmosphere.ZenithColor;fragment.Horizon=settings.Atmosphere.HorizonColor;
    for(const auto& light:lights)if(light.CullingMask&camera.CullingMask) {
        fragment.SunDirection={light.TowardsLight.X,light.TowardsLight.Y,light.TowardsLight.Z,1};
        fragment.SunRadiance=light.Radiance;break;
    }
    pass.SetPipeline(found->second);pass.SetVertexUniform(0,&vertex,sizeof(vertex));
    pass.SetFragmentUniform(0,&fragment,sizeof(fragment));pass.Draw(3);
    ++_Statistics.DrawCalls;++_Statistics.Triangles;
    return true;
}
}
