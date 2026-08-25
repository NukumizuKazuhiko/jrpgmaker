// Sampled-texture quad shader (P3 DEBT-029 texture pipeline).
// Renders a fullscreen quad sampling a host-uploaded texture through the RHI
// v0 sampled-texture slot. The pixel shader samples register t0 with sampler
// s0, which maps to the RHI SetSampledTexture(tex, sampler) binding.
// ADR-003: HLSL single source, DXC dual target.
//
// Descriptor binding (single source, dual target):
//  - Vulkan: [[vk::binding]] pins the texture to set 0 / binding 0 and the
//    sampler to set 0 / binding 1, matching the Vulkan backend's descriptor
//    set layout (SAMPLED_IMAGE at binding 0, SAMPLER at binding 1).
//  - D3D12: register(t0)/register(s0) matches the root-signature descriptor
//    tables (SRV t0, sampler s0, pixel visibility).
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
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    output.uv = input.uv;
    return output;
}

float4 ps_main(VSOutput input) : SV_Target {
    return g_texture.Sample(g_sampler, input.uv);
}