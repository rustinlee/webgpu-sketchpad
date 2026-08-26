#include <cassert>
#include <iostream>
#include <vector>
#include <array>
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
//#include <webgpu/webgpu.h>
#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu/webgpu.hpp>
#ifdef WEBGPU_BACKEND_WGPU
#include <webgpu/wgpu.h>
#endif // WEBGPU_BACKEND_WGPU
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace wgpu;

const char* triangleShaderSource = R"(
@group(0) @binding(0) var<uniform> uTime: f32;

struct VertexInput {
	@location(0) position: vec2f,
	@location(1) texCoord: vec2f,
};

struct VertexOutput {
	@builtin(position) position: vec4f,
	@location(0) texCoord: vec2f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;
	out.position = vec4f(in.position, 0.0, 1.0);
	out.texCoord = in.texCoord;
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return vec4f(in.texCoord.x, in.texCoord.y, 0.5 + sin(uTime) * 0.5, 1.0);
}
)";

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

Device requestDeviceSync(Adapter adapter, WGPUDeviceDescriptor const *descriptor) {
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
	void InitializePipeline();
	void InitializeBuffers();
	void InitializeBindGroups();
	std::pair<WGPUSurfaceTexture, WGPUTextureView> GetNextSurfaceViewData();
	RequiredLimits GetRequiredLimits(Adapter adapter) const;

	GLFWwindow *window;
	WGPUSurface surface;
	Device device;
	Queue queue;
	RenderPipeline pipeline;
	TextureFormat surfaceFormat = TextureFormat::Undefined;
	Buffer vertexBuffer;
	uint32_t vertexCount;
	Buffer uniformBuffer;
	PipelineLayout pipelineLayout;
	BindGroupLayout bindGroupLayout;
	BindGroup bindGroup;
};

std::pair<WGPUSurfaceTexture, WGPUTextureView> Application::GetNextSurfaceViewData() {
	WGPUSurfaceTexture surfaceTexture;
	wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
	if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
		return { surfaceTexture, nullptr };
	}

	WGPUTextureViewDescriptor viewDescriptor;
	viewDescriptor.nextInChain = nullptr;
	viewDescriptor.label = "Surface Texture View";
	viewDescriptor.format = wgpuTextureGetFormat(surfaceTexture.texture);
	viewDescriptor.dimension = WGPUTextureViewDimension_2D;
	viewDescriptor.baseMipLevel = 0;
	viewDescriptor.mipLevelCount = 1;
	viewDescriptor.baseArrayLayer = 0;
	viewDescriptor.arrayLayerCount = 1;
	viewDescriptor.aspect = WGPUTextureAspect_All;
	WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

#ifndef WEBGPU_BACKEND_WGPU
	wgpuTextureRelease(surfaceTexture.texture);
#endif

	return { surfaceTexture, targetView };
}

RequiredLimits Application::GetRequiredLimits(Adapter adapter) const {
	SupportedLimits supportedLimits;
	adapter.getLimits(&supportedLimits);

	RequiredLimits requiredLimits = Default; // TODO: doesn't appear to be working

	requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
	requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;

	requiredLimits.limits.maxVertexAttributes = 1; // 2 // position and UV
	requiredLimits.limits.maxVertexBuffers = 1;
	int maxVertexStride = 4 * sizeof(float);
	int maxVertexCount = 6;
	requiredLimits.limits.maxBufferSize = maxVertexCount * maxVertexStride;
	requiredLimits.limits.maxVertexBufferArrayStride = maxVertexStride;

	return requiredLimits;
}

