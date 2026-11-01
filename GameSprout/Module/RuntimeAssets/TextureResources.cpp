#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
using LibraryResourceDecode::Unsigned;
using LibraryResourceDecode::FloatAt;
using LibraryResourceDecode::ColorValue;
using LibraryResourceDecode::ReferenceValue;
bool LibraryRuntimeResources::LoadTexture(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::TextureRenderData& output, std::string& error) {
    output = {};
    Json::Value descriptor;
    QByteArray bytes;
    if (!Read(reference, eAssetType::Texture, descriptor, bytes, error)) return false;
    GameSproutRuntime::TextureRenderData data;
    const auto fail = [&error]() { error = "Invalid prepared RGBA8 texture layout or sampling settings"; return false; };
    if (descriptor["Encoding"] != "RawBytes" || descriptor["Format"] != "RGBA8"
        || !Unsigned(descriptor["Width"], data.Width) || !Unsigned(descriptor["Height"], data.Height)
        || data.Width == 0 || data.Height == 0 || !descriptor["Mips"].isArray() || descriptor["Mips"].empty()) return fail();
    const Json::Value& mip = descriptor["Mips"][0];
    if (std::uint64_t(data.Width) * 4 > std::uint64_t(bytes.size())
        || data.Height > std::uint64_t(bytes.size()) / (std::uint64_t(data.Width) * 4)) return fail();
    const std::uint64_t size = std::uint64_t(data.Width) * data.Height * 4;
    if (!mip.isObject() || !mip["Offset"].isUInt64() || !mip["Size"].isUInt64() || !mip["RowPitch"].isUInt64()
        || mip["Offset"].asUInt64() != 0 || mip["Size"].asUInt64() != size
        || mip["RowPitch"].asUInt64() != std::uint64_t(data.Width)*4 || size > std::uint64_t(bytes.size())
        || mip["Width"] != descriptor["Width"] || mip["Height"] != descriptor["Height"]) return fail();
    const Json::Value& settings = descriptor["Settings"];
    if (!settings.isObject()) return fail();
    for (const char* key : {"ColorSpace","FilterMode","WrapU","WrapV"})
        if (settings.isMember(key) && !settings[key].isString()) return fail();
    const std::string colorSpace = settings.get("ColorSpace","SRGB").asString();
    if (colorSpace != "SRGB" && colorSpace != "Linear") return fail();
    data.SRGB = colorSpace == "SRGB";
    const std::string filter = settings.get("FilterMode","Bilinear").asString();
    if (filter != "Point" && filter != "Bilinear" && filter != "Trilinear") return fail();
    data.Sampler.MinFilter = data.Sampler.MagFilter = filter == "Point" ? GameSproutRuntime::TextureFilter::Nearest : GameSproutRuntime::TextureFilter::Linear;
    const auto wrap = [](const std::string& value, GameSproutRuntime::TextureAddressMode& mode) {
        if (value == "Repeat") mode = GameSproutRuntime::TextureAddressMode::Repeat;
        else if (value == "ClampToEdge") mode = GameSproutRuntime::TextureAddressMode::ClampToEdge;
        else if (value == "MirrorRepeat") mode = GameSproutRuntime::TextureAddressMode::MirroredRepeat;
        else return false;
        return true;
    };
    if (!wrap(settings.get("WrapU","Repeat").asString(),data.Sampler.AddressU)
        || !wrap(settings.get("WrapV","Repeat").asString(),data.Sampler.AddressV)) return fail();
    data.Pixels.assign(reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData()) + size);
    output = std::move(data);
    return true;
}
