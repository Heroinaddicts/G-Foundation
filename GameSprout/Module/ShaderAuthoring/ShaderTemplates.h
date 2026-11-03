#pragma once
#include <string_view>

namespace ShaderTemplates {
inline constexpr std::string_view Unlit=R"hlsl(/* GameSproutShader {
    "Name": "GameSprout/Unlit",
    "Passes": [
        { "Name": "Forward", "LightMode": "Forward", "Vertex": "VSMain", "Fragment": "PSMain",
          "Cull": "Back", "Blend": "Opaque", "DepthTest": true, "DepthWrite": true, "DepthCompare": "LessEqual" }
    ]
} */
/* GameSproutMaterial {
    "Name": "GameSprout/Unlit",
    "Properties": [
        { "Name": "Tint", "DisplayName": "基础颜色", "Type": "Color", "Default": [1, 1, 1, 1], "Group": "表面" },
        { "Name": "Amount", "DisplayName": "颜色强度", "Type": "Range", "Default": 1, "Min": 0, "Max": 1, "Group": "表面" },
        { "Name": "BaseTexture", "DisplayName": "基础贴图", "Type": "Texture", "Default": null, "Group": "表面" }
    ]
} */

// GameSprout世界：Unity左手系，+Y向上、+Z向前；View转换到-Z向前的相机空间。
// 矩阵为column-major，按P*V*M*position相乘；Shader不再额外翻转坐标。
// 引擎绘制数据由Runtime上传，不属于材质参数，不能改变字段顺序。
cbuffer GameSproutDraw : register(b0, space1)
{
    column_major float4x4 Model;
    column_major float4x4 ViewProjection;
    column_major float4x4 NormalMatrix;
};

struct VertexInput
{
    [[vk::location(0)]] float3 Position : TEXCOORD0;
    [[vk::location(1)]] float3 Normal : TEXCOORD1;
    [[vk::location(2)]] float4 Color : TEXCOORD2;
    [[vk::location(3)]] float2 UV : TEXCOORD3;
};
struct VertexOutput
{
    float4 Position : SV_Position;
    float3 WorldNormal : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float2 UV : TEXCOORD2;
};
VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.Position = mul(ViewProjection, mul(Model, float4(input.Position, 1)));
    output.WorldNormal = mul((float3x3)NormalMatrix, input.Normal);
    output.Color = input.Color;
    output.UV = input.UV;
    return output;
}

// 材质数据按反射布局上传；字段名须对应上方Properties，纹理ST为平铺/偏移。
cbuffer Material : register(b0, space3)
{
    float4 Tint;
    float4 BaseTextureST;
    float Amount;
};
Texture2D<float4> BaseTexture : register(t0, space2);
SamplerState BaseSampler : register(s0, space2);

float4 PSMain(VertexOutput input) : SV_Target0
{
    float2 uv = input.UV * BaseTextureST.xy + BaseTextureST.zw;
    float4 color = BaseTexture.Sample(BaseSampler, uv) * Tint * input.Color;
    return float4(color.rgb * Amount, color.a);
}
)hlsl";
}
