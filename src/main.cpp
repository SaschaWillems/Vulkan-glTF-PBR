/*
* Vulkan Example - Physical based rendering a glTF 2.0 model (metal/roughness workflow) with image based lighting
*
* Note: Requires the separate asset pack (see data/README.md)
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

// PBR reference: http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// glTF format: https://github.com/KhronosGroup/glTF
// tinyglTF loader: https://github.com/syoyo/tinygltf

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <chrono>

#include <vulkan/vulkan.h>
#include "VulkanExampleBase.h"
#include "VulkanTexture.hpp"
#include "VulkanglTFModel.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "tiny_gltf.h"

/*
	Utility functions
*/
VkPipelineShaderStageCreateInfo loadShader(VkDevice device, std::string filename, VkShaderStageFlagBits stage)
{
	VkPipelineShaderStageCreateInfo shaderStage{};
	shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStage.stage = stage;
	shaderStage.pName = "main";
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	std::string assetpath = "shaders/" + filename;
	AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, assetpath.c_str(), AASSET_MODE_STREAMING);
	assert(asset);
	size_t size = AAsset_getLength(asset);
	assert(size > 0);
	char *shaderCode = new char[size];
	AAsset_read(asset, shaderCode, size);
	AAsset_close(asset);
	VkShaderModule shaderModule;
	VkShaderModuleCreateInfo moduleCreateInfo;
	moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleCreateInfo.pNext = NULL;
	moduleCreateInfo.codeSize = size;
	moduleCreateInfo.pCode = (uint32_t*)shaderCode;
	moduleCreateInfo.flags = 0;
	VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderStage.module));
	delete[] shaderCode;
#else
	std::ifstream is("./../data/shaders/" + filename, std::ios::binary | std::ios::in | std::ios::ate);

	if (is.is_open()) {
		size_t size = is.tellg();
		is.seekg(0, std::ios::beg);
		char* shaderCode = new char[size];
		is.read(shaderCode, size);
		is.close();
		assert(size > 0);
		VkShaderModuleCreateInfo moduleCreateInfo{};
		moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleCreateInfo.codeSize = size;
		moduleCreateInfo.pCode = (uint32_t*)shaderCode;
		vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderStage.module);
		delete[] shaderCode;
	}
	else {
		std::cerr << "Error: Could not open shader file \"" << filename << "\"" << std::endl;
		shaderStage.module = VK_NULL_HANDLE;
	}

#endif
	assert(shaderStage.module != VK_NULL_HANDLE);
	return shaderStage;
}

/*
	PBR example main class
*/
class VulkanExample : public VulkanExampleBase
{
public:
	struct Textures {
		vks::TextureCubeMap environmentCube;
		vks::Texture2D empty;
		vks::Texture2D lutBrdf;
		vks::TextureCubeMap irradianceCube;
		vks::TextureCubeMap prefilteredCube;
	} textures;

	struct Models {
		vkglTF::Model object;
		vkglTF::Model skybox;
	} models;

	struct Buffer {
		VkBuffer buffer;
		VkDeviceMemory memory;
		VkDescriptorBufferInfo descriptor;
		void *mapped;
	};

	struct UniformBuffers {
		Buffer object;
		Buffer skybox;
		Buffer params;
	} uniformBuffers;

	struct UBOMatrices {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec3 camPos;
	} uboMatrices;

	struct UBOParams {
		glm::vec4 lightDir = glm::vec4(0.0f, -0.5f, -0.5f, 1.0f);
		float exposure = 4.5f;
		float gamma = 2.2f;
	} uboParams;

	VkPipelineLayout pipelineLayout;

