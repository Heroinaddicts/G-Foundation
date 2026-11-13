#pragma once
#include "SceneDirtyState.h"
#include "../Shared/AssetSourceIO.h"
#include <stdexcept>

// A saved row is also its dirty-comparison value. Keep only one text form,
// already indented for the Objects array, so saving does not re-encode every
// unchanged component or retain another complete JSON object tree.
namespace SceneSaveSnapshot {
inline std::string Row(const Json::Value& node) {
    std::string source=AssetSourceIO::JsonText(node),result;
    if(!source.empty()&&source.back()=='\n')source.pop_back();
    result.reserve(source.size()+128);
    result="        ";
    std::size_t first=0;
    for(std::size_t newline=source.find('\n');newline!=std::string::npos;newline=source.find('\n',first)) {
        result.append(source,first,newline-first+1);
        result+="        ";
        first=newline+1;
    }
    result.append(source,first,std::string::npos);
    return result;
}
inline const std::string& CurrentRow(const SceneDirtyState& state,std::uint64_t id) {
    const auto changed=state.ChangedNodes.find(id);
    if(changed!=state.ChangedNodes.end())return changed->second;
    const auto saved=state.BaselineNodes.find(id);
    if(saved==state.BaselineNodes.end())throw std::runtime_error("Scene save snapshot is missing an object row");
    return saved->second;
}
inline std::string Encode(const SceneDirtyState& state) {
    if(!state.Known)throw std::runtime_error("Scene save snapshot is unavailable");
    auto header=state.CurrentRoot;
    header["Objects"]=Json::Value(Json::arrayValue);
    std::string source=AssetSourceIO::JsonText(header);
    if(state.CurrentOrder.empty())return source;
    // Match the root member at exactly four spaces; annotations may themselves
    // contain a nested member named Objects with an empty array.
    const std::string marker="\n    \"Objects\" : []";
    const auto position=source.find(marker);
    if(position==std::string::npos)throw std::runtime_error("Scene source writer did not emit the Objects array");
    const std::string opening="\n    \"Objects\" : \n    [\n",closing="\n    ]";
    std::size_t size=source.size()-marker.size();
    const auto grow=[&](std::size_t bytes) {
        if(bytes>source.max_size()-size)throw std::length_error("Scene source text exceeds addressable memory");
        size+=bytes;
    };
    grow(opening.size());grow(closing.size());
    bool first=true;
    for(const auto id:state.CurrentOrder) {
        const auto& row=CurrentRow(state,id);
        if(!first)grow(2);
        grow(row.size());first=false;
    }
    std::string result;result.reserve(size);
    result.append(source,0,position);result+=opening;
    first=true;
    for(const auto id:state.CurrentOrder) {
        if(!first)result+=",\n";
        result+=CurrentRow(state,id);first=false;
    }
    result+=closing;result.append(source,position+marker.size(),std::string::npos);
    return result;
}
}
