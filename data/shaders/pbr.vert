#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;

layout (set = 0, binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 model;
	mat4 view;
	vec3 camPos;
	float flipUV;
} ubo;

layout (set = 2, binding = 0) uniform UBONode {
	mat4 matrix;
	mat4 normalMatrix;
} node;

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
	vec4 locPos = ubo.model * node.matrix * vec4(inPos, 1.0);
	locPos.y = -locPos.y;
	outWorldPos = locPos.xyz / locPos.w;
	outNormal = normalize(transpose(inverse(mat3(ubo.model * node.matrix))) * inNormal);
	outUV = inUV;
	if (ubo.flipUV == 1.0) {
		outUV.t = 1.0 - inUV.t;
	}
	gl_Position =  ubo.projection * ubo.view * vec4(outWorldPos, 1.0);
	//outWorldPos.x *= -1.0f;
	//outWorldPos.z *= -1.0f;
}
