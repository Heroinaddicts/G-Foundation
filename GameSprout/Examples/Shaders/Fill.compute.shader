// Compute 可写资源使用 space1；执行 Dispatch 的接口之后在 runtime 中接入。
RWTexture2D<float4> Destination : register(u0, space1);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    Destination.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    Destination[id.xy] = float4(float2(id.xy) / float2(width, height), 0, 1);
}
