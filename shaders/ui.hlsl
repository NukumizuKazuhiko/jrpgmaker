// Minimal UI projection shader. UI geometry is already in clip space so the
// RHI seam remains independent of window coordinates and camera state.
struct VSInput {
    float3 position : POSITION;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    return output;
}

float4 ps_main(VSOutput input) : SV_Target {
    return input.color;
}
