# GPhysics

GPhysics is a pure C++ physics backend privately linked into GameSproutRuntime.
It wraps **Jolt Physics 5.6.0**, commit
`e77f175595e64cb44218cc9d9d56fc365ad0e36a` (MIT license). CMake downloads the
pinned archive into the build dependency directory and verifies its SHA-256.
The upstream source and generated files are not part of the Editor/Runtime SDK.

The backend uses CPU jobs on macOS, Windows, Linux, iOS and Android; Jolt's GPU
compute backends and sample applications are disabled. Only the current macOS
ARM64 configuration has been built and exercised in this integration.

Runtime owns fixed-step scheduling and all node/component identities. GPhysics
only owns bodies, collision shapes and value contacts. Shape user data preserves
the Runtime's per-Collider mapping through compound bodies. Contact manifold
reduction is disabled to preserve distinct Collider material/Trigger identities.
Worker callbacks collect values under a multi-producer queue lock. Runtime
subscribers are invoked only after `World::Step` returns.

The backend provides Box, Sphere, Capsule, triangle-mesh and convex-hull shapes, compound bodies,
Dynamic/Kinematic/Static motion, forces/impulses, collision masks, CCD and raycasts.
Non-convex triangle meshes support Static and Kinematic bodies; Dynamic bodies
and Triggers require a convex hull. Convex cooking uses Jolt's 256 hull-vertex
limit (interior input vertices are discarded) and reports a cooking failure
rather than replacing the mesh with a primitive. Referenced vertices, indices,
finite positions and nondegenerate triangles are validated. `CookMeshGeometry`
returns the actual radius-zero hull for Runtime's editor/debug wireframes without
creating a physics World. Runtime caches source decoding and body-local affine
geometry by provider revision, mesh identity and relative transform; body-only
translation/rotation does not re-read or re-cook the mesh. Mesh transforms bake
nonuniform/mirrored scale (with corrected winding), including static child shear.
Static pairs do not generate simulation contacts. EmptyShape allows a Rigidbody
without Collider geometry to continue moving under gravity without contact.

Physics materials have one friction coefficient, restitution and independent
combine modes. The selected mode priority is Maximum > Multiply > Minimum >
Average. Large finite pair values are reduced in double precision and clamped
to finite float before passing to Jolt. Independent PhysX-style static/dynamic
friction is not claimed or silently approximated.

Runtime validates positive, orthogonal TRS transforms. Box colliders accept
nonuniform positive scale; Sphere/Capsule currently require uniform world scale.
Shear and singular/negative scale are reported as unsupported. Locking all six
degrees of freedom uses a zero-velocity kinematic carrier inside Jolt, while the
public body retains its configured motion type and collision/trigger semantics.
The carrier rejects accumulated motion from forces and velocities; unlocking
restores the configured dynamic/kinematic behavior. Physics poses
are separate from authoring values and reset when gameplay ends. Mesh cooking,
joints, character controllers and ragdolls are not implemented in this version.

The Runtime SDK exposes each supported Rigidbody setting through
`IRigidbodyComponent`: enabled state, motion type, mass, gravity factor, linear
and angular damping, continuous collision detection and constraints. Individual
setters preserve unrelated settings and return `false` on invalid input, just
like the atomic `GetSettings` / `SetSettings` transaction. For example:

```cpp
body->SetMass(2.0f);
body->SetPositionFrozen(GameSproutRuntime::PhysicsAxis::X, true);
body->SetRotationFrozen(GameSproutRuntime::PhysicsAxis::Z, true);
body->SetConstraints(GameSproutRuntime::PhysicsConstraint::FreezeAll);
body->SetConstraints(GameSproutRuntime::PhysicsConstraint::None);
```

Constraints use world axes. FreezeAll preserves Rigidbody ownership and contact
subscriptions, consumes force/impulse commands without storing them, and keeps
linear/angular velocity at zero. Unlocking starts without accumulated movement;
new commands then work normally. Explicit teleport remains available. Settings
reach the backend at the next fixed step or the host's zero-time
`SynchronizePhysics` safe point; changing a property never runs an extra game tick.

Runtime raycasts can exclude the querying character without changing its Layer:

```cpp
GameSproutRuntime::PhysicsQuerySettings query;
query.IgnoredObjectIDs = {character->GetInstanceID()};
query.IncludeTriggers = false;
GameSproutRuntime::PhysicsRaycastHit hit;
std::string error;
const bool grounded = runtime->Raycast(character->GetScene(),
    {footPosition, {0, -1, 0}}, probeDistance, hit, query, error);
```

`IgnoredObjectIDs` contains live Runtime object identities, not Scene/Prefab
FileIDs. An ignored object excludes all its own Colliders. A Rigidbody owner
also excludes child Colliders attached to that compound body. Ignoring just one
child leaves the compound's other Colliders queryable. Independent descendant
Rigidbody bodies are not recursively excluded; add their object IDs explicitly
when excluding an entire hierarchy. Missing, duplicate, and other-scene IDs have
no effect. The collision mask and trigger policy still apply to every remaining
Collider. A miss returns `false` with an empty error and a cleared hit; invalid
parameters or synchronization failures return `false` with an error.

For standalone backend checks:

```sh
cmake -S GameSprout/GPhysics -B /tmp/gamesprout-gphysics -G Ninja -DGPHYSICS_BUILD_TESTS=ON
cmake --build /tmp/gamesprout-gphysics --parallel 10
ctest --test-dir /tmp/gamesprout-gphysics --output-on-failure
```

See upstream [license](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/LICENSE)
and [architecture](https://jrouwe.github.io/JoltPhysics/) for engine constraints.
