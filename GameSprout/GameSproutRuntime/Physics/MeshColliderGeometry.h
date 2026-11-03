#pragma once
#include "Api/RuntimeResourceApi.h"
#include "GPhysics.h"

namespace GameSproutRuntime {
// CPU-only decoding shared by simulation and debug geometry. Input is already
// prepared mesh bytes; neither path opens source assets or depends on GRHI.
// Unity-style world coordinates do not require a second vertex/index conversion
// for Jolt. Authored triangle fronts retain their numerical cross-product normal.
bool DecodeMeshColliderGeometry(const MeshRenderData &,GPhysics::MeshGeometry &,std::string &error);
// Bake the complete invertible affine transform, including mirror/nonuniform
// scale/shear. Mirroring reverses winding so triangle front faces remain solid.
bool TransformMeshColliderGeometry(const GPhysics::MeshGeometry &,const Mat4 &,
                                  GPhysics::MeshGeometry &,std::string &error);
}
