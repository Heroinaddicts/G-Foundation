// Authored world: Unity-compatible, Y up, +Z forward. GPU view space is RH,
// camera forward -Z; canonical projection NDC depth is [0,1].
// CPU matrices are column-major and multiply column vectors: P * V * M * position.
cbuffer DrawUniform : register(b0, space1)
{
    column_major float4x4 Model;
    column_major float4x4 ViewProjection;
    column_major float4x4 NormalMatrix;
    float4 BaseColor;
    float4 TextureST;
};

struct MeshInput {
    [[vk::location(0)]] float3 Position : TEXCOORD0;
    [[vk::location(1)]] float3 Normal : TEXCOORD1;
    [[vk::location(2)]] float4 Color : TEXCOORD2;
    [[vk::location(3)]] float2 UV : TEXCOORD3;
};
struct MeshNoUVInput {
    [[vk::location(0)]] float3 Position : TEXCOORD0;
    [[vk::location(1)]] float3 Normal : TEXCOORD1;
    [[vk::location(2)]] float4 Color : TEXCOORD2;
};
struct MeshNoColorInput {
    [[vk::location(0)]] float3 Position : TEXCOORD0;
    [[vk::location(1)]] float3 Normal : TEXCOORD1;
    [[vk::location(3)]] float2 UV : TEXCOORD3;
};
struct MeshNoUVNoColorInput {
    [[vk::location(0)]] float3 Position : TEXCOORD0;
    [[vk::location(1)]] float3 Normal : TEXCOORD1;
};
struct MeshOutput {
    float4 Position : SV_Position;
    float3 Normal : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float3 WorldPosition : TEXCOORD3;
    float2 NormalUV : TEXCOORD4;
};
MeshOutput TransformVertex(float3 position, float3 normal, float4 color, float2 uv)
{
    MeshOutput output;
    float4 worldPosition = mul(Model, float4(position, 1.0));
    output.Position = mul(ViewProjection, worldPosition);
    float3 transformed = mul((float3x3)NormalMatrix, normal);
    float lengthSquared = dot(transformed, transformed);
    output.Normal = lengthSquared > 1e-12 ? transformed * rsqrt(lengthSquared) : float3(0, 1, 0);
    output.Color = color * BaseColor;
    output.UV = uv * TextureST.xy + TextureST.zw;
    output.WorldPosition = worldPosition.xyz;
    output.NormalUV = uv;
    return output;
}
MeshOutput MeshVertex(MeshInput input) {
    return TransformVertex(input.Position, input.Normal, input.Color, input.UV);
}
// Separate entry point and layout: no attempt to read UV bytes absent from the mesh.
MeshOutput MeshNoUVVertex(MeshNoUVInput input) {
    return TransformVertex(input.Position, input.Normal, input.Color, float2(0, 0));
}
MeshOutput MeshNoColorVertex(MeshNoColorInput input) {
    return TransformVertex(input.Position, input.Normal, float4(1, 1, 1, 1), input.UV);
}
MeshOutput MeshNoUVNoColorVertex(MeshNoUVNoColorInput input) {
    return TransformVertex(input.Position, input.Normal, float4(1, 1, 1, 1), float2(0, 0));
}

// Binding 1 advances once per instance. The CPU uploads two column-major Mat4s
// (Model, inverse-transpose Normal), 128 bytes in all. Keep the columns explicit:
// an HLSL float4x4 constructor would interpret its vector arguments as rows.
struct InstanceInput {
    [[vk::location(4)]] float4 Model0 : TEXCOORD4;
    [[vk::location(5)]] float4 Model1 : TEXCOORD5;
    [[vk::location(6)]] float4 Model2 : TEXCOORD6;
    [[vk::location(7)]] float4 Model3 : TEXCOORD7;
    [[vk::location(8)]] float4 Normal0 : TEXCOORD8;
    [[vk::location(9)]] float4 Normal1 : TEXCOORD9;
    [[vk::location(10)]] float4 Normal2 : TEXCOORD10;
    [[vk::location(11)]] float4 Normal3 : TEXCOORD11;
};
MeshOutput TransformInstanceVertex(float3 position, float3 normal, float4 color, float2 uv, InstanceInput instance)
{
    MeshOutput output;
    float4 worldPosition = instance.Model0 * position.x + instance.Model1 * position.y
                         + instance.Model2 * position.z + instance.Model3;
    output.Position = mul(ViewProjection, worldPosition);
    float3 transformed = instance.Normal0.xyz * normal.x + instance.Normal1.xyz * normal.y
                       + instance.Normal2.xyz * normal.z;
    float lengthSquared = dot(transformed, transformed);
    output.Normal = lengthSquared > 1e-12 ? transformed * rsqrt(lengthSquared) : float3(0, 1, 0);
    output.Color = color * BaseColor;
    output.UV = uv * TextureST.xy + TextureST.zw;
    output.WorldPosition = worldPosition.xyz;
    output.NormalUV = uv;
    return output;
}
MeshOutput InstancedMeshVertex(MeshInput input, InstanceInput instance) {
    return TransformInstanceVertex(input.Position, input.Normal, input.Color, input.UV, instance);
}
MeshOutput InstancedMeshNoUVVertex(MeshNoUVInput input, InstanceInput instance) {
    return TransformInstanceVertex(input.Position, input.Normal, input.Color, float2(0, 0), instance);
}
MeshOutput InstancedMeshNoColorVertex(MeshNoColorInput input, InstanceInput instance) {
    return TransformInstanceVertex(input.Position, input.Normal, float4(1, 1, 1, 1), input.UV, instance);
}
MeshOutput InstancedMeshNoUVNoColorVertex(MeshNoUVNoColorInput input, InstanceInput instance) {
    return TransformInstanceVertex(input.Position, input.Normal, float4(1, 1, 1, 1), float2(0, 0), instance);
}

