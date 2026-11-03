/* GameSproutShader {
    "Name": "GameSprout/StylizedWater",
    "Passes": [
        { "Name": "Forward", "LightMode": "Forward", "Vertex": "VSMain", "Fragment": "PSMain",
          "Cull": "Off", "Blend": "Alpha", "DepthTest": true, "DepthWrite": false, "DepthCompare": "LessEqual" }
    ]
} */
/* GameSproutMaterial {
    "Name": "GameSprout/StylizedWater",
    "Properties": [
        { "Name": "DeepColor", "DisplayName": "深水颜色", "Type": "Color", "ColorSpace": "Linear", "Default": [0.012, 0.12, 0.17, 1], "Group": "水色" },
        { "Name": "ShallowColor", "DisplayName": "波光颜色", "Type": "Color", "ColorSpace": "Linear", "Default": [0.035, 0.29, 0.30, 1], "Group": "水色" },
        { "Name": "SkyColor", "DisplayName": "天空反光", "Type": "Color", "ColorSpace": "Linear", "Default": [0.38, 0.59, 0.72, 1], "Group": "水色" },
        { "Name": "FoamColor", "DisplayName": "浪花颜色", "Type": "Color", "ColorSpace": "Linear", "Default": [0.72, 0.91, 0.86, 1], "Group": "浪花" },
        { "Name": "WaveScale", "DisplayName": "长波密度", "Type": "Range", "Default": 0.11, "Min": 0.02, "Max": 0.5, "Group": "波浪" },
        { "Name": "WaveHeight", "DisplayName": "长波高度（米）", "Type": "Range", "Default": 0.06, "Min": 0, "Max": 0.3, "Group": "波浪" },
        { "Name": "FlowSpeed", "DisplayName": "流动速度", "Type": "Range", "Default": 0.65, "Min": 0, "Max": 3, "Group": "波浪" },
        { "Name": "NormalStrength", "DisplayName": "波纹起伏", "Type": "Range", "Default": 1.1, "Min": 0, "Max": 3, "Group": "波浪" },
        { "Name": "MicroScale", "DisplayName": "细波密度", "Type": "Range", "Default": 0.9, "Min": 0.15, "Max": 3, "Group": "波浪" },
        { "Name": "FoamAmount", "DisplayName": "浪花数量", "Type": "Range", "Default": 0.32, "Min": 0, "Max": 1, "Group": "浪花" },
        { "Name": "FresnelStrength", "DisplayName": "掠射反光", "Type": "Range", "Default": 0.65, "Min": 0, "Max": 1, "Group": "光照" },
        { "Name": "SunShininess", "DisplayName": "太阳高光锐度", "Type": "Range", "Default": 180, "Min": 16, "Max": 512, "Group": "光照" },
        { "Name": "SunStrength", "DisplayName": "太阳高光强度", "Type": "Range", "Default": 1.4, "Min": 0, "Max": 4, "Group": "光照" },
        { "Name": "Opacity", "DisplayName": "水面不透明度", "Type": "Range", "Default": 0.94, "Min": 0.2, "Max": 1, "Group": "水色" },
        { "Name": "FogColor", "DisplayName": "远景雾色", "Type": "Color", "ColorSpace": "Linear", "Default": [0.46, 0.66, 0.79, 1], "Group": "空气透视" },
        { "Name": "FogStart", "DisplayName": "雾起始距离（米）", "Type": "Float", "Min": 0, "Default": 100, "Group": "空气透视" },
        { "Name": "FogEnd", "DisplayName": "雾结束距离（米）", "Type": "Float", "Min": 0.001, "Default": 1100, "Group": "空气透视" },
        { "Name": "FogMaxOpacity", "DisplayName": "最大雾强度", "Type": "Range", "Default": 0, "Min": 0, "Max": 1, "Group": "空气透视", "Tooltip": "自定义 Shader 显式处理空气透视；与场景环境使用相同参数即可衔接。" }
    ]
} */

// 世界单位为米，Y 向上。波纹使用 World XZ，避免源模型的图集 UV 拉伸。
// Shader 输出线性 RGB，由 Runtime 的 sRGB 渲染目标执行显示转换。
// 这里的泡沫来自波峰，不冒充尚未接入的场景深度、折射或岸线检测。

