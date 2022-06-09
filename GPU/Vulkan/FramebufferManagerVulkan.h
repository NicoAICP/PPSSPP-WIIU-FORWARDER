// Copyright (c) 2015- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/PresentationCommon.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "GPU/Vulkan/DepalettizeShaderVulkan.h"

class TextureCacheVulkan;
class DrawEngineVulkan;
class VulkanContext;
class ShaderManagerVulkan;
class VulkanTexture;
class VulkanPushBuffer;

class FramebufferManagerVulkan : public FramebufferManagerCommon {
public:
	FramebufferManagerVulkan(Draw::DrawContext *draw, VulkanContext *vulkan);
	~FramebufferManagerVulkan();

	void SetTextureCache(TextureCacheVulkan *tc);
	void SetShaderManager(ShaderManagerVulkan *sm);
	void SetDrawEngine(DrawEngineVulkan *td);
	void SetVulkan2D(Vulkan2D *vk2d) { vulkan2D_ = vk2d; }
	void SetPushBuffer(VulkanPushBuffer *push) { push_ = push; }

	// x,y,w,h are relative to destW, destH which fill out the target completely.
	void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) override;

	void BeginFrameVulkan();  // there's a BeginFrame in the base class, which this calls
	void EndFrame();

	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan, Draw::DrawContext *draw);
	int GetLineWidth();
	void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) override;

	bool NotifyStencilUpload(u32 addr, int size, StencilUpload flags = StencilUpload::NEEDS_CLEAR) override;

	VkImageView BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags);

	// If within a render pass, this will just issue a regular clear. If beginning a new render pass,
	// do that.
	void NotifyClear(bool clearColor, bool clearAlpha, bool clearDepth, uint32_t color, float depth);

protected:
	void Bind2DShader() override;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override;
	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;

private:
	void InitDeviceObjects();
	void DestroyDeviceObjects();

	VulkanContext *vulkan_;

	// Used to keep track of command buffers here but have moved all that into Thin3D.

	TextureCacheVulkan *textureCacheVulkan_ = nullptr;
	ShaderManagerVulkan *shaderManagerVulkan_ = nullptr;
	DrawEngineVulkan *drawEngineVulkan_ = nullptr;
	VulkanPushBuffer *push_;

	enum {
		MAX_COMMAND_BUFFERS = 32,
	};

	VkPipelineCache pipelineCache2D_;

	// Basic shaders
	VkShaderModule fsBasicTex_ = VK_NULL_HANDLE;
	VkShaderModule vsBasicTex_ = VK_NULL_HANDLE;

	VkShaderModule stencilVs_ = VK_NULL_HANDLE;
	VkShaderModule stencilFs_ = VK_NULL_HANDLE;

	VkPipeline cur2DPipeline_ = VK_NULL_HANDLE;

	VkSampler linearSampler_;
	VkSampler nearestSampler_;

	// Simple 2D drawing engine.
	Vulkan2D *vulkan2D_;
};
