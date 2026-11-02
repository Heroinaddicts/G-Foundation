#pragma once
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace GameSproutRuntime {
inline constexpr std::uint8_t DefaultLayer = 0;
inline constexpr std::uint8_t IgnoreRaycastLayer = 1;
inline constexpr std::uint8_t UILayer = 2;
inline constexpr std::uint32_t AllLayers = 0xFFFFFFFFu;
inline constexpr std::uint32_t DefaultRaycastLayers = AllLayers & ~(1u << IgnoreRaycastLayer);
struct TagDefinition {
    std::uint64_t ID = 0; // Stable project identity; never derived from a vector index or name hash.
    std::string Name;
    bool operator==(const TagDefinition&) const = default;
};
struct RuntimeProjectSettings {
    std::array<std::string,32> LayerNames;
    std::vector<TagDefinition> Tags{{0,"Untagged"}};
    std::uint64_t NextTagID = 1; // High-water mark, including deleted tags. IDs are never reused.
    std::array<std::uint32_t,32> CollisionMatrix;
    RuntimeProjectSettings() {
        LayerNames[DefaultLayer]="Default";
        LayerNames[IgnoreRaycastLayer]="Ignore Raycast";
        LayerNames[UILayer]="UI";
        CollisionMatrix.fill(AllLayers);
    }
    bool operator==(const RuntimeProjectSettings&) const = default;
};
// Shared value validation for Editor and Player shells. This header has no Qt/Editor/GRHI dependency.
inline bool ValidateProjectSettings(const RuntimeProjectSettings& value,std::string& error) {
    error.clear();
    const auto fail=[&](const char* message){error=message;return false;};
    if(value.LayerNames[0]!="Default" || value.LayerNames[1]!="Ignore Raycast" || value.LayerNames[2]!="UI")
        return fail("Built-in Layer names and indices cannot be changed");
    const auto validName=[](const std::string& name) {
        return !name.empty() && name.size()<=128 && name.find('\0')==std::string::npos &&
            name.find_first_not_of(" \t\r\n")!=std::string::npos &&
            name.find_first_of("\r\n\t")==std::string::npos && name.front()!=' ' && name.back()!=' ';
    };
    std::set<std::string> names;
    for(const auto& name:value.LayerNames)if(!name.empty()) {
        if(!validName(name))return fail("Layer names must be 1..128 bytes without NUL or surrounding whitespace");
        if(!names.insert(name).second)return fail("Layer names must be unique");
    }
    for(std::size_t a=0;a<32;++a)for(std::size_t b=0;b<32;++b)
        if(bool(value.CollisionMatrix[a]&(1u<<b))!=bool(value.CollisionMatrix[b]&(1u<<a)))
            return fail("Layer collision matrix must be symmetric");
    if(value.Tags.empty() || value.Tags.size()>65536 || !value.NextTagID)
        return fail("Project must define Untagged and a nonzero next Tag identity");
    names.clear();std::set<std::uint64_t> identities;bool untagged=false;
    for(const auto& tag:value.Tags) {
        if(!validName(tag.Name))return fail("Tag names must be 1..128 bytes without NUL or surrounding whitespace");
        if(!names.insert(tag.Name).second || !identities.insert(tag.ID).second)return fail("Tag names and identities must be unique");
        if(tag.ID>=value.NextTagID)return fail("NextTagID must be greater than every defined Tag identity");
        if(tag.ID==0) {if(tag.Name!="Untagged")return fail("Tag zero must remain Untagged");untagged=true;}
    }
    if(!untagged)return fail("Project must define Untagged with identity zero");
    return true;
}
}
