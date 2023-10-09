/* Copyright (c) 2018-2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

#version 450
#extension GL_GOOGLE_include_directive : require

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;
layout (location = 3) in vec2 inUV1;
layout (location = 4) in vec4 inColor0;

// Scene bindings

layout (set = 0, binding = 0) uniform UBO {
	mat4 projection;
	mat4 model;
	mat4 view;
	vec3 camPos;
} ubo;

layout (set = 0, binding = 1) uniform UBOParams {
	vec4 lightDir;
	float exposure;
	float gamma;
	float prefilteredCubeMipLevels;
	float scaleIBLAmbient;
	float debugViewInputs;
	float debugViewEquation;
} uboParams;

layout (set = 0, binding = 2) uniform samplerCube samplerIrradiance;
layout (set = 0, binding = 3) uniform samplerCube prefilteredMap;
layout (set = 0, binding = 4) uniform sampler2D samplerBRDFLUT;

// Material bindings

// Textures

layout (set = 1, binding = 0) uniform sampler2D colorMap;
layout (set = 1, binding = 1) uniform sampler2D physicalDescriptorMap;
layout (set = 1, binding = 2) uniform sampler2D normalMap;
layout (set = 1, binding = 3) uniform sampler2D aoMap;
layout (set = 1, binding = 4) uniform sampler2D emissiveMap;

// Properties

#include "includes/shadermaterial.glsl"

layout(std430, set = 3, binding = 0) buffer SSBO
{
   ShaderMaterial materials[ ];
};

layout (push_constant) uniform PushConstants {
	int materialIndex;
} pushConstants;

layout (location = 0) out vec4 outColor;

#include "includes/srgbtolinear.glsl"

void main()
{
	ShaderMaterial material = materials[pushConstants.materialIndex];

	float perceptualRoughness;
	float metallic;
	vec3 diffuseColor;
	vec4 baseColor;

	if (material.baseColorTextureSet > -1) {
		baseColor = SRGBtoLINEAR(texture(colorMap, material.baseColorTextureSet == 0 ? inUV0 : inUV1)) * material.baseColorFactor;
	} else {
		baseColor = material.baseColorFactor;
	}

	outColor = baseColor;
}
