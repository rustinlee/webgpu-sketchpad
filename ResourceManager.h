#pragma once

#include <webgpu/webgpu.hpp>

#include <vector>
#include <filesystem>

class ResourceManager {
public:
	using path = std::filesystem::path;

	static wgpu::ShaderModule loadShaderModule(const path& path, wgpu::Device device);
};
