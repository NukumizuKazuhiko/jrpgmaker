// Skinned mesh shader with the generic sampled-texture slot.
// The material meaning remains owned by the selected render-style plugin;
// this shader only provides the RHI texture/sampler transport for the demo.
#define kMaxBonesPerObject 32u

#if defined(VULKAN_TARGET)
[[vk::binding(2, 0)]]
cbuffer PerObjectBones : register(b1) {
    float4x4 g_bones[kMaxBonesPerObject];
};
[[vk::binding(0, 0)]]
Texture2D g_texture : register(t0);
[[vk::binding(1, 0)]]
SamplerState g_sampler : register(s0);
#else
cbuffer PerObjectBones : register(b1) {
    float4x4 g_bones[kMaxBonesPerObject];
};
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);
#endif

struct VSInput {
    float3 position : POSITION;
    uint4 joints : JOINTS;
    float4 weights : WEIGHTS;
    float2 uv : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    float3 skinned = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0u; i < 4u; ++i) {
        uint joint_index = input.joints[i];
        float weight = input.weights[i];
        if (joint_index < kMaxBonesPerObject && weight > 0.0f) {
            skinned += weight * mul(g_bones[joint_index], float4(input.position, 1.0f)).xyz;
        }
    }
    output.position = float4(skinned, 1.0f);
    output.uv = input.uv;
    return output;
}

float4 ps_main(VSOutput input) : SV_Target {
    return g_texture.Sample(g_sampler, input.uv);
}
