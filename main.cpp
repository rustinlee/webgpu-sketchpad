#include <iostream>
#include <webgpu/webgpu.h>

int main() {
	WGPUInstanceDescriptor desc = {};
	desc.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_EMSCRIPTEN
	WGPUInstance instance = wgpuCreateInstance(nullptr);
#else
	WGPUInstance instance = wgpuCreateInstance(&desc);
#endif

	if (!instance) {
		std::cerr << "Failed to initialize WebGPU instance" << std::endl;
		return 1;
	}

	std::cout << "WebGPU instance: " << instance << std::endl;

	wgpuInstanceRelease(instance);

	return 0;
}