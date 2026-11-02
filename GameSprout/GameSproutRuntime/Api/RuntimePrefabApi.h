#pragma once
#include <cstdint>
#include <vector>

namespace GameSproutRuntime {
// Prepared BinaryValueLE1 wrapper: Format=GameSproutPrefab, Data=object document.
// The SDK deliberately exposes bytes, never authoring JSON or editor types.
struct PrefabData { std::vector<std::uint8_t> Bytes; };
struct PrefabObjectMapping {
    std::uint64_t SourceID = 0;
    std::uint64_t ObjectID = 0; // InstanceID in the target Runtime.
};
}
