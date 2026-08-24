// Triangle golden baseline shader (P1, ADR-003: HLSL single source, DXC dual target).
// Since P2 the geometry is provided by a vertex buffer (the RHI v0 gained
// vertex input for glTF mesh rendering); the positions below match the old
// SV_VertexID-generated triangle exactly, so the golden reference is unchanged.

struct VSInput {
    float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    return output;
}

float4 ps_main(VSOutput input) : SV_Target {
    return float4(0.0f, 0.0f, 1.0f, 1.0f);
}