#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
using LibraryResourceDecode::Unsigned;
namespace {
using GameSproutRuntime::TexturePrefetchSource;
bool DecodeTexture(const Json::Value& descriptor,std::uint64_t fileSize,TexturePrefetchSource& data) {
    if (descriptor["Encoding"] != "RawBytes" || descriptor["Format"] != "RGBA8"
        || !Unsigned(descriptor["Width"], data.Width) || !Unsigned(descriptor["Height"], data.Height)
        || data.Width == 0 || data.Height == 0 || !descriptor["Mips"].isArray() || descriptor["Mips"].empty()) return false;
    const Json::Value& mip = descriptor["Mips"][0];
    if (std::uint64_t(data.Width) * 4 > fileSize
        || data.Height > fileSize / (std::uint64_t(data.Width) * 4)) return false;
    const std::uint64_t size = std::uint64_t(data.Width) * data.Height * 4;
    if (!mip.isObject() || !mip["Offset"].isUInt64() || !mip["Size"].isUInt64() || !mip["RowPitch"].isUInt64()
        || mip["Offset"].asUInt64() != 0 || mip["Size"].asUInt64() != size
        || mip["RowPitch"].asUInt64() != std::uint64_t(data.Width)*4 || size > fileSize
        || mip["Width"] != descriptor["Width"] || mip["Height"] != descriptor["Height"]) return false;
    const Json::Value& settings = descriptor["Settings"];
    if (!settings.isObject()) return false;
    for (const char* key : {"ColorSpace","FilterMode","WrapU","WrapV"})
        if (settings.isMember(key) && !settings[key].isString()) return false;
    const std::string colorSpace = settings.get("ColorSpace","SRGB").asString();
    if (colorSpace != "SRGB" && colorSpace != "Linear") return false;
    data.SRGB = colorSpace == "SRGB";
    const std::string filter = settings.get("FilterMode","Bilinear").asString();
    if (filter != "Point" && filter != "Bilinear" && filter != "Trilinear") return false;
    data.Sampler.MinFilter = data.Sampler.MagFilter = filter == "Point" ? GameSproutRuntime::TextureFilter::Nearest : GameSproutRuntime::TextureFilter::Linear;
    const auto wrap = [](const std::string& value, GameSproutRuntime::TextureAddressMode& mode) {
        if (value == "Repeat") mode = GameSproutRuntime::TextureAddressMode::Repeat;
        else if (value == "ClampToEdge") mode = GameSproutRuntime::TextureAddressMode::ClampToEdge;
        else if (value == "MirrorRepeat") mode = GameSproutRuntime::TextureAddressMode::MirroredRepeat;
        else return false;
        return true;
    };
    if (!wrap(settings.get("WrapU","Repeat").asString(),data.Sampler.AddressU)
        || !wrap(settings.get("WrapV","Repeat").asString(),data.Sampler.AddressV)) return false;
    data.ArtifactSize=fileSize;
    return true;
}
}

bool LibraryRuntimeResources::ResolveTexturePrefetch(const GameSproutRuntime::ResourceReference& reference,
    TexturePrefetchSource& output) {
    output={};
    Json::Value descriptor;
    QFile file;
    ResourceState* observed=nullptr;
    std::string error;
    if(!OpenArtifact(reference,eAssetType::Texture,descriptor,file,observed,error))return false;
    TexturePrefetchSource source;
    if(!DecodeTexture(descriptor,static_cast<std::uint64_t>(file.size()),source))return false;
    source.ArtifactPath=file.fileName().toStdString();
    output=std::move(source);
    return true;
}

bool LibraryRuntimeResources::LoadTexture(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::TextureRenderData& output,std::string& error) {
    output={};
    Json::Value descriptor;
    QFile file;
    ResourceState* observed=nullptr;
    if(!OpenArtifact(reference,eAssetType::Texture,descriptor,file,observed,error))return false;
    TexturePrefetchSource source;
    if(!DecodeTexture(descriptor,static_cast<std::uint64_t>(file.size()),source)) {
        error="Invalid prepared RGBA8 texture layout or sampling settings";
        return false;
    }
    GameSproutRuntime::TextureRenderData data;
    data.Width=source.Width;data.Height=source.Height;data.SRGB=source.SRGB;data.Sampler=source.Sampler;
    data.Pixels.resize(std::uint64_t(data.Width)*data.Height*4);
    const auto read=file.read(reinterpret_cast<char*>(data.Pixels.data()),static_cast<qint64>(data.Pixels.size()));
    if(read!=static_cast<qint64>(data.Pixels.size())) {
        if(observed)observed->ReadFailed=true;
        error="Incomplete prepared texture";
        return false;
    }
    if(observed)observed->ReadFailed=false;
    output=std::move(data);
    return true;
}
