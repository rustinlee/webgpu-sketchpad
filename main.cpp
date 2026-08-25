#include <cassert>
#include <iostream>
#include <vector>
#include <array>
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <webgpu/webgpu.h>
#ifdef WEBGPU_BACKEND_WGPU
#include <webgpu/wgpu.h>
#endif // WEBGPU_BACKEND_WGPU
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const *options) {
	struct UserData {
		WGPUAdapter adapter = nullptr;
		bool requestEnded = false;
	};
	UserData userData;

	auto onAdapterRequestCompleted = [](
		WGPURequestAdapterStatus status, WGPUAdapter adapter, const char *message, void *pUserData
	) {
		UserData& userData = *static_cast<UserData*>(pUserData);
		if (status == WGPURequestAdapterStatus_Success) {
			userData.adapter = adapter;
		} else {
			std::cout << "Could not get WebGPU adapter: " << message << std::endl;
		}
		userData.requestEnded = true;
	};

	wgpuInstanceRequestAdapter(instance, options, onAdapterRequestCompleted, &userData);

	// In an Emscripten context, wgpuInstanceRequestAdapter may not immediately call its callback
#ifdef __EMSCRIPTEN__
	while (!userData.requestEnded) {
		emscripten_sleep(100);
	}
#endif

	assert(userData.requestEnded);

	return userData.adapter;
}

WGPUDevice requestDeviceSync(WGPUAdapter adapter, WGPUDeviceDescriptor const *descriptor) {
	struct UserData {
		WGPUDevice device = nullptr;
		bool requestEnded = false;
	};
	UserData userData;

	auto onDeviceRequestCompleted = [](
		WGPURequestDeviceStatus status, WGPUDevice device, const char *message, void *pUserData
	) {
		UserData& userData = *static_cast<UserData*>(pUserData);
		if (status == WGPURequestDeviceStatus_Success) {
			userData.device = device;
		} else {
			std::cout << "Could not get WebGPU device: " << message << std::endl;
		}
		userData.requestEnded = true;
	};

	wgpuAdapterRequestDevice(
		adapter,
		descriptor,
		onDeviceRequestCompleted,
		(void*)&userData
	);

#ifdef __EMSCRIPTEN__
	while (!userData.requestEnded) {
		emscripten_sleep(100);
	}
#endif // __EMSCRIPTEN__

	assert(userData.requestEnded);

	return userData.device;
}

void inspectDevice(WGPUDevice device) {
	std::vector<WGPUFeatureName> features;
	size_t featureCount = wgpuDeviceEnumerateFeatures(device, nullptr);
	features.resize(featureCount);
	wgpuDeviceEnumerateFeatures(device, features.data());

	std::cout << "Device features:" << std::endl;
	std::cout << std::hex;
	for (auto f : features) {
		std::cout << " - 0x" << f << std::endl;
	}
	std::cout << std::dec;

	WGPUSupportedLimits limits = {};
	limits.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_DAWN
	bool success = wgpuDeviceGetLimits(device, &limits) == WGPUStatus_Success;
#else
	bool success = wgpuDeviceGetLimits(device, &limits);
#endif

	if (success) {
		std::cout << "Device limits:" << std::endl;
		std::cout << " - maxTextureDimension1D: " << limits.limits.maxTextureDimension1D << std::endl;
		std::cout << " - maxTextureDimension2D: " << limits.limits.maxTextureDimension2D << std::endl;
		std::cout << " - maxTextureDimension3D: " << limits.limits.maxTextureDimension3D << std::endl;
		std::cout << " - maxTextureArrayLayers: " << limits.limits.maxTextureArrayLayers << std::endl;
		// [...] Extra device limits
	}
}

class Application {
public:
	bool Initialize();
	void Terminate();
	void MainLoop();
	bool IsRunning();

private:
	GLFWwindow *window;
	WGPUSurface surface;
	WGPUDevice device;
	WGPUQueue queue;
};