	struct Pipelines {
		VkPipeline skybox;
		VkPipeline pbr;
		VkPipeline pbrAlphaBlend;
	} pipelines;

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout scene;
		VkDescriptorSetLayout material;
	} descriptorSetLayouts;

	struct DescriptorSets {
		VkDescriptorSet object;
		VkDescriptorSet skybox;
	} descriptorSets;

	glm::vec3 rotation = glm::vec3(0.0f);

	struct PushConstBlockMaterial {
		float hasBaseColorTexture;
		float hasMetallicRoughnessTexture;
		float hasNormalTexture;
		float hasOcclusionTexture;
		float hasEmissiveTexture;
	} pushConstBlockMaterial;

	VulkanExample() : VulkanExampleBase()
	{
		title = "Vulkan glTf 2.0 PBR";
		camera.type = Camera::CameraType::firstperson;
		camera.movementSpeed = 2.0f;
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		camera.rotationSpeed = 0.25f;
		camera.setRotation({ -12.0f, 152.0f, 0.0f });
		camera.setPosition({ 1.05f, 0.31f, 1.85f });
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.skybox, nullptr);
		vkDestroyPipeline(device, pipelines.pbr, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material, nullptr);

		models.object.destroy(device);
		models.skybox.destroy(device);

		vkDestroyBuffer(device, uniformBuffers.object.buffer, nullptr);
		vkFreeMemory(device, uniformBuffers.object.memory, nullptr);
		vkDestroyBuffer(device, uniformBuffers.skybox.buffer, nullptr);
		vkFreeMemory(device, uniformBuffers.skybox.memory, nullptr);
		vkDestroyBuffer(device, uniformBuffers.params.buffer, nullptr);
		vkFreeMemory(device, uniformBuffers.params.memory, nullptr);

		textures.environmentCube.destroy();
		textures.irradianceCube.destroy();
		textures.prefilteredCube.destroy();
		textures.lutBrdf.destroy();
	}

	void renderPrimitive(vkglTF::Model &model, vkglTF::Primitive &primitive, VkCommandBuffer commandBuffer) {
		std::array<VkDescriptorSet, 2> descriptorsets = {
			descriptorSets.object,
			primitive.material.descriptorSet,
		};

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, descriptorsets.data(), 0, NULL);

		// Pass material parameters as push constants
		PushConstBlockMaterial pushConstBlockMaterial{
			static_cast<float>(primitive.material.hasBaseColorTexture),
			static_cast<float>(primitive.material.hasMetallicRoughnessTexture),
			static_cast<float>(primitive.material.hasNormalTexture),
			static_cast<float>(primitive.material.hasOcclusionTexture),
			static_cast<float>(primitive.material.hasEmissiveTexture),
			static_cast<float>(primitive.material.alphaMode == vkglTF::Material::ALPHAMODE_MASK),
			primitive.material.alphaCutoff
		};
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstBlockMaterial), &pushConstBlockMaterial);

		vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufferBeginInfo{};
		cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		VkClearValue clearValues[3];
		if (settings.multiSampling) {
			clearValues[0].color = { { 1.0f, 1.0f, 1.0f, 1.0f } };
			clearValues[1].color = { { 1.0f, 1.0f, 1.0f, 1.0f } };
			clearValues[2].depthStencil = { 1.0f, 0 };
		}
		else {
			clearValues[0].color = { { 0.1f, 0.1f, 0.1f, 1.0f } };
			clearValues[1].depthStencil = { 1.0f, 0 };
		}

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = settings.multiSampling ? 3 : 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (size_t i = 0; i < drawCmdBuffers.size(); ++i) {
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufferBeginInfo));
			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport{};
			viewport.width = (float)width;
			viewport.height = (float)height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor{};
			scissor.extent = { width, height };
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.skybox, 0, NULL);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skybox);
			models.skybox.draw(drawCmdBuffers[i]);

			vkglTF::Model &model = models.object;
			vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &model.vertices.buffer, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

			// Opaque primitives first
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbr);
			for (auto primitive : model.primitives) {
				if (primitive.material.alphaMode != vkglTF::Material::ALPHAMODE_BLEND) {
					renderPrimitive(model, primitive, drawCmdBuffers[i]);
				}
			}

			// Transparent last
			// TODO: Correct depth sorting
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbrAlphaBlend);
			for (auto primitive : model.primitives) {
				if (primitive.material.alphaMode == vkglTF::Material::ALPHAMODE_BLEND) {
					renderPrimitive(model, primitive, drawCmdBuffers[i]);
				}
			}

			vkCmdEndRenderPass(drawCmdBuffers[i]);
			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void loadAssets()
	{
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
		const std::string assetpath = "";
#else
		const std::string assetpath = "./../data/";
		struct stat info;
		if (stat(assetpath.c_str(), &info) != 0) {
			std::string msg = "Could not locate asset path in \"" + assetpath + "\".\nMake sure binary is run from correct relative directory!";
			std::cerr << msg << std::endl;
#if defined(_WIN32)
			MessageBox(NULL, msg.c_str(), "Fatal error", MB_OK | MB_ICONERROR);
#endif
			exit(-1);
		}
#endif
		textures.empty.loadFromFile(assetpath + "textures/empty.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.environmentCube.loadFromFile(assetpath + "textures/papermill_hdr16f_cube.ktx", VK_FORMAT_R16G16B16A16_SFLOAT, vulkanDevice, queue);
		models.skybox.loadFromFile(assetpath + "models/Box/glTF-Embedded/Box.gltf", vulkanDevice, queue);
		models.object.loadFromFile(assetpath + "models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf", vulkanDevice, queue);
	}

	void setupDescriptors()
	{
		/*
			Descriptor Pool
		*/
		uint32_t imageSamplerCount = 0;
		uint32_t materialCount = 0;
		// Environment samplers (radiance, irradiance, brdf lut)
		imageSamplerCount += 3;
		// Model materials
		std::vector<vkglTF::Model*> modellist = { &models.skybox, &models.object };
		for (auto &model : modellist) {
			for (auto &material : model->materials) {
				imageSamplerCount += 5;
				materialCount++;
			}
		}

		std::vector<VkDescriptorPoolSize> poolSizes = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageSamplerCount }
		};
		VkDescriptorPoolCreateInfo descriptorPoolCI{};
		descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolCI.poolSizeCount = 2;
		descriptorPoolCI.pPoolSizes = poolSizes.data();
		descriptorPoolCI.maxSets = 2 + materialCount;
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

		/*
			Descriptor sets
		*/

		// Scene (matrices and environment maps)
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			};
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.scene));

			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorPool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.scene;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.object));

			std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};

			writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[0].descriptorCount = 1;
			writeDescriptorSets[0].dstSet = descriptorSets.object; // TODO: Rename to scene?
			writeDescriptorSets[0].dstBinding = 0;
			writeDescriptorSets[0].pBufferInfo = &uniformBuffers.object.descriptor;

			writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[1].descriptorCount = 1;
			writeDescriptorSets[1].dstSet = descriptorSets.object;
			writeDescriptorSets[1].dstBinding = 1;
			writeDescriptorSets[1].pBufferInfo = &uniformBuffers.params.descriptor;

			writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[2].descriptorCount = 1;
			writeDescriptorSets[2].dstSet = descriptorSets.object;
			writeDescriptorSets[2].dstBinding = 2;
			writeDescriptorSets[2].pImageInfo = &textures.irradianceCube.descriptor;

			writeDescriptorSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[3].descriptorCount = 1;
			writeDescriptorSets[3].dstSet = descriptorSets.object;
			writeDescriptorSets[3].dstBinding = 3;
			writeDescriptorSets[3].pImageInfo = &textures.prefilteredCube.descriptor;

			writeDescriptorSets[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[4].descriptorCount = 1;
			writeDescriptorSets[4].dstSet = descriptorSets.object;
			writeDescriptorSets[4].dstBinding = 4;
			writeDescriptorSets[4].pImageInfo = &textures.lutBrdf.descriptor;

			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
		}

		// Material (samplers)
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			};
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.material));

			// Per-Material descriptor sets
			for (auto &material : models.object.materials) {
				VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
				descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				descriptorSetAllocInfo.descriptorPool = descriptorPool;
				descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.material;
				descriptorSetAllocInfo.descriptorSetCount = 1;
				VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &material.descriptorSet));

				std::vector<VkDescriptorImageInfo> imageDescriptors = {
					material.hasBaseColorTexture ? material.baseColorTexture.descriptor : textures.empty.descriptor,
					material.hasNormalTexture ? material.normalTexture.descriptor : textures.empty.descriptor,
					material.hasOcclusionTexture ? material.occlusionTexture.descriptor : textures.empty.descriptor,
					material.hasMetallicRoughnessTexture ? material.metallicRoughnessTexture.descriptor : textures.empty.descriptor,
					material.hasEmissiveTexture ? material.emissiveTexture.descriptor : textures.empty.descriptor
				};

				std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};
				for (size_t i = 0; i < imageDescriptors.size(); i++) {
					writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					writeDescriptorSets[i].descriptorCount = 1;
					writeDescriptorSets[i].dstSet = material.descriptorSet;
					writeDescriptorSets[i].dstBinding = static_cast<uint32_t>(i);
					writeDescriptorSets[i].pImageInfo = &imageDescriptors[i];
				}

				vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
			}
		}

		// Skybox (fixed set)
		{
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorPool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.scene;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.skybox));

			std::array<VkWriteDescriptorSet, 3> writeDescriptorSets{};

			writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[0].descriptorCount = 1;
			writeDescriptorSets[0].dstSet = descriptorSets.skybox;
			writeDescriptorSets[0].dstBinding = 0;
			writeDescriptorSets[0].pBufferInfo = &uniformBuffers.skybox.descriptor;

			writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[1].descriptorCount = 1;
			writeDescriptorSets[1].dstSet = descriptorSets.skybox;
			writeDescriptorSets[1].dstBinding = 1;
			writeDescriptorSets[1].pBufferInfo = &uniformBuffers.params.descriptor;

			writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[2].descriptorCount = 1;
			writeDescriptorSets[2].dstSet = descriptorSets.skybox;
			writeDescriptorSets[2].dstBinding = 2;
			writeDescriptorSets[2].pImageInfo = &textures.prefilteredCube.descriptor;

			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
		}
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
		inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
		rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizationStateCI.lineWidth = 1.0f;

		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.blendEnable = VK_FALSE;
		
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
		colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendStateCI.attachmentCount = 1;
		colorBlendStateCI.pAttachments = &blendAttachmentState;

		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
		depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilStateCI.depthTestEnable = VK_FALSE;
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilStateCI.front = depthStencilStateCI.back;
		depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

		VkPipelineViewportStateCreateInfo viewportStateCI{};
		viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportStateCI.viewportCount = 1;
		viewportStateCI.scissorCount = 1;

		VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
		multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicStateCI{};
		dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
		dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

		// Pipeline layout
		std::array<VkDescriptorSetLayout, 2> setLayouts = { 
			descriptorSetLayouts.scene, descriptorSetLayouts.material 
		};
		VkPipelineLayoutCreateInfo pipelineLayoutCI{};
		pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutCI.setLayoutCount = 2;
		pipelineLayoutCI.pSetLayouts = setLayouts.data();
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.size = sizeof(PushConstBlockMaterial);
		pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		pipelineLayoutCI.pushConstantRangeCount = 1;
		pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

		// Vertex bindings an attributes
		VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
		std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
			{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 },
			{ 2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6 }
		};
		VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
		vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputStateCI.vertexBindingDescriptionCount = 1;
		vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
		vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

		// Pipelines
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCI.layout = pipelineLayout;
		pipelineCI.renderPass = renderPass;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pVertexInputState = &vertexInputStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// Skybox pipeline (background cube)
		rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
		shaderStages = {
			loadShader(device, "skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(device, "skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skybox));
		for (auto shaderStage : shaderStages) {
			vkDestroyShaderModule(device, shaderStage.module, nullptr);
		}

		// PBR pipeline
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
		shaderStages = {
			loadShader(device, "pbr.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(device, "pbr.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};
		depthStencilStateCI.depthWriteEnable = VK_TRUE;
		depthStencilStateCI.depthTestEnable = VK_TRUE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbr));

		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbrAlphaBlend));

		for (auto shaderStage : shaderStages) {
			vkDestroyShaderModule(device, shaderStage.module, nullptr);
		}
	}

	/*
		Generate a BRDF integration map storing roughness/NdotV as a look-up-table
	*/
	void generateBRDFLUT()
	{
		auto tStart = std::chrono::high_resolution_clock::now();

		const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
		const int32_t dim = 512;

		// Image
		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = format;
		imageCI.extent.width = dim;
		imageCI.extent.height = dim;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &textures.lutBrdf.image));
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, textures.lutBrdf.image, &memReqs);
		VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &textures.lutBrdf.deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device, textures.lutBrdf.image, textures.lutBrdf.deviceMemory, 0));

		// View
		VkImageViewCreateInfo viewCI{};
		viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCI.format = format;
		viewCI.subresourceRange = {};
		viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewCI.subresourceRange.levelCount = 1;
		viewCI.subresourceRange.layerCount = 1;
		viewCI.image = textures.lutBrdf.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &textures.lutBrdf.view));

		// Sampler
		VkSamplerCreateInfo samplerCI{};
		samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCI.magFilter = VK_FILTER_LINEAR;
		samplerCI.minFilter = VK_FILTER_LINEAR;
		samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.minLod = 0.0f;
		samplerCI.maxLod = 1.0f;
		samplerCI.maxAnisotropy = 1.0f;
		samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &textures.lutBrdf.sampler));

		// FB, Att, RP, Pipe, etc.
		VkAttachmentDescription attDesc{};
		// Color attachment
		attDesc.format = format;
		attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription{};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Create the actual renderpass
		VkRenderPassCreateInfo renderPassCI{};
		renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCI.attachmentCount = 1;
		renderPassCI.pAttachments = &attDesc;
		renderPassCI.subpassCount = 1;
		renderPassCI.pSubpasses = &subpassDescription;
		renderPassCI.dependencyCount = 2;
		renderPassCI.pDependencies = dependencies.data();

		VkRenderPass renderpass;
		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderpass));

		VkFramebufferCreateInfo framebufferCI{};
		framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferCI.renderPass = renderpass;
		framebufferCI.attachmentCount = 1;
		framebufferCI.pAttachments = &textures.lutBrdf.view;
		framebufferCI.width = dim;
		framebufferCI.height = dim;
		framebufferCI.layers = 1;

		VkFramebuffer framebuffer;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &framebuffer));

		// Desriptors
		VkDescriptorSetLayout descriptorsetlayout;
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

		// Pipeline layout
		VkPipelineLayout pipelinelayout;
		VkPipelineLayoutCreateInfo pipelineLayoutCI{};
		pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutCI.setLayoutCount = 1;
		pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

		// Pipeline
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
		inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
		rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizationStateCI.lineWidth = 1.0f;

		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
		colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendStateCI.attachmentCount = 1;
		colorBlendStateCI.pAttachments = &blendAttachmentState;

		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
		depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilStateCI.depthTestEnable = VK_FALSE;
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilStateCI.front = depthStencilStateCI.back;
		depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

		VkPipelineViewportStateCreateInfo viewportStateCI{};
		viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportStateCI.viewportCount = 1;
		viewportStateCI.scissorCount = 1;

		VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
		multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI{};
		dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
		dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
		
		VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
		emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCI.layout = pipelinelayout;
		pipelineCI.renderPass = renderpass;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pVertexInputState = &emptyInputStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = 2;
		pipelineCI.pStages = shaderStages.data();

		// Look-up-table (from BRDF) pipeline		
		shaderStages = {
			loadShader(device, "genbrdflut.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(device, "genbrdflut.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};
		VkPipeline pipeline;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
		for (auto shaderStage : shaderStages) {
			vkDestroyShaderModule(device, shaderStage.module, nullptr);
		}

		// Render
		VkClearValue clearValues[1];
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderpass;
		renderPassBeginInfo.renderArea.extent.width = dim;
		renderPassBeginInfo.renderArea.extent.height = dim;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = framebuffer;

		VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.width = (float)dim;
		viewport.height = (float)dim;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor{};
		scissor.extent.width = width;
		scissor.extent.height = height;

		vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
		vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdDraw(cmdBuf, 3, 1, 0, 0);
		vkCmdEndRenderPass(cmdBuf);
		vulkanDevice->flushCommandBuffer(cmdBuf, queue);

		vkQueueWaitIdle(queue);

		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelinelayout, nullptr);
		vkDestroyRenderPass(device, renderpass, nullptr);
		vkDestroyFramebuffer(device, framebuffer, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);

		textures.lutBrdf.descriptor.imageView = textures.lutBrdf.view;
		textures.lutBrdf.descriptor.sampler = textures.lutBrdf.sampler;
		textures.lutBrdf.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		textures.lutBrdf.device = vulkanDevice;

		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		std::cout << "Generating BRDF LUT took " << tDiff << " ms" << std::endl;
	}

	/*
		Offline generation for the cube maps used for PBR lighting		
		- Irradiance cube map
		- Pre-filterd environment cubemap
	*/
	void generateCubemaps()
	{
		enum Target { IRRADIANCE = 0, PREFILTEREDENV = 1 };

		for (uint32_t target = 0; target < PREFILTEREDENV + 1; target++) {

			vks::TextureCubeMap cubemap;

			auto tStart = std::chrono::high_resolution_clock::now();

			VkFormat format;
			int32_t dim;

			switch (target) {
			case IRRADIANCE:
				format = VK_FORMAT_R32G32B32A32_SFLOAT;
				dim = 64;
				break;
			case PREFILTEREDENV:
				format = VK_FORMAT_R16G16B16A16_SFLOAT;
				dim = 512;
				break;
			};

			const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

			// Create target cubemap
			{
				// Image
				VkImageCreateInfo imageCI{};
				imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				imageCI.imageType = VK_IMAGE_TYPE_2D;
				imageCI.format = format;
				imageCI.extent.width = dim;
				imageCI.extent.height = dim;
				imageCI.extent.depth = 1;
				imageCI.mipLevels = numMips;
				imageCI.arrayLayers = 6;
				imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
				imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
				imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
				VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &cubemap.image));
				VkMemoryRequirements memReqs;
				vkGetImageMemoryRequirements(device, cubemap.image, &memReqs);
				VkMemoryAllocateInfo memAllocInfo{};
				memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				memAllocInfo.allocationSize = memReqs.size;
				memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &cubemap.deviceMemory));
				VK_CHECK_RESULT(vkBindImageMemory(device, cubemap.image, cubemap.deviceMemory, 0));

				// View
				VkImageViewCreateInfo viewCI{};
				viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
				viewCI.format = format;
				viewCI.subresourceRange = {};
				viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				viewCI.subresourceRange.levelCount = numMips;
				viewCI.subresourceRange.layerCount = 6;
				viewCI.image = cubemap.image;
				VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &cubemap.view));

				// Sampler
				VkSamplerCreateInfo samplerCI{};
				samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				samplerCI.magFilter = VK_FILTER_LINEAR;
				samplerCI.minFilter = VK_FILTER_LINEAR;
				samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				samplerCI.minLod = 0.0f;
				samplerCI.maxLod = static_cast<float>(numMips);
				samplerCI.maxAnisotropy = 1.0f;
				samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
				VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &cubemap.sampler));
			}

			// FB, Att, RP, Pipe, etc.
			VkAttachmentDescription attDesc{};
			// Color attachment
			attDesc.format = format;
			attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
			attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpassDescription{};
			subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassDescription.colorAttachmentCount = 1;
			subpassDescription.pColorAttachments = &colorReference;

			// Use subpass dependencies for layout transitions
			std::array<VkSubpassDependency, 2> dependencies;
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			// Renderpass
			VkRenderPassCreateInfo renderPassCI{};
			renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassCI.attachmentCount = 1;
			renderPassCI.pAttachments = &attDesc;
			renderPassCI.subpassCount = 1;
			renderPassCI.pSubpasses = &subpassDescription;
			renderPassCI.dependencyCount = 2;
			renderPassCI.pDependencies = dependencies.data();
			VkRenderPass renderpass;
			VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderpass));

			struct Offscreen {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Create offscreen framebuffer
			{
				// Image
				VkImageCreateInfo imageCI{};
				imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				imageCI.imageType = VK_IMAGE_TYPE_2D;
				imageCI.format = format;
				imageCI.extent.width = dim;
				imageCI.extent.height = dim;
				imageCI.extent.depth = 1;
				imageCI.mipLevels = 1;
				imageCI.arrayLayers = 1;
				imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
				imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
				imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
				imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &offscreen.image));
				VkMemoryRequirements memReqs;
				vkGetImageMemoryRequirements(device, offscreen.image, &memReqs);
				VkMemoryAllocateInfo memAllocInfo{};
				memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				memAllocInfo.allocationSize = memReqs.size;
				memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &offscreen.memory));
				VK_CHECK_RESULT(vkBindImageMemory(device, offscreen.image, offscreen.memory, 0));

				// View
				VkImageViewCreateInfo viewCI{};
				viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
				viewCI.format = format;
				viewCI.flags = 0;
				viewCI.subresourceRange = {};
				viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				viewCI.subresourceRange.baseMipLevel = 0;
				viewCI.subresourceRange.levelCount = 1;
				viewCI.subresourceRange.baseArrayLayer = 0;
				viewCI.subresourceRange.layerCount = 1;
				viewCI.image = offscreen.image;
				VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &offscreen.view));

				// Framebuffer
				VkFramebufferCreateInfo framebufferCI{};
				framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferCI.renderPass = renderpass;
				framebufferCI.attachmentCount = 1;
				framebufferCI.pAttachments = &offscreen.view;
				framebufferCI.width = dim;
				framebufferCI.height = dim;
				framebufferCI.layers = 1;
				VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &offscreen.framebuffer));

				VkCommandBuffer layoutCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.image = offscreen.image;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = 0;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				vkCmdPipelineBarrier(layoutCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				vulkanDevice->flushCommandBuffer(layoutCmd, queue, true);
			}

			// Descriptors
			VkDescriptorSetLayout descriptorsetlayout;
			VkDescriptorSetLayoutBinding setLayoutBinding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCI.pBindings = &setLayoutBinding;
			descriptorSetLayoutCI.bindingCount = 1;
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

			// Descriptor Pool
			VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
			VkDescriptorPoolCreateInfo descriptorPoolCI{};
			descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptorPoolCI.poolSizeCount = 1;
			descriptorPoolCI.pPoolSizes = &poolSize;
			descriptorPoolCI.maxSets = 2;
			VkDescriptorPool descriptorpool;
			VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorpool));

			// Descriptor sets
			VkDescriptorSet descriptorset;
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorpool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorsetlayout;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorset));
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSet.descriptorCount = 1;
			writeDescriptorSet.dstSet = descriptorset;
			writeDescriptorSet.dstBinding = 0;
			writeDescriptorSet.pImageInfo = &textures.environmentCube.descriptor;
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

			struct PushBlockIrradiance {
				glm::mat4 mvp;
				float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
				float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
			} pushBlockIrradiance;

			struct PushBlockPrefilterEnv {
				glm::mat4 mvp;
				float roughness;
				uint32_t numSamples = 32u;
			} pushBlockPrefilterEnv;

			// Pipeline layout
			VkPipelineLayout pipelinelayout;
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

			switch (target) {
				case IRRADIANCE:
					pushConstantRange.size = sizeof(PushBlockIrradiance);
					break;
				case PREFILTEREDENV:
					pushConstantRange.size = sizeof(PushBlockPrefilterEnv);
					break;
			};

			VkPipelineLayoutCreateInfo pipelineLayoutCI{};
			pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutCI.setLayoutCount = 1;
			pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
			pipelineLayoutCI.pushConstantRangeCount = 1;
			pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

			// Pipeline
			VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
			inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
			rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
			rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizationStateCI.lineWidth = 1.0f;

			VkPipelineColorBlendAttachmentState blendAttachmentState{};
			blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blendAttachmentState.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
			colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlendStateCI.attachmentCount = 1;
			colorBlendStateCI.pAttachments = &blendAttachmentState;

			VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
			depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencilStateCI.depthTestEnable = VK_FALSE;
			depthStencilStateCI.depthWriteEnable = VK_FALSE;
			depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			depthStencilStateCI.front = depthStencilStateCI.back;
			depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

			VkPipelineViewportStateCreateInfo viewportStateCI{};
			viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportStateCI.viewportCount = 1;
			viewportStateCI.scissorCount = 1;

			VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
			multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			
			std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicStateCI{};
			dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
			dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

			// Vertex input state
			VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
			VkVertexInputAttributeDescription vertexInputAttribute = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

			VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
			vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputStateCI.vertexBindingDescriptionCount = 1;
			vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
			vertexInputStateCI.vertexAttributeDescriptionCount = 1;
			vertexInputStateCI.pVertexAttributeDescriptions = &vertexInputAttribute;

			std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

			VkGraphicsPipelineCreateInfo pipelineCI{};
			pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineCI.layout = pipelinelayout;
			pipelineCI.renderPass = renderpass;
			pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
			pipelineCI.pVertexInputState = &vertexInputStateCI;
			pipelineCI.pRasterizationState = &rasterizationStateCI;
			pipelineCI.pColorBlendState = &colorBlendStateCI;
			pipelineCI.pMultisampleState = &multisampleStateCI;
			pipelineCI.pViewportState = &viewportStateCI;
			pipelineCI.pDepthStencilState = &depthStencilStateCI;
			pipelineCI.pDynamicState = &dynamicStateCI;
			pipelineCI.stageCount = 2;
			pipelineCI.pStages = shaderStages.data();
			pipelineCI.renderPass = renderpass;

			shaderStages[0] = loadShader(device, "filtercube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
			switch (target) {
				case IRRADIANCE:
					shaderStages[1] = loadShader(device, "irradiancecube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
					break;
				case PREFILTEREDENV:
					shaderStages[1] = loadShader(device, "prefilterenvmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
					break;
			};
			VkPipeline pipeline;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
			for (auto shaderStage : shaderStages) {
				vkDestroyShaderModule(device, shaderStage.module, nullptr);
			}

			// Render cubemap
			VkClearValue clearValues[1];
			clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };

			VkRenderPassBeginInfo renderPassBeginInfo{};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = renderpass;
			renderPassBeginInfo.framebuffer = offscreen.framebuffer;
			renderPassBeginInfo.renderArea.extent.width = dim;
			renderPassBeginInfo.renderArea.extent.height = dim;
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValues;

			std::vector<glm::mat4> matrices = {
				glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
			};

			VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkViewport viewport{};
			viewport.width = (float)dim;
			viewport.height = (float)dim;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor{};
			scissor.extent.width = width;
			scissor.extent.height = height;

			vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
			vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

			VkImageSubresourceRange subresourceRange{};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = numMips;
			subresourceRange.layerCount = 6;

			// Change image layout for all cubemap faces to transfer destination
			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.image = cubemap.image;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = 0;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			for (uint32_t m = 0; m < numMips; m++) {
				for (uint32_t f = 0; f < 6; f++) {
					viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
					viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
					vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

					// Render scene from cube face's point of view
					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Pass parameters for current pass using a push constant block
					switch (target) {
						case IRRADIANCE:
							pushBlockIrradiance.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
							vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockIrradiance), &pushBlockIrradiance);
							break;
						case PREFILTEREDENV:
							pushBlockPrefilterEnv.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
							pushBlockPrefilterEnv.roughness = (float)m / (float)(numMips - 1);
							vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockPrefilterEnv), &pushBlockPrefilterEnv);
							break;
					};

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
					vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

					VkDeviceSize offsets[1] = { 0 };

					models.skybox.draw(cmdBuf);

					vkCmdEndRenderPass(cmdBuf);

					VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
					subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					subresourceRange.baseMipLevel = 0;
					subresourceRange.levelCount = numMips;
					subresourceRange.layerCount = 6;

					{
						VkImageMemoryBarrier imageMemoryBarrier{};
						imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
						imageMemoryBarrier.image = offscreen.image;
						imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
						imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
						imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
						imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
						vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
					}

					// Copy region for transfer from framebuffer to cube face
					VkImageCopy copyRegion{};

					copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.srcSubresource.baseArrayLayer = 0;
					copyRegion.srcSubresource.mipLevel = 0;
					copyRegion.srcSubresource.layerCount = 1;
					copyRegion.srcOffset = { 0, 0, 0 };

					copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.dstSubresource.baseArrayLayer = f;
					copyRegion.dstSubresource.mipLevel = m;
					copyRegion.dstSubresource.layerCount = 1;
					copyRegion.dstOffset = { 0, 0, 0 };

					copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
					copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
					copyRegion.extent.depth = 1;

					vkCmdCopyImage(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						cubemap.image,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1,
						&copyRegion);

					{
						VkImageMemoryBarrier imageMemoryBarrier{};
						imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
						imageMemoryBarrier.image = offscreen.image;
						imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
						imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
						imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
						imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
						vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
					}

				}
			}

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.image = cubemap.image;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			vulkanDevice->flushCommandBuffer(cmdBuf, queue);

			vkDestroyRenderPass(device, renderpass, nullptr);
			vkDestroyFramebuffer(device, offscreen.framebuffer, nullptr);
			vkFreeMemory(device, offscreen.memory, nullptr);
			vkDestroyImageView(device, offscreen.view, nullptr);
			vkDestroyImage(device, offscreen.image, nullptr);
			vkDestroyDescriptorPool(device, descriptorpool, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelinelayout, nullptr);

			cubemap.descriptor.imageView = cubemap.view;
			cubemap.descriptor.sampler = cubemap.sampler;
			cubemap.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			cubemap.device = vulkanDevice;

			switch (target) {
				case IRRADIANCE:
					textures.irradianceCube = cubemap;
					break;
				case PREFILTEREDENV:
					textures.prefilteredCube = cubemap;
					break;
			};

			auto tEnd = std::chrono::high_resolution_clock::now();
			auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
			std::cout << "Generating cube map with " << numMips << " mip levels took " << tDiff << " ms" << std::endl;
		}
	}

	/* 
		Prepare and initialize uniform buffer containing shader uniforms
	*/
	void prepareUniformBuffers()
	{
		// Objact vertex shader uniform buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboMatrices),
			&uniformBuffers.object.buffer,
			&uniformBuffers.object.memory));

		// Skybox vertex shader uniform buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboMatrices),
			&uniformBuffers.skybox.buffer,
			&uniformBuffers.skybox.memory));

		// Shared parameter uniform buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboParams),
			&uniformBuffers.params.buffer,
			&uniformBuffers.params.memory));

		// Descriptors
		uniformBuffers.object.descriptor = { uniformBuffers.object.buffer, 0, sizeof(uboMatrices) };
		uniformBuffers.skybox.descriptor = { uniformBuffers.skybox.buffer, 0, sizeof(uboMatrices) };
		uniformBuffers.params.descriptor = { uniformBuffers.params.buffer, 0, sizeof(uboParams) };

		// Map persistent
		VK_CHECK_RESULT(vkMapMemory(device, uniformBuffers.object.memory, 0, sizeof(uboMatrices), 0, &uniformBuffers.object.mapped));
		VK_CHECK_RESULT(vkMapMemory(device, uniformBuffers.skybox.memory, 0, sizeof(uboMatrices), 0, &uniformBuffers.skybox.mapped));
		VK_CHECK_RESULT(vkMapMemory(device, uniformBuffers.params.memory, 0, sizeof(uboParams), 0, &uniformBuffers.params.mapped));

		updateUniformBuffers();
		updateParams();
	}

	void updateUniformBuffers()
	{
		// 3D object
		uboMatrices.projection = camera.matrices.perspective;
		uboMatrices.view = camera.matrices.view;
		uboMatrices.model = glm::mat4(1.0f);
		uboMatrices.model = glm::rotate(uboMatrices.model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
		uboMatrices.model = glm::rotate(uboMatrices.model, -0.5f * (float)M_PI, glm::vec3(1.0f, 0.0f, 0.0f));
		uboMatrices.camPos = camera.position * -1.0f;
		memcpy(uniformBuffers.object.mapped, &uboMatrices, sizeof(uboMatrices));

		// Skybox
		uboMatrices.model = glm::mat4(glm::mat3(camera.matrices.view));
		memcpy(uniformBuffers.skybox.mapped, &uboMatrices, sizeof(uboMatrices));
	}

	void updateParams()
	{
		uboParams.lightDir = glm::vec4(0.0f, -0.5f, -0.5f, 1.0f);
		memcpy(uniformBuffers.params.mapped, &uboParams, sizeof(uboParams));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();

		loadAssets();
		generateBRDFLUT();
		generateCubemaps();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		buildCommandBuffers();

		prepared = true;
	}

	virtual void render()
	{
		if (!prepared) {
			return;
		}
		VulkanExampleBase::prepareFrame();
		VK_CHECK_RESULT(vkWaitForFences(device, 1, &waitFences[currentBuffer], VK_TRUE, UINT64_MAX));
		VK_CHECK_RESULT(vkResetFences(device, 1, &waitFences[currentBuffer]));
		const VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pWaitDstStageMask = &waitDstStageMask;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &presentCompleteSemaphore;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderCompleteSemaphore;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, waitFences[currentBuffer]));
		VulkanExampleBase::submitFrame();
		if (!paused) {
			rotation.y += frameTimer * 0.1f;
			if (rotation.y > 2.0f * (float)M_PI) {
				rotation.y -= 2.0f * (float)M_PI;
			}
			updateUniformBuffers();
		}

	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
	}

