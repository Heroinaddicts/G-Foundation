// Vertex 阶段：根据顶点编号生成覆盖屏幕的大三角形，不需要顶点 Buffer。
struct VertexOutput {
    float4 Position : SV_Position; // GPU 使用的裁剪空间位置。
    float2 UV : TEXCOORD0;         // 交给 Fragment 阶段的贴图坐标。
};
VertexOutput main(uint vertexID : SV_VertexID) {
    VertexOutput output;
    output.UV = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.UV * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}