bool Application::Initialize() {
	if (!glfwInit()) {
		std::cerr << "Could not initialize GLFW!" << std::endl;
		return false;
	}

	//glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	window = glfwCreateWindow(640, 480, "WebGPU Sketchpad", nullptr, nullptr);

	if (!window) {
		std::cerr << "Could not open GLFW window!" << std::endl;
		Terminate();
		return false;
	}

	std::cout << "GLFW window opened: " << window << std::endl;

	WGPUInstanceDescriptor desc = {};
	desc.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_EMSCRIPTEN
	WGPUInstance instance = wgpuCreateInstance(nullptr);
#else
	WGPUInstance instance = wgpuCreateInstance(&desc);
#endif

	if (!instance) {
		std::cerr << "Failed to initialize WebGPU instance" << std::endl;
		return false;
	}

	std::cout << "WebGPU instance: " << instance << std::endl;

	std::cout << "Requesting adapter..." << std::endl;
	surface = glfwGetWGPUSurface(instance, window);
	std::cout << "Acquired surface: " << surface << std::endl;

	WGPURequestAdapterOptions adapterOptions = {};
	adapterOptions.nextInChain = nullptr;
	adapterOptions.compatibleSurface = surface;

	WGPUAdapter adapter = requestAdapterSync(instance, &adapterOptions);
	wgpuInstanceRelease(instance);

	std::cout << "Got adapter: " << adapter << std::endl;

#ifndef __EMSCRIPTEN__
	WGPUSupportedLimits supportedLimits = {};
	supportedLimits.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_DAWN
	bool success = wgpuAdapterGetLimits(adapter, &supportedLimits) == WGPUStatus_Success;
#else
	bool success = wgpuAdapterGetLimits(adapter, &supportedLimits);
#endif

	if (success) {
		std::cout << "Adapter limits:" << std::endl;
		std::cout << " - maxTextureDimension1D: " << supportedLimits.limits.maxTextureDimension1D << std::endl;
		std::cout << " - maxTextureDimension2D: " << supportedLimits.limits.maxTextureDimension2D << std::endl;
		std::cout << " - maxTextureDimension3D: " << supportedLimits.limits.maxTextureDimension3D << std::endl;
		std::cout << " - maxTextureArrayLayers: " << supportedLimits.limits.maxTextureArrayLayers << std::endl;
	}
#endif

	std::cout << "Requesting device..." << std::endl;

	WGPUDeviceDescriptor deviceDesc = {};
	deviceDesc.nextInChain = nullptr;
	deviceDesc.label = "WebGPU Device";
	deviceDesc.requiredFeatureCount = 0; // TODO: require RGBA32Sfloat textures, among other things
	deviceDesc.requiredLimits = nullptr;
	deviceDesc.defaultQueue.nextInChain = nullptr;
	deviceDesc.label = "Default Queue";
	deviceDesc.deviceLostCallback = [](
		WGPUDeviceLostReason reason, const char *message, void* /* pUserData */
	) {
		std::cout << "Device lost: reason " << reason;
		if (message) std::cout << " (" << message << ")";
		std::cout << std::endl;
	};

	device = requestDeviceSync(adapter, &deviceDesc);
	wgpuAdapterRelease(adapter);

	std::cout << "Got device: " << device << std::endl;
	inspectDevice(device);

	queue = wgpuDeviceGetQueue(device);

	auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, void* /* pUserData */) {
		std::cout << "Queued work finished with status: " << status << std::endl;
	};
	wgpuQueueOnSubmittedWorkDone(queue, onQueueWorkDone, nullptr /* pUserData */);

	WGPUCommandEncoderDescriptor encoderDesc = {};
	encoderDesc.nextInChain = nullptr;
	encoderDesc.label = "Default Command Encoder";
	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

	wgpuCommandEncoderInsertDebugMarker(encoder, "debug marker 1");
	wgpuCommandEncoderInsertDebugMarker(encoder, "debug marker 2");

	WGPUCommandBufferDescriptor cmdBufferDesc = {};
	cmdBufferDesc.nextInChain = nullptr;
	cmdBufferDesc.label = "Debug Command Buffer";
	WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
	wgpuCommandEncoderRelease(encoder);

	std::cout << "Submitting command..." << std::endl;
	wgpuQueueSubmit(queue, 1, &command);
	wgpuCommandBufferRelease(command);
	std::cout << "Command submitted." << std::endl;

	/*
	std::array<WGPUCommandBuffer, 3> commands;
	commands[0] = ;
	commands[1] = ;
	commands[2] = ;
	wgpuQueueSubmit(queue, commands.size(), commands.data());
	for (auto cmd : commands) {
		wgpuCommandBufferRelease(cmd);
	}
	*/

	return true;
}

void Application::Terminate() {
	glfwDestroyWindow(window);
	glfwTerminate();
	wgpuSurfaceRelease(surface);
	wgpuQueueRelease(queue);
	wgpuDeviceRelease(device);
}

void Application::MainLoop() {
	glfwPollEvents();

	//std::cout << "Tick/Poll device " << device << std::endl;
#if defined(WEBGPU_BACKEND_DAWN)
	wgpuDeviceTick(device);
#elif defined(WEBGPU_BACKEND_WGPU)
	wgpuDevicePoll(device, false, nullptr);
#endif
}

bool Application::IsRunning() {
	return !glfwWindowShouldClose(window);
}

int main() {
	Application app;

	if (!app.Initialize()) {
		return 1;
	}

#ifdef __EMSCRIPTEN__
	auto callback = [](void *arg) {
		Application* pApp = reinterpret_cast<Application*>(arg);
		pApp->MainLoop();
	};
	emscripten_set_main_loop_arg(callback, &app, 0, true);
#else
	while (app.IsRunning()) {
		app.MainLoop();
	}
#endif

	app.Terminate();

	return 0;
}