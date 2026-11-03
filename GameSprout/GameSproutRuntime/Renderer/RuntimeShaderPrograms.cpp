#include "RuntimeRenderer.h"
#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace {
using namespace GameSproutRuntime;
std::uint32_t ValueSize(ShaderValueType type) {
    switch(type) {
    case ShaderValueType::Float:case ShaderValueType::Int:case ShaderValueType::UInt:return 4;
    case ShaderValueType::Float2:return 8;
    case ShaderValueType::Float3:return 12;
    case ShaderValueType::Float4:return 16;
    }
    return 0;
}
bool ProgramValid(const ShaderProgramData& data,ShaderProgramStage stage,std::string& error) {
    const auto fail=[&](const char* message){error=message;return false;};
    if(data.Stage!=stage||data.Codes.empty()||data.Codes.size()>8||data.Uniforms.size()>4||data.Textures.size()>16)
        return fail("Invalid prepared Shader program stage/counts");
    if(stage==ShaderProgramStage::Vertex&&!data.Textures.empty())return fail("Vertex texture sampling is not supported by this Runtime contract");
    std::set<std::string> names;
    for(std::size_t index=0;index<data.Textures.size();++index) {
        const auto& binding=data.Textures[index];
        if(binding.Slot!=index||binding.Name.empty()||!names.insert(binding.Name).second)
            return fail("Shader texture slots must be dense and property names unique");
    }
    for(std::size_t index=0;index<data.Uniforms.size();++index) {
        const auto& block=data.Uniforms[index];
        if(block.Slot!=index||!block.Size||block.Size%16||block.Size>16384||block.Members.size()>512)
            return fail("Invalid prepared Shader uniform slot/size");
        if(block.Source==ShaderUniformSource::Draw) {
            if(stage!=ShaderProgramStage::Vertex||index!=0||block.Size!=192||!block.Members.empty())
                return fail("GameSproutDraw must be vertex b0 with three matrices (192 bytes)");
            continue;
        }
        if(block.Source!=ShaderUniformSource::Material)return fail("Unknown Shader uniform source");
        names.clear();std::vector<bool> occupied(block.Size,false);
        for(const auto& member:block.Members) {
            const auto size=ValueSize(member.Type);
            if(member.Name.empty()||!names.insert(member.Name).second||!size||member.Offset%4||
                member.Offset>block.Size||size>block.Size-member.Offset)
                return fail("Invalid prepared Shader material member layout");
            for(std::uint32_t byte=member.Offset;byte<member.Offset+size;++byte) {
                if(occupied[byte])return fail("Shader material uniform members overlap");
                occupied[byte]=true;
            }
        }
    }
    std::set<std::pair<ShaderProgramFormat,ShaderProgramPlatform>> formats;
    for(const auto& code:data.Codes) {
        if(code.EntryPoint.empty()||code.EntryPoint.size()>256||code.EntryPoint.find('\0')!=std::string::npos||
            code.Bytes.empty()||code.Bytes.size()>64u*1024u*1024u||
            static_cast<unsigned>(code.Format)>static_cast<unsigned>(ShaderProgramFormat::Metallib)||
            static_cast<unsigned>(code.Platform)>static_cast<unsigned>(ShaderProgramPlatform::IOSSimulator)||
            !formats.emplace(code.Format,code.Platform).second)
            return fail("Invalid prepared Shader platform code");
        if(code.Format==ShaderProgramFormat::MSL&&code.Bytes.back()!=0)return fail("MSL code must include its terminating NUL");
        if(code.Format==ShaderProgramFormat::SPIRV&&(code.Bytes.size()<20||code.Bytes.size()%4))
            return fail("SPIR-V bytecode is not a complete aligned module");
    }
    return true;
}
bool NativePlatform(const ShaderProgramCode& code) {
    if(code.Format!=ShaderProgramFormat::Metallib)return true;
#if defined(__APPLE__)
#if TARGET_OS_SIMULATOR
    return code.Platform==ShaderProgramPlatform::IOSSimulator;
#elif TARGET_OS_IPHONE
    return code.Platform==ShaderProgramPlatform::IOS;
#else
    return code.Platform==ShaderProgramPlatform::MacOS;
#endif
#else
    return false;
#endif
}
GRHI::IShader* CreateProgram(GRHI::IDevice& device,const ShaderProgramData& program) {
    std::vector<GRHI::ShaderCode> codes;
    for(const auto& source:program.Codes) {
        if(!NativePlatform(source))continue;
        GRHI::ShaderCode code;
        switch(source.Format) {
        case ShaderProgramFormat::SPIRV:code.Format=GRHI::ShaderCodeFormat::SPIRV;break;
        case ShaderProgramFormat::MSL:code.Format=GRHI::ShaderCodeFormat::MSL;break;
        case ShaderProgramFormat::DXIL:code.Format=GRHI::ShaderCodeFormat::DXIL;break;
        case ShaderProgramFormat::Metallib:code.Format=GRHI::ShaderCodeFormat::Metallib;break;
        }
        code.Data=source.Bytes.data();code.DataSize=source.Bytes.size();code.EntryPoint=source.EntryPoint.c_str();
        code.SamplerCount=static_cast<std::uint32_t>(program.Textures.size());
        code.UniformBufferCount=static_cast<std::uint32_t>(program.Uniforms.size());codes.push_back(code);
    }
    GRHI::ShaderDesc description;
    description.Stage=program.Stage==ShaderProgramStage::Vertex?GRHI::ShaderStage::Vertex:GRHI::ShaderStage::Fragment;
    description.Codes=codes.data();description.CodeCount=codes.size();return device.CreateShader(description);
}
}

