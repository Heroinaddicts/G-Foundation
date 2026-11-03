#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Current .mesh authoring format. All integers and IEEE754 float32 values are
// little endian; geometry is already Unity left handed, Y up, +Z forward, in metres. The source
// header is removed during import; Runtime receives only interleaved GPU bytes.
namespace NativeMeshDefinition {
inline constexpr std::array<char,8> Magic{'G','S','M','E','S','H','0','2'};
inline constexpr std::uint32_t Version=2;
inline constexpr std::size_t HeaderBytes=32;
inline constexpr std::size_t MaximumNameBytes=1024;
inline constexpr std::uint64_t MaximumSourceBytes=512ull*1024*1024;
inline constexpr std::uint32_t HasNormals=1u;
inline constexpr std::uint32_t HasTangents=2u;
inline constexpr std::uint32_t HasUV0=4u;
inline constexpr std::uint32_t HasColors=8u;
inline constexpr std::uint32_t ValidFlags=15u;
// Bytes 8..31: uint32 Version,Flags,VertexCount,IndexCount,SubMeshCount,NameBytes.
// Followed by UTF8 name (1..1024 bytes, no NUL), padded to 4 bytes with zeroes.
// Planar arrays: Position float3 always, Normal float3, Tangent float4, UV0
// float2, Color float4 when their flag is set. Each array has VertexCount items.
// Then IndexCount uint32 indices; then SubMeshCount uint32 triples
// {FirstIndex,IndexCount,MaterialSlot}. Sections partition the index array in
// order without gaps/overlaps; triangle topology only. No trailing bytes.
}