#if defined(GAMESPROUT_VERTEX)
cbuffer GameSproutDraw : register(b0, space1)
{
    column_major float4x4 Model;
    column_major float4x4 ViewProjection;
    column_major float4x4 NormalMatrix;
};
cbuffer GameSproutFrame : register(b1, space1)
{
    float4 Time;               // x: 累计秒，y: 本帧秒，zw: sin/cos(x)。
    float4 CameraPosition;     // 当前视口相机的世界位置。
    float4 MainLightDirection; // xyz: 表面指向主方向光。
    float4 MainLightColor;     // rgb: 线性光色乘强度。
};
cbuffer WaterGeometry : register(b2, space1)
{
    float WaveScale;
    float WaveHeight;
    float FlowSpeed;
};
#elif defined(GAMESPROUT_FRAGMENT)
cbuffer WaterSurface : register(b0, space3)
{
    float4 DeepColor;
    float4 ShallowColor;
    float4 SkyColor;
    float4 FoamColor;
    float4 FogColor;
    float WaveScale;
    float FlowSpeed;
    float NormalStrength;
    float MicroScale;
    float FoamAmount;
    float FresnelStrength;
    float SunShininess;
    float SunStrength;
    float Opacity;
    float FogStart;
    float FogEnd;
    float FogMaxOpacity;
};
cbuffer GameSproutFrame : register(b1, space3)
{
    float4 Time;
    float4 CameraPosition;
    float4 MainLightDirection;
    float4 MainLightColor;
};
#endif

struct VertexInput
{
    [[vk::location(0)]] float3 Position : TEXCOORD0;
};
struct Varyings
{
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
};

float2 WaveCell(int2 cell)
{
    // 每个世界格点独立散列；不把坐标取模，也不重复一张噪声贴图。
    uint hash = uint(cell.x) * 0x9e3779b9u ^ uint(cell.y) * 0x85ebca6bu;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    float2 gradient = float2(hash & 65535u, hash >> 16) * (1.0 / 32767.5) - 1;
    return gradient * rsqrt(max(dot(gradient, gradient), 0.0001));
}

float3 WaveNoise(float2 p)
{
    int2 cell = int2(floor(p));
    float2 f = frac(p);
    // 五次插值使值、梯度和二阶导数在格子边界连续。
    float2 u = f * f * f * (f * (f * 6 - 15) + 10);
    float2 du = 30 * f * f * (f * (f - 2) + 1);
    // 格点保存随机梯度而非随机高度；法线不会在每个格点都回到水平。
    float2 ga = WaveCell(cell);
    float2 gb = WaveCell(cell + int2(1, 0));
    float2 gc = WaveCell(cell + int2(0, 1));
    float2 gd = WaveCell(cell + int2(1, 1));
    float a = dot(ga, f);
    float b = dot(gb, f - float2(1, 0));
    float c = dot(gc, f - float2(0, 1));
    float d = dot(gd, f - float2(1, 1));
    float crossTerm = a - b - c + d;
    float value = a + (b - a) * u.x + (c - a) * u.y + crossTerm * u.x * u.y;
    float2 gradient = lerp(lerp(ga, gb, u.x), lerp(gc, gd, u.x), u.y)
                    + float2(b - a + crossTerm * u.y, c - a + crossTerm * u.x) * du;
    return float3(value, gradient) * 1.4;
}

#if defined(GAMESPROUT_VERTEX)
Varyings VSMain(VertexInput input)
{
    Varyings output;
    float3 world = mul(Model, float4(input.Position, 1)).xyz;
    float t = Time.x * FlowSpeed;
    float swell = WaveNoise(world.xz * WaveScale * 0.23 + float2(t * 0.07, -t * 0.05)).x;
    // 稀疏网格只负责低幅长波；细波由片元法线表现，不需要细分整片海面。
    world.y += WaveHeight * swell;
    output.WorldPosition = world;
    output.Position = mul(ViewProjection, float4(world, 1));
    return output;
}
#endif