// Same 128-matrix limit as Renderer::MaxSkinBones. b0 remains the ordinary draw uniform.
cbuffer SkinUniform : register(b1, space1)
{
    column_major float4x4 SkinBones[128];
};
MeshOutput SkinVertex(float3 position, float3 normal, float4 color, float2 uv, uint4 bones, float4 weights)
{
    // The CPU validates every index and the normalized, nonnegative weights before uploading.
    // Each matrix maps the bind mesh's local space into this instance's current mesh local space.
    float4x4 skin = SkinBones[bones.x] * weights.x + SkinBones[bones.y] * weights.y
                 + SkinBones[bones.z] * weights.z + SkinBones[bones.w] * weights.w;
    float3 skinnedPosition = mul(skin, float4(position, 1.0)).xyz;
    // Inverse transpose of the blended linear deformation, including nonuniform bone scale.
    float3 a = float3(skin[0][0], skin[1][0], skin[2][0]);
    float3 b = float3(skin[0][1], skin[1][1], skin[2][1]);
    float3 c = float3(skin[0][2], skin[1][2], skin[2][2]);
    float determinant = dot(a, cross(b, c));
    float3 skinnedNormal = normal;
    if (abs(determinant) > 1e-12)
        skinnedNormal = (cross(b, c) * normal.x + cross(c, a) * normal.y + cross(a, b) * normal.z) / determinant;
    return TransformVertex(skinnedPosition, skinnedNormal, color, uv);
}
MeshOutput SkinnedMeshVertex(MeshInput input,
    [[vk::location(4)]] uint4 bones : TEXCOORD4, [[vk::location(5)]] float4 weights : TEXCOORD5) {
    return SkinVertex(input.Position, input.Normal, input.Color, input.UV, bones, weights);
}
MeshOutput SkinnedMeshNoUVVertex(MeshNoUVInput input,
    [[vk::location(4)]] uint4 bones : TEXCOORD4, [[vk::location(5)]] float4 weights : TEXCOORD5) {
    return SkinVertex(input.Position, input.Normal, input.Color, float2(0, 0), bones, weights);
}
MeshOutput SkinnedMeshNoColorVertex(MeshNoColorInput input,
    [[vk::location(4)]] uint4 bones : TEXCOORD4, [[vk::location(5)]] float4 weights : TEXCOORD5) {
    return SkinVertex(input.Position, input.Normal, float4(1, 1, 1, 1), input.UV, bones, weights);
}
MeshOutput SkinnedMeshNoUVNoColorVertex(MeshNoUVNoColorInput input,
    [[vk::location(4)]] uint4 bones : TEXCOORD4, [[vk::location(5)]] float4 weights : TEXCOORD5) {
    return SkinVertex(input.Position, input.Normal, float4(1, 1, 1, 1), float2(0, 0), bones, weights);
}

Texture2D<float4> BaseTexture : register(t0, space2);
SamplerState BaseSampler : register(s0, space2);
float4 MeshFragment(MeshOutput input) : SV_Target0
{
    float lengthSquared = dot(input.Normal, input.Normal);
    float3 normal = lengthSquared > 1e-12 ? input.Normal * rsqrt(lengthSquared) : float3(0, 1, 0);
    float3 lightDirection = normalize(float3(0.35, 0.8, 0.6));
    float lighting = 0.28 + 0.72 * max(dot(normal, lightDirection), 0.0);
    float4 color = BaseTexture.Sample(BaseSampler, input.UV) * input.Color;
    return float4(color.rgb * lighting, color.a);
}

