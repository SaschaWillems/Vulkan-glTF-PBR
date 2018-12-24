/*
* Vulkan utilities
*
* Copyright(C) 2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "vulkan/vulkan.h"
#include "VulkanDevice.hpp"
#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

/*
	Vulkan buffer object
*/
struct Buffer {
	VkDevice device;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDescriptorBufferInfo descriptor;
	int32_t count = 0;
	void *mapped = nullptr;
	void create(vks::VulkanDevice *device, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, bool map = true) {
		this->device = device->logicalDevice;
		device->createBuffer(usageFlags, memoryPropertyFlags, size, &buffer, &memory);
		descriptor = { buffer, 0, size };
		if (map) {
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, memory, 0, size, 0, &mapped));
		}
	}
	void destroy() {
		if (mapped) {
			unmap();
		}
		vkDestroyBuffer(device, buffer, nullptr);
		vkFreeMemory(device, memory, nullptr);
		buffer = VK_NULL_HANDLE;
		memory = VK_NULL_HANDLE;
	}
	void map() {
		VK_CHECK_RESULT(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
	}
	void unmap() {
		if (mapped) {
			vkUnmapMemory(device, memory);
			mapped = nullptr;
		}
	}
	void flush(VkDeviceSize size = VK_WHOLE_SIZE) {
		VkMappedMemoryRange mappedRange{};
		mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		mappedRange.memory = memory;
		mappedRange.size = size;
		VK_CHECK_RESULT(vkFlushMappedMemoryRanges(device, 1, &mappedRange));
	}
};

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

void readDirectory(const std::string& directory, std::vector<std::string> &filelist)
{
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	AAssetDir* assetDir = AAssetManager_openDir(androidApp->activity->assetManager, directory.c_str());
	AAssetDir_rewind(assetDir);
	const char* assetName;
	while((assetName = AAssetDir_getNextFileName(assetDir)) != 0) {
		filelist.push_back(assetName);
	}
	AAssetDir_close(assetDir);
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
	std::string pattern(directory + "/*.ktx");
	WIN32_FIND_DATA data;
	HANDLE hFind;
	if ((hFind = FindFirstFile(pattern.c_str(), &data)) != INVALID_HANDLE_VALUE) {
		do {
			filelist.push_back(data.cFileName);
		} while (FindNextFile(hFind, &data) != 0);
		FindClose(hFind);
	}
#endif
}