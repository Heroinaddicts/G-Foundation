#pragma once
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
namespace GameSproutRuntime {
struct TransformPose { Vector3 Position; Quaternion Rotation; Vector3 Scale{1,1,1}; };
}
