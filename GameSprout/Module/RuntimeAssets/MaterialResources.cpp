#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
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
    output = std::move(data);
    return true;
}
