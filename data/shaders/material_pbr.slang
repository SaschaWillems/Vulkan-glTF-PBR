/* Copyright (c) 2018-2024, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

// Slang port of the Vulkan GLSL PBR fragment shader based on the Khronos glTF PBR implementation.

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

struct PBRInfo
{
    float NdotL;
    float NdotV;
    float NdotH;
    float LdotH;
    float VdotH;
    float perceptualRoughness;
    float metalness;
    float3 reflectance0;
    float3 reflectance90;
    float alphaRoughness;
    float3 diffuseColor;
    float3 specularColor;
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

static const float M_PI = 3.141592653589793;
static const float c_MinRoughness = 0.04;

static const float PBR_WORKFLOW_METALLIC_ROUGHNESS = 0.0;
static const float PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1.0;

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
    float3 outcol = Uncharted2Tonemap(color.rgb * uboParams.exposure);
    outcol = outcol * (1.0 / Uncharted2Tonemap(float3(11.2, 11.2, 11.2)));
    return float4(pow(outcol, float3(1.0 / uboParams.gamma, 1.0 / uboParams.gamma, 1.0 / uboParams.gamma)), color.a);
}

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

float3 getNormal(FragmentInput input, ShaderMaterial material)
{
    float3 tangentNormal = normalMap.Sample(selectUV(input, material.normalTextureSet)).xyz * 2.0 - 1.0;

    float3 q1 = ddx(input.inWorldPos);
    float3 q2 = ddy(input.inWorldPos);
    float2 st1 = ddx(input.inUV0);
    float2 st2 = ddy(input.inUV0);

    float3 N = normalize(input.inNormal);
    float3 T = normalize(q1 * st2.y - q2 * st1.y);
    float3 B = -normalize(cross(N, T));
    float3x3 TBN = float3x3(T, B, N);

    return normalize(mul(TBN, tangentNormal));
}

float3 getIBLContribution(PBRInfo pbrInputs, float3 n, float3 reflection)
{
    float lod = pbrInputs.perceptualRoughness * uboParams.prefilteredCubeMipLevels;
    float3 brdf = samplerBRDFLUT.Sample(float2(pbrInputs.NdotV, 1.0 - pbrInputs.perceptualRoughness)).rgb;
    float3 diffuseLight = SRGBtoLINEAR(tonemap(samplerIrradiance.Sample(n))).rgb;
    float3 specularLight = SRGBtoLINEAR(tonemap(prefilteredMap.SampleLevel(reflection, lod))).rgb;

    float3 diffuse = diffuseLight * pbrInputs.diffuseColor;
    float3 specular = specularLight * (pbrInputs.specularColor * brdf.x + brdf.y);

    diffuse *= uboParams.scaleIBLAmbient;
    specular *= uboParams.scaleIBLAmbient;

    return diffuse + specular;
}

float3 diffuse(PBRInfo pbrInputs)
{
    return pbrInputs.diffuseColor / M_PI;
}

float3 specularReflection(PBRInfo pbrInputs)
{
    return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance0) * pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
}

float geometricOcclusion(PBRInfo pbrInputs)
{
    float NdotL = pbrInputs.NdotL;
    float NdotV = pbrInputs.NdotV;
    float r = pbrInputs.alphaRoughness;

    float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
    float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
    return attenuationL * attenuationV;
}

float microfacetDistribution(PBRInfo pbrInputs)
{
    float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
    float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
    return roughnessSq / (M_PI * f * f);
}

float convertMetallic(float3 diffuse, float3 specular, float maxSpecular)
{
    float perceivedDiffuse = sqrt(0.299 * diffuse.r * diffuse.r + 0.587 * diffuse.g * diffuse.g + 0.114 * diffuse.b * diffuse.b);
    float perceivedSpecular = sqrt(0.299 * specular.r * specular.r + 0.587 * specular.g * specular.g + 0.114 * specular.b * specular.b);
    if (perceivedSpecular < c_MinRoughness)
    {
        return 0.0;
    }

    float a = c_MinRoughness;
    float b = perceivedDiffuse * (1.0 - maxSpecular) / (1.0 - c_MinRoughness) + perceivedSpecular - 2.0 * c_MinRoughness;
    float c = c_MinRoughness - perceivedSpecular;
    float D = max(b * b - 4.0 * a * c, 0.0);
    return clamp((-b + sqrt(D)) / (2.0 * a), 0.0, 1.0);
}

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target0
{
    ShaderMaterial material = materials[pushConstants.materialIndex];

    float perceptualRoughness = 0.0;
    float metallic = 0.0;
    float3 diffuseColor = 0.0;
    float4 baseColor = 0.0;

    float3 f0 = float3(0.04, 0.04, 0.04);

    if (material.alphaMask == 1.0)
    {
        if (material.baseColorTextureSet > -1)
        {
            baseColor = SRGBtoLINEAR(colorMap.Sample(selectUV(input, material.baseColorTextureSet))) * material.baseColorFactor;
        }
        else
        {
            baseColor = material.baseColorFactor;
        }

        if (baseColor.a < material.alphaMaskCutoff)
        {
            discard;
        }
    }

    if (material.workflow == PBR_WORKFLOW_METALLIC_ROUGHNESS)
    {
        perceptualRoughness = material.roughnessFactor;
        metallic = material.metallicFactor;

        if (material.physicalDescriptorTextureSet > -1)
        {
            float4 mrSample = physicalDescriptorMap.Sample(selectUV(input, material.physicalDescriptorTextureSet));
            perceptualRoughness = mrSample.g * perceptualRoughness;
            metallic = mrSample.b * metallic;
        }
        else
        {
            perceptualRoughness = clamp(perceptualRoughness, c_MinRoughness, 1.0);
            metallic = clamp(metallic, 0.0, 1.0);
        }

        if (material.baseColorTextureSet > -1)
        {
            baseColor = SRGBtoLINEAR(colorMap.Sample(selectUV(input, material.baseColorTextureSet))) * material.baseColorFactor;
        }
        else
        {
            baseColor = material.baseColorFactor;
        }
    }

    if (material.workflow == PBR_WORKFLOW_SPECULAR_GLOSSINESS)
    {
        if (material.physicalDescriptorTextureSet > -1)
        {
            perceptualRoughness = 1.0 - physicalDescriptorMap.Sample(selectUV(input, material.physicalDescriptorTextureSet)).a;
        }
        else
        {
            perceptualRoughness = 0.0;
        }

        const float epsilon = 1e-6;

        float4 diffuseSample = SRGBtoLINEAR(colorMap.Sample(input.inUV0));
        float3 specular = SRGBtoLINEAR(physicalDescriptorMap.Sample(input.inUV0)).rgb;
        float maxSpecular = max(max(specular.r, specular.g), specular.b);

        metallic = convertMetallic(diffuseSample.rgb, specular, maxSpecular);

        float3 baseColorDiffusePart = diffuseSample.rgb * ((1.0 - maxSpecular) / (1.0 - c_MinRoughness) / max(1.0 - metallic, epsilon)) * material.diffuseFactor.rgb;
        float3 baseColorSpecularPart = specular - (float3(c_MinRoughness, c_MinRoughness, c_MinRoughness) * (1.0 - metallic) * (1.0 / max(metallic, epsilon))) * material.specularFactor.rgb;
        baseColor = float4(lerp(baseColorDiffusePart, baseColorSpecularPart, metallic * metallic), diffuseSample.a);
    }

    baseColor *= input.inColor0;

    diffuseColor = baseColor.rgb * (float3(1.0, 1.0, 1.0) - f0);
    diffuseColor *= 1.0 - metallic;

    float alphaRoughness = perceptualRoughness * perceptualRoughness;
    float3 specularColor = lerp(f0, baseColor.rgb, metallic);

    float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
    float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
    float3 specularEnvironmentR0 = specularColor.rgb;
    float3 specularEnvironmentR90 = float3(1.0, 1.0, 1.0) * reflectance90;

    float3 n = material.normalTextureSet > -1 ? getNormal(input, material) : normalize(input.inNormal);
    n.y *= -1.0;
    float3 v = normalize(ubo.camPos - input.inWorldPos);
    float3 l = normalize(uboParams.lightDir.xyz);
    float3 h = normalize(l + v);
    float3 reflection = normalize(reflect(-v, n));

    float NdotL = clamp(dot(n, l), 0.001, 1.0);
    float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
    float NdotH = clamp(dot(n, h), 0.0, 1.0);
    float LdotH = clamp(dot(l, h), 0.0, 1.0);
    float VdotH = clamp(dot(v, h), 0.0, 1.0);

    PBRInfo pbrInputs = {
        NdotL,
        NdotV,
        NdotH,
        LdotH,
        VdotH,
        perceptualRoughness,
        metallic,
        specularEnvironmentR0,
        specularEnvironmentR90,
        alphaRoughness,
        diffuseColor,
        specularColor
    };

    float3 F = specularReflection(pbrInputs);
    float G = geometricOcclusion(pbrInputs);
    float D = microfacetDistribution(pbrInputs);

    static const float3 u_LightColor = float3(1.0, 1.0, 1.0);

    float3 diffuseContrib = (1.0 - F) * diffuse(pbrInputs);
    float3 specContrib = F * G * D / (4.0 * NdotL * NdotV);
    float3 color = NdotL * u_LightColor * (diffuseContrib + specContrib);

    color += getIBLContribution(pbrInputs, n, reflection);

    const float u_OcclusionStrength = 1.0;
    if (material.occlusionTextureSet > -1)
    {
        float ao = aoMap.Sample(selectUV(input, material.occlusionTextureSet)).r;
        color = lerp(color, color * ao, u_OcclusionStrength);
    }

    float3 emissive = material.emissiveFactor.rgb * material.emissiveStrength;
    if (material.emissiveTextureSet > -1)
    {
        emissive *= SRGBtoLINEAR(emissiveMap.Sample(selectUV(input, material.emissiveTextureSet))).rgb;
    }
    color += emissive;

    float4 outColor = float4(color, baseColor.a);

    if (uboParams.debugViewInputs > 0.0)
    {
        int index = int(uboParams.debugViewInputs);
        switch (index)
        {
        case 1:
            outColor = material.baseColorTextureSet > -1 ? colorMap.Sample(selectUV(input, material.baseColorTextureSet)) : float4(1.0, 1.0, 1.0, 1.0);
            break;
        case 2:
            outColor.rgb = material.normalTextureSet > -1 ? normalMap.Sample(selectUV(input, material.normalTextureSet)).rgb : normalize(input.inNormal);
            break;
        case 3:
            outColor.rgb = material.occlusionTextureSet > -1 ? aoMap.Sample(selectUV(input, material.occlusionTextureSet)).rrr : float3(0.0, 0.0, 0.0);
            break;
        case 4:
            outColor.rgb = material.emissiveTextureSet > -1 ? emissiveMap.Sample(selectUV(input, material.emissiveTextureSet)).rgb : float3(0.0, 0.0, 0.0);
            break;
        case 5:
            outColor.rgb = physicalDescriptorMap.Sample(input.inUV0).bbb;
            break;
        case 6:
            outColor.rgb = physicalDescriptorMap.Sample(input.inUV0).ggg;
            break;
        default:
            break;
        }
        outColor = SRGBtoLINEAR(outColor);
    }

    if (uboParams.debugViewEquation > 0.0)
    {
        int index = int(uboParams.debugViewEquation);
        switch (index)
        {
        case 1:
            outColor.rgb = diffuseContrib;
            break;
        case 2:
            outColor.rgb = F;
            break;
        case 3:
            outColor.rgb = G.xxx;
            break;
        case 4:
            outColor.rgb = D.xxx;
            break;
        case 5:
            outColor.rgb = specContrib;
            break;
        default:
            break;
        }
    }

    return outColor;
}