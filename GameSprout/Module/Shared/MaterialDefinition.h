#pragma once

#include "AssetArtifactIO.h"
#include "AssetValueBinary.h"
#include "ILibraryManager.h"
#include <cctype>
#include <cmath>
#include <limits>
#include <set>

// Authoring metadata is deliberately separate from GPU binding reflection.
// Values live in the .mat document's root; the schema lives with its Shader.
namespace MaterialDefinition {
inline constexpr qint64 MaxDocumentBytes=16*1024*1024;
inline Json::Value Vector(double x, double y, double z, double w) {
    Json::Value value(Json::arrayValue);
    for (double component : {x,y,z,w}) value.append(component);
    return value;
}
inline bool Identifier(const std::string & name) {
    if (name.empty() || name.size()>128) return false;
    const auto alpha=[](unsigned char c) { return (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_'; };
    if (!alpha(static_cast<unsigned char>(name.front()))) return false;
    for (unsigned char c : name) if (!alpha(c) && !(c>='0' && c<='9')) return false;
    return true;
}
inline bool FiniteNumber(const Json::Value & value) {
    return value.isNumeric() && std::isfinite(value.asDouble())
        && std::abs(value.asDouble())<=static_cast<double>(std::numeric_limits<float>::max());
}
inline bool VectorValue(const Json::Value & value) {
    if (!value.isArray() || value.size()!=4) return false;
    for (const auto & component : value) if (!FiniteNumber(component)) return false;
    return true;
}
inline bool Reference(const Json::Value & value) {
    return value.isObject() && value["Guid"].isString() && Guid(value["Guid"].asString()).IsValid()
        && value["LocalID"].isUInt64() && value["LocalID"].asUInt64()!=0;
}
inline bool ShaderReference(const Json::Value & document, std::string & error) {
    error.clear();
    if (!document.isObject()) { error="Material must be an object"; return false; }
    if (!document.isMember("Shader") || (document["Shader"].isString() && document["Shader"].asString()=="GameSprout/Preview")
        || Reference(document["Shader"])) return true;
    error="Shader must be GameSprout/Preview or a valid Shader GUID + LocalID reference";
    return false;
}
inline bool PropertyValue(const Json::Value & property, const Json::Value & value) {
    const std::string type=property["Type"].asString();
    if (type=="Color" || type=="Vector") return VectorValue(value);
    if (type=="Texture") return value.isNull() || Reference(value);
    if (type=="Toggle") return value.isBool();
    if (type=="Int") return value.isInt();
    if (type=="Enum") return value.isUInt() && value.asUInt()<property["Options"].size();
    if (!FiniteNumber(value)) return false;
    if (type=="Range") return value.asDouble()>=property["Min"].asDouble() && value.asDouble()<=property["Max"].asDouble();
    return type=="Float";
}
inline bool ValidateSchema(const Json::Value & schema, std::string & error) {
    error.clear();
    if (!schema.isObject() || !schema["Name"].isString() || schema["Name"].asString().empty()
        || !schema["Properties"].isArray() || schema["Properties"].size()>512) {
        error="Material schema requires Name and a Properties array (at most 512 entries)"; return false;
    }
    const std::set<std::string> types{"Float","Int","Range","Color","Vector","Texture","Toggle","Enum"};
    const std::set<std::string> reserved{"Version","Name","Shader","ExternalMaterial","Textures","UnresolvedTextures"};
    std::set<std::string> names;
    std::set<std::string> textureTransforms;
    for (const auto & property : schema["Properties"]) {
        if (!property.isObject() || !property["Name"].isString() || !Identifier(property["Name"].asString())
            || reserved.count(property["Name"].asString()) || !names.insert(property["Name"].asString()).second
            || !property["Type"].isString() || !types.count(property["Type"].asString())
            || !property["DisplayName"].isString() || property["DisplayName"].asString().empty()
            || !property.isMember("Default")) {
            error="Material property requires a unique identifier, DisplayName, supported Type and Default"; return false;
        }
        const std::string name=property["Name"].asString();
        const std::string type=property["Type"].asString();
        for (const char * key : {"Group","Tooltip"}) if (property.isMember(key) && !property[key].isString()) {
            error="Material property "+name+": "+key+" must be text"; return false;
        }
        for (const char * key : {"Hidden","HDR"}) if (property.isMember(key) && !property[key].isBool()) {
            error="Material property "+name+": "+key+" must be boolean"; return false;
        }
        if (type=="Range" && (!FiniteNumber(property["Min"]) || !FiniteNumber(property["Max"])
            || property["Min"].asDouble()>property["Max"].asDouble())) {
            error="Material Range "+name+" requires finite Min <= Max"; return false;
        }
        if (type=="Enum") {
            if (!property["Options"].isArray() || property["Options"].empty() || property["Options"].size()>256) {
                error="Material Enum "+name+" requires a nonempty Options array"; return false;
            }
            std::set<std::string> options;
            for (const auto & option : property["Options"]) if (!option.isString() || option.asString().empty()
                || !options.insert(option.asString()).second) {
                error="Material Enum "+name+" options must be unique text labels"; return false;
            }
        }
        if (!PropertyValue(property,property["Default"])) {
            error="Invalid default value for material property "+name; return false;
        }
        if (type=="Texture") textureTransforms.insert(name+"ST");
    }
    for (const auto & name : textureTransforms) if (names.count(name)) {
        error="Material texture transform "+name+" is implicit and cannot also be declared as a property"; return false;
    }
    return true;
}
inline Json::Value PreviewSchema() {
    Json::Value schema(Json::objectValue);
    schema["Name"]="GameSprout/Preview";
    schema["Properties"]=Json::Value(Json::arrayValue);
    const auto add=[&](const char * name,const char * display,const char * type,const Json::Value & value) {
        Json::Value property;
        property["Name"]=name; property["DisplayName"]=display; property["Type"]=type; property["Default"]=value;
        if (std::string(type)=="Range") { property["Min"]=0.0; property["Max"]=1.0; }
        schema["Properties"].append(property);
    };
    add("BaseColor","基础颜色","Color",Vector(1,1,1,1));
    add("BaseColorTexture","基础贴图","Texture",Json::Value());
    add("Metallic","金属度","Range",0.0);
    add("Roughness","粗糙度","Range",0.5);
    add("DoubleSided","双面渲染","Toggle",false);
    return schema;
}

// .shader remains valid HLSL: the declaration is a block comment ignored by DXC.
// Scan comments rather than searching arbitrary text, so strings/line comments
// mentioning the marker cannot accidentally replace the authoring schema.
inline bool ParseShaderSchema(const QByteArray & source, Json::Value & schema, std::string & error) {
    error.clear();
    Json::Value result;
    result["Name"]="Shader"; result["Properties"]=Json::Value(Json::arrayValue);
    bool found=false;
    const std::string text=source.toStdString();
    for (size_t i=0; i+1<text.size();) {
        if (text[i]=='\"' || text[i]=='\'') {
            const char quote=text[i++];
            while (i<text.size()) { if (text[i]=='\\') { i+=2; continue; } if (text[i++]==quote) break; }
            continue;
        }
        if (text[i]=='/' && text[i+1]=='/') {
            const size_t end=text.find('\n',i+2); i=end==std::string::npos ? text.size() : end+1; continue;
        }
        if (text[i]!='/' || text[i+1]!='*') { ++i; continue; }
        const size_t start=i+2;
        const size_t end=text.find("*/",start);
        const std::string comment=text.substr(start,end==std::string::npos ? text.size()-start : end-start);
        size_t offset=0;
        while (offset<comment.size() && std::isspace(static_cast<unsigned char>(comment[offset]))) ++offset;
        const std::string marker="GameSproutMaterial";
        if (comment.compare(offset,marker.size(),marker)==0) {
            offset+=marker.size();
            if (offset==comment.size() || std::isspace(static_cast<unsigned char>(comment[offset])) || comment[offset]=='{') {
                if (end==std::string::npos || found || comment.size()>256*1024) {
                    error="Shader requires one terminated GameSproutMaterial block, at most 256 KiB"; return false;
                }
                if (!AssetArtifactIO::Parse(comment.substr(offset),result) || !ValidateSchema(result,error)) {
                    if (error.empty()) error="Invalid JSON in GameSproutMaterial block";
                    return false;
                }
                found=true;
            }
        }
        i=end==std::string::npos ? text.size() : end+2;
    }
    schema=std::move(result);
    return true;
}
inline bool ValidateValues(const Json::Value & schema, const Json::Value & document, std::string & error) {
    if (!ValidateSchema(schema,error) || !ShaderReference(document,error)) return false;
    for (const auto & property : schema["Properties"]) {
        const std::string name=property["Name"].asString();
        if (document.isMember(name) && !PropertyValue(property,document[name])) {
            error="Invalid material value for "+name; return false;
        }
        if (property["Type"]=="Texture" && document.isMember(name+"ST") && !VectorValue(document[name+"ST"])) {
            error="Texture transform "+name+"ST must contain four finite numbers"; return false;
        }
    }
    return true;
}
inline bool NormalizeValues(const Json::Value & schema, Json::Value & document, std::string & error) {
    if (!ValidateValues(schema,document,error)) return false;
    Json::Value normalized=document;
    if (!normalized.isMember("Shader")) normalized["Shader"]="GameSprout/Preview";
    for (const auto & property : schema["Properties"]) {
        const std::string name=property["Name"].asString();
        if (!normalized.isMember(name)) normalized[name]=property["Default"];
        if (property["Type"]=="Texture" && !normalized.isMember(name+"ST")) normalized[name+"ST"]=Vector(1,1,0,0);
    }
    document=std::move(normalized);
    return true;
}

// Editor main-thread helpers. Workers instead resolve the Shader through their
// immutable source-path snapshot and read its HLSL using AssetImportJob.
inline bool ResolveSchema(ILibraryManager * library, const Json::Value & document, Json::Value & schema, std::string & error) {
    error.clear();
    if (!ShaderReference(document,error)) return false;
    if (!document.isMember("Shader") || document["Shader"].isString()) { schema=PreviewSchema(); return true; }
    const Json::Value & reference=document["Shader"];
    IAsset * shader=library ? library->QueryAsset(Guid(reference["Guid"].asString()),reference["LocalID"].asUInt64()) : nullptr;
    if (!shader || shader->GetAssetType()!=eAssetType::Shader) { error="Material Shader is missing or is not a graphics Shader"; return false; }
    Json::Value descriptor;
    if (!AssetArtifactIO::Parse(shader->GetDataDescriptorJson(),descriptor) || !descriptor.isObject()
        || !ValidateSchema(descriptor["Material"],error)) {
        if (error.empty()) error="Shader has no valid material property descriptor";
        return false;
    }
    schema=descriptor["Material"];
    return true;
}
inline bool ReadPreparedMaterial(ILibraryManager * library, const Guid & guid, uint64_t localID,
    Json::Value & document, std::string & error) {
    error.clear();
    IAsset * asset=library ? library->QueryAsset(guid,localID) : nullptr;
    if (!asset || asset->GetAssetType()!=eAssetType::Material) { error="Material resource does not exist"; return false; }
    if (asset->GetDataVersion()!=2 || asset->GetDataSize()>static_cast<uint64_t>(MaxDocumentBytes)) {
        error="Material prepared data version or size is unsupported"; return false;
    }
    Json::Value descriptor,body;
    if (!AssetArtifactIO::Parse(asset->GetDataDescriptorJson(),descriptor) || !descriptor.isObject()
        || descriptor["Encoding"]!="BinaryValueLE1") {
        error="Material has an invalid prepared data descriptor"; return false;
    }
    QFile file(QString::fromStdString(asset->GetArtifactPath()));
    if (!file.open(QIODevice::ReadOnly)) { error=file.errorString().toStdString(); return false; }
    if (file.size()<0 || file.size()>MaxDocumentBytes || static_cast<uint64_t>(file.size())!=asset->GetDataSize()) {
        error="Material prepared file size does not match its descriptor"; return false;
    }
    const QByteArray bytes=file.read(MaxDocumentBytes+1);
    if (file.error()!=QFileDevice::NoError || !file.atEnd() || bytes.size()>MaxDocumentBytes
        || static_cast<uint64_t>(bytes.size())!=asset->GetDataSize()
        || !AssetValueBinary::Decode(bytes,body) || !body.isObject()) {
        error="Cannot read prepared material values"; return false;
    }
    if (body["Format"]=="GameSproutDocument") body=Json::Value(body["Data"]);
    if (!body.isObject()) { error="Prepared material body must be an object"; return false; }
    document=std::move(body);
    return true;
}
}
