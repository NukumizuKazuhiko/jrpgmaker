// Alpha-masked glyph quads. The atlas is uploaded as white RGB plus glyph
// coverage in alpha, so the same shader supports theme tinting.
#if defined(VULKAN_TARGET)
[[vk::binding(0, 0)]]
Texture2D g_texture : register(t0);
[[vk::binding(1, 0)]]
SamplerState g_sampler : register(s0);
#else
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);
#endif

struct VSInput {
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 ps_main(VSOutput input) : SV_Target {
    return g_texture.Sample(g_sampler, input.uv) * input.color;
}
