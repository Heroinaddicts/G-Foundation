#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Backend-only values. No Runtime nodes, Editor/Qt, GPU objects or Jolt types
// cross this boundary. The owner calls all World methods on one thread.
namespace GPhysics {
struct Vector3 {
    float X = 0, Y = 0, Z = 0;
    bool operator==(const Vector3 &) const = default;
};
struct Quaternion {
    float X = 0, Y = 0, Z = 0, W = 1;
    bool operator==(const Quaternion &) const = default;
};
struct Pose {
    Vector3 Position;
    Quaternion Rotation;
    bool operator==(const Pose &) const = default;
};
enum class MotionType : uint8_t { Static, Dynamic, Kinematic };
enum class ShapeType : uint8_t { Box, Sphere, Capsule, TriangleMesh, ConvexHull };
enum class CombineMode : uint8_t { Average, Minimum, Multiply, Maximum };
struct Material {
    float Friction = 0.6f, Restitution = 0;
    CombineMode FrictionCombine = CombineMode::Average, RestitutionCombine = CombineMode::Average;
    bool operator==(const Material &) const = default;
};
// Used by the contact listener. Maximum > Multiply > Minimum > Average.
Material CombineMaterials(const Material &, const Material &);
struct MeshGeometry {
    std::vector<Vector3> Vertices;
    std::vector<uint32_t> Indices;
};
// No World or GPU is needed. Convex output is the actual cooked hull (radius 0),
// useful for editor wireframes; invalid/non-finite/degenerate input fails.
bool CookMeshGeometry(const MeshGeometry &, bool convex, MeshGeometry &, std::string &error);
struct Shape {
    uint64_t Collider = 0;
    ShapeType Type = ShapeType::Box;
    Pose LocalPose;
    Vector3 HalfExtents{0.5f, 0.5f, 0.5f};
    float Radius = 0.5f, HalfHeight = 0.5f;
    // Borrowed only for PutBody; the backend never keeps or dereferences Mesh
    // after that call. GeometryRevision is an owner-issued monotonic identity.
    const MeshGeometry *Mesh = nullptr;
    uint64_t GeometryRevision = 0;
    bool Trigger = false;
    uint32_t Layer = 0, Mask = 0xffffffffu;
    Material Surface;
    bool operator==(const Shape &) const = default;
};
struct BodySettings {
    uint64_t Key = 0;
    MotionType Motion = MotionType::Static;
    Pose Transform;
    Vector3 LinearVelocity, AngularVelocity;
    float Mass = 1, GravityFactor = 1, LinearDamping = 0.05f, AngularDamping = 0.05f;
    // Translation XYZ then rotation XYZ. Zero locks all motion while preserving
    // this body's public motion type and collision/trigger participation.
    uint8_t AllowedDOFs = 0x3f;
    bool Continuous = false, AllowSleeping = true;
    std::vector<Shape> Shapes;
};
struct BodyState {
    Pose Transform;
    Vector3 LinearVelocity, AngularVelocity;
    bool Active = false;
};
struct Contact {
    enum class Phase : uint8_t { Enter, Stay, Exit };
    Phase State = Phase::Enter;
    uint64_t FirstCollider = 0, SecondCollider = 0;
    bool Trigger = false;
    Vector3 Normal;
    float Penetration = 0;
    std::vector<Vector3> Points;
};
struct RayHit {
    uint64_t Collider = 0;
    float Fraction = 0;
    Vector3 Position, Normal;
};
// Backend diagnostics only; no Jolt values or SDK virtual methods are exposed.
struct MeshCookStatistics {
    uint64_t CookCalls=0,CacheHits=0,CachedShapes=0,RetainedBytes=0;
};
class World {
  public:
    World();
    ~World();
    World(const World &) = delete;
    World &operator=(const World &) = delete;
    bool Initialize(std::string &error);
    bool PutBody(const BodySettings &, std::string &error);
    void RemoveBody(uint64_t key);
    bool ContainsBody(uint64_t key) const;
    MeshCookStatistics GetMeshCookStatistics() const;
    bool GetBodyState(uint64_t key, BodyState &) const;
    bool SetPose(uint64_t key, const Pose &, bool kinematic, float deltaSeconds);
    bool SetVelocity(uint64_t key, Vector3 linear, Vector3 angular);
    bool AddForce(uint64_t key, Vector3 value, bool impulse);
    bool AddTorque(uint64_t key, Vector3 value, bool impulse);
    void SetGravity(Vector3 gravity);
    bool Step(float deltaSeconds, std::vector<Contact> &events, std::string &error);
    // Ignored collider keys reject individual compound leaves, not unrelated
    // colliders on the same layer or the other leaves of a compound body.
    bool Raycast(Vector3 origin, Vector3 displacement, uint32_t mask, bool triggers, RayHit &,
                 const std::vector<uint64_t>& ignoredColliders = {}) const;

  private:
    struct Impl;
    Impl *_Impl = nullptr;
};
} // namespace GPhysics
