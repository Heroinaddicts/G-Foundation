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
only owns bodies, primitive shapes and value contacts. Shape user data preserves
the Runtime's per-Collider mapping through compound bodies. Contact manifold
reduction is disabled to preserve distinct Collider material/Trigger identities.
Worker callbacks collect values under a multi-producer queue lock. Runtime
subscribers are invoked only after `World::Step` returns.

The first version provides Box, Sphere and Capsule shapes, compound bodies,
Dynamic/Kinematic/Static motion, forces/impulses, collision masks, CCD and raycasts.
Static pairs do not generate simulation contacts. EmptyShape allows a Rigidbody
without Collider geometry to continue moving under gravity without contact.

Physics materials have one friction coefficient, restitution and independent
combine modes. The selected mode priority is Maximum > Multiply > Minimum >
Average. Large finite pair values are reduced in double precision and clamped
to finite float before passing to Jolt. Independent PhysX-style static/dynamic
friction is not claimed or silently approximated.

Runtime validates positive, orthogonal TRS transforms. Box colliders accept
nonuniform positive scale; Sphere/Capsule currently require uniform world scale.
Shear, singular/negative scale and freezing all six degrees of freedom are
reported as unsupported instead of being sent to Jolt assertions. Physics poses
are separate from authoring values and reset when gameplay ends. Mesh cooking,
joints, character controllers and ragdolls are not implemented in this version.

For standalone backend checks:

```sh
cmake -S GameSprout/GPhysics -B /tmp/gamesprout-gphysics -G Ninja -DGPHYSICS_BUILD_TESTS=ON
cmake --build /tmp/gamesprout-gphysics --parallel 10
ctest --test-dir /tmp/gamesprout-gphysics --output-on-failure
```

See upstream [license](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/LICENSE)
and [architecture](https://jrouwe.github.io/JoltPhysics/) for engine constraints.
