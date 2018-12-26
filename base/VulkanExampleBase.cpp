/*
* Vulkan Example base class, stripped down version
*
* Copyright (C) 2016-2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanExampleBase.h"

std::vector<const char*> VulkanExampleBase::args;

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location, int32_t msgCode, const char * pLayerPrefix, const char * pMsg, void * pUserData)
{
	std::string prefix("");
	if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
		prefix += "ERROR:";
	};
	if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
		prefix += "WARNING:";
	};
	if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
		prefix += "DEBUG:";
	}
	std::stringstream debugMessage;
	debugMessage << prefix << " [" << pLayerPrefix << "] Code " << msgCode << " : " << pMsg;
#if defined(__ANDROID__)
	LOGD("%s", debugMessage.str().c_str());
#else
	std::cout << debugMessage.str() << "\n";
#endif
	fflush(stdout);
	return VK_FALSE;
}

VkResult VulkanExampleBase::createInstance(bool enableValidation)
{
	this->settings.validation = enableValidation;

	// Validation can also be forced via a define
#if defined(_VALIDATION)
	this->settings.validation = true;
#endif	

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = name.c_str();
	appInfo.pEngineName = name.c_str();
	appInfo.apiVersion = VK_API_VERSION_1_0;

	std::vector<const char*> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };

	// Enable surface extensions depending on os
#if defined(_WIN32)
	instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	instanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(_DIRECT2DISPLAY)
	instanceExtensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	instanceExtensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif

	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pNext = NULL;
	instanceCreateInfo.pApplicationInfo = &appInfo;
	if (instanceExtensions.size() > 0)
	{
		if (settings.validation) {
			instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		}
		instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
		instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	}
	if (settings.validation) {
#if !defined(__ANDROID__)
		instanceCreateInfo.enabledLayerCount = 1;
		const char *validationLayerNames[] = {
			"VK_LAYER_LUNARG_standard_validation"
		};
		instanceCreateInfo.ppEnabledLayerNames = validationLayerNames;
#else
		instanceCreateInfo.enabledLayerCount = 6;
		const char *validationLayerNames[] = {
			"VK_LAYER_GOOGLE_threading",
			"VK_LAYER_LUNARG_parameter_validation",
			"VK_LAYER_LUNARG_object_tracker",
			"VK_LAYER_LUNARG_core_validation",
			"VK_LAYER_LUNARG_swapchain",
			"VK_LAYER_GOOGLE_unique_objects"
		};
		instanceCreateInfo.ppEnabledLayerNames = validationLayerNames;
#endif
	}
	return vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
}

std::string VulkanExampleBase::getWindowTitle()
{
	std::string device(deviceProperties.deviceName);
	std::string windowTitle;
	windowTitle = title + " - " + device + " - " + std::to_string(lastFPS) + " fps";
	return windowTitle;
}

void VulkanExampleBase::prepare()
{
	/*
		Swapchain
	*/
	initSwapchain();
	setupSwapChain();

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	width = swapChain.extent.width;
	height = swapChain.extent.height;
#endif

	/*
		Command pool
	*/
	VkCommandPoolCreateInfo cmdPoolInfo = {};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &cmdPool));

	/*
		Render pass
	*/

	if (settings.multiSampling) {
		std::array<VkAttachmentDescription, 4> attachments = {};

		// Multisampled attachment that we render to
		attachments[0].format = swapChain.colorFormat;
		attachments[0].samples = settings.sampleCount;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// This is the frame buffer attachment to where the multisampled image
		// will be resolved to and which will be presented to the swapchain
		attachments[1].format = swapChain.colorFormat;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// Multisampled depth attachment we render to
		attachments[2].format = depthFormat;
		attachments[2].samples = settings.sampleCount;
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// Depth resolve attachment
		attachments[3].format = depthFormat;
		attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = {};
		colorReference.attachment = 0;
		colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 2;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// Resolve attachment reference for the color attachment
		VkAttachmentReference resolveReference = {};
		resolveReference.attachment = 1;
		resolveReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		// Pass our resolve attachments to the sub pass
		subpass.pResolveAttachments = &resolveReference;
		subpass.pDepthStencilAttachment = &depthReference;

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

		VkRenderPassCreateInfo renderPassCI = {};
		renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassCI.pAttachments = attachments.data();
		renderPassCI.subpassCount = 1;
		renderPassCI.pSubpasses = &subpass;
		renderPassCI.dependencyCount = 2;
		renderPassCI.pDependencies = dependencies.data();
		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderPass));
	}
	else {
		std::array<VkAttachmentDescription, 2> attachments = {};
		// Color attachment
		attachments[0].format = swapChain.colorFormat;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		// Depth attachment
		attachments[1].format = depthFormat;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = {};
		colorReference.attachment = 0;
		colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 1;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;
		subpassDescription.pDepthStencilAttachment = &depthReference;
		subpassDescription.inputAttachmentCount = 0;
		subpassDescription.pInputAttachments = nullptr;
		subpassDescription.preserveAttachmentCount = 0;
		subpassDescription.pPreserveAttachments = nullptr;
		subpassDescription.pResolveAttachments = nullptr;

		// Subpass dependencies for layout transitions
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

		VkRenderPassCreateInfo renderPassCI{};
		renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassCI.pAttachments = attachments.data();
		renderPassCI.subpassCount = 1;
		renderPassCI.pSubpasses = &subpassDescription;
		renderPassCI.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassCI.pDependencies = dependencies.data();
		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderPass));
	}

	/*
		Pipeline cache
	*/
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo{};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache));

	/*
		Frame buffer
	*/
	setupFrameBuffer();
}

void VulkanExampleBase::renderFrame()
{
	auto tStart = std::chrono::high_resolution_clock::now();
	if (viewUpdated)
	{
		viewUpdated = false;
	}

	render();
	frameCounter++;
	auto tEnd = std::chrono::high_resolution_clock::now();
	auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
	frameTimer = (float)tDiff / 1000.0f;
	camera.update(frameTimer);
	if (camera.moving())
	{
		viewUpdated = true;
	}
	fpsTimer += (float)tDiff;
	if (fpsTimer > 1000.0f)
	{
		lastFPS = static_cast<uint32_t>((float)frameCounter * (1000.0f / fpsTimer));
#if defined(_WIN32)
		std::string windowTitle = getWindowTitle();
		SetWindowText(window, windowTitle.c_str());
#endif
		fpsTimer = 0.0f;
		frameCounter = 0;
	}
}