void Application::InitializeBuffers() {
	// Vertex buffer
	std::vector<float> vertexData = {
		-1.0, -1.0, 0.0, 0.0,
		+3.0, -1.0, 1.0, 0.0,
		-1.0, +3.0, 1.0, 1.0,
	};
	vertexCount = static_cast<uint32_t>(vertexData.size() / 4);

	BufferDescriptor bufferDesc;
	bufferDesc.size = vertexData.size() * sizeof(float);
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
	bufferDesc.mappedAtCreation = false;
	vertexBuffer = device.createBuffer(bufferDesc);

	queue.writeBuffer(vertexBuffer, 0, vertexData.data(), bufferDesc.size);

	// Uniform buffer
	bufferDesc.size = 4 * sizeof(float);
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
	bufferDesc.mappedAtCreation = false;
	uniformBuffer = device.createBuffer(bufferDesc);

	float currentTime = 1.0f;
	queue.writeBuffer(uniformBuffer, 0, &currentTime, sizeof(float));
}

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
	std::cout << "Got surface: " << surface << std::endl;
	WGPURequestAdapterOptions adapterOptions = {};
	adapterOptions.nextInChain = nullptr;
	adapterOptions.compatibleSurface = surface;
	WGPUAdapter adapter = requestAdapterSync(instance, &adapterOptions);
	std::cout << "Got adapter: " << adapter << std::endl;
	wgpuInstanceRelease(instance);

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

	RequiredLimits requiredLimits = GetRequiredLimits(adapter);
	//deviceDesc.requiredLimits = &requiredLimits; // TODO: Default limits don't seem to work currently
	device = requestDeviceSync(adapter, &deviceDesc);

	std::cout << "Got device: " << device << std::endl;
	inspectDevice(device);

	queue = wgpuDeviceGetQueue(device);

	// Configure surface
	WGPUSurfaceConfiguration surfaceConfig = {};
	surfaceConfig.nextInChain = nullptr;
	surfaceConfig.width = 640;
	surfaceConfig.height = 480;
	surfaceFormat = wgpuSurfaceGetPreferredFormat(surface, adapter);
	surfaceConfig.format = surfaceFormat;
	surfaceConfig.viewFormatCount = 0;
	surfaceConfig.viewFormats = nullptr;
	surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
	surfaceConfig.device = device;
	surfaceConfig.presentMode = WGPUPresentMode_Fifo;
	surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
	wgpuSurfaceConfigure(surface, &surfaceConfig);

	wgpuAdapterRelease(adapter);

	InitializePipeline();
	InitializeBuffers();
	InitializeBindGroups();

	return true;
}

void Application::InitializePipeline() {
	ShaderModuleDescriptor shaderDesc;
#ifdef WEBGPU_BACKEND_WGPU
	shaderDesc.hintCount = 0;
	shaderDesc.hints = nullptr;
#endif
	ShaderModuleWGSLDescriptor shaderCodeDesc;
	shaderCodeDesc.chain.next = nullptr;
	shaderCodeDesc.chain.sType = SType::ShaderModuleWGSLDescriptor;
	shaderDesc.nextInChain = &shaderCodeDesc.chain;
	shaderCodeDesc.code = triangleShaderSource;
	ShaderModule shaderModule = device.createShaderModule(shaderDesc);

	// Full screen triangle pipeline rasterization description
	RenderPipelineDescriptor pipelineDesc;

	// Vertex attributes: float2 position, float2 texCoord
	VertexBufferLayout vertexBufferLayout;
	std::vector<VertexAttribute> vertexAttribs(2);
	vertexAttribs[0].shaderLocation = 0;
	vertexAttribs[0].format = VertexFormat::Float32x2;
	vertexAttribs[0].offset = 0;
	vertexAttribs[1].shaderLocation = 1;
	vertexAttribs[1].format = VertexFormat::Float32x2;
	vertexAttribs[1].offset = 2 * sizeof(float);

	vertexBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttribs.size());
	vertexBufferLayout.attributes = vertexAttribs.data();

	vertexBufferLayout.arrayStride = 4 * sizeof(float);
	vertexBufferLayout.stepMode = VertexStepMode::Vertex;

	pipelineDesc.vertex.bufferCount = 1;
	pipelineDesc.vertex.buffers = &vertexBufferLayout;
	pipelineDesc.vertex.module = shaderModule;
	pipelineDesc.vertex.entryPoint = "vs_main";
	pipelineDesc.vertex.constantCount = 0;
	pipelineDesc.vertex.constants = nullptr;

	pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;
	pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;
	pipelineDesc.primitive.frontFace = FrontFace::CCW;
	pipelineDesc.primitive.cullMode = CullMode::None; // TODO: cull backfaces once hello triangle is working

	FragmentState fragmentState;
	fragmentState.module = shaderModule;
	fragmentState.entryPoint = "fs_main";
	fragmentState.constantCount = 0;
	fragmentState.constants = nullptr;
	pipelineDesc.fragment = &fragmentState;

	pipelineDesc.depthStencil = nullptr;
	pipelineDesc.layout = nullptr;

	BlendState blendState;

	ColorTargetState colorTarget;
	colorTarget.format = surfaceFormat;
	colorTarget.blend = &blendState;
	colorTarget.writeMask = ColorWriteMask::All;

	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;

	blendState.color.srcFactor = BlendFactor::SrcAlpha;
	blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
	blendState.color.operation = BlendOperation::Add;

	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = ~0u;
	pipelineDesc.multisample.alphaToCoverageEnabled = false;

	BindGroupLayoutEntry bindingLayout = Default;
	bindingLayout.binding = 0;
	bindingLayout.visibility = ShaderStage::Fragment;
	bindingLayout.buffer.type = BufferBindingType::Uniform;
	bindingLayout.buffer.minBindingSize = 4 * sizeof(float);

	BindGroupLayoutDescriptor bindGroupLayoutDesc;
	bindGroupLayoutDesc.entryCount = 1;
	bindGroupLayoutDesc.entries = &bindingLayout;
	bindGroupLayout = device.createBindGroupLayout(bindGroupLayoutDesc);

	PipelineLayoutDescriptor pipelineLayoutDesc;
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = reinterpret_cast<WGPUBindGroupLayout*>(&bindGroupLayout);
	pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
	pipelineDesc.layout = pipelineLayout;

	pipeline = device.createRenderPipeline(pipelineDesc);

	shaderModule.release();
}

