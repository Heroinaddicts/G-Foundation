#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
#include "../Shared/ShaderDefinition.h"

namespace {
template<typename T> bool EnumValue(const Json::Value& value,const std::initializer_list<const char*>& names,T& result) {
    if(!value.isString())return false;unsigned index=0;
    for(const auto* name:names){if(value==name){result=static_cast<T>(index);return true;}++index;}return false;
}
bool Program(const Json::Value& value,const Json::Value& schema,const QByteArray& bytes,bool vertex,
    GameSproutRuntime::ShaderProgramData& output,std::vector<std::pair<std::uint64_t,std::uint64_t>>& ranges,std::string& error) {
    using namespace GameSproutRuntime;
    if(!value.isObject()||value["Stage"]!=(vertex?"Vertex":"Fragment")){error="Invalid prepared Shader stage";return false;}
    Json::Value layout;
    if(!ShaderDefinition::ProgramLayout(value["Reflection"],schema,vertex,layout,error))return false;
    if(AssetArtifactIO::JsonText(value["Uniforms"])!=AssetArtifactIO::JsonText(layout["Uniforms"])||AssetArtifactIO::JsonText(value["Textures"])!=AssetArtifactIO::JsonText(layout["Textures"])){error="Prepared Shader bindings differ from reflection";return false;}
    ShaderProgramData program;program.Stage=vertex?ShaderProgramStage::Vertex:ShaderProgramStage::Fragment;
    for(const auto& block:layout["Uniforms"]) {
        ShaderUniformBlock uniform;uniform.Slot=block["Slot"].asUInt();uniform.Size=block["Size"].asUInt();
        uniform.Source=block["Source"]=="Draw"?ShaderUniformSource::Draw:ShaderUniformSource::Material;
        for(const auto& member:block["Members"]) {
            ShaderUniformMember field;field.Name=member["Name"].asString();field.Offset=member["Offset"].asUInt();
            if(!EnumValue(member["Type"],{"Float","Int","UInt","Float2","Float3","Float4"},field.Type)){error="Invalid prepared uniform type";return false;}
            uniform.Members.push_back(std::move(field));
        }
        program.Uniforms.push_back(std::move(uniform));
    }
    for(const auto& texture:layout["Textures"])program.Textures.push_back({texture["Name"].asString(),texture["Slot"].asUInt()});
    if(!value["Variants"].isArray()||value["Variants"].empty()||value["Variants"].size()>16){error="Shader has no valid platform programs";return false;}
    std::set<std::pair<ShaderProgramFormat,ShaderProgramPlatform>> formats;
    for(const auto& variant:value["Variants"]) {
        ShaderProgramCode code;
        if(!EnumValue(variant["Format"],{"SPIRV","MSL","DXIL","MetalLib"},code.Format)
            ||!EnumValue(variant["Platform"],{"Vulkan","Direct3D12","Apple","macOS","iOS","iOSSimulator"},code.Platform)
            ||!variant["EntryPoint"].isString()||!MaterialDefinition::Identifier(variant["EntryPoint"].asString())
            ||!variant["Offset"].isUInt64()||!variant["Size"].isUInt64()||!formats.insert({code.Format,code.Platform}).second){error="Invalid Shader platform program descriptor";return false;}
        const bool platformValid=(code.Format==ShaderProgramFormat::SPIRV&&code.Platform==ShaderProgramPlatform::Vulkan)
            ||(code.Format==ShaderProgramFormat::MSL&&code.Platform==ShaderProgramPlatform::Apple)
            ||(code.Format==ShaderProgramFormat::DXIL&&code.Platform==ShaderProgramPlatform::Direct3D12)
            ||(code.Format==ShaderProgramFormat::Metallib&&(code.Platform==ShaderProgramPlatform::MacOS||code.Platform==ShaderProgramPlatform::IOS||code.Platform==ShaderProgramPlatform::IOSSimulator));
        if(!platformValid){error="Shader program format does not match its target platform";return false;}
        const auto offset=variant["Offset"].asUInt64(),size=variant["Size"].asUInt64();
        if(offset%4||!size||offset>std::uint64_t(bytes.size())||size>std::uint64_t(bytes.size())-offset){error="Shader program exceeds artifact bytes";return false;}
        for(const auto& range:ranges)if(offset<range.second&&offset+size>range.first){error="Shader program ranges overlap";return false;}ranges.push_back({offset,offset+size});
        const QByteArray payload=bytes.mid(qsizetype(offset),qsizetype(size));
        if((code.Format==ShaderProgramFormat::SPIRV&&(payload.size()<20||payload.size()%4||!payload.startsWith(QByteArray::fromHex("03022307"))))
            ||(code.Format==ShaderProgramFormat::MSL&&(!payload.endsWith(char(0))||payload.left(payload.size()-1).contains(char(0))))
            ||(code.Format==ShaderProgramFormat::DXIL&&!payload.startsWith("DXBC"))
            ||(code.Format==ShaderProgramFormat::Metallib&&!payload.startsWith("MTLB"))){error="Invalid compiled Shader program payload";return false;}
        code.EntryPoint=variant["EntryPoint"].asString();
        code.Bytes.assign(reinterpret_cast<const std::uint8_t*>(payload.constData()),reinterpret_cast<const std::uint8_t*>(payload.constData()+payload.size()));
        program.Codes.push_back(std::move(code));
    }
    output=std::move(program);return true;
}
}
bool LibraryRuntimeResources::LoadShader(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::ShaderRenderData& output,std::string& error) {
    output={};Json::Value descriptor;QByteArray bytes;
    if(!Read(reference,eAssetType::Shader,descriptor,bytes,error))return false;
    if(descriptor["Format"]!="GameSproutShader"||descriptor["ShaderVersion"]!=1||descriptor["Encoding"]!="RawBytes"
        ||!descriptor["Name"].isString()||!descriptor["Passes"].isArray()||descriptor["Passes"].size()!=1
        ||!MaterialDefinition::ValidateSchema(descriptor["Material"],error)) {if(error.empty())error="Invalid graphics Shader bundle";return false;}
    GameSproutRuntime::ShaderRenderData shader;shader.Name=descriptor["Name"].asString();
    std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges;
    for(const auto& value:descriptor["Passes"]) {
        if(!ShaderDefinition::ValidateLinkedPass(value,error))return false;
        GameSproutRuntime::ShaderPassData pass;
        if(!value["Name"].isString()||!MaterialDefinition::Identifier(value["Name"].asString())||value["LightMode"]!="Forward"
            ||!EnumValue(value["Cull"],{"Off","Front","Back"},pass.Cull)
            ||!EnumValue(value["Blend"],{"Opaque","Alpha","PremultipliedAlpha"},pass.Blend)
            ||!EnumValue(value["DepthCompare"],{"Never","Less","Equal","LessEqual","Greater","NotEqual","GreaterEqual","Always"},pass.DepthCompare)
            ||!value["DepthTest"].isBool()||!value["DepthWrite"].isBool()){error="Invalid Shader Pass state";return false;}
        pass.Name=value["Name"].asString();pass.LightMode="Forward";pass.DepthTest=value["DepthTest"].asBool();pass.DepthWrite=value["DepthWrite"].asBool();
        if(!Program(value["Vertex"],descriptor["Material"],bytes,true,pass.Vertex,ranges,error)
            ||!Program(value["Fragment"],descriptor["Material"],bytes,false,pass.Fragment,ranges,error))return false;
        shader.Passes.push_back(std::move(pass));
    }
    output=std::move(shader);return true;
}
