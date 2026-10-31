// Runtime preview convention: RH, Y up, camera forward -Z, NDC depth [0,1].
// CPU matrices are column-major and multiply column vectors: P * V * M * position.
cbuffer DrawUniform : register(b0, space1)
{
    column_major float4x4 Model;
    column_major float4x4 ViewProjection;
    column_major float4x4 NormalMatrix;
    float4 BaseColor;
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
};
MeshOutput TransformVertex(float3 position, float3 normal, float4 color, float2 uv)
{
    MeshOutput output;
    output.Position = mul(ViewProjection, mul(Model, float4(position, 1.0)));
    float3 transformed = mul((float3x3)NormalMatrix, normal);
    float lengthSquared = dot(transformed, transformed);
    output.Normal = lengthSquared > 1e-12 ? transformed * rsqrt(lengthSquared) : float3(0, 1, 0);
    output.Color = color * BaseColor;
    output.UV = uv;
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
