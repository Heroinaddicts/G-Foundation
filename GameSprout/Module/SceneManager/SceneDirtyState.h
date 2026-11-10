#pragma once
#include "JsonCpp/json.hpp"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// Saved authored values, indexed by document identity rather than Runtime
// addresses. A changed node is compared to its own saved row, including Undo.
struct SceneDirtyState {
    std::uint64_t SceneID=0,Revision=0,NotifiedRevision=0;
    bool Known=false,RootDifferent=false,OrderDifferent=false;
    bool NotifiedDirty=false;
    std::string FullError;
    Json::Value BaselineRoot;
    // The formatted source row is shared by exact dirty comparison and saving.
    // Only changed rows need another value; unchanged rows are never re-encoded.
    std::map<std::uint64_t,std::string> BaselineNodes;
    std::vector<std::uint64_t> BaselineOrder;
    Json::Value CurrentRoot;
    std::vector<std::uint64_t> CurrentOrder;
    std::map<std::uint64_t,std::string> ChangedNodes;
    std::set<std::uint64_t> DifferentNodes;
    std::set<std::uint64_t> AnimationOwners;
    std::map<std::uint64_t,std::string> Errors;
};