// Built-in Lit. Runtime supplies scene-derived, per-object matching lights.
// Colors and output are linear. An sRGB render target applies the output transfer
// function; a linear offscreen target intentionally returns linear UNorm pixels.
cbuffer LitUniform : register(b0, space3)
{
    float4 CameraPosition; // XYZ world camera position; W enables direct specular highlights.
    float4 LightDirections[4];
    float4 LightRadiances[4];
    float4 LitParameters; // Metallic, perceptual roughness, normal scale, has normal map.
    float4 Emission;
    float4 NormalTextureST;
    float4 StyleParameters; // Strength, light threshold, transition softness, saturation.
    float4 StylizedShadowTint;
};
cbuffer EnvironmentUniform : register(b1, space3)
{
    float4 EnvironmentInfo;
    float4 AmbientColor;
    float4 SkyColor;
    float4 EquatorColor;
    float4 GroundColor;
    float4 EnvironmentSH[9];
    float4 AtmosphereParameters;
    float4 AtmosphereFogColor;
    float4 AtmosphereZenithColor;
    float4 AtmosphereHorizonColor;
};
#include "Atmosphere.hlsl"
// Canonical light clip -> top-first atlas UV. This block is independent from
// scene ambient diffuse; shadows only attenuate the selected direct light.
cbuffer ShadowUniform : register(b2, space3)
{
    float4x4 WorldToShadow[4];
    float4x4 ShadowCameraView;
    float4 CascadeSplits;
    float4 ShadowInfo;
    float4 CascadeBias[4]; // Depth bias, normal bias, soft filtering. Entry 0 W enables dynamic sampling.
    float4 CascadeAtlas[4];
    float4 DynamicDepth[4]; // Clip Z row for each dynamic cascade.
    float4 DynamicDepthBias;
};
Texture2D<float> ShadowTexture : register(t2, space2);
SamplerState ShadowSampler : register(s2, space2);
Texture2D<float> DynamicShadowTexture : register(t3, space2);
SamplerState DynamicShadowSampler : register(s3, space2);

float SampleShadowVisibility(float2 uv, float2 reference, bool sampleDynamic)
{
    // The layers share a tile but normalize Z independently. A tap is lit
    // only if neither depth test is occluded, before averaging PCF taps.
    float visibility = reference.x <= ShadowTexture.SampleLevel(ShadowSampler, uv, 0).r ? 1.0 : 0.0;
    if (sampleDynamic)
        visibility *= reference.y <= DynamicShadowTexture.SampleLevel(DynamicShadowSampler, uv, 0).r ? 1.0 : 0.0;
    return visibility;
}

float DirectionalShadow(float3 worldPosition, float3 normal)
{
    if (ShadowInfo.x < 0.5) return 1.0;
    // Matrices are column-major, same packing as Runtime's Mat4.
    float depth = -mul(ShadowCameraView, float4(worldPosition, 1.0)).z;
    if (depth < 0.0 || depth >= ShadowInfo.z) return 1.0;
    int cascade = 0;
    if (depth > CascadeSplits.x) cascade = 1;
    if (depth > CascadeSplits.y && ShadowInfo.x > 2.5) cascade = 2;
    if (depth > CascadeSplits.z && ShadowInfo.x > 3.5) cascade = 3;
    cascade = min(cascade, (int)ShadowInfo.x - 1);
    float3 query = worldPosition + normal * CascadeBias[cascade].y;
    float4 worldQuery = float4(query, 1.0);
    float4 clip = mul(WorldToShadow[cascade], worldQuery);
    float3 ndc = clip.xyz / clip.w;
    float2 localUV = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(localUV < 0.0) || any(localUV > 1.0)) return 1.0;
    bool sampleStatic = ndc.z >= 0.0 && ndc.z <= 1.0;
    float dynamicZ = 0.0;
    bool sampleDynamic = CascadeBias[0].w > 0.5;
    if (sampleDynamic)
    {
        dynamicZ = dot(DynamicDepth[cascade], worldQuery) / clip.w;
        sampleDynamic = dynamicZ >= 0.0 && dynamicZ <= 1.0;
    }
    if (!sampleStatic && !sampleDynamic) return 1.0;
    uint width, height; ShadowTexture.GetDimensions(width, height);
    float2 texel = 1.0 / float2(width, height);
    float4 tile = CascadeAtlas[cascade];
    float2 uv = localUV * tile.xy + tile.zw;
    // A zero reference is always lit for a depth texture. This disables the
    // static test outside its fitted depth range without rejecting dynamic Z.
    float2 reference = float2(sampleStatic ? ndc.z - CascadeBias[cascade].x : 0.0,
                              dynamicZ - DynamicDepthBias[cascade]);
    float visibility = 0.0;
    if (CascadeBias[cascade].z < 0.5)
        visibility = SampleShadowVisibility(uv, reference, sampleDynamic);
    else
    {
        [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float2 tap = clamp(uv + float2(x, y) * texel, tile.zw + texel * 0.5, tile.zw + tile.xy - texel * 0.5);
            visibility += SampleShadowVisibility(tap, reference, sampleDynamic);
        }
        visibility /= 9.0;
    }
    float fade = saturate((ShadowInfo.z - depth) / max(ShadowInfo.z - ShadowInfo.w, 0.0001));
    return 1.0 - (1.0 - visibility) * ShadowInfo.y * fade;
}