void VulkanExampleBase::renderLoop()
{
	destWidth = width;
	destHeight = height;
#if defined(_WIN32)
	MSG msg;
	bool quitMessageReceived = false;
	while (!quitMessageReceived) {
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT) {
				quitMessageReceived = true;
				break;
			}
		}
		if (!IsIconic(window)) {
			renderFrame();
		}
	}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	while (1)
	{
		int ident;
		int events;
		struct android_poll_source* source;
		bool destroy = false;

		focused = true;

		while ((ident = ALooper_pollAll(focused ? 0 : -1, NULL, &events, (void**)&source)) >= 0)
		{
			if (source != NULL)
			{
				source->process(androidApp, source);
			}
			if (androidApp->destroyRequested != 0)
			{
				LOGD("Android app destroy requested");
				destroy = true;
				break;
			}
		}

		// App destruction requested
		// Exit loop, example will be destroyed in application main
		if (destroy)
		{
			break;
		}

		// Render frame
		if (prepared)
		{
			auto tStart = std::chrono::high_resolution_clock::now();
			render();
			frameCounter++;
			auto tEnd = std::chrono::high_resolution_clock::now();
			auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
			frameTimer = tDiff / 1000.0f;
			camera.update(frameTimer);
			fpsTimer += (float)tDiff;
			if (fpsTimer > 1000.0f)
			{
				lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
				fpsTimer = 0.0f;
				frameCounter = 0;
			}

			// Check gamepad state
			const float deadZone = 0.0015f;
			// todo : check if gamepad is present
			// todo : time based and relative axis positions
			if (camera.type != Camera::CameraType::firstperson)
			{
				// Rotate
				if (std::abs(gamePadState.axisLeft.x) > deadZone) {
					camera.rotate(glm::vec3(0.0f, gamePadState.axisLeft.x * 0.5f, 0.0f));
				}
				if (std::abs(gamePadState.axisLeft.y) > deadZone) {
					camera.rotate(glm::vec3(gamePadState.axisLeft.y * 0.5f, 0.0f, 0.0f));
				}
			} else {
				camera.updatePad(gamePadState.axisLeft, gamePadState.axisRight, frameTimer);
			}
		}
	}
#elif defined(_DIRECT2DISPLAY)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		fpsTimer += (float)tDiff;
		if (fpsTimer > 1000.0f)
		{
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			fpsTimer = 0.0f;
			frameCounter = 0;
		}
	}
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
		}

		while (wl_display_prepare_read(display) != 0)
			wl_display_dispatch_pending(display);
		wl_display_flush(display);
		wl_display_read_events(display);
		wl_display_dispatch_pending(display);

		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		fpsTimer += (float)tDiff;
		if (fpsTimer > 1000.0f)
		{
			std::string windowTitle = getWindowTitle();
			wl_shell_surface_set_title(shell_surface, windowTitle.c_str());
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			fpsTimer = 0.0f;
			frameCounter = 0;
		}
	}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	xcb_flush(connection);
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
		}
		xcb_generic_event_t *event;
		while ((event = xcb_poll_for_event(connection)))
		{
			handleEvent(event);
			free(event);
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		fpsTimer += (float)tDiff;
		if (fpsTimer > 1000.0f)
		{
			std::string windowTitle = getWindowTitle();
			xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
				window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
				windowTitle.size(), windowTitle.c_str());
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			fpsTimer = 0.0f;
			frameCounter = 0;
		}
	}
#endif
	// Flush device to make sure all resources can be freed 
	vkDeviceWaitIdle(device);
}

VulkanExampleBase::VulkanExampleBase()
{
	char* numConvPtr;
	// Parse command line arguments
	for (size_t i = 0; i < args.size(); i++)
	{
		if (args[i] == std::string("-validation")) {
			settings.validation = true;
		}
		if (args[i] == std::string("-vsync")) {
			settings.vsync = true;
		}
		if ((args[i] == std::string("-f")) || (args[i] == std::string("--fullscreen"))) {
			settings.fullscreen = true;
		}
		if ((args[i] == std::string("-w")) || (args[i] == std::string("--width"))) {
			uint32_t w = strtol(args[i + 1], &numConvPtr, 10);
			if (numConvPtr != args[i + 1]) { width = w; };
		}
		if ((args[i] == std::string("-h")) || (args[i] == std::string("--height"))) {
			uint32_t h = strtol(args[i + 1], &numConvPtr, 10);
			if (numConvPtr != args[i + 1]) { height = h; };
		}
	}
	
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	// Vulkan library is loaded dynamically on Android
	bool libLoaded = vks::android::loadVulkanLibrary();
	assert(libLoaded);
#elif defined(_DIRECT2DISPLAY)

#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	initWaylandConnection();
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	initxcbConnection();
#endif

#if defined(_WIN32)
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	FILE *stream;
	freopen_s(&stream, "CONOUT$", "w+", stdout);
	freopen_s(&stream, "CONOUT$", "w+", stderr);
	SetConsoleTitle(TEXT("Vulkan validation output"));
#endif
}

VulkanExampleBase::~VulkanExampleBase()
{
	// Clean up Vulkan resources
	swapChain.cleanup();
	vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);
	for (uint32_t i = 0; i < frameBuffers.size(); i++) {
		vkDestroyFramebuffer(device, frameBuffers[i], nullptr);
	}
	vkDestroyImageView(device, depthStencil.view, nullptr);
	vkDestroyImage(device, depthStencil.image, nullptr);
	vkFreeMemory(device, depthStencil.mem, nullptr);
	vkDestroyPipelineCache(device, pipelineCache, nullptr);
	vkDestroyCommandPool(device, cmdPool, nullptr);
	if (settings.multiSampling) {
		vkDestroyImage(device, multisampleTarget.color.image, nullptr);
		vkDestroyImageView(device, multisampleTarget.color.view, nullptr);
		vkFreeMemory(device, multisampleTarget.color.memory, nullptr);
		vkDestroyImage(device, multisampleTarget.depth.image, nullptr);
		vkDestroyImageView(device, multisampleTarget.depth.view, nullptr);
		vkFreeMemory(device, multisampleTarget.depth.memory, nullptr);
	}
	delete vulkanDevice;
	if (settings.validation) {
		vkDestroyDebugReportCallback(instance, debugReportCallback, nullptr);
	}
	vkDestroyInstance(instance, nullptr);