void Application::InitializeBindGroups() {
	BindGroupEntry binding;
	binding.binding = 0;
	binding.buffer = uniformBuffer;
	binding.offset = 0;
	binding.size = 4 * sizeof(float);

	BindGroupDescriptor bindGroupDesc;
	bindGroupDesc.layout = bindGroupLayout;
	bindGroupDesc.entryCount = 1;
	bindGroupDesc.entries = &binding;
	bindGroup = device.createBindGroup(bindGroupDesc);
}

void Application::Terminate() {
	vertexBuffer.release();
	uniformBuffer.release();
	pipeline.release();
	pipelineLayout.release();
	bindGroupLayout.release();
	bindGroup.release();
	glfwDestroyWindow(window);
	glfwTerminate();
	wgpuSurfaceUnconfigure(surface);
	wgpuSurfaceRelease(surface);
	wgpuQueueRelease(queue);
	wgpuDeviceRelease(device);
}

void Application::MainLoop() {
	glfwPollEvents();
	float t = static_cast<float>(glfwGetTime());
	queue.writeBuffer(uniformBuffer, 0, &t, sizeof(float));

	auto [surfaceTexture, targetView] = GetNextSurfaceViewData();
	if (!targetView) return;

	WGPUCommandEncoderDescriptor encoderDesc = {};
	encoderDesc.nextInChain = nullptr;
	encoderDesc.label = "Default Command Encoder";
	CommandEncoder encoder = device.createCommandEncoder(encoderDesc);

	WGPURenderPassDescriptor renderPassDesc = {};
	renderPassDesc.nextInChain = nullptr;

	WGPURenderPassColorAttachment renderPassColorAttachment = {};
	renderPassColorAttachment.view = targetView;
	renderPassColorAttachment.resolveTarget = nullptr;
	renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
	renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
	renderPassColorAttachment.clearValue = WGPUColor { 0.5, 0.2, 0.2, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
	renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif
	renderPassDesc.depthStencilAttachment = nullptr;
	renderPassDesc.timestampWrites = nullptr;
	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;

	// Create the render pass
	RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

	renderPass.setPipeline(pipeline);
	renderPass.setVertexBuffer(0, vertexBuffer, 0, vertexBuffer.getSize());
	renderPass.setBindGroup(0, bindGroup, 0, nullptr);

	renderPass.draw(vertexCount, 1, 0, 0);
	renderPass.end();
	renderPass.release();

	// Encode and submit render pass
	WGPUCommandBufferDescriptor cmdBufferDesc = {};
	cmdBufferDesc.nextInChain = nullptr;
	cmdBufferDesc.label = "Command Buffer";
	WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
	wgpuCommandEncoderRelease(encoder);

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

	//std::cout << "Submitting command..." << std::endl;
	wgpuQueueSubmit(queue, 1, &command);
	wgpuCommandBufferRelease(command);
	//std::cout << "Command submitted." << std::endl;

	wgpuTextureViewRelease(targetView);
#ifndef __EMSCRIPTEN__
	wgpuSurfacePresent(surface);
#endif

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