#if defined(GAMESPROUT_FRAGMENT)
float4 PSMain(Varyings input) : SV_Target0
{
    float2 p = input.WorldPosition.xz;
    float t = Time.x * FlowSpeed;
    float footprint = max(length(ddx(p)), length(ddy(p)));
    float3 swell = WaveNoise(p * WaveScale * 0.23 + float2(t * 0.07, -t * 0.05));
    float2 along = float2(0.82, 0.57);
    float2 across = float2(-0.57, 0.82);
    // 低频场扰动波纹位置，破坏平行波列与重复交叉图案。
    float2 domain = float2(dot(p, along) * 0.7, dot(p, across) * 1.6) * WaveScale;
    domain += float2(swell.x, swell.y * 0.25) * 1.7 + float2(-t * 0.13, t * 0.08);
    float3 ripple = WaveNoise(domain);
    float2 fineDomain = float2(dot(p, float2(0.36, 0.93)), dot(p, float2(-0.93, 0.36))) * MicroScale * 0.5;
    float3 fine = WaveNoise(fineDomain + float2(t * 0.13, t * 0.19) + float2(ripple.x, swell.x) * 0.6);

    // 按屏幕像素的世界覆盖范围逐层淡出，亚像素波纹不继续参与高光。
    float rippleVisibility = 1 - smoothstep(0.3, 1.8, footprint * WaveScale);
    float fineVisibility = 1 - smoothstep(0.18, 0.9, footprint * MicroScale * 0.5);
    float2 gradient = (ripple.y * along * 0.7 + ripple.z * across * 1.6) * rippleVisibility;
    gradient += (fine.y * float2(0.36, 0.93) + fine.z * float2(-0.93, 0.36)) * 0.22 * fineVisibility;
    gradient += swell.yz * 0.08;
    float3 normal = normalize(float3(-gradient.x * 0.14 * NormalStrength, 1,
                                    -gradient.y * 0.14 * NormalStrength));
    float3 toCamera = CameraPosition.xyz - input.WorldPosition;
    float3 view = toCamera * rsqrt(max(dot(toCamera, toCamera), 0.0001));

    float variation = saturate(0.5 + swell.x * 0.35 + ripple.x * 0.15 * rippleVisibility);
    float3 color = lerp(DeepColor.rgb, ShallowColor.rgb, variation * 0.65);
    float fresnel = pow(1 - saturate(dot(normal, view)), 4) * FresnelStrength;
    color = lerp(color, SkyColor.rgb, fresnel);

    float lightLength = dot(MainLightDirection.xyz, MainLightDirection.xyz);
    float3 light = MainLightDirection.xyz * rsqrt(max(lightLength, 0.0001));
    float3 halfway = view + light;
    halfway *= rsqrt(max(dot(halfway, halfway), 0.0001));
    float variance = dot(ddx(normal), ddx(normal)) + dot(ddy(normal), ddy(normal));
    float filteredShininess = max(8, SunShininess / (1 + variance * SunShininess * 2));
    float sun = pow(saturate(dot(normal, halfway)), filteredShininess) * SunStrength;
    sun *= filteredShininess / SunShininess;
    // 水属于低反射率介质；不能把整个 Blinn 高光直接当成白色反光。
    float waterReflectance = 0.02 + 0.98 * pow(1 - saturate(dot(view, halfway)), 5);
    sun *= waterReflectance * 3;
    float3 sunColor = min(MainLightColor.rgb, float3(4, 4, 4));
    color *= 0.82 + 0.18 * saturate(dot(normal, light));
    color += sunColor * sun * saturate(light.y * 4);

    // 不规则波峰的窄条泡沫。两个平滑阶跃之差保留覆盖率，
    // 不会像单边阈值那样随 fwidth 增大而扩展成宽亮块。
    float bandWidth = 0.025 + FoamAmount * 0.015;
    float aa = max(fwidth(ripple.x), 0.001);
    float foamBand = smoothstep(0.46 - bandWidth - aa, 0.46 - bandWidth + aa, ripple.x)
                   - smoothstep(0.46 + bandWidth - aa, 0.46 + bandWidth + aa, ripple.x);
    float foamVisibility = 1 - smoothstep(0.045, 0.2, footprint * WaveScale);
    float breakup = smoothstep(0.02, 0.55, swell.x) * smoothstep(0.05, 0.55, fine.x);
    float foam = foamBand * breakup * FoamAmount * 0.7 * foamVisibility;
    color = lerp(color, FoamColor.rgb, foam);
    // 自定义 Shader 显式处理距离雾；不依赖尚未声明的内置 Lit 常量。
    float fogDistance = saturate((length(toCamera) - FogStart) / max(FogEnd - FogStart, 0.001));
    float fog = fogDistance * fogDistance * (3 - 2 * fogDistance) * FogMaxOpacity;
    color = lerp(color, FogColor.rgb, fog);
    return float4(max(color, 0), saturate(Opacity + foam * 0.1));
}
#endif
