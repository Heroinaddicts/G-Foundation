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
    // Canonical row text retains exact saved-value comparison without keeping
    // a second allocation-heavy Json object tree for every component field.
    std::map<std::uint64_t,std::string> BaselineNodes;
    std::vector<std::uint64_t> BaselineOrder;
    std::set<std::uint64_t> DifferentNodes;
    std::map<std::uint64_t,std::string> Errors;
};
