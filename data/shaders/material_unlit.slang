/* Copyright (c) 2018-2026, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

// Slang port of the Vulkan GLSL unlit fragment shader.

struct FragmentInput
{
    [[vk::location(0)]] float3 inWorldPos;
    [[vk::location(1)]] float3 inNormal;
    [[vk::location(2)]] float2 inUV0;
    [[vk::location(3)]] float2 inUV1;
    [[vk::location(4)]] float4 inColor0;
};

struct SceneUBO
{
    float4x4 projection;
    float4x4 model;
    float4x4 view;
    float3 camPos;
};

struct SceneParams
{
    float4 lightDir;
    float exposure;
    float gamma;
    float prefilteredCubeMipLevels;
    float scaleIBLAmbient;
    float debugViewInputs;
    float debugViewEquation;
};

struct ShaderMaterial
{
    float4 baseColorFactor;
    float4 emissiveFactor;
    float4 diffuseFactor;
    float4 specularFactor;
    float workflow;
    int baseColorTextureSet;
    int physicalDescriptorTextureSet;
    int normalTextureSet;
    int occlusionTextureSet;
    int emissiveTextureSet;
    float metallicFactor;
    float roughnessFactor;
    float alphaMask;
    float alphaMaskCutoff;
    float emissiveStrength;
};

struct PushConstants
{
    int meshIndex;
    int materialIndex;
};

layout(binding = 0, set = 0) ConstantBuffer<SceneUBO> ubo;
layout(binding = 1, set = 0) ConstantBuffer<SceneParams> uboParams;
layout(binding = 2, set = 0) SamplerCube samplerIrradiance;
layout(binding = 3, set = 0) SamplerCube prefilteredMap;
layout(binding = 4, set = 0) Sampler2D samplerBRDFLUT;

layout(binding = 0, set = 1) Sampler2D colorMap;
layout(binding = 1, set = 1) Sampler2D physicalDescriptorMap;
layout(binding = 2, set = 1) Sampler2D normalMap;
layout(binding = 3, set = 1) Sampler2D aoMap;
layout(binding = 4, set = 1) Sampler2D emissiveMap;

layout(binding = 0, set = 3) StructuredBuffer<ShaderMaterial> materials;

[[vk::push_constant]] ConstantBuffer<PushConstants> pushConstants;

float4 SRGBtoLINEAR(float4 srgbIn)
{
    bool3 bLess = step(float3(0.04045, 0.04045, 0.04045), srgbIn.xyz) != 0.0;
    float3 lowPart = srgbIn.xyz / float3(12.92, 12.92, 12.92);
    float3 highPart = pow((srgbIn.xyz + float3(0.055, 0.055, 0.055)) / float3(1.055, 1.055, 1.055), float3(2.4, 2.4, 2.4));
    float3 selector = float3(bLess.x ? 1.0 : 0.0, bLess.y ? 1.0 : 0.0, bLess.z ? 1.0 : 0.0);
    return float4(lerp(lowPart, highPart, selector), srgbIn.w);
}

float2 selectUV(FragmentInput input, int textureSet)
{
    return textureSet == 0 ? input.inUV0 : input.inUV1;
}

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target0
{
    ShaderMaterial material = materials[pushConstants.materialIndex];

    float4 baseColor;
    if (material.baseColorTextureSet > -1)
    {
        baseColor = SRGBtoLINEAR(colorMap.Sample(selectUV(input, material.baseColorTextureSet))) * material.baseColorFactor;
    }
    else
    {
        baseColor = material.baseColorFactor;
    }

    return baseColor;
}