#if defined(_DIRECT2DISPLAY)
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	wl_shell_surface_destroy(shell_surface);
	wl_surface_destroy(surface);
	if (keyboard)
		wl_keyboard_destroy(keyboard);
	if (pointer)
		wl_pointer_destroy(pointer);
	wl_seat_destroy(seat);
	wl_shell_destroy(shell);
	wl_compositor_destroy(compositor);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	// todo : android cleanup (if required)
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	xcb_destroy_window(connection, window);
	xcb_disconnect(connection);
#endif
}

void VulkanExampleBase::initVulkan()
{
	VkResult err;

	/*
		Instance creation
	*/
	err = createInstance(settings.validation);
	if (err) {
		std::cerr << "Could not create Vulkan instance!" << std::endl;
		exit(err);
	}

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	vks::android::loadVulkanFunctions(instance);
#endif

	/*
		Validation layers
	*/
	if (settings.validation) {
		vkCreateDebugReportCallback = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
		vkDestroyDebugReportCallback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
		VkDebugReportCallbackCreateInfoEXT debugCreateInfo{};
		debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
		debugCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)debugMessageCallback;
		debugCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
		VK_CHECK_RESULT(vkCreateDebugReportCallback(instance, &debugCreateInfo, nullptr, &debugReportCallback));
	}

	/*
		GPU selection
	*/
	uint32_t gpuCount = 0;
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
	assert(gpuCount > 0);
	std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
	err = vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());
	if (err) {
		std::cerr << "Could not enumerate physical devices!" << std::endl;
		exit(err);
	}
	uint32_t selectedDevice = 0;
#if !defined(VK_USE_PLATFORM_ANDROID_KHR)	
	for (size_t i = 0; i < args.size(); i++) {
		if ((args[i] == std::string("-g")) || (args[i] == std::string("--gpu"))) {
			char* endptr;
			uint32_t index = strtol(args[i + 1], &endptr, 10);
			if (endptr != args[i + 1])  { 
				if (index > gpuCount - 1) {
					std::cerr << "Selected device index " << index << " is out of range, reverting to device 0 (use -listgpus to show available Vulkan devices)" << std::endl;
				} else {
					std::cout << "Selected Vulkan device " << index << std::endl;
					selectedDevice = index;
				}
			};
			break;
		}
	}
#endif

	physicalDevice = physicalDevices[selectedDevice];

	vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
	vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);

	/*
		Device creation
	*/
	vulkanDevice = new vks::VulkanDevice(physicalDevice);
	VkPhysicalDeviceFeatures enabledFeatures{};
	if (deviceFeatures.samplerAnisotropy) {
		enabledFeatures.samplerAnisotropy = VK_TRUE;
	}
	std::vector<const char*> enabledExtensions{};
	VkResult res = vulkanDevice->createLogicalDevice(enabledFeatures, enabledExtensions);
	if (res != VK_SUCCESS) {
		std::cerr << "Could not create Vulkan device!" << std::endl;
		exit(res);
	}
	device = vulkanDevice->logicalDevice;

	/*
		Graphics queue
	*/
	vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.graphics, 0, &queue);

	/*
		Suitable depth format
	*/
	std::vector<VkFormat> depthFormats = { VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_D16_UNORM };
	VkBool32 validDepthFormat = false;
	for (auto& format : depthFormats) {
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
		if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			depthFormat = format;
			validDepthFormat = true;
			break;
		}
	}
	assert(validDepthFormat);

	swapChain.connect(instance, physicalDevice, device);

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	// Get Android device name and manufacturer (to display along GPU name)
	androidProduct = "";
	char prop[PROP_VALUE_MAX+1];
	int len = __system_property_get("ro.product.manufacturer", prop);
	if (len > 0) {
		androidProduct += std::string(prop) + " ";
	};
	len = __system_property_get("ro.product.model", prop);
	if (len > 0) {
		androidProduct += std::string(prop);
	};
	LOGD("androidProduct = %s", androidProduct.c_str());
#endif	
}

#if defined(_WIN32)