namespace GameSproutRuntime {
Renderer::ShaderGPU* Renderer::Shader(const ResourceReference& reference) {
    auto [position,inserted]=_UserShaders.try_emplace(ResourceKey(reference));auto& entry=position->second;
    if(inserted) {
        try {
            entry.ResourceRevision=_Resources.GetResourceRevision(reference);
            if(_Resources.LoadShader(reference,entry.Data,entry.Error)) {
                if(entry.Data.Passes.size()!=1||entry.Data.Passes[0].Name.empty()||entry.Data.Passes[0].LightMode!="Forward")
                    entry.Error="Runtime currently requires exactly one Forward Shader pass";
                else {
                    const auto& pass=entry.Data.Passes[0];
                    if(static_cast<unsigned>(pass.Cull)>2||static_cast<unsigned>(pass.Blend)>2||static_cast<unsigned>(pass.DepthCompare)>7)
                        entry.Error="Invalid Shader render state";
                    else if(ProgramValid(pass.Vertex,ShaderProgramStage::Vertex,entry.Error)&&ProgramValid(pass.Fragment,ShaderProgramStage::Fragment,entry.Error)) {
                        entry.Vertex=CreateProgram(_Device,pass.Vertex);
                        if(entry.Vertex)entry.Fragment=CreateProgram(_Device,pass.Fragment);
                        if(entry.Vertex&&entry.Fragment)entry.Loaded=true;
                        else entry.Error=DeviceError("Create prepared user Shader program");
                    }
                }
            }
        }catch(const std::exception& failure){entry.Error=failure.what();}
        catch(...){entry.Error="Resource provider threw while loading Shader";}
        if(!entry.Loaded){delete entry.Vertex;delete entry.Fragment;entry.Vertex=nullptr;entry.Fragment=nullptr;}
    }
    if(!entry.Loaded){Diagnostic("Shader "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+entry.Error);return nullptr;}
    return &entry;
}

GRHI::IGraphicsPipeline* Renderer::ShaderPipeline(const ResourceReference& reference,ShaderGPU& shader,GRHI::TextureFormat format,bool negative) {
    ShaderPipelineKey key{reference.Guid,reference.LocalID,format,negative};
    if(const auto found=_ShaderPipelines.find(key);found!=_ShaderPipelines.end())return found->second;
    const auto& pass=shader.Data.Passes[0];
    const GRHI::VertexBindingDesc binding{0,48};
    const GRHI::VertexAttributeDesc attributes[]={{0,0,GRHI::VertexFormat::Float3,0},{1,0,GRHI::VertexFormat::Float3,12},
        {2,0,GRHI::VertexFormat::Float4,24},{3,0,GRHI::VertexFormat::Float2,40}};
    GRHI::GraphicsPipelineDesc description;
    description.VertexShader=shader.Vertex;description.FragmentShader=shader.Fragment;description.ColorFormat=format;
    description.VertexBindings=&binding;description.VertexBindingCount=1;description.VertexAttributes=attributes;description.VertexAttributeCount=4;
    switch(pass.Cull){case ShaderCullMode::Off:description.Cull=GRHI::CullMode::None;break;case ShaderCullMode::Front:description.Cull=GRHI::CullMode::Front;break;case ShaderCullMode::Back:description.Cull=GRHI::CullMode::Back;break;}
    switch(pass.Blend){case ShaderBlendMode::Opaque:description.Blend=GRHI::BlendMode::Opaque;break;case ShaderBlendMode::Alpha:description.Blend=GRHI::BlendMode::Alpha;break;case ShaderBlendMode::PremultipliedAlpha:description.Blend=GRHI::BlendMode::PremultipliedAlpha;break;}
    description.Winding=negative?GRHI::FrontFace::CounterClockwise:GRHI::FrontFace::Clockwise;
    description.DepthFormat=GRHI::TextureFormat::D32_Float;description.DepthTest=pass.DepthTest;description.DepthWrite=pass.DepthWrite;
    description.DepthCompare=static_cast<GRHI::CompareOp>(pass.DepthCompare);
    auto* pipeline=_Device.CreateGraphicsPipeline(description);
    if(!pipeline){Diagnostic(DeviceError("Create user Shader graphics pipeline"));return nullptr;}
    try {_ShaderPipelines.emplace(std::move(key),pipeline);}catch(...){delete pipeline;throw;}
    return pipeline;
}
}
