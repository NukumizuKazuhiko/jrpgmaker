// Camera scene shader (P2 subtask 6): renders meshes through a view-projection
// matrix passed via push constants (RHI SetPushConstants). The world transform
// is baked into vertex positions on the CPU (v0 static scene; P4 introduces
// per-object uniforms). ADR-003: HLSL single source, DXC dual target.

// The struct layout is column-major (HLSL default) and must match the GLM
// view-projection matrix uploaded by the host byte-for-byte.
//
// Target mapping (ADR-003 single source, dual target):
//  - Vulkan: a [[vk::push_constant]] global matches the RHI pipeline layout's
//    push-constant range (64 bytes, vertex stage).
//  - D3D12: a cbuffer at register b0 matches the RHI root constants.
// compile_shaders.ps1 defines VULKAN_PUSH_CONSTANT for the -spirv targets so
// the correct form is selected per target.
#ifdef VULKAN_PUSH_CONSTANT
struct CameraConstants {
    float4x4 view_projection;
};

[[vk::push_constant]]
CameraConstants camera_constants;
#else
cbuffer CameraConstants : register(b0) {
    float4x4 view_projection;
};
#endif

struct VSInput {
    float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
#ifdef VULKAN_PUSH_CONSTANT
    output.position = mul(camera_constants.view_projection, float4(input.position, 1.0f));
#else
    output.position = mul(view_projection, float4(input.position, 1.0f));
#endif
    return output;
}

float4 ps_main(VSOutput input) : SV_Target {
    return float4(1.0f, 0.5f, 0.0f, 1.0f);
}