HWND VulkanExampleBase::setupWindow(HINSTANCE hinstance, WNDPROC wndproc)
{
	this->windowInstance = hinstance;

	WNDCLASSEX wndClass;

	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wndClass.lpfnWndProc = wndproc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = hinstance;
	wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wndClass.lpszMenuName = NULL;
	wndClass.lpszClassName = name.c_str();
	wndClass.hIconSm = LoadIcon(NULL, IDI_WINLOGO);

	if (!RegisterClassEx(&wndClass)) {
		std::cout << "Could not register window class!\n";
		fflush(stdout);
		exit(1);
	}

	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	if (settings.fullscreen) {
		DEVMODE dmScreenSettings;
		memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
		dmScreenSettings.dmSize = sizeof(dmScreenSettings);
		dmScreenSettings.dmPelsWidth = screenWidth;
		dmScreenSettings.dmPelsHeight = screenHeight;
		dmScreenSettings.dmBitsPerPel = 32;
		dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
		if ((width != (uint32_t)screenWidth) && (height != (uint32_t)screenHeight)) {
			if (ChangeDisplaySettings(&dmScreenSettings, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)	{
				if (MessageBox(NULL, "Fullscreen Mode not supported!\n Switch to window mode?", "Error", MB_YESNO | MB_ICONEXCLAMATION) == IDYES) {
					settings.fullscreen = false;
				} else {
					return nullptr;
				}
			}
		}
	}

	DWORD dwExStyle;
	DWORD dwStyle;

	if (settings.fullscreen) {
		dwExStyle = WS_EX_APPWINDOW;
		dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	} else {
		dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
		dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	}

	RECT windowRect;
	windowRect.left = 0L;
	windowRect.top = 0L;
	windowRect.right = settings.fullscreen ? (long)screenWidth : (long)width;
	windowRect.bottom = settings.fullscreen ? (long)screenHeight : (long)height;

	AdjustWindowRectEx(&windowRect, dwStyle, FALSE, dwExStyle);

	std::string windowTitle = getWindowTitle();
	window = CreateWindowEx(0,
		name.c_str(),
		windowTitle.c_str(),
		dwStyle | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		0,
		0,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		NULL,
		NULL,
		hinstance,
		NULL);

	if (!settings.fullscreen) {
		uint32_t x = (GetSystemMetrics(SM_CXSCREEN) - windowRect.right) / 2;
		uint32_t y = (GetSystemMetrics(SM_CYSCREEN) - windowRect.bottom) / 2;
		SetWindowPos(window, 0, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	}

	if (!window) {
		printf("Could not create window!\n");
		fflush(stdout);
		return nullptr;
		exit(1);
	}

	ShowWindow(window, SW_SHOW);
	SetForegroundWindow(window);
	SetFocus(window);

	return window;
}

void VulkanExampleBase::handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CLOSE:
		prepared = false;
		DestroyWindow(hWnd);
		PostQuitMessage(0);
		break;
	case WM_PAINT:
		ValidateRect(window, NULL);
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case KEY_P:
			paused = !paused;
			break;
		case KEY_ESCAPE:
			PostQuitMessage(0);
			break;
		}

		if (camera.firstperson)
		{
			switch (wParam)
			{
			case KEY_W:
				camera.keys.up = true;
				break;
			case KEY_S:
				camera.keys.down = true;
				break;
			case KEY_A:
				camera.keys.left = true;
				break;
			case KEY_D:
				camera.keys.right = true;
				break;
			}
		}

		keyPressed((uint32_t)wParam);
		break;
	case WM_KEYUP:
		if (camera.firstperson)
		{
			switch (wParam)
			{
			case KEY_W:
				camera.keys.up = false;
				break;
			case KEY_S:
				camera.keys.down = false;
				break;
			case KEY_A:
				camera.keys.left = false;
				break;
			case KEY_D:
				camera.keys.right = false;
				break;
			}
		}
		break;
	case WM_LBUTTONDOWN:
		mousePos = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseButtons.left = true;
		break;
	case WM_RBUTTONDOWN:
		mousePos = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseButtons.right = true;
		break;
	case WM_MBUTTONDOWN:
		mousePos = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseButtons.middle = true;
		break;
	case WM_LBUTTONUP:
		mouseButtons.left = false;
		break;
	case WM_RBUTTONUP:
		mouseButtons.right = false;
		break;
	case WM_MBUTTONUP:
		mouseButtons.middle = false;
		break;
	case WM_MOUSEWHEEL:
	{
		short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		camera.translate(glm::vec3(0.0f, 0.0f, -(float)wheelDelta * 0.005f * camera.movementSpeed));
		viewUpdated = true;
		break;
	}
	case WM_MOUSEMOVE:
	{
		handleMouseMove(LOWORD(lParam), HIWORD(lParam));
		break;
	}
	case WM_SIZE:
		if ((prepared) && (wParam != SIZE_MINIMIZED)) {
			if ((resizing) || ((wParam == SIZE_MAXIMIZED) || (wParam == SIZE_RESTORED))) {
				destWidth = LOWORD(lParam);
				destHeight = HIWORD(lParam);
				windowResize();
			}
		}
		break;
	case WM_ENTERSIZEMOVE:
		resizing = true;
		break;
	case WM_EXITSIZEMOVE:
		resizing = false;
		break;
	}
}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
int32_t VulkanExampleBase::handleAppInput(struct android_app* app, AInputEvent* event)
{
    ImGuiIO& io = ImGui::GetIO();
    bool uiMouseCapture = io.WantCaptureMouse;

	VulkanExampleBase* vulkanExample = reinterpret_cast<VulkanExampleBase*>(app->userData);
	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION)
	{
		int32_t eventSource = AInputEvent_getSource(event);
		switch (eventSource) {
			case AINPUT_SOURCE_JOYSTICK: {
				// Left thumbstick
				vulkanExample->gamePadState.axisLeft.x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
				vulkanExample->gamePadState.axisLeft.y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
				// Right thumbstick
				vulkanExample->gamePadState.axisRight.x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
				vulkanExample->gamePadState.axisRight.y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
				break;
			}

			case AINPUT_SOURCE_TOUCHSCREEN: {
				int32_t action = AMotionEvent_getAction(event);
				int32_t pointerCount = AMotionEvent_getPointerCount(event);
				int32_t flags = action & AMOTION_EVENT_ACTION_MASK;

				switch (flags) {
					case AMOTION_EVENT_ACTION_DOWN:
                    case AMOTION_EVENT_ACTION_POINTER_DOWN: {
                        for (uint32_t i = 0; i < pointerCount; i++) {
                            vulkanExample->touchPoints[i].x = AMotionEvent_getX(event, i);
                            vulkanExample->touchPoints[i].y = AMotionEvent_getY(event, i);
                        };
                        int32_t pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
                        if (pointerIndex < 2) {
                            vulkanExample->touchPoints[pointerIndex].down = true;
                        }
                        if (pointerCount < 2) {
                            // Detect single tap
                            int64_t eventTime = AMotionEvent_getEventTime(event);
                            int64_t downTime = AMotionEvent_getDownTime(event);
                            if (eventTime - downTime <= vks::android::TAP_TIMEOUT) {
                                float deadZone = (160.f / vks::android::screenDensity) * vks::android::TAP_SLOP * vks::android::TAP_SLOP;
                                float x = AMotionEvent_getX(event, 0) - vulkanExample->touchPoints[0].x;
                                float y = AMotionEvent_getY(event, 0) - vulkanExample->touchPoints[0].y;
                                if ((x * x + y * y) < deadZone) {
                                    vulkanExample->mousePos.x = vulkanExample->touchPoints[0].x;
                                    vulkanExample->mousePos.y = vulkanExample->touchPoints[0].y;
                                    vulkanExample->mouseButtons.left = true;
                                }
                            };
                        }
                        break;
                    }
					case AMOTION_EVENT_ACTION_UP:
                    case AMOTION_EVENT_ACTION_POINTER_UP: {
                        int32_t pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
                        if (pointerIndex < 2) {
                            vulkanExample->touchPoints[pointerIndex].down = false;
                        }
                        if (pointerCount < 2) {
                            vulkanExample->touchPoints[1].down = false;
                        }
                        break;
                    }
					case AMOTION_EVENT_ACTION_MOVE: {
					    // Pinch and zoom
                        if (!uiMouseCapture && vulkanExample->touchPoints[0].down && vulkanExample->touchPoints[1].down) {
                            for (uint32_t i = 0; i < pointerCount; i++) {
                                if (vulkanExample->touchPoints[i].down) {
                                    vulkanExample->touchPoints[i].x = AMotionEvent_getX(event, i);
                                    vulkanExample->touchPoints[i].y = AMotionEvent_getY(event, i);
                                }
                            };
                            float dx = vulkanExample->touchPoints[1].x - vulkanExample->touchPoints[0].x;
                            float dy = vulkanExample->touchPoints[1].y - vulkanExample->touchPoints[0].y;
                            float d = sqrt(dx * dx + dy * dy);
                            if (d < vulkanExample->pinchDist) {
                                vulkanExample->camera.translate(glm::vec3(0.0f, 0.0f, 0.1f));
                            };
                            if (d > vulkanExample->pinchDist) {
                                vulkanExample->camera.translate(glm::vec3(0.0f, 0.0f, -0.1f));
                            };
                            vulkanExample->pinchDist = d;
                        } else {
                            // Rotate
                            if (!uiMouseCapture && vulkanExample->touchPoints[0].down) {
                                int32_t eventX = AMotionEvent_getX(event, 0);
                                int32_t eventY = AMotionEvent_getY(event, 0);

                                float deltaX = (vulkanExample->touchPoints[0].y - eventY) * vulkanExample->camera.rotationSpeed * 0.5f;
                                float deltaY = (vulkanExample->touchPoints[0].x - eventX) * vulkanExample->camera.rotationSpeed * 0.5f;

                                vulkanExample->camera.rotate(glm::vec3(deltaX, 0.0f, 0.0f));
                                vulkanExample->camera.rotate(glm::vec3(0.0f, -deltaY, 0.0f));

                                vulkanExample->touchPoints[0].x = eventX;
                                vulkanExample->touchPoints[0].y = eventY;
                            }
                        }
						break;
					}
					default:
						return 1;
				}
			}

			return 1;
		}
	}

	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY)
	{
		int32_t keyCode = AKeyEvent_getKeyCode((const AInputEvent*)event);
		int32_t action = AKeyEvent_getAction((const AInputEvent*)event);
		int32_t button = 0;

		if (action == AKEY_EVENT_ACTION_UP)
			return 0;

		switch (keyCode)
		{
		case AKEYCODE_BUTTON_A:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_A);
			break;
		case AKEYCODE_BUTTON_B:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_B);
			break;
		case AKEYCODE_BUTTON_X:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_X);
			break;
		case AKEYCODE_BUTTON_Y:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_Y);
			break;
		case AKEYCODE_BUTTON_L1:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_L1);
			break;
		case AKEYCODE_BUTTON_R1:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_R1);
			break;
		case AKEYCODE_BUTTON_START:
			vulkanExample->paused = !vulkanExample->paused;
			break;
		};

		LOGD("Button %d pressed", keyCode);
	}

	return 0;
}

