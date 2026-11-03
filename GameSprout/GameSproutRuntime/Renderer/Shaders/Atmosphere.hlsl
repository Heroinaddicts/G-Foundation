// Scene atmosphere is independent from diffuse ambient lighting. It is
// opt-in, and all colors remain linear until the target's display conversion.
float3 ApplySceneFog(float3 color, float3 worldPosition)
{
    if (AtmosphereParameters.x < 0.5) return color;
    float distanceToCamera = length(CameraPosition.xyz - worldPosition);
    float amount = smoothstep(AtmosphereParameters.y, AtmosphereParameters.z, distanceToCamera)
                 * AtmosphereParameters.w;
    return lerp(color, AtmosphereFogColor.rgb, amount);
}

// An unbuffered full-screen triangle; no scene GameObject, mesh or component
// is created. Removing view translation keeps the sky infinitely distant.
cbuffer SkyVertexUniform : register(b0, space1)
{
    float4 SkyCameraRight;
    float4 SkyCameraUp;
    float4 SkyCameraForward;
    float4 SkyProjection; // Perspective X/Y extent at unit depth; zero for orthographic.
};
cbuffer SkyFragmentUniform : register(b0, space3)
{
    float4 SkyZenith;
    float4 SkyHorizon;
    float4 SkySunDirection;
    float4 SkySunRadiance;
};
struct SkyOutput
{
    float4 Position : SV_Position;
    float3 RayDirection : TEXCOORD0;
};
SkyOutput SkyVertex(uint vertexID : SV_VertexID)
{
    SkyOutput output;
    float2 corner = float2((vertexID << 1) & 2, vertexID & 2);
    float2 clip = corner * 2.0 - 1.0;
    output.Position = float4(clip, 1.0, 1.0);
    output.RayDirection = SkyCameraForward.xyz + SkyCameraRight.xyz * (clip.x * SkyProjection.x)
                        + SkyCameraUp.xyz * (clip.y * SkyProjection.y);
    return output;
}
float4 SkyFragment(SkyOutput input) : SV_Target0
{
    float3 ray = normalize(input.RayDirection);
    float height = pow(saturate(ray.y), 0.65);
    float3 color = lerp(SkyHorizon.rgb, SkyZenith.rgb, height);
    // A small antialiased sun and restrained broad glow preserve the palette;
    // they use the scene's actual brightest directional light, never a fixed one.
    if (SkySunDirection.w > 0.5 && SkySunDirection.y > -0.05)
    {
        float facing = dot(ray, SkySunDirection.xyz);
        float edge = max(fwidth(facing), 0.00002);
        float disc = smoothstep(0.9998 - edge, 0.9998 + edge, facing);
        float glow = pow(saturate(facing), 128.0) * 0.04;
        color += min(SkySunRadiance.rgb, 2.0) * (disc * 0.65 + glow);
    }
    return float4(color, 1.0);
}