#if !defined(VK_USE_PLATFORM_ANDROID_KHR)
	virtual void keyPressed(uint32_t key)
	{
		switch (key) {
			case KEY_F1:
				if (uboParams.exposure > 0.1f) {
					uboParams.exposure -= 0.1f;
					updateParams();
					std::cout << "Exposure: " << uboParams.exposure << std::endl;
				}
				break;
			case KEY_F2:
				if (uboParams.exposure < 10.0f) {
					uboParams.exposure += 0.1f;
					updateParams();
					std::cout << "Exposure: " << uboParams.exposure << std::endl;
				}
				break;
			case KEY_F3:
				if (uboParams.gamma > 0.1f) {
					uboParams.gamma -= 0.1f;
					updateParams();
					std::cout << "Gamma: " << uboParams.gamma << std::endl;
				}
				break;
			case KEY_F4:
				if (uboParams.gamma < 10.0f) {
					uboParams.gamma += 0.1f;
					updateParams();
					std::cout << "Gamma: " << uboParams.gamma << std::endl;
				}
				break;
		}
	}
#endif
};

VulkanExample *vulkanExample;

// OS specific macros for the example main entry points
#if defined(_WIN32)
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleMessages(hWnd, uMsg, wParam, lParam);
	}
	return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	for (int32_t i = 0; i < __argc; i++) { VulkanExample::args.push_back(__argv[i]); };
	vulkanExample = new VulkanExample();
	vulkanExample->initVulkan();
	vulkanExample->setupWindow(hInstance, WndProc);
	vulkanExample->prepare();
	vulkanExample->renderLoop();
	delete(vulkanExample);
	return 0;
}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
// Android entry point
// A note on app_dummy(): This is required as the compiler may otherwise remove the main entry point of the application
void android_main(android_app* state)
{
	app_dummy();
	vulkanExample = new VulkanExample();
	state->userData = vulkanExample;
	state->onAppCmd = VulkanExample::handleAppCommand;
	state->onInputEvent = VulkanExample::handleAppInput;
	androidApp = state;
	vks::android::getDeviceConfig();
	vulkanExample->renderLoop();
	delete(vulkanExample);
}
#elif defined(_DIRECT2DISPLAY)
// Linux entry point with direct to display wsi
static void handleEvent()
{
}
int main(const int argc, const char *argv[])
{
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
	vulkanExample = new VulkanExample();
	vulkanExample->initVulkan();
	vulkanExample->prepare();
	vulkanExample->renderLoop();
	delete(vulkanExample);
	return 0;
}
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
int main(const int argc, const char *argv[])
{
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
	vulkanExample = new VulkanExample();
	vulkanExample->initVulkan();
	vulkanExample->setupWindow();
	vulkanExample->prepare();
	vulkanExample->renderLoop();
	delete(vulkanExample);
	return 0;
}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
static void handleEvent(const xcb_generic_event_t *event)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleEvent(event);
	}
}
int main(const int argc, const char *argv[])
{
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
	vulkanExample = new VulkanExample();
	vulkanExample->initVulkan();
	vulkanExample->setupWindow();
	vulkanExample->prepare();
	vulkanExample->renderLoop();
	delete(vulkanExample);
	return 0;
}
#endif