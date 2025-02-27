#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inColor;

layout (binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 view;
	vec4 lightPos;
} ubo;

layout (std140, push_constant) uniform PushConsts 
{
	mat4 model;
	vec3 color;
} pushConsts;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 3) out vec3 outViewVec;
layout (location = 4) out vec3 outLightVec;

void main() 
{
	outNormal = inNormal;
	outColor = inColor * pushConsts.color;
	
	gl_Position = ubo.projection * ubo.view * pushConsts.model * vec4(inPos.xyz, 1.0);
	
    vec4 pos = ubo.view * pushConsts.model * vec4(inPos, 1.0);
    outNormal = mat3(ubo.view * pushConsts.model) * inNormal;
	vec3 lPos = ubo.lightPos.xyz;
    outLightVec = lPos - pos.xyz;
    outViewVec = -pos.xyz;
}