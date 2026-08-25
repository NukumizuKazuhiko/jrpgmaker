// Skinned-mesh shader (P4): per-vertex bone weights transform a position by up
// to kMaxBonesPerObject bone matrices supplied through the RHI per-object
// vertex-uniform buffer (D3D12 root CBV b1 / Vulkan UBO binding 2 in set 0).
//
// ADR-003: HLSL single source, DXC dual target.
//
#define kMaxBonesPerObject 32u

// Descriptor binding (single source, dual target):
//  - D3D12: cbuffer at register b1 matches the root signature's root CBV
//    parameter 3 (vertex visibility).
//  - Vulkan: [[vk::binding(2, 0)]] pins the constant buffer to set 0 / binding
//    2, matching the Vulkan backend's UBO descriptor (vertex stage). The
//    bindless-capable array is declared with a fixed stride; the RHI contract
//    uploads exactly vertex_uniform_size bytes, so the shader must not read
//    beyond min(bone_count, kMaxBones).
#if defined(VULKAN_TARGET)
[[vk::binding(2, 0)]]
cbuffer PerObjectBones : register(b1) {
    float4x4 g_bones[kMaxBonesPerObject];
};
#else
cbuffer PerObjectBones : register(b1) {
    float4x4 g_bones[kMaxBonesPerObject];
};
#endif

struct VSInput {
    float3 position : POSITION;
    // JOINTS_0 decoded by the importer into u16; the RHI uploads them as raw
    // u16x4. HLSL declares uint4; D3D12 maps R16G16B16A16_UINT, Vulkan maps
    // R16G16B16A16_UINT, so both backends consume the same 8-byte slot.
    uint4 joints : JOINTS;
    float4 weights : WEIGHTS;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    // Transform the bind-pose position by the four influencing bones and mix by
    // the vertex weights. The joint index 0xFFFF (no influence) is skipped via
    // its zero weight; clamping the index guards against stray u16 reads.
    float3 skinned = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0u; i < 4u; ++i) {
        uint joint_index = input.joints[i];
        float weight = input.weights[i];
        if (joint_index < kMaxBonesPerObject && weight > 0.0f) {
            skinned += weight * mul(g_bones[joint_index], float4(input.position, 1.0f)).xyz;
        }
    }
    output.position = float4(skinned, 1.0f);
    // Diagnostic color: encode per-vertex blend so the golden image visibly
    // differs between poses (brightness varies with the effective weight sum).
    output.color = float3(saturate(length(skinned)) * 0.5f, 0.0f, 1.0f);
    return output;
}

float4 ps_main(VSOutput input) : SV_Target {
    return float4(input.color, 1.0f);
}