void VulkanExampleBase::handleAppCommand(android_app * app, int32_t cmd)
{
	assert(app->userData != NULL);
	VulkanExampleBase* vulkanExample = reinterpret_cast<VulkanExampleBase*>(app->userData);
	switch (cmd)
	{
	case APP_CMD_SAVE_STATE:
		LOGD("APP_CMD_SAVE_STATE");
		/*
		vulkanExample->app->savedState = malloc(sizeof(struct saved_state));
		*((struct saved_state*)vulkanExample->app->savedState) = vulkanExample->state;
		vulkanExample->app->savedStateSize = sizeof(struct saved_state);
		*/
		break;
	case APP_CMD_INIT_WINDOW:
		LOGD("APP_CMD_INIT_WINDOW");
		if (androidApp->window != NULL)
		{
			vulkanExample->initVulkan();
			vulkanExample->prepare();
			assert(vulkanExample->prepared);
		}
		else
		{
			LOGE("No window assigned!");
		}
		break;
	case APP_CMD_LOST_FOCUS:
		LOGD("APP_CMD_LOST_FOCUS");
		vulkanExample->focused = false;
		break;
	case APP_CMD_GAINED_FOCUS:
		LOGD("APP_CMD_GAINED_FOCUS");
		vulkanExample->focused = true;
		break;
	case APP_CMD_TERM_WINDOW:
		// Window is hidden or closed, clean up resources
		LOGD("APP_CMD_TERM_WINDOW");
		vulkanExample->swapChain.cleanup();
		break;
	}
}
#elif defined(_DIRECT2DISPLAY)
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
/*static*/void VulkanExampleBase::registryGlobalCb(void *data,
		wl_registry *registry, uint32_t name, const char *interface,
		uint32_t version)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->registryGlobal(registry, name, interface, version);
}

/*static*/void VulkanExampleBase::seatCapabilitiesCb(void *data, wl_seat *seat,
		uint32_t caps)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->seatCapabilities(seat, caps);
}

/*static*/void VulkanExampleBase::pointerEnterCb(void *data,
		wl_pointer *pointer, uint32_t serial, wl_surface *surface,
		wl_fixed_t sx, wl_fixed_t sy)
{
}

/*static*/void VulkanExampleBase::pointerLeaveCb(void *data,
		wl_pointer *pointer, uint32_t serial, wl_surface *surface)
{
}

/*static*/void VulkanExampleBase::pointerMotionCb(void *data,
		wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->pointerMotion(pointer, time, sx, sy);
}
void VulkanExampleBase::pointerMotion(wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
	handleMouseMove(wl_fixed_to_int(sx), wl_fixed_to_int(sy));
}

/*static*/void VulkanExampleBase::pointerButtonCb(void *data,
		wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button,
		uint32_t state)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->pointerButton(pointer, serial, time, button, state);
}

void VulkanExampleBase::pointerButton(struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
	switch (button)
	{
	case BTN_LEFT:
		mouseButtons.left = !!state;
		break;
	case BTN_MIDDLE:
		mouseButtons.middle = !!state;
		break;
	case BTN_RIGHT:
		mouseButtons.right = !!state;
		break;
	default:
		break;
	}
}

