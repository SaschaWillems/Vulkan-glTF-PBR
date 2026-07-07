/* Copyright (c) 2018-2025, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

struct VertexInput
{
    [[vk::location(0)]] float3 inPos;
    [[vk::location(1)]] float3 inNormal;
    [[vk::location(2)]] float2 inUV0;
    [[vk::location(3)]] float2 inUV1;
    [[vk::location(4)]] uint4 inJoint0;
    [[vk::location(5)]] float4 inWeight0;
    [[vk::location(6)]] float4 inColor0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 outWorldPos;
    [[vk::location(1)]] float3 outNormal;
    [[vk::location(2)]] float2 outUV0;
    [[vk::location(3)]] float2 outUV1;
    [[vk::location(4)]] float4 outColor0;
};

struct SceneUBO
{
    float4x4 projection;
    float4x4 model;
    float4x4 view;
    float3 camPos;
};

static const int MAX_NUM_JOINTS = 128;

struct MeshShaderDataBlock
{
    float4x4 matrix;
    float4x4 jointMatrix[MAX_NUM_JOINTS];
    uint jointCount;
};

struct PushConstants
{
    int meshIndex;
    int materialIndex;
};

layout(binding = 0, set = 0) ConstantBuffer<SceneUBO> ubo;
layout(binding = 0, set = 2) StructuredBuffer<MeshShaderDataBlock> meshData;

[[vk::push_constant]] ConstantBuffer<PushConstants> pushConstants;

float3x3 inverse3x3(float3x3 m)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float invDet = 1.0 / dot(c2, m[2]);
    return transpose(float3x3(c0 * invDet, c1 * invDet, c2 * invDet));
}

float4x4 getSkinMatrix(VertexInput input, MeshShaderDataBlock mesh)
{
    return
        input.inWeight0.x * mesh.jointMatrix[input.inJoint0.x] +
        input.inWeight0.y * mesh.jointMatrix[input.inJoint0.y] +
        input.inWeight0.z * mesh.jointMatrix[input.inJoint0.z] +
        input.inWeight0.w * mesh.jointMatrix[input.inJoint0.w];
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    MeshShaderDataBlock mesh = meshData[pushConstants.meshIndex];

    VertexOutput output = {};
    output.outColor0 = input.inColor0;

    float4 locPos;

    if (mesh.jointCount > 0)
    {
        float4x4 skinMat = getSkinMatrix(input, mesh);
        float4x4 worldMat = mul(mul(ubo.model, mesh.matrix), skinMat);
        locPos = mul(worldMat, float4(input.inPos, 1.0));

        float3x3 normalMat = float3x3(worldMat[0].xyz, worldMat[1].xyz, worldMat[2].xyz);
        output.outNormal = normalize(mul(transpose(inverse3x3(normalMat)), input.inNormal));
    }
    else
    {
        float4x4 worldMat = mul(ubo.model, mesh.matrix);
        locPos = mul(worldMat, float4(input.inPos, 1.0));

        float3x3 normalMat = float3x3(worldMat[0].xyz, worldMat[1].xyz, worldMat[2].xyz);
        output.outNormal = normalize(mul(transpose(inverse3x3(normalMat)), input.inNormal));
    }

    locPos.y = -locPos.y;
    output.outWorldPos = locPos.xyz / locPos.w;
    output.outUV0 = input.inUV0;
    output.outUV1 = input.inUV1;
    output.position = mul(mul(ubo.projection, ubo.view), float4(output.outWorldPos, 1.0));

    return output;
}