/*
* Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "vulkan/vulkan.h"
#include "VulkanDevice.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <gli/gli.hpp>

#include "tiny_gltf.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

namespace vkglTF
{
	/*
		glTF texture loading class
	*/
	struct Texture {
		vks::VulkanDevice *device;
		VkImage image;
		VkImageLayout imageLayout;
		VkDeviceMemory deviceMemory;
		VkImageView view;
		uint32_t width, height;
		uint32_t mipLevels;
		uint32_t layerCount;
		VkDescriptorImageInfo descriptor;
		VkSampler sampler;

		void updateDescriptor()
		{
			descriptor.sampler = sampler;
			descriptor.imageView = view;
			descriptor.imageLayout = imageLayout;
		}

		void destroy()
		{
			vkDestroyImageView(device->logicalDevice, view, nullptr);
			vkDestroyImage(device->logicalDevice, image, nullptr);
			vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
			vkDestroySampler(device->logicalDevice, sampler, nullptr);
		}

		/*
			Load a texture from a glTF image (stored as vector of chars loaded via stb_image)
			Also generates the mip chain as glTF images are stored as jpg or png without any mips
		*/
		void fromglTfImage(tinygltf::Image &gltfimage, vks::VulkanDevice *device, VkQueue copyQueue)
		{
			this->device = device;

			unsigned char* buffer = nullptr;
			VkDeviceSize bufferSize = 0;
			bool deleteBuffer = false;
			if (gltfimage.component == 3) {
				// Most devices don't support RGB only on Vulkan so convert if necessary
				// TODO: Check actual format support and transform only if required
				bufferSize = gltfimage.width * gltfimage.height * 4;
				buffer = new unsigned char[bufferSize];
				unsigned char* rgba = buffer;
				unsigned char* rgb = &gltfimage.image[0];
				for (size_t i = 0; i< gltfimage.width * gltfimage.height; ++i) {
					for (int32_t j = 0; j < 3; ++j) {
						rgba[j] = rgb[j];
					}
					rgba += 4;
					rgb += 3;
				}
				deleteBuffer = true;
			}
			else {
				buffer = &gltfimage.image[0];
				bufferSize = gltfimage.image.size();
			}

			VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

			VkFormatProperties formatProperties;

			width = gltfimage.width;
			height = gltfimage.height;
			mipLevels = static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

			vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);
			assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
			assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

			VkMemoryAllocateInfo memAllocInfo{};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			VkMemoryRequirements memReqs{};

			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			VkBufferCreateInfo bufferCreateInfo{};
			bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferCreateInfo.size = bufferSize;
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));
			vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

			uint8_t *data;
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
			memcpy(data, buffer, bufferSize);
			vkUnmapMemory(device->logicalDevice, stagingMemory);

			VkImageCreateInfo imageCreateInfo{};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { width, height, 1 };
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));
			vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
			VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

			VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = 0;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = width;
			bufferCopyRegion.imageExtent.height = height;
			bufferCopyRegion.imageExtent.depth = 1;

			vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			device->flushCommandBuffer(copyCmd, copyQueue, true);

			vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

			// Generate the mip chain (glTF uses jpg and png, so we need to create this manually)
			VkCommandBuffer blitCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
			for (uint32_t i = 1; i < mipLevels; i++) {
				VkImageBlit imageBlit{};

				imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.srcSubresource.layerCount = 1;
				imageBlit.srcSubresource.mipLevel = i - 1;
				imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1));
				imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
				imageBlit.srcOffsets[1].z = 1;

				imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.dstSubresource.layerCount = 1;
				imageBlit.dstSubresource.mipLevel = i;
				imageBlit.dstOffsets[1].x = int32_t(width >> i);
				imageBlit.dstOffsets[1].y = int32_t(height >> i);
				imageBlit.dstOffsets[1].z = 1;

				VkImageSubresourceRange mipSubRange = {};
				mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				mipSubRange.baseMipLevel = i;
				mipSubRange.levelCount = 1;
				mipSubRange.layerCount = 1;

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = 0;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imageMemoryBarrier.image = image;
					imageMemoryBarrier.subresourceRange = mipSubRange;
					vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}

				vkCmdBlitImage(blitCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					imageMemoryBarrier.image = image;
					imageMemoryBarrier.subresourceRange = mipSubRange;
					vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}
			}

			subresourceRange.levelCount = mipLevels;
			imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			device->flushCommandBuffer(blitCmd, copyQueue, true);

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
			samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			samplerInfo.maxAnisotropy = 1.0;
			samplerInfo.anisotropyEnable = VK_FALSE;
			samplerInfo.maxLod = (float)mipLevels;
			samplerInfo.maxAnisotropy = 8.0f;
			samplerInfo.anisotropyEnable = VK_TRUE;
			VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = format;
			viewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.layerCount = 1;
			viewInfo.subresourceRange.levelCount = mipLevels;
			VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &view));

			descriptor.sampler = sampler;
			descriptor.imageView = view;
			descriptor.imageLayout = imageLayout;
		}
	};

	/*
		glTF material class
	*/
	struct Material {		
		enum AlphaMode{ ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
		AlphaMode alphaMode = ALPHAMODE_OPAQUE;
		float alphaCutoff = 1.0f;
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		vkglTF::Texture *baseColorTexture;
		vkglTF::Texture *metallicRoughnessTexture;
		vkglTF::Texture *normalTexture;
		vkglTF::Texture *occlusionTexture;
		vkglTF::Texture *emissiveTexture;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	};

	/*
		glTF primitive class
	*/
	struct Primitive {
		uint32_t firstIndex;
		uint32_t indexCount;
		Material &material;
		glm::mat4 matrix = glm::mat4(1.0f);
		struct UniformBuffer {
			VkBuffer buffer;
			VkDeviceMemory memory;
			VkDescriptorBufferInfo descriptor;
			VkDescriptorSet descriptorSet;
			void *mapped;
		} uniformBuffer;
		void prepareUniformBuffer(vks::VulkanDevice *device) {
			VK_CHECK_RESULT(device->createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				sizeof(matrix),
				&uniformBuffer.buffer,
				&uniformBuffer.memory,
				&matrix));
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, uniformBuffer.memory, 0, sizeof(matrix), 0, &uniformBuffer.mapped));
			uniformBuffer.descriptor = { uniformBuffer.buffer, 0, sizeof(matrix) };
		}
	};
	};

	/*
		glTF model loading and rendering class
	*/
	struct Model {

		struct Vertex {
			glm::vec3 pos;
			glm::vec3 normal;
			glm::vec2 uv;
		};

		struct Vertices {
			VkBuffer buffer;
			VkDeviceMemory memory;
		} vertices;
		struct Indices {
			int count;
			VkBuffer buffer;
			VkDeviceMemory memory;
		} indices;

		std::vector<Primitive> primitives;

		std::vector<Texture> textures;
		std::vector<Material> materials;

		void destroy(VkDevice device)
		{
			vkDestroyBuffer(device, vertices.buffer, nullptr);
			vkFreeMemory(device, vertices.memory, nullptr);
			vkDestroyBuffer(device, indices.buffer, nullptr);
			vkFreeMemory(device, indices.memory, nullptr);
			for (auto texture : textures) {
				texture.destroy();
			}
		};

		void loadNode(const tinygltf::Node &node, const glm::mat4 &parentMatrix, const tinygltf::Model &model, std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, float globalscale)
		{
			// Generate local node matrix
			glm::vec3 translation = glm::vec3(0.0f);
			if (node.translation.size() == 3) {
				translation = glm::make_vec3(node.translation.data());
			}
			glm::mat4 rotation = glm::mat4(1.0f);
			if (node.rotation.size() == 4) {
				glm::quat q = glm::make_quat(node.rotation.data());
				rotation = glm::mat4(q);
			}
			glm::vec3 scale = glm::vec3(1.0f);
			if (node.scale.size() == 3) {
				scale = glm::make_vec3(node.scale.data());
			}
			glm::mat4 localNodeMatrix = glm::mat4(1.0f);
			if (node.matrix.size() == 16) {
				localNodeMatrix = glm::make_mat4x4(node.matrix.data());
			} else {
				// T * R * S
				localNodeMatrix = glm::translate(glm::mat4(1.0f), translation) * rotation * glm::scale(glm::mat4(1.0f), scale);
			}
			localNodeMatrix = parentMatrix * localNodeMatrix;

			// Parent node with children
			if (node.children.size() > 0) {
				for (auto i = 0; i < node.children.size(); i++) {
					loadNode(model.nodes[node.children[i]], localNodeMatrix, model, indexBuffer, vertexBuffer, globalscale);
				}
			}

			// Node contains mesh data
			if (node.mesh > -1) {
				const tinygltf::Mesh mesh = model.meshes[node.mesh];
				for (size_t j = 0; j < mesh.primitives.size(); j++) {
					const tinygltf::Primitive &primitive = mesh.primitives[j];
					if (primitive.indices < 0) {
						continue;
					}
					uint32_t indexStart = static_cast<uint32_t>(indexBuffer.size());
					uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
					uint32_t indexCount = 0;
					// Vertices
					{
						const float *bufferPos = nullptr;
						const float *bufferNormals = nullptr;
						const float *bufferTexCoords = nullptr;

						// Position attribute is required
						assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

						const tinygltf::Accessor &posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
						const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
						bufferPos = reinterpret_cast<const float *>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));

						if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
							const tinygltf::Accessor &normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
							const tinygltf::BufferView &normView = model.bufferViews[normAccessor.bufferView];
							bufferNormals = reinterpret_cast<const float *>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
						}

						if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
							const tinygltf::Accessor &uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
							const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
							bufferTexCoords = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
						}

						for (size_t v = 0; v < posAccessor.count; v++) {
							Vertex vert{};
							vert.pos = glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
							vert.normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
							vert.uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
							vertexBuffer.push_back(vert);
						}
					}
					// Indices
					{
						const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
						const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
						const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

						indexCount = static_cast<uint32_t>(accessor.count);

						switch (accessor.componentType) {
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
							uint32_t *buf = new uint32_t[accessor.count];
							memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
							for (size_t index = 0; index < accessor.count; index++) {
								indexBuffer.push_back(buf[index] + vertexStart);
							}
							break;
						}
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
							uint16_t *buf = new uint16_t[accessor.count];
							memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
							for (size_t index = 0; index < accessor.count; index++) {
								indexBuffer.push_back(buf[index] + vertexStart);
							}
							break;
						}
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
							uint8_t *buf = new uint8_t[accessor.count];
							memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
							for (size_t index = 0; index < accessor.count; index++) {
								indexBuffer.push_back(buf[index] + vertexStart);
							}
							break;
						}
						default:
							std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
							return;
						}
					}
					primitives.push_back({ indexStart, indexCount, materials[primitive.material], nodeIndex, localNodeMatrix });
				}
			}
		}

		void loadImages(tinygltf::Model &gltfModel, vks::VulkanDevice *device, VkQueue transferQueue)
		{
			for (tinygltf::Image &image : gltfModel.images) {
				vkglTF::Texture texture;
				texture.fromglTfImage(image, device, transferQueue);
				textures.push_back(texture);
			}
		}

		void loadMaterials(tinygltf::Model &gltfModel, vks::VulkanDevice *device, VkQueue transferQueue)
		{
			for (tinygltf::Material &mat : gltfModel.materials) {
				vkglTF::Material material{};
				if (mat.values.find("baseColorTexture") != mat.values.end()) {
					material.baseColorTexture = &textures[gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source];
				}
				if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
					material.metallicRoughnessTexture = &textures[gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source];
				}
				if (mat.values.find("roughnessFactor") != mat.values.end()) {
					material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
				}
				if (mat.values.find("metallicFactor") != mat.values.end()) {
					material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
				}
				if (mat.values.find("baseColorFactor") != mat.values.end()) {
					material.baseColorFactor = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
				}				
				if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
					material.normalTexture = &textures[gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source];
				}
				if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
					material.emissiveTexture = &textures[gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source];
				}
				if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
					material.occlusionTexture = &textures[gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source];
				}
				if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
					tinygltf::Parameter param = mat.additionalValues["alphaMode"];
					if (param.string_value == "BLEND") {
						material.alphaMode = Material::ALPHAMODE_BLEND;
					}
					if (param.string_value == "MASK") {
						material.alphaMode = Material::ALPHAMODE_MASK;
					}
				}
				if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
					material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
				}
				materials.push_back(material);
			}
		}

		void loadFromFile(std::string filename, vks::VulkanDevice *device, VkQueue transferQueue, float scale = 1.0f)
		{
			tinygltf::Model gltfModel;
			tinygltf::TinyGLTF gltfContext;
			std::string error;

#if defined(__ANDROID__)
			AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
			assert(asset);
			size_t size = AAsset_getLength(asset);
			assert(size > 0);
			char* fileData = new char[size];
			AAsset_read(asset, fileData, size);
			AAsset_close(asset);
			std::string baseDir;
			bool fileLoaded = gltfContext.LoadASCIIFromString(&gltfModel, &error, fileData, size, baseDir);
			free(fileData);
#else
			bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, filename.c_str());
