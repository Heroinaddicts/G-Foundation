#include "RuntimeRenderer.h"
#include "RuntimeMath.h"

namespace {
constexpr std::size_t MaxDebugVertices=1048576;
bool ValidTint(const GameSproutRuntime::Color& tint) {
    return GameSproutRuntime::RuntimeMath::IsFinite(tint) && tint.R>=0 && tint.G>=0 && tint.B>=0 && tint.A>=0 && tint.A<=1;
}
}
namespace GameSproutRuntime {
bool Renderer::PrepareDebug(const DebugDrawList* draw,std::vector<DebugBatch>& batches) {
    if(!draw) return true;
    if(draw->Lines.size()>MaxDebugVertices/2 || draw->Triangles.size()>MaxDebugVertices/3 ||
       draw->Lines.size()*2+draw->Triangles.size()*3>MaxDebugVertices)
        return Fail("Debug draw exceeds 1,048,576 vertices");
    // 先验证整个输入，拒绝时不获取渲染目标、不提交半份调试几何。
    for(const auto& line:draw->Lines) if(!RuntimeMath::IsFinite(line.From) || !RuntimeMath::IsFinite(line.To) || !ValidTint(line.Tint))
        return Fail("Debug line contains an invalid position or color");
    for(const auto& triangle:draw->Triangles) if(!RuntimeMath::IsFinite(triangle.A) || !RuntimeMath::IsFinite(triangle.B) ||
        !RuntimeMath::IsFinite(triangle.C) || !ValidTint(triangle.Tint)) return Fail("Debug triangle contains an invalid position or color");
    // 先深度测试，再overlay；同层先填充再线框，确保工具轮廓在填充之上。
    for(bool depth:{true,false}) for(bool triangles:{true,false}) {
        DebugBatch batch;batch.Triangles=triangles;batch.DepthTest=depth;
        if(triangles) {
            for(const auto& triangle:draw->Triangles) if(triangle.DepthTest==depth) {
                batch.Vertices.push_back({triangle.A,triangle.Tint});batch.Vertices.push_back({triangle.B,triangle.Tint});
                batch.Vertices.push_back({triangle.C,triangle.Tint});
            }
        } else {
            for(const auto& line:draw->Lines) if(line.DepthTest==depth) {
                batch.Vertices.push_back({line.From,line.Tint});batch.Vertices.push_back({line.To,line.Tint});
            }
        }
        if(!batch.Vertices.empty()) batches.push_back(std::move(batch));
    }
    return true;
}

bool Renderer::DrawDebug(GRHI::IRenderPass& pass,const Mat4& viewProjection,GRHI::TextureFormat format,
                         const std::vector<DebugBatch>& batches,std::vector<GRHI::IBuffer*>& buffers) {
    static_assert(sizeof(DebugVertex)==28);
    for(const auto& batch:batches) {
        PipelineKey key;key.Grid=true;key.Format=format;key.DebugTriangles=batch.Triangles;key.DepthTest=batch.DepthTest;
        auto* pipeline=Pipeline(key);if(!pipeline) return false;
        GRHI::BufferDesc description;description.Usage=GRHI::BufferUsage::Vertex;description.Size=batch.Vertices.size()*sizeof(DebugVertex);
        GRHI::BufferInitialData initial{batch.Vertices.data(),description.Size};
        auto* buffer=_Device.CreateBuffer(description,&initial);
        if(!buffer) return Fail(DeviceError("Create Runtime debug draw vertices"));
        try {buffers.push_back(buffer);} catch(...) {delete buffer;throw;}
        DrawUniform uniform;uniform.ViewProjection=viewProjection;
        pass.SetPipeline(pipeline);pass.SetVertexBuffer(0,buffer,0);
        pass.SetVertexUniform(0,&uniform,sizeof(uniform));pass.Draw(static_cast<std::uint32_t>(batch.Vertices.size()));
        ++_Statistics.DrawCalls;
        if(batch.Triangles) _Statistics.Triangles+=static_cast<std::uint32_t>(batch.Vertices.size()/3);
    }
    return true;
}
}
