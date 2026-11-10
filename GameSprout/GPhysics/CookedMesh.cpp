#include "MeshCooking.h"

#include <Jolt/AABBTree/NodeCodec/NodeCodecQuadTreeHalfFloat.h>
#include <Jolt/AABBTree/TriangleCodec/TriangleCodecIndexed8BitPackSOA4Flags.h>
#include <Jolt/Core/StreamIn.h>
#include <Jolt/Core/StreamOut.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace GPhysics {
namespace {
constexpr std::array<std::uint8_t, 8> Magic{'G', 'S', 'J', 'O', 'L', 'T', 0, 1};
constexpr std::size_t HeaderSize = Magic.size() + sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
constexpr std::uint64_t HashStart = 14695981039346656037ull;
constexpr std::uint64_t HashByte(std::uint64_t hash, std::uint8_t byte) {
    return (hash ^ byte) * 1099511628211ull;
}

#if defined(JPH_CPU_ARM)
constexpr std::string_view Architecture = "ARM";
#elif defined(JPH_CPU_X86)
constexpr std::string_view Architecture = "X86";
#elif defined(JPH_CPU_RISCV)
constexpr std::string_view Architecture = "RISCV";
#elif defined(JPH_CPU_PPC)
constexpr std::string_view Architecture = "PPC";
#elif defined(JPH_CPU_LOONGARCH)
constexpr std::string_view Architecture = "LoongArch";
#elif defined(JPH_CPU_E2K)
constexpr std::string_view Architecture = "E2K";
#else
constexpr std::string_view Architecture = "WASM";
#endif

constexpr std::uint64_t Format() {
    using JPH::uint64; // Used by the upstream JPH_VERSION_ID macro.
    std::uint64_t hash = HashStart;
    for (const auto byte : Magic)
        hash = HashByte(hash, byte);
    for (const auto part : {std::string_view(GPHYSICS_JOLT_REVISION), std::string_view(GPHYSICS_TARGET_SYSTEM),
                            std::string_view(GPHYSICS_COMPILER_ID), Architecture})
        for (const char byte : part)
            hash = HashByte(hash, static_cast<std::uint8_t>(byte));
    for (auto version = JPH_VERSION_ID; version != 0; version >>= 8)
        hash = HashByte(hash, static_cast<std::uint8_t>(version));
    hash = HashByte(hash, JPH_CPU_ARCH_BITS);
    return HashByte(hash, std::endian::native == std::endian::little ? 1 : 2);
}

std::uint64_t PayloadHash(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = HashStart;
    for (const auto byte : bytes)
        hash = HashByte(hash, byte);
    return hash;
}

struct BackendLease {
    BackendLease() { Detail::AcquireMeshBackend(); }
    ~BackendLease() { Detail::ReleaseMeshBackend(); }
    BackendLease(const BackendLease &) = delete;
    BackendLease &operator=(const BackendLease &) = delete;
};

class ArtifactWriter final : public JPH::StreamOut {
  public:
    explicit ArtifactWriter(std::vector<std::uint8_t> &bytes) : _Bytes(bytes) {}
    void WriteBytes(const void *data, std::size_t size) override {
        if (size) {
            const auto *first = static_cast<const std::uint8_t *>(data);
            _Bytes.insert(_Bytes.end(), first, first + size);
        }
    }
    bool IsFailed() const override { return false; }

  private:
    std::vector<std::uint8_t> &_Bytes;
};

class ArtifactReader final : public JPH::StreamIn {
  public:
    explicit ArtifactReader(std::span<const std::uint8_t> bytes) : _Remaining(bytes) {}
    void ReadBytes(void *data, std::size_t size) override {
        if (!size)
            return;
        if (_Failed || size > _Remaining.size()) {
            // Some Jolt math reads consume the output before checking failure.
            std::memset(data, 0, size);
            _Failed = true;
            return;
        }
        std::memcpy(data, _Remaining.data(), size);
        _Remaining = _Remaining.subspan(size);
    }
    bool IsEOF() const override { return _Failed; }
    bool IsFailed() const override { return _Failed; }
    bool IsComplete() const { return !_Failed && _Remaining.empty(); }

  private:
    std::span<const std::uint8_t> _Remaining;
    bool _Failed = false;
};

template <typename T>
bool ReadValue(std::span<const std::uint8_t> &bytes, T &value) {
    if (bytes.size() < sizeof(T))
        return false;
    std::memcpy(&value, bytes.data(), sizeof(T));
    bytes = bytes.subspan(sizeof(T));
    return true;
}

bool SkipArray(std::span<const std::uint8_t> &bytes, std::size_t elementSize, std::uint32_t &count) {
    if (!ReadValue(bytes, count) || count > bytes.size() / elementSize)
        return false;
    bytes = bytes.subspan(static_cast<std::size_t>(count) * elementSize);
    return true;
}

// Jolt allocates its arrays before attempting to read their contents. Validate
// each stored count against the remaining bytes in the pinned leaf layout.
bool ValidPayload(std::span<const std::uint8_t> bytes, bool convex) {
    JPH::EShapeSubType type{};
    std::uint64_t userData = 0;
    if (!ReadValue(bytes, type) || type != (convex ? JPH::EShapeSubType::ConvexHull : JPH::EShapeSubType::Mesh) ||
        !ReadValue(bytes, userData) || userData != 0)
        return false;

    if (!convex) {
        constexpr auto treeHeaders = JPH::NodeCodecQuadTreeHalfFloat::HeaderSize +
                                     sizeof(JPH::TriangleCodecIndexed8BitPackSOA4Flags::TriangleHeader);
        std::uint32_t treeBytes = 0;
        return ReadValue(bytes, treeBytes) && treeBytes == bytes.size() && treeBytes > treeHeaders;
    }

    // Density, center of mass, inertia, then minimum and maximum bounds.
    // Vec3 values are written as XYZ, while Mat44 writes all 16 floats.
    constexpr std::size_t hullPrefix = (1 + 3 + 16 + 3 + 3) * sizeof(float);
    constexpr std::size_t pointSize = 32, faceSize = 4; // ConvexHullShape private array element sizes.
    if (bytes.size() < hullPrefix)
        return false;
    bytes = bytes.subspan(hullPrefix);
    std::uint32_t points = 0, faces = 0, planes = 0, indices = 0;
    return SkipArray(bytes, pointSize, points) && points > 0 && points <= JPH::ConvexHullShape::cMaxPointsInHull &&
           SkipArray(bytes, faceSize, faces) && faces > 0 &&
           SkipArray(bytes, sizeof(JPH::Plane), planes) && planes == faces &&
           SkipArray(bytes, sizeof(std::uint8_t), indices) && indices > 0 &&
           bytes.size() == 3 * sizeof(float); // Convex radius, volume, inner radius.
}
} // namespace