/*static*/void VulkanExampleBase::pointerAxisCb(void *data,
		wl_pointer *pointer, uint32_t time, uint32_t axis,
		wl_fixed_t value)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->pointerAxis(pointer, time, axis, value);
}

void VulkanExampleBase::pointerAxis(wl_pointer *pointer, uint32_t time,
		uint32_t axis, wl_fixed_t value)
{
	double d = wl_fixed_to_double(value);
	switch (axis)
	{
	case REL_X:
		camera.translate(glm::vec3(0.0f, 0.0f, -d * 0.005f * camera.movementSpeed));
		viewUpdated = true;
		break;
	default:
		break;
	}
}

/*static*/void VulkanExampleBase::keyboardKeymapCb(void *data,
		struct wl_keyboard *keyboard, uint32_t format, int fd, uint32_t size)
{
}

/*static*/void VulkanExampleBase::keyboardEnterCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial,
		struct wl_surface *surface, struct wl_array *keys)
{
}

/*static*/void VulkanExampleBase::keyboardLeaveCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial,
		struct wl_surface *surface)
{
}

/*static*/void VulkanExampleBase::keyboardKeyCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
		uint32_t key, uint32_t state)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->keyboardKey(keyboard, serial, time, key, state);
}

void VulkanExampleBase::keyboardKey(struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	switch (key)
	{
	case KEY_W:
		camera.keys.up = !!state;
		break;
	case KEY_S:
		camera.keys.down = !!state;
		break;
	case KEY_A:
		camera.keys.left = !!state;
		break;
	case KEY_D:
		camera.keys.right = !!state;
		break;
	case KEY_P:
		if (state)
			paused = !paused;
		break;
	case KEY_ESC:
		quit = true;
		break;
	}

	if (state)
		keyPressed(key);
}

/*static*/void VulkanExampleBase::keyboardModifiersCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed,
		uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
}

void VulkanExampleBase::seatCapabilities(wl_seat *seat, uint32_t caps)
{
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer)
	{
		pointer = wl_seat_get_pointer(seat);
		static const struct wl_pointer_listener pointer_listener =
		{ pointerEnterCb, pointerLeaveCb, pointerMotionCb, pointerButtonCb,
				pointerAxisCb, };
		wl_pointer_add_listener(pointer, &pointer_listener, this);
	}
	else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer)
	{
		wl_pointer_destroy(pointer);
		pointer = nullptr;
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard)
	{
		keyboard = wl_seat_get_keyboard(seat);
		static const struct wl_keyboard_listener keyboard_listener =
		{ keyboardKeymapCb, keyboardEnterCb, keyboardLeaveCb, keyboardKeyCb,
				keyboardModifiersCb, };
		wl_keyboard_add_listener(keyboard, &keyboard_listener, this);
	}
	else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard)
	{
		wl_keyboard_destroy(keyboard);
		keyboard = nullptr;
	}
}

void VulkanExampleBase::registryGlobal(wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, "wl_compositor") == 0)
	{
		compositor = (wl_compositor *) wl_registry_bind(registry, name,
				&wl_compositor_interface, 3);
	}
	else if (strcmp(interface, "wl_shell") == 0)
	{
		shell = (wl_shell *) wl_registry_bind(registry, name,
				&wl_shell_interface, 1);
	}
	else if (strcmp(interface, "wl_seat") == 0)
	{
		seat = (wl_seat *) wl_registry_bind(registry, name, &wl_seat_interface,
				1);

		static const struct wl_seat_listener seat_listener =
		{ seatCapabilitiesCb, };
		wl_seat_add_listener(seat, &seat_listener, this);
	}
}

/*static*/void VulkanExampleBase::registryGlobalRemoveCb(void *data,
		struct wl_registry *registry, uint32_t name)
{
}

void VulkanExampleBase::initWaylandConnection()
{
	display = wl_display_connect(NULL);
	if (!display)
	{
		std::cout << "Could not connect to Wayland display!\n";
		fflush(stdout);
		exit(1);
	}

	registry = wl_display_get_registry(display);
	if (!registry)
	{
		std::cout << "Could not get Wayland registry!\n";
		fflush(stdout);
		exit(1);
	}

	static const struct wl_registry_listener registry_listener =
	{ registryGlobalCb, registryGlobalRemoveCb };
	wl_registry_add_listener(registry, &registry_listener, this);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	if (!compositor || !shell || !seat)
	{
		std::cout << "Could not bind Wayland protocols!\n";
		fflush(stdout);
		exit(1);
	}
}

static void PingCb(void *data, struct wl_shell_surface *shell_surface,
		uint32_t serial)
{
	wl_shell_surface_pong(shell_surface, serial);
}

static void ConfigureCb(void *data, struct wl_shell_surface *shell_surface,
		uint32_t edges, int32_t width, int32_t height)
{
}

static void PopupDoneCb(void *data, struct wl_shell_surface *shell_surface)
{
}

wl_shell_surface *VulkanExampleBase::setupWindow()
{
	surface = wl_compositor_create_surface(compositor);
	shell_surface = wl_shell_get_shell_surface(shell, surface);

	static const struct wl_shell_surface_listener shell_surface_listener =
	{ PingCb, ConfigureCb, PopupDoneCb };

	wl_shell_surface_add_listener(shell_surface, &shell_surface_listener, this);
	wl_shell_surface_set_toplevel(shell_surface);
	std::string windowTitle = getWindowTitle();
	wl_shell_surface_set_title(shell_surface, windowTitle.c_str());
	return shell_surface;
}

#elif defined(VK_USE_PLATFORM_XCB_KHR)

static inline xcb_intern_atom_reply_t* intern_atom_helper(xcb_connection_t *conn, bool only_if_exists, const char *str)
{
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, only_if_exists, strlen(str), str);
	return xcb_intern_atom_reply(conn, cookie, NULL);
}

