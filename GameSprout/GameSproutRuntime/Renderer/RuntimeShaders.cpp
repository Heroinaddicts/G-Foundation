#include "RuntimeShaders.h"
#include "Shaders/GeneratedShaders.h"

namespace GameSproutRuntime {
GRHI::IShader* CreatePreviewShader(GRHI::IDevice& device,PreviewShader shader) {
    GRHI::ShaderCode codes[3];
    GRHI::ShaderStage stage=GRHI::ShaderStage::Vertex;
    switch(shader) {
#define GS_SHADER_CASE(Name,Stage) \
    case PreviewShader::Name: \
        stage=GRHI::ShaderStage::Stage; \
        codes[0].Format=GRHI::ShaderCodeFormat::SPIRV;codes[0].Data=EmbeddedShaders::Name##SPIRV;codes[0].DataSize=sizeof(EmbeddedShaders::Name##SPIRV);codes[0].EntryPoint=EmbeddedShaders::Name##SPIRVEntry; \
        codes[1].Format=GRHI::ShaderCodeFormat::MSL;codes[1].Data=EmbeddedShaders::Name##MSL;codes[1].DataSize=sizeof(EmbeddedShaders::Name##MSL);codes[1].EntryPoint=EmbeddedShaders::Name##MSLEntry; \
        codes[2].Format=GRHI::ShaderCodeFormat::DXIL;codes[2].Data=EmbeddedShaders::Name##DXIL;codes[2].DataSize=sizeof(EmbeddedShaders::Name##DXIL);codes[2].EntryPoint=EmbeddedShaders::Name##DXILEntry; \
        for(auto& code:codes) {code.SamplerCount=EmbeddedShaders::Name##Samplers;code.UniformBufferCount=EmbeddedShaders::Name##Uniforms;} \
        break;
        GS_SHADER_CASE(MeshVertex,Vertex)
        GS_SHADER_CASE(MeshNoUVVertex,Vertex)
        GS_SHADER_CASE(MeshNoColorVertex,Vertex)
        GS_SHADER_CASE(MeshNoUVNoColorVertex,Vertex)
        GS_SHADER_CASE(MeshFragment,Fragment)
        GS_SHADER_CASE(GridVertex,Vertex)
        GS_SHADER_CASE(GridFragment,Fragment)
#undef GS_SHADER_CASE
    }
    GRHI::ShaderDesc description;description.Stage=stage;description.Codes=codes;description.CodeCount=3;
    return device.CreateShader(description);
}
}
