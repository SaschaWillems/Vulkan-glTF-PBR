/* Copyright (c) 2018-2026, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

// Combined Slang vertex and fragment shader for the skybox pipeline.

struct VertexInput
{
    [[vk::location(0)]] float3 inPos;
    [[vk::location(1)]] float3 inNormal;
    [[vk::location(2)]] float2 inUV;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 outUVW;
};

struct FragmentInput
{
    [[vk::location(0)]] float3 inUVW;
};

struct SceneUBO
{
    float4x4 projection;
    float4x4 model;
};

struct SkyboxParams
{
    float4 _pad0;
    float exposure;
    float gamma;
};

layout(binding = 0, set = 0) ConstantBuffer<SceneUBO> ubo;
layout(binding = 1, set = 0) ConstantBuffer<SkyboxParams> uboParams;
layout(binding = 2, set = 0) SamplerCube samplerEnv;

float3 Uncharted2Tonemap(float3 color)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

float4 tonemap(float4 color)
{
    float3 outColor = Uncharted2Tonemap(color.rgb * uboParams.exposure);
    outColor *= 1.0 / Uncharted2Tonemap(float3(11.2, 11.2, 11.2));
    return float4(pow(outColor, float3(1.0 / uboParams.gamma, 1.0 / uboParams.gamma, 1.0 / uboParams.gamma)), color.a);
}

float4 SRGBtoLINEAR(float4 srgbIn)
{
    bool3 threshold = step(float3(0.04045, 0.04045, 0.04045), srgbIn.xyz) != 0.0;
    float3 lowPart = srgbIn.xyz / float3(12.92, 12.92, 12.92);
    float3 highPart = pow((srgbIn.xyz + float3(0.055, 0.055, 0.055)) / float3(1.055, 1.055, 1.055), float3(2.4, 2.4, 2.4));
    float3 selector = float3(threshold.x ? 1.0 : 0.0, threshold.y ? 1.0 : 0.0, threshold.z ? 1.0 : 0.0);
    return float4(lerp(lowPart, highPart, selector), srgbIn.w);
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output = {};
    output.outUVW = input.inPos;
    output.position = mul(mul(ubo.projection, ubo.model), float4(input.inPos, 1.0));
    return output;
}

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target0
{
    float3 color = SRGBtoLINEAR(tonemap(samplerEnv.SampleLevel(input.inUVW, 1.5))).rgb;
    return float4(color, 1.0);
}