#endif
			std::vector<uint32_t> indexBuffer;
			std::vector<Vertex> vertexBuffer;

			if (fileLoaded) {
				loadImages(gltfModel, device, transferQueue);
				loadMaterials(gltfModel, device, transferQueue);
				const tinygltf::Scene &scene = gltfModel.scenes[gltfModel.defaultScene];
				for (size_t i = 0; i < scene.nodes.size(); i++) {
					const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
					loadNode(node, glm::mat4(1.0f), gltfModel, indexBuffer, vertexBuffer, scale);
				}
			}
			else {
				// TODO: throw
				std::cerr << "Could not load gltf file: " << error << std::endl;
				return;
			}

			size_t vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);
			size_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);
			indices.count = static_cast<uint32_t>(indexBuffer.size());

			assert((vertexBufferSize > 0) && (indexBufferSize > 0));

			struct StagingBuffer {
				VkBuffer buffer;
				VkDeviceMemory memory;
			} vertexStaging, indexStaging;

			// Create staging buffers
			// Vertex data
			VK_CHECK_RESULT(device->createBuffer(
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				vertexBufferSize,
				&vertexStaging.buffer,
				&vertexStaging.memory,
				vertexBuffer.data()));
			// Index data
			VK_CHECK_RESULT(device->createBuffer(
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				indexBufferSize,
				&indexStaging.buffer,
				&indexStaging.memory,
				indexBuffer.data()));

			// Create device local buffers
			// Vertex buffer
			VK_CHECK_RESULT(device->createBuffer(
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				vertexBufferSize,
				&vertices.buffer,
				&vertices.memory));
			// Index buffer
			VK_CHECK_RESULT(device->createBuffer(
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				indexBufferSize,
				&indices.buffer,
				&indices.memory));

			// Copy from staging buffers
			VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkBufferCopy copyRegion = {};

			copyRegion.size = vertexBufferSize;
			vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, vertices.buffer, 1, &copyRegion);

			copyRegion.size = indexBufferSize;
			vkCmdCopyBuffer(copyCmd, indexStaging.buffer, indices.buffer, 1, &copyRegion);

			device->flushCommandBuffer(copyCmd, transferQueue, true);

			vkDestroyBuffer(device->logicalDevice, vertexStaging.buffer, nullptr);
			vkFreeMemory(device->logicalDevice, vertexStaging.memory, nullptr);
			vkDestroyBuffer(device->logicalDevice, indexStaging.buffer, nullptr);
			vkFreeMemory(device->logicalDevice, indexStaging.memory, nullptr);

			// Prepare per-primitive uniform buffers
			for (auto& primitive : primitives) {
				primitive.prepareUniformBuffer(device);
			}
		}

		void draw(VkCommandBuffer commandBuffer)
		{
			const VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
			vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
			for (auto primitive : primitives) {
				vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
			}
		}
	};
}