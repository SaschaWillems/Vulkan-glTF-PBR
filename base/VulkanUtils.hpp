/*
* Vulkan utilities
*
* Copyright(C) 2018-2025 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include "vulkan/vulkan.h"
#include <slang/slang.h>
#include <slang/slang-com-ptr.h>
#include "VulkanDevice.hpp"
#if defined(__ANDROID__)
#include <android/asset_manager.h>
#elif defined(__linux__)
#include <dirent.h>
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
	VkDeviceSize actualBufferSize{ 0 };
	void *mapped = nullptr;
	void create(vks::VulkanDevice *device, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, bool map = true) {
		this->device = device->logicalDevice;
		device->createBuffer(usageFlags, memoryPropertyFlags, size, &buffer, &memory, nullptr, &actualBufferSize);
		descriptor = { buffer, 0, size };
		if (map) {
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, memory, 0, actualBufferSize, 0, &mapped));
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

VkPipelineShaderStageCreateInfo loadSlangShader(VkDevice device, const std::string& filename, const std::string& entryPointName, VkShaderStageFlagBits stage)
{
	VkPipelineShaderStageCreateInfo shaderStage{};
	shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStage.stage = stage;
	shaderStage.pName = "main";

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	std::cerr << "Error: Runtime Slang compilation is not supported on Android builds" << std::endl;
	shaderStage.module = VK_NULL_HANDLE;
#else
	auto printSlangDiagnostics = [](slang::IBlob* diagnosticsBlob) {
		if (diagnosticsBlob) {
			std::cerr << static_cast<const char*>(diagnosticsBlob->getBufferPointer()) << std::endl;
		}
	};

	std::filesystem::path shaderPath = std::filesystem::path(VK_EXAMPLE_DATA_DIR) / "shaders" / filename;
	std::ifstream is(shaderPath, std::ios::binary | std::ios::in);
	if (!is.is_open()) {
		std::cerr << "Error: Could not open Slang shader file \"" << shaderPath.string() << "\"" << std::endl;
		shaderStage.module = VK_NULL_HANDLE;
	}
	else {
		std::string shaderSource((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
		is.close();

		Slang::ComPtr<slang::IGlobalSession> globalSession;
		Slang::ComPtr<slang::ISession> session;
		Slang::ComPtr<slang::IModule> shaderModule;
		Slang::ComPtr<slang::IEntryPoint> entryPoint;
		Slang::ComPtr<slang::IComponentType> composedProgram;
		Slang::ComPtr<slang::IComponentType> linkedProgram;
		Slang::ComPtr<slang::IBlob> spirvCode;
		Slang::ComPtr<slang::IBlob> diagnosticsBlob;

		if (SLANG_FAILED(createGlobalSession(globalSession.writeRef()))) {
			std::cerr << "Error: Failed to create Slang global session" << std::endl;
			shaderStage.module = VK_NULL_HANDLE;
		}
		else {
			slang::TargetDesc targetDesc = {};
			targetDesc.format = SLANG_SPIRV;
			targetDesc.profile = globalSession->findProfile("spirv_1_5");

			slang::CompilerOptionEntry optionEntry = {
				slang::CompilerOptionName::EmitSpirvDirectly,
				{slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
			};

			slang::SessionDesc sessionDesc = {};
			sessionDesc.targets = &targetDesc;
			sessionDesc.targetCount = 1;
			sessionDesc.compilerOptionEntries = &optionEntry;
			sessionDesc.compilerOptionEntryCount = 1;
			sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

			if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
				std::cerr << "Error: Failed to create Slang compilation session" << std::endl;
				shaderStage.module = VK_NULL_HANDLE;
			}
			else {
				shaderModule = session->loadModuleFromSourceString(
					shaderPath.stem().string().c_str(),
					shaderPath.string().c_str(),
					shaderSource.c_str(),
					diagnosticsBlob.writeRef());
				printSlangDiagnostics(diagnosticsBlob);
				diagnosticsBlob = nullptr;
				if (!shaderModule) {
					std::cerr << "Error: Failed to load Slang module \"" << filename << "\"" << std::endl;
					shaderStage.module = VK_NULL_HANDLE;
				}
				else {
					shaderModule->findEntryPointByName(entryPointName.c_str(), entryPoint.writeRef());
					if (!entryPoint) {
						std::cerr << "Error: Failed to find Slang entry point \"" << entryPointName << "\" in \"" << filename << "\"" << std::endl;
						shaderStage.module = VK_NULL_HANDLE;
					}
					else {
						slang::IComponentType* componentTypes[] = { shaderModule, entryPoint };
						SlangResult result = session->createCompositeComponentType(componentTypes, 2, composedProgram.writeRef(), diagnosticsBlob.writeRef());
						printSlangDiagnostics(diagnosticsBlob);
						diagnosticsBlob = nullptr;
						if (SLANG_FAILED(result)) {
							std::cerr << "Error: Failed to compose Slang shader program" << std::endl;
							shaderStage.module = VK_NULL_HANDLE;
						}
						else {
							result = composedProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
							printSlangDiagnostics(diagnosticsBlob);
							diagnosticsBlob = nullptr;
							if (SLANG_FAILED(result)) {
								std::cerr << "Error: Failed to link Slang shader program" << std::endl;
								shaderStage.module = VK_NULL_HANDLE;
							}
							else {
								result = linkedProgram->getEntryPointCode(0, 0, spirvCode.writeRef(), diagnosticsBlob.writeRef());
								printSlangDiagnostics(diagnosticsBlob);
								if (SLANG_FAILED(result) || !spirvCode || spirvCode->getBufferSize() == 0) {
									std::cerr << "Error: Failed to compile Slang shader \"" << filename << "\" to SPIR-V" << std::endl;
									shaderStage.module = VK_NULL_HANDLE;
								}
								else {
									VkShaderModuleCreateInfo moduleCreateInfo{};
									moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
									moduleCreateInfo.codeSize = spirvCode->getBufferSize();
									moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(spirvCode->getBufferPointer());
									VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderStage.module));
								}
							}
						}
					}
				}
			}
		}
	}
#endif

	assert(shaderStage.module != VK_NULL_HANDLE);
	return shaderStage;
}

void readDirectory(const std::string& directory, const std::string &extension, std::map<std::string, std::string> &filelist, bool recursive)
{
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	AAssetDir* assetDir = AAssetManager_openDir(androidApp->activity->assetManager, directory.c_str());
	AAssetDir_rewind(assetDir);
	const char* assetName;
	while ((assetName = AAssetDir_getNextFileName(assetDir)) != 0) {
		std::string filename(assetName);
		if (filename.find(extension) == std::string::npos) {
			continue;
		}
		filename.erase(filename.find_last_of("."), std::string::npos);
		filelist[filename] = directory + "/" + assetName;
	}
	AAssetDir_close(assetDir);
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
	std::string searchpattern(directory + "/*" + extension);
	WIN32_FIND_DATA data;
	HANDLE hFind;
	if ((hFind = FindFirstFile(searchpattern.c_str(), &data)) != INVALID_HANDLE_VALUE) {
		do {
			std::string filename(data.cFileName);
			filename.erase(filename.find_last_of("."), std::string::npos);
			filelist[filename] = directory + "/" + data.cFileName;
		} while (FindNextFile(hFind, &data) != 0);
		FindClose(hFind);
	}
	if (recursive) {
		std::string dirpattern = directory + "/*";
		if ((hFind = FindFirstFile(dirpattern.c_str(), &data)) != INVALID_HANDLE_VALUE) {
			do {
				if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					char subdir[MAX_PATH];
					strcpy(subdir, directory.c_str());
					strcat(subdir, "/");
					strcat(subdir, data.cFileName);
					if ((strcmp(data.cFileName, ".") != 0) && (strcmp(data.cFileName, "..") != 0)) {
						readDirectory(subdir, extension, filelist, recursive);
					}
				}
			} while (FindNextFile(hFind, &data) != 0);
			FindClose(hFind);
		}
	}
#elif defined(__linux__)
	struct dirent *entry;
	DIR *dir = opendir(directory.c_str());
	if (dir == NULL) {
		return;
	}
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_REG) {
			std::string filename(entry->d_name);
			if (filename.find(extension) != std::string::npos) {
				filename.erase(filename.find_last_of("."), std::string::npos);
				filelist[filename] = directory + "/" + entry->d_name;
			}
		}
		if (recursive && (entry->d_type == DT_DIR)) {
			std::string subdir = directory + "/" + entry->d_name;
			if ((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)) {
				readDirectory(subdir, extension, filelist, recursive);
			}
		}
	}
	closedir(dir);
#endif
}