// Set up a window using XCB and request event types
xcb_window_t VulkanExampleBase::setupWindow()
{
	uint32_t value_mask, value_list[32];

	window = xcb_generate_id(connection);

	value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	value_list[0] = screen->black_pixel;
	value_list[1] =
		XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_KEY_PRESS |
		XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_POINTER_MOTION |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE;

	if (settings.fullscreen)
	{
		width = destWidth = screen->width_in_pixels;
		height = destHeight = screen->height_in_pixels;
	}

	xcb_create_window(connection,
		XCB_COPY_FROM_PARENT,
		window, screen->root,
		0, 0, width, height, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual,
		value_mask, value_list);

	/* Magic code that will send notification when window is destroyed */
	xcb_intern_atom_reply_t* reply = intern_atom_helper(connection, true, "WM_PROTOCOLS");
	atom_wm_delete_window = intern_atom_helper(connection, false, "WM_DELETE_WINDOW");

	xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, (*reply).atom, 4, 32, 1,
		&(*atom_wm_delete_window).atom);

	std::string windowTitle = getWindowTitle();
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		title.size(), windowTitle.c_str());

	free(reply);

	if (settings.fullscreen)
	{
		xcb_intern_atom_reply_t *atom_wm_state = intern_atom_helper(connection, false, "_NET_WM_STATE");
		xcb_intern_atom_reply_t *atom_wm_fullscreen = intern_atom_helper(connection, false, "_NET_WM_STATE_FULLSCREEN");
		xcb_change_property(connection,
				XCB_PROP_MODE_REPLACE,
				window, atom_wm_state->atom,
				XCB_ATOM_ATOM, 32, 1,
				&(atom_wm_fullscreen->atom));
		free(atom_wm_fullscreen);
		free(atom_wm_state);
	}	

	xcb_map_window(connection, window);

	return(window);
}

// Initialize XCB connection
void VulkanExampleBase::initxcbConnection()
{
	const xcb_setup_t *setup;
	xcb_screen_iterator_t iter;
	int scr;

	connection = xcb_connect(NULL, &scr);
	if (connection == NULL) {
		printf("Could not find a compatible Vulkan ICD!\n");
		fflush(stdout);
		exit(1);
	}

	setup = xcb_get_setup(connection);
	iter = xcb_setup_roots_iterator(setup);
	while (scr-- > 0)
		xcb_screen_next(&iter);
	screen = iter.data;
}

void VulkanExampleBase::handleEvent(const xcb_generic_event_t *event)
{
	switch (event->response_type & 0x7f)
	{
	case XCB_CLIENT_MESSAGE:
		if ((*(xcb_client_message_event_t*)event).data.data32[0] ==
			(*atom_wm_delete_window).atom) {
			quit = true;
		}
		break;
	case XCB_MOTION_NOTIFY:
	{
		xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;
		handleMouseMove((int32_t)motion->event_x, (int32_t)motion->event_y);
		break;
	}
	break;
	case XCB_BUTTON_PRESS:
	{
		xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
		if (press->detail == XCB_BUTTON_INDEX_1)
			mouseButtons.left = true;
		if (press->detail == XCB_BUTTON_INDEX_2)
			mouseButtons.middle = true;
		if (press->detail == XCB_BUTTON_INDEX_3)
			mouseButtons.right = true;
	}
	break;
	case XCB_BUTTON_RELEASE:
	{
		xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
		if (press->detail == XCB_BUTTON_INDEX_1)
			mouseButtons.left = false;
		if (press->detail == XCB_BUTTON_INDEX_2)
			mouseButtons.middle = false;
		if (press->detail == XCB_BUTTON_INDEX_3)
			mouseButtons.right = false;
	}
	break;
	case XCB_KEY_PRESS:
	{
		const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
		switch (keyEvent->detail)
		{
			case KEY_W:
				camera.keys.up = true;
				break;
			case KEY_S:
				camera.keys.down = true;
				break;
			case KEY_A:
				camera.keys.left = true;
				break;
			case KEY_D:
				camera.keys.right = true;
				break;
			case KEY_P:
				paused = !paused;
				break;
		}
	}
	break;	
	case XCB_KEY_RELEASE:
	{
		const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
		switch (keyEvent->detail)
		{
			case KEY_W:
				camera.keys.up = false;
				break;
			case KEY_S:
				camera.keys.down = false;
				break;
			case KEY_A:
				camera.keys.left = false;
				break;
			case KEY_D:
				camera.keys.right = false;
				break;			
			case KEY_ESCAPE:
				quit = true;
				break;
		}
		keyPressed(keyEvent->detail);
	}
	break;
	case XCB_DESTROY_NOTIFY:
		quit = true;
		break;
	case XCB_CONFIGURE_NOTIFY:
	{
		const xcb_configure_notify_event_t *cfgEvent = (const xcb_configure_notify_event_t *)event;
		if ((prepared) && ((cfgEvent->width != width) || (cfgEvent->height != height)))
		{
				destWidth = cfgEvent->width;
				destHeight = cfgEvent->height;
				if ((destWidth > 0) && (destHeight > 0))
				{
					windowResize();
				}
		}
	}
	break;
	default:
		break;
	}
}
#endif

void VulkanExampleBase::windowResized() {}

void VulkanExampleBase::keyPressed(uint32_t) {}

void VulkanExampleBase::buildCommandBuffers() {}

void VulkanExampleBase::setupFrameBuffer()
{
	/*
	MSAA
	*/
	if (settings.multiSampling) {
		// Check if device supports requested sample count for color and depth frame buffer
		//assert((deviceProperties.limits.framebufferColorSampleCounts >= sampleCount) && (deviceProperties.limits.framebufferDepthSampleCounts >= sampleCount));

		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = swapChain.colorFormat;
		imageCI.extent.width = width;
		imageCI.extent.height = height;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.samples = settings.sampleCount;
		imageCI.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &multisampleTarget.color.image));

		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, multisampleTarget.color.image, &memReqs);
		VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllocInfo.allocationSize = memReqs.size;
		VkBool32 lazyMemTypePresent;
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
		if (!lazyMemTypePresent) {
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		}
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &multisampleTarget.color.memory));
		vkBindImageMemory(device, multisampleTarget.color.image, multisampleTarget.color.memory, 0);

		// Create image view for the MSAA target
		VkImageViewCreateInfo imageViewCI{};
		imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCI.image = multisampleTarget.color.image;
		imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCI.format = swapChain.colorFormat;
		imageViewCI.components.r = VK_COMPONENT_SWIZZLE_R;
		imageViewCI.components.g = VK_COMPONENT_SWIZZLE_G;
		imageViewCI.components.b = VK_COMPONENT_SWIZZLE_B;
		imageViewCI.components.a = VK_COMPONENT_SWIZZLE_A;
		imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCI.subresourceRange.levelCount = 1;
		imageViewCI.subresourceRange.layerCount = 1;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &multisampleTarget.color.view));

		// Depth target
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = depthFormat;
		imageCI.extent.width = width;
		imageCI.extent.height = height;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.samples = settings.sampleCount;
		imageCI.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &multisampleTarget.depth.image));

		vkGetImageMemoryRequirements(device, multisampleTarget.depth.image, &memReqs);
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
		if (!lazyMemTypePresent) {
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		}
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &multisampleTarget.depth.memory));
		vkBindImageMemory(device, multisampleTarget.depth.image, multisampleTarget.depth.memory, 0);

		// Create image view for the MSAA target
		imageViewCI.image = multisampleTarget.depth.image;
		imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCI.format = depthFormat;
		imageViewCI.components.r = VK_COMPONENT_SWIZZLE_R;
		imageViewCI.components.g = VK_COMPONENT_SWIZZLE_G;
		imageViewCI.components.b = VK_COMPONENT_SWIZZLE_B;
		imageViewCI.components.a = VK_COMPONENT_SWIZZLE_A;
		imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		imageViewCI.subresourceRange.levelCount = 1;
		imageViewCI.subresourceRange.layerCount = 1;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &multisampleTarget.depth.view));
	}


	// Depth/Stencil attachment is the same for all frame buffers

	VkImageCreateInfo image = {};
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.pNext = NULL;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = depthFormat;
	image.extent = { width, height, 1 };
	image.mipLevels = 1;
	image.arrayLayers = 1;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image.flags = 0;

	VkMemoryAllocateInfo mem_alloc = {};
	mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mem_alloc.pNext = NULL;
	mem_alloc.allocationSize = 0;
	mem_alloc.memoryTypeIndex = 0;

	VkImageViewCreateInfo depthStencilView = {};
	depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depthStencilView.pNext = NULL;
	depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depthStencilView.format = depthFormat;
	depthStencilView.flags = 0;
	depthStencilView.subresourceRange = {};
	depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	depthStencilView.subresourceRange.baseMipLevel = 0;
	depthStencilView.subresourceRange.levelCount = 1;
	depthStencilView.subresourceRange.baseArrayLayer = 0;
	depthStencilView.subresourceRange.layerCount = 1;

	VkMemoryRequirements memReqs;
	VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &depthStencil.image));
	vkGetImageMemoryRequirements(device, depthStencil.image, &memReqs);
	mem_alloc.allocationSize = memReqs.size;
	mem_alloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &mem_alloc, nullptr, &depthStencil.mem));
	VK_CHECK_RESULT(vkBindImageMemory(device, depthStencil.image, depthStencil.mem, 0));

	depthStencilView.image = depthStencil.image;
	VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &depthStencil.view));

	//

	VkImageView attachments[4];

	if (settings.multiSampling) {
		attachments[0] = multisampleTarget.color.view;
		attachments[2] = multisampleTarget.depth.view;
		attachments[3] = depthStencil.view;
	}
	else {
		attachments[1] = depthStencil.view;
	}

	VkFramebufferCreateInfo frameBufferCI{};
	frameBufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	frameBufferCI.pNext = NULL;
	frameBufferCI.renderPass = renderPass;
	frameBufferCI.attachmentCount = settings.multiSampling ? 4 :2;
	frameBufferCI.pAttachments = attachments;
	frameBufferCI.width = width;
	frameBufferCI.height = height;
	frameBufferCI.layers = 1;

	// Create frame buffers for every swap chain image
	frameBuffers.resize(swapChain.imageCount);
	for (uint32_t i = 0; i < frameBuffers.size(); i++) {
		if (settings.multiSampling) {
			attachments[1] = swapChain.buffers[i].view;
		}
		else {
			attachments[0] = swapChain.buffers[i].view;
		}
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCI, nullptr, &frameBuffers[i]));
	}
}

void VulkanExampleBase::windowResize()
{
	if (!prepared) {
		return;
	}
	prepared = false;

	vkDeviceWaitIdle(device);
	width = destWidth;
	height = destHeight;
	setupSwapChain();
	vkDestroyImageView(device, depthStencil.view, nullptr);
	vkDestroyImage(device, depthStencil.image, nullptr);
	vkFreeMemory(device, depthStencil.mem, nullptr);
	for (uint32_t i = 0; i < frameBuffers.size(); i++) {
		vkDestroyFramebuffer(device, frameBuffers[i], nullptr);
	}
	setupFrameBuffer();
	buildCommandBuffers();
	vkDeviceWaitIdle(device);

	camera.updateAspectRatio((float)width / (float)height);
	windowResized();

	prepared = true;
}

void VulkanExampleBase::handleMouseMove(int32_t x, int32_t y)
{
	int32_t dx = (int32_t)mousePos.x - x;
	int32_t dy = (int32_t)mousePos.y - y;

	ImGuiIO& io = ImGui::GetIO();
	bool handled = io.WantCaptureMouse;

	if (handled) {
		mousePos = glm::vec2((float)x, (float)y);
		return;
	}

	if (handled) {
		mousePos = glm::vec2((float)x, (float)y);
		return;
	}

	if (mouseButtons.left) {
		camera.rotate(glm::vec3(dy * camera.rotationSpeed, -dx * camera.rotationSpeed, 0.0f));
		viewUpdated = true;
	}
	if (mouseButtons.right) {
		camera.translate(glm::vec3(-0.0f, 0.0f, dy * .005f * camera.movementSpeed));
		viewUpdated = true;
	}
	if (mouseButtons.middle) {
		camera.translate(glm::vec3(-dx * 0.01f, -dy * 0.01f, 0.0f));
		viewUpdated = true;
	}
	mousePos = glm::vec2((float)x, (float)y);
}

void VulkanExampleBase::initSwapchain()
{
#if defined(_WIN32)
	swapChain.initSurface(windowInstance, window);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)	
	swapChain.initSurface(androidApp->window);
#elif defined(_DIRECT2DISPLAY)
	swapChain.initSurface(width, height);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	swapChain.initSurface(display, surface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	swapChain.initSurface(connection, window);
#endif
}

void VulkanExampleBase::setupSwapChain()
{
	swapChain.create(&width, &height, settings.vsync);
}