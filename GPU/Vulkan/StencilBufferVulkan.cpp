// Copyright (c) 2014- PPSSPP Project.

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

#include "Common/GPU/thin3d.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"

#include "Common/Log.h"
#include "Core/Reporting.h"
#include "GPU/Common/StencilCommon.h"
#include "GPU/Vulkan/FramebufferManagerVulkan.h"
#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"

// This shader references gl_FragDepth to prevent early fragment tests.
// They shouldn't happen since it uses discard, but Adreno detects that incorrectly - see #10634.
// This only affected the 5xx generation and was fixed sometime before driver version 512.384.0.0 (whatever that means).
// Strangely, this same code appears to fix a different issue on Exynos instead so now it's enabled on Mali as well.
static const char *stencil_fs_adreno = R"(#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_conservative_depth : enable
layout (depth_unchanged) out float gl_FragDepth;
layout (binding = 0) uniform sampler2D tex;
layout (push_constant) uniform params {
	int u_stencilValue;
};
layout (location = 0) in vec2 v_texcoord0;
layout (location = 0) out vec4 fragColor0;

void main() {
	gl_FragDepth = gl_FragCoord.z;

	if (u_stencilValue == 0) {
		fragColor0 = vec4(0.0);
	} else {
		vec4 index = texture(tex, v_texcoord0);
		int indexBits = int(floor(index.a * 255.99)) & 0xFF;
		if ((indexBits & u_stencilValue) == 0)
			discard;
		fragColor0 = index.aaaa;
	}
}
)";

static const char *stencil_fs = R"(#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (binding = 0) uniform sampler2D tex;
layout (push_constant) uniform params {
	int u_stencilValue;
};
layout (location = 0) in vec2 v_texcoord0;
layout (location = 0) out vec4 fragColor0;

void main() {
	if (u_stencilValue == 0) {
		fragColor0 = vec4(0.0);
	} else {
		vec4 index = texture(tex, v_texcoord0);
		int indexBits = int(floor(index.a * 255.99)) & 0xFF;
		if ((indexBits & u_stencilValue) == 0)
			discard;
		fragColor0 = index.aaaa;
	}
}
)";


static const char stencil_vs[] = R"(#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (location = 0) out vec2 v_texcoord0;
out gl_PerVertex { vec4 gl_Position; };
void main() {
	int id = gl_VertexIndex;
	v_texcoord0.x = (id == 2) ? 2.0 : 0.0;
	v_texcoord0.y = (id == 1) ? 2.0 : 0.0;
	gl_Position = vec4(v_texcoord0 * vec2(2.0, 2.0) + vec2(-1.0, -1.0), 0.0, 1.0);
}
)";

// In Vulkan we should be able to simply copy the stencil data directly to a stencil buffer without
// messing about with bitplane textures and the like. Or actually, maybe not... Let's start with
// the traditional approach.
bool FramebufferManagerVulkan::NotifyStencilUpload(u32 addr, int size, StencilUpload flags) {
	addr &= 0x3FFFFFFF;
	if (!MayIntersectFramebuffer(addr)) {
		return false;
	}

	VirtualFramebuffer *dstBuffer = 0;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		if (vfb->fb_address == addr) {
			dstBuffer = vfb;
		}
	}
	if (!dstBuffer) {
		return false;
	}

	int values = 0;
	u8 usedBits = 0;

	const u8 *src = Memory::GetPointer(addr);
	if (!src) {
		return false;
	}

	switch (dstBuffer->format) {
	case GE_FORMAT_565:
		// Well, this doesn't make much sense.
		return false;
	case GE_FORMAT_5551:
		usedBits = StencilBits5551(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 2;
		break;
	case GE_FORMAT_4444:
		usedBits = StencilBits4444(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 16;
		break;
	case GE_FORMAT_8888:
		usedBits = StencilBits8888(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 256;
		break;
	case GE_FORMAT_INVALID:
	case GE_FORMAT_DEPTH16:
		// Inconceivable.
		_assert_(false);
		break;
	}

	std::string error;
	if (!stencilVs_) {
		const char *stencil_fs_source = stencil_fs;
		// See comment above the stencil_fs_adreno definition.
		u32 vendorID = vulkan_->GetPhysicalDeviceProperties().properties.vendorID;
		if (g_Config.bVendorBugChecksEnabled && (draw_->GetBugs().Has(Draw::Bugs::NO_DEPTH_CANNOT_DISCARD_STENCIL) || vendorID == VULKAN_VENDOR_ARM))
			stencil_fs_source = stencil_fs_adreno;

		stencilVs_ = CompileShaderModule(vulkan_, VK_SHADER_STAGE_VERTEX_BIT, stencil_vs, &error);
		stencilFs_ = CompileShaderModule(vulkan_, VK_SHADER_STAGE_FRAGMENT_BIT, stencil_fs_source, &error);
	}
	VkRenderPass rp = (VkRenderPass)draw_->GetNativeObject(Draw::NativeObject::FRAMEBUFFER_RENDERPASS);

	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	shaderManagerVulkan_->DirtyLastShader();
	textureCacheVulkan_->ForgetLastTexture();

	u16 w = dstBuffer->renderWidth;
	u16 h = dstBuffer->renderHeight;
	float u1 = 1.0f;
	float v1 = 1.0f;
	Draw::Texture *tex = MakePixelTexture(src, dstBuffer->format, dstBuffer->fb_stride, dstBuffer->bufferWidth, dstBuffer->bufferHeight, u1, v1);
	if (!tex)
		return false;

	if (dstBuffer->fbo) {
		// Typically, STENCIL_IS_ZERO means it's already bound, so this bind will be optimized away.
		draw_->BindFramebufferAsRenderTarget(dstBuffer->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::DONT_CARE }, "Stencil");
	} else {
		// something is wrong...
	}

	VkPipeline pipeline = vulkan2D_->GetPipeline(rp, stencilVs_, stencilFs_, false, Vulkan2D::VK2DDepthStencilMode::STENCIL_REPLACE_ALWAYS);
	renderManager->BindPipeline(pipeline, PIPELINE_FLAG_USES_DEPTH_STENCIL);
	renderManager->SetViewport({ 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f });
	renderManager->SetScissor({ { 0, 0, },{ (uint32_t)w, (uint32_t)h } });

	draw_->BindTextures(0, 1, &tex);
	VkImageView drawPixelsImageView = (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE0_IMAGEVIEW);
	VkDescriptorSet descSet = vulkan2D_->GetDescriptorSet(drawPixelsImageView, nearestSampler_, VK_NULL_HANDLE, VK_NULL_HANDLE);

	// Note: Even with skipZero, we don't necessarily start framebuffers at 0 in Vulkan.  Clear anyway.
	// Not an actual clear, because we need to draw to alpha only as well.
	uint32_t value = 0;
	renderManager->PushConstants(vulkan2D_->GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &value);
	renderManager->SetStencilParams(0xFF, 0xFF, 0x00);
	renderManager->Draw(vulkan2D_->GetPipelineLayout(), descSet, 0, nullptr, VK_NULL_HANDLE, 0, 3);  // full screen triangle

	for (int i = 1; i < values; i += i) {
		if (!(usedBits & i)) {
			// It's already zero, let's skip it.
			continue;
		}

		// These are the stencil bits that will be written.  We discard when the bit doesn't match.
		uint8_t writeMask = 0;
		// This is the value to test the texture alpha against in the shader.
		uint32_t value = 0;
		if (dstBuffer->format == GE_FORMAT_4444) {
			writeMask = i | (i << 4);
			value = i * 16;
		} else if (dstBuffer->format == GE_FORMAT_5551) {
			writeMask = 0xFF;
			value = i * 128;
		} else {
			writeMask = i;
			value = i;
		}
		renderManager->SetStencilParams(writeMask, 0xFF, 0xFF);
		// Need to specify both VERTEX and FRAGMENT bits here since that's what we set up in the pipeline layout, and we need
		// that for the post shaders. There's probably not really a cost to this.
		renderManager->PushConstants(vulkan2D_->GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &value);
		renderManager->Draw(vulkan2D_->GetPipelineLayout(), descSet, 0, nullptr, VK_NULL_HANDLE, 0, 3);  // full screen triangle
	}

	tex->Release();
	RebindFramebuffer("RebindFramebuffer - NotifyStencilUpload");
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	return true;
}
