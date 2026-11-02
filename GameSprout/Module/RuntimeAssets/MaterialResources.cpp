#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
#include "../Shared/MaterialDefinition.h"
using LibraryResourceDecode::Unsigned;
using LibraryResourceDecode::FloatAt;
using LibraryResourceDecode::ColorValue;
using LibraryResourceDecode::ReferenceValue;
bool LibraryRuntimeResources::LoadMaterial(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::MaterialRenderData& output, std::string& error) {
    output = {};
    const auto fail = [&error]() { error = "Invalid prepared material data"; return false; };
    Json::Value body;
    GameSproutRuntime::ResourceReference current = reference;
    std::vector<GameSproutRuntime::ResourceReference> visited;
    // 提取材质后，场景仍可持有原 FBX 的 GUID + LocalID；产物中的映射转到独立 .mat。
    // 限制链长度并检测循环，错误映射不会无限递归或静默显示另一份材质。
    for (;;) {
        if (visited.size() >= 32 || std::find(visited.begin(), visited.end(), current) != visited.end()) {
            error = "Material remapping contains a cycle or exceeds 32 references";
            return false;
        }
        visited.push_back(current);
        Json::Value descriptor;
        QByteArray bytes;
        if (!Read(current,eAssetType::Material,descriptor,bytes,error)) return false;
        if (descriptor["Encoding"] != "BinaryValueLE1" || !AssetValueBinary::Decode(bytes,body) || !body.isObject()) return fail();
        if (body["Format"] == "GameSproutDocument") body = Json::Value(body["Data"]);
        if (!body.isObject()) return fail();
        if (!body.isMember("ExternalMaterial")) break;
        if (!ReferenceValue(body["ExternalMaterial"], current)) return fail();
    }
    GameSproutRuntime::MaterialRenderData data;
    for (const char* name : {"EnableGPUInstancing","SpecularHighlights"})
        if (body.isMember(name) && !body[name].isBool()) return fail();
    data.EnableGPUInstancing=body.get("EnableGPUInstancing",true).asBool();
    data.SpecularHighlights=body.get("SpecularHighlights",true).asBool();
    if (body.isMember("SortingPriority")) {
        if (!body["SortingPriority"].isInt() || body["SortingPriority"].asInt()<-50 || body["SortingPriority"].asInt()>50) return fail();
        data.SortingPriority=body["SortingPriority"].asInt();
    }
    if (body.isMember("BaseColor") && !ColorValue(body["BaseColor"],data.BaseColor)) return fail();
    if (body.isMember("BaseColorTextureST")) {
        const auto& transform = body["BaseColorTextureST"];
        if (!transform.isArray() || transform.size() != 4) return fail();
        float values[4];
        for (Json::ArrayIndex index = 0; index < 4; ++index) {
            if (!transform[index].isNumeric() || !std::isfinite(transform[index].asDouble())) return fail();
            values[index] = transform[index].asFloat();
            if (!std::isfinite(values[index])) return fail();
        }
        data.BaseColorTextureST = {values[0],values[1],values[2],values[3]};
    }
    if (body.isMember("DoubleSided")) {
        if (!body["DoubleSided"].isBool()) return fail();
        data.DoubleSided = body["DoubleSided"].asBool();
    }
    if (body.isMember("BaseColorTexture") && !body["BaseColorTexture"].isNull()
        && !ReferenceValue(body["BaseColorTexture"],data.BaseColorTexture)) return fail();
    if(body.isMember("Shader")&&body["Shader"].isObject()) {
        if(!ReferenceValue(body["Shader"],data.Shader))return fail();
        auto* shader=_Library?_Library->QueryAsset(Guid(data.Shader.Guid),data.Shader.LocalID):nullptr;
        Json::Value descriptor;
        if(!shader||shader->GetAssetType()!=eAssetType::Shader||!AssetArtifactIO::Parse(shader->GetDataDescriptorJson(),descriptor)
            ||descriptor["Format"]!="GameSproutShader"||descriptor["ShaderVersion"]!=1) {error="Material references an unavailable graphics Shader bundle";return false;}
        Json::Value normalized=body;
        if(!MaterialDefinition::NormalizeValues(descriptor["Material"],normalized,error))return false;
        std::map<std::string,GameSproutRuntime::ShaderValueType> types;
        for(const auto& pass:descriptor["Passes"])for(const char* stage:{"Vertex","Fragment"})for(const auto& block:pass[stage]["Uniforms"])for(const auto& field:block["Members"]) {
            const auto name=field["Name"].asString(),type=field["Type"].asString();
            GameSproutRuntime::ShaderValueType value;
            if(type=="Float")value=GameSproutRuntime::ShaderValueType::Float;
            else if(type=="Int")value=GameSproutRuntime::ShaderValueType::Int;
            else if(type=="UInt")value=GameSproutRuntime::ShaderValueType::UInt;
            else if(type=="Float4")value=GameSproutRuntime::ShaderValueType::Float4;
            else {error="Unsupported material property type: "+type;return false;}
            const auto [found,inserted]=types.emplace(name,value);if(!inserted&&found->second!=value){error="Inconsistent material binding type: "+name;return false;}
        }
        for(const auto& [name,type]:types) {
            GameSproutRuntime::MaterialPropertyData property;property.Name=name;property.Type=type;
            const auto& value=normalized[name];
            if(type==GameSproutRuntime::ShaderValueType::Float) {
                if(!MaterialDefinition::FiniteNumber(value))return fail();property.Numbers[0]=value.asFloat();
            }else if(type==GameSproutRuntime::ShaderValueType::Float4) {
                if(!MaterialDefinition::VectorValue(value))return fail();for(unsigned i=0;i<4;++i)property.Numbers[i]=value[i].asFloat();
            }else if(type==GameSproutRuntime::ShaderValueType::Int) {
                if(value.isBool())property.Integer=value.asBool()?1:0;else if(value.isInt())property.Integer=value.asInt();else return fail();
            }else {
                if(value.isBool())property.Unsigned=value.asBool()?1:0;else if(value.isUInt())property.Unsigned=value.asUInt();else return fail();
            }
            data.Properties.push_back(std::move(property));
        }
        for(const auto& property:descriptor["Material"]["Properties"])if(property["Type"]=="Texture") {
            GameSproutRuntime::MaterialTextureData texture;texture.Name=property["Name"].asString();
            if(!normalized[texture.Name].isNull()&&!ReferenceValue(normalized[texture.Name],texture.Texture))return fail();
            data.Textures.push_back(std::move(texture));
        }
    }else if(body["Shader"]=="GameSprout/Lit") {
        Json::Value normalized=body;
        if(!MaterialDefinition::NormalizeValues(MaterialDefinition::LitSchema(),normalized,error))return false;
        data.ShadingModel=GameSproutRuntime::MaterialShadingModel::Lit;
        if(!ColorValue(normalized["BaseColor"],data.BaseColor)||!ColorValue(normalized["Emission"],data.EmissionColor))return fail();
        const auto readNumber=[&](const char* name,float& value) {
            if(!MaterialDefinition::FiniteNumber(normalized[name]))return false;
            value=normalized[name].asFloat();return value>=0;
        };
        if(!readNumber("Metallic",data.Metallic)||!readNumber("Roughness",data.Roughness)||!readNumber("NormalScale",data.NormalScale))return fail();
        if(!normalized["NormalTexture"].isNull()&&!ReferenceValue(normalized["NormalTexture"],data.NormalTexture))return fail();
        const auto& transform=normalized["NormalTextureST"];
        if(!MaterialDefinition::VectorValue(transform))return fail();
        data.NormalTextureST={transform[0].asFloat(),transform[1].asFloat(),transform[2].asFloat(),transform[3].asFloat()};
    }else if(body.isMember("Shader")&&body["Shader"]!="GameSprout/Preview")return fail();
    output = std::move(data);
    return true;
}
