#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include "GPhysics.h"

namespace GPhysics::Detail {
void AcquireMeshBackend();
void ReleaseMeshBackend();
JPH::Shape::ShapeResult CreateMeshShape(const MeshGeometry &, bool convex, uint64_t collider);
}
