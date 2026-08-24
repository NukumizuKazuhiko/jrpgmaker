// Triangle golden baseline shader (P1, ADR-003: HLSL single source, DXC dual target).
// Geometry is generated in the vertex shader from the vertex index, so the RHI
// v0 has no vertex buffer or input-layout concept.

struct VSOutput {
    float4 position : SV_Position;
};

VSOutput vs_main(uint vertex_index : SV_VertexID) {
    float2 position;
    switch (vertex_index) {
    case 0:
        position = float2(-0.5f, -0.5f);
        break;
    case 1:
        position = float2(0.5f, -0.5f);
        break;
    default:
        position = float2(0.0f, 0.5f);
        break;
    }
    VSOutput output;
    output.position = float4(position, 0.0f, 1.0f);
    return output;
}

float4 ps_main(VSOutput input) : SV_Target {
    return float4(0.0f, 0.0f, 1.0f, 1.0f);
}