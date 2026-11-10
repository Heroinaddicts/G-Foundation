#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"

bool LibraryRuntimeResources::LoadFont(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::FontRenderData& output,std::string& error) {
    output={};
    Json::Value descriptor;
    QByteArray bytes;
    if(!Read(reference,eAssetType::Font,descriptor,bytes,error))return false;
    const auto fail=[&error] {error="Invalid prepared font descriptor or glyph";return false;};
    const auto number=[](const Json::Value& source,float& value) {
        if(!source.isNumeric())return false;
        value=source.asFloat();
        return std::isfinite(value);
    };
    GameSproutRuntime::FontRenderData data;
    const auto& glyphs=descriptor["Glyphs"];
    Json::Value atlas;
    atlas["Guid"]=descriptor["AtlasGuid"];
    atlas["LocalID"]=descriptor["AtlasLocalID"];
    if(descriptor["Encoding"]!="RawBytes"||!glyphs.isArray()||glyphs.empty()||glyphs.size()>4096||
       !LibraryResourceDecode::ReferenceValue(atlas,data.Atlas)||
       !number(descriptor["PixelSize"],data.PixelSize)||data.PixelSize<=0||
       !number(descriptor["Ascent"],data.Ascent)||!number(descriptor["Descent"],data.Descent))return fail();
    data.Glyphs.reserve(glyphs.size());
    for(const auto& source:glyphs) {
        GameSproutRuntime::FontGlyph glyph;
        if(!source.isObject()||!LibraryResourceDecode::Unsigned(source["Codepoint"],glyph.Codepoint)||
           !LibraryResourceDecode::Unsigned(source["X"],glyph.X)||!LibraryResourceDecode::Unsigned(source["Y"],glyph.Y)||
           !LibraryResourceDecode::Unsigned(source["Width"],glyph.Width)||!LibraryResourceDecode::Unsigned(source["Height"],glyph.Height)||
           !number(source["BearingX"],glyph.BearingX)||!number(source["BearingY"],glyph.BearingY)||
           !number(source["AdvanceX"],glyph.AdvanceX))return fail();
        data.Glyphs.push_back(glyph);
    }
    ResetDependencies(reference);
    AddDependency(reference,data.Atlas);
    output=std::move(data);
    return true;
}
