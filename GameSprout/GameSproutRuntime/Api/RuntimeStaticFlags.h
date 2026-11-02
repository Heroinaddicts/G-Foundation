#pragma once
#include <cstdint>

namespace GameSproutRuntime {
// Authored declarations for separate precomputation systems. These values do
// not freeze Transform, disable scripts, or select a physics motion type.
// No static optimization consumer is implemented in the current Runtime.
enum class StaticFlags : std::uint32_t {
    None = 0,
    ContributeGI = 1u << 0,
    OccluderStatic = 1u << 1,
    BatchingStatic = 1u << 2,
    NavigationStatic = 1u << 3,
    OccludeeStatic = 1u << 4,
    OffMeshLinkGeneration = 1u << 5,
    ReflectionProbeStatic = 1u << 6,
    Everything = (1u << 7) - 1u
};
constexpr StaticFlags operator|(StaticFlags a,StaticFlags b) noexcept {
    return static_cast<StaticFlags>(static_cast<std::uint32_t>(a)|static_cast<std::uint32_t>(b));
}
constexpr StaticFlags operator&(StaticFlags a,StaticFlags b) noexcept {
    return static_cast<StaticFlags>(static_cast<std::uint32_t>(a)&static_cast<std::uint32_t>(b));
}
constexpr StaticFlags operator^(StaticFlags a,StaticFlags b) noexcept {
    return static_cast<StaticFlags>(static_cast<std::uint32_t>(a)^static_cast<std::uint32_t>(b));
}
// Complement only the defined seven bits. It never invents reserved flags.
constexpr StaticFlags operator~(StaticFlags value) noexcept {
    return static_cast<StaticFlags>((~static_cast<std::uint32_t>(value))&static_cast<std::uint32_t>(StaticFlags::Everything));
}
constexpr StaticFlags& operator|=(StaticFlags& a,StaticFlags b) noexcept {return a=a|b;}
constexpr StaticFlags& operator&=(StaticFlags& a,StaticFlags b) noexcept {return a=a&b;}
constexpr StaticFlags& operator^=(StaticFlags& a,StaticFlags b) noexcept {return a=a^b;}
constexpr bool IsValidStaticFlags(StaticFlags value) noexcept {
    return (static_cast<std::uint32_t>(value)&~static_cast<std::uint32_t>(StaticFlags::Everything))==0;
}
}