struct CookedMesh::Impl {
    BackendLease Lease;
    JPH::RefConst<JPH::Shape> Shape;
};

std::uint64_t MeshArtifactFormat() { return Format(); }

bool CookMeshArtifact(const MeshGeometry &mesh, bool convex, std::vector<std::uint8_t> &output, std::string &error) {
    error.clear();
    const BackendLease lease;
    const auto cooked = Detail::CreateMeshShape(mesh, convex, 0);
    if (cooked.HasError()) {
        error = cooked.GetError().c_str();
        return false;
    }
    std::vector<std::uint8_t> bytes(HeaderSize);
    ArtifactWriter writer(bytes);
    // These leaf shapes use Jolt's default material and have no children.
    // Game materials and collider IDs belong to the body and aren't serialized here.
    cooked.Get()->SaveBinaryState(writer);
    const std::span<const std::uint8_t> payload(bytes.data() + HeaderSize, bytes.size() - HeaderSize);
    if (payload.size() > std::numeric_limits<std::uint32_t>::max() || !ValidPayload(payload, convex)) {
        error = "Invalid Jolt mesh artifact payload";
        return false;
    }
    const std::uint64_t format = Format(), hash = PayloadHash(payload);
    const auto size = static_cast<std::uint32_t>(payload.size());
    auto *header = bytes.data();
    std::memcpy(header, Magic.data(), Magic.size());
    header += Magic.size();
    std::memcpy(header, &format, sizeof(format));
    header += sizeof(format);
    std::memcpy(header, &size, sizeof(size));
    header += sizeof(size);
    std::memcpy(header, &hash, sizeof(hash));
    output = std::move(bytes);
    return true;
}

CookedMesh::~CookedMesh() { delete _Impl; }
CookedMesh::CookedMesh(CookedMesh &&other) noexcept : _Impl(std::exchange(other._Impl, nullptr)) {}
CookedMesh &CookedMesh::operator=(CookedMesh &&other) noexcept {
    if (this != &other) {
        delete _Impl;
        _Impl = std::exchange(other._Impl, nullptr);
    }
    return *this;
}

bool CookedMesh::Restore(std::span<const std::uint8_t> bytes, bool convex, std::string &error) {
    error.clear();
    if (bytes.size() < HeaderSize || !std::equal(Magic.begin(), Magic.end(), bytes.begin())) {
        error = "Invalid Jolt mesh artifact header";
        return false;
    }
    bytes = bytes.subspan(Magic.size());
    std::uint64_t format = 0, hash = 0;
    std::uint32_t size = 0;
    if (!ReadValue(bytes, format) || format != Format() || !ReadValue(bytes, size) ||
        !ReadValue(bytes, hash) || size != bytes.size()) {
        error = "Jolt mesh artifact format or size does not match";
        return false;
    }
    if (!ValidPayload(bytes, convex) || hash != PayloadHash(bytes)) {
        error = "Invalid Jolt mesh artifact payload";
        return false;
    }
    const BackendLease lease;
    ArtifactReader reader(bytes);
    auto restored = JPH::Shape::sRestoreFromBinaryState(reader);
    if (restored.HasError() || !reader.IsComplete()) {
        error = "Cannot restore Jolt mesh artifact";
        return false;
    }
    auto *next = new Impl{{}, restored.Get()};
    delete std::exchange(_Impl, next);
    return true;
}

const void *CookedMesh::NativeShape() const { return _Impl ? _Impl->Shape.GetPtr() : nullptr; }

bool CookedMesh::SupportsScale(Vector3 scale) const {
    if (!_Impl || !std::isfinite(scale.X) || !std::isfinite(scale.Y) || !std::isfinite(scale.Z) ||
        !_Impl->Shape->IsValidScale(JPH::Vec3(scale.X, scale.Y, scale.Z)))
        return false;
    if (_Impl->Shape->GetSubType() == JPH::EShapeSubType::ConvexHull)
        return true;

    // Triangle active-edge flags are baked in local space. An anisotropic
    // transform may change them, and a reflection changes ray-facing winding.
    const bool reflected = ((scale.X < 0) != (scale.Y < 0)) != (scale.Z < 0);
    const auto [smallest, largest] = std::minmax({std::abs(scale.X), std::abs(scale.Y), std::abs(scale.Z)});
    return !reflected && largest - smallest <= largest * 0.0001f;
}
} // namespace GPhysics