Texture2D<float4> NormalTexture : register(t1, space2);
SamplerState NormalSampler : register(s1, space2);

float3 SafeDirection(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12 ? value * rsqrt(lengthSquared) : fallback;
}
float3 SurfaceNormal(MeshOutput input)
{
    float3 normal = SafeDirection(input.Normal, float3(0, 1, 0));
    // Derivative tangent frame avoids inventing tangents absent from the current
    // imported mesh layout. Mirrored UVs preserve handedness. Degenerate UVs use
    // the authored vertex normal instead of emitting NaNs.
    float2 uv = input.NormalUV * NormalTextureST.xy + NormalTextureST.zw;
    float3 dx = ddx(input.WorldPosition), dy = ddy(input.WorldPosition);
    float2 tx = ddx(uv), ty = ddy(uv);
    float determinant = tx.x * ty.y - tx.y * ty.x;
    if (LitParameters.w < 0.5 || abs(determinant) < 1e-10) return normal;
    float3 tangent = (dx * ty.y - dy * tx.y) / determinant;
    float3 bitangent = (dy * tx.x - dx * ty.x) / determinant;
    tangent -= normal * dot(normal, tangent);
    if (dot(tangent, tangent) < 1e-12 || dot(bitangent, bitangent) < 1e-12) return normal;
    tangent = normalize(tangent);
    float handedness = dot(cross(normal, tangent), bitangent) < 0.0 ? -1.0 : 1.0;
    bitangent = cross(normal, tangent) * handedness;
    float3 mapped = NormalTexture.Sample(NormalSampler, uv).xyz * 2.0 - 1.0;
    mapped.xy *= LitParameters.z;
    mapped = SafeDirection(mapped, float3(0, 0, 1));
    return SafeDirection(tangent * mapped.x + bitangent * mapped.y + normal * mapped.z, normal);
}
float3 DiffuseEnvironment(float3 normal)
{
    int mode = (int)EnvironmentInfo.x;
    if (mode == 1) return AmbientColor.rgb;
    if (mode == 2)
    {
        float y = clamp(normal.y, -1.0, 1.0);
        return y >= 0.0 ? lerp(EquatorColor.rgb, SkyColor.rgb, y) : lerp(EquatorColor.rgb, GroundColor.rgb, -y);
    }
    if (mode == 3)
    {
        // Public coefficients already contain diffuse convolution E/pi and
        // basis scaling. Evaluate in world space, preserving signed terms until
        // the complete sum; applying SH constants or pi here would double-scale.
        float basis[9] = {1.0, normal.y, normal.z, normal.x, normal.x * normal.y,
            normal.y * normal.z, 3.0 * normal.z * normal.z - 1.0,
            normal.x * normal.z, normal.x * normal.x - normal.y * normal.y};
        float3 irradiance = 0.0;
        [unroll] for (int index = 0; index < 9; ++index) irradiance += EnvironmentSH[index].rgb * basis[index];
        return max(irradiance, 0.0);
    }
    return 0.0;
}
float4 LitFragment(MeshOutput input) : SV_Target0
{
    const float pi = 3.141592653589793;
    float4 base = BaseTexture.Sample(BaseSampler, input.UV) * input.Color;
    float3 albedo = saturate(base.rgb);
    float metallic = saturate(LitParameters.x);
    // The UI roughness is perceptual; GGX alpha is its square. The floor avoids
    // singular, subpixel ideal mirrors without changing the stored author value.
    float alpha = max(LitParameters.y * LitParameters.y, 0.0025);
    float alphaSquared = alpha * alpha;
    float3 normal = SurfaceNormal(input);
    float3 view = SafeDirection(CameraPosition.xyz - input.WorldPosition, normal);
    float nDotV = max(saturate(dot(normal, view)), 0.0001);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 color = max(Emission.rgb, 0.0);
    // URP's metallic workflow keeps the dielectric 4% reflected fraction out of
    // the diffuse lobe: oneMinusReflectivity = .96 * (1 - metallic). Diffuse
    // irradiance remains independent of the viewing camera. Missing reflection
    // IBL is not replaced with an ambient specular constant.
    float3 ambient = DiffuseEnvironment(normal) * albedo * (0.96 * (1.0 - metallic));
    color += ambient;
    float3 stylizedDirect = 0.0;
    float stylizedIllumination = 0.0;
    [unroll] for (int index = 0; index < 4; ++index)
    {
        float3 light = LightDirections[index].xyz;
        float nDotL = saturate(dot(normal, light));
        if (LightDirections[index].w < 0.5 || nDotL <= 0.0) continue;
        float3 halfway = SafeDirection(light + view, normal);
        float nDotH = saturate(dot(normal, halfway));
        float vDotH = saturate(dot(view, halfway));
        float distributionDenominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
        float distribution = alphaSquared / max(pi * distributionDenominator * distributionDenominator, 1e-8);
        // Height-correlated Smith visibility for GGX, including the 1/(4 NoL NoV) factor.
        float visibilityL = nDotV * sqrt(nDotL * nDotL * (1.0 - alphaSquared) + alphaSquared);
        float visibilityV = nDotL * sqrt(nDotV * nDotV * (1.0 - alphaSquared) + alphaSquared);
        float visibility = 0.5 / max(visibilityL + visibilityV, 1e-6);
        float fresnelWeight = pow(1.0 - vDotH, 5.0);
        float3 fresnel = f0 + (1.0 - f0) * fresnelWeight;
        // URP-compatible relative light units: the Lambert pi factor is
        // absorbed into the author-facing intensity, consistently with the
        // imported Light.intensity. Environment SH is already E/pi and is unchanged.
        float3 diffuse = albedo * (0.96 * (1.0 - metallic));
        float3 specular = CameraPosition.w >= 0.5 ? distribution * visibility * fresnel * pi : 0.0;
        float shadow = LightDirections[index].w > 1.5 ? DirectionalShadow(input.WorldPosition, SafeDirection(input.Normal, float3(0, 1, 0))) : 1.0;
        color += (diffuse + specular) * LightRadiances[index].rgb * nDotL * shadow;
        if (StyleParameters.x > 0.0)
        {
            // A soft light band preserves broad painted forms. Screen-space
            // filtering keeps a near-hard author threshold from shimmering.
            float width = max(max(StyleParameters.z, fwidth(nDotL)), 0.001);
            float band = smoothstep(StyleParameters.y - width, StyleParameters.y + width, nDotL);
            float illumination = lerp(nDotL, band, 0.9) * shadow;
            stylizedIllumination = max(stylizedIllumination, illumination);
            // Keep shiny surfaces legible without tiny, clipped white GGX peaks.
            float3 softSpecular = min(specular, 0.25) * 0.25;
            stylizedDirect += (diffuse * illumination + softSpecular * nDotL * shadow) * LightRadiances[index].rgb;
        }
    }
    if (StyleParameters.x > 0.0)
    {
        // Occluded and back-facing regions retain real ambient light, tinted
        // by the material. No ambient or direct illumination is fabricated.
        float3 ambientTint = lerp(StylizedShadowTint.rgb, float3(1, 1, 1), saturate(stylizedIllumination));
        float3 stylized = max(Emission.rgb, 0.0) + ambient * ambientTint + stylizedDirect;
        float luminance = dot(stylized, float3(0.2126, 0.7152, 0.0722));
        stylized = max(lerp(luminance.xxx, stylized, StyleParameters.w), 0.0);
        color = lerp(color, stylized, StyleParameters.x);
    }
    return float4(ApplySceneFog(color, input.WorldPosition), base.a);
}

struct GridInput {
    [[vk::location(0)]] float3 Position : TEXCOORD0;
    [[vk::location(1)]] float4 Color : TEXCOORD1;
};
struct GridOutput {
    float4 Position : SV_Position;
    float4 Color : TEXCOORD0;
};
GridOutput GridVertex(GridInput input) {
    GridOutput output;
    output.Position = mul(ViewProjection, float4(input.Position, 1.0));
    output.Color = input.Color;
    return output;
}
float4 GridFragment(GridOutput input) : SV_Target0 {
    return input.Color;
}

// SDL GPU requires a fragment stage even for a depth-only graphics pipeline.
void ShadowFragment() {}
