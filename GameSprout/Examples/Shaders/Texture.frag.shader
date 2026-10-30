// Fragment 的纹理和采样器使用 space2，槽位从 0 开始且连续。
Texture2D<float4> SourceTexture : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    return SourceTexture.Sample(SourceSampler, uv);
}
