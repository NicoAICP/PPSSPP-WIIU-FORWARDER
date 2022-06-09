// Copyright (c) 2012- PPSSPP Project.

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

#include <unordered_map>

#include <d3d9.h>

// Keeps track of allocated FBOs.
// Also provides facilities for drawing and later converting raw
// pixel data.

#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"

namespace DX9 {

class TextureCacheDX9;
class DrawEngineDX9;
class ShaderManagerDX9;

class FramebufferManagerDX9 : public FramebufferManagerCommon {
public:
	FramebufferManagerDX9(Draw::DrawContext *draw);
	~FramebufferManagerDX9();

	void SetTextureCache(TextureCacheDX9 *tc);
	void SetShaderManager(ShaderManagerDX9 *sm);
	void SetDrawEngine(DrawEngineDX9 *td);
	void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) override;

	void DestroyAllFBOs();

	void EndFrame();
	void DeviceLost();
	void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) override;

	void BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags);

	virtual bool NotifyStencilUpload(u32 addr, int size, StencilUpload flags = StencilUpload::NEEDS_CLEAR) override;

	bool GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer, int maxRes);
	bool GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer) override;
	bool GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer) override;
	bool GetOutputFramebuffer(GPUDebugBuffer &buffer) override;

	LPDIRECT3DSURFACE9 GetOffscreenSurface(LPDIRECT3DSURFACE9 similarSurface, VirtualFramebuffer *vfb);
	LPDIRECT3DSURFACE9 GetOffscreenSurface(D3DFORMAT fmt, u32 w, u32 h);

protected:
	void Bind2DShader() override;
	void DecimateFBOs() override;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override;

	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;

private:
	void PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h) override;
	void PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h);
	bool GetRenderTargetFramebuffer(LPDIRECT3DSURFACE9 renderTarget, LPDIRECT3DSURFACE9 offscreen, int w, int h, GPUDebugBuffer &buffer);

	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DDEVICE9 deviceEx_;

	LPDIRECT3DVERTEXSHADER9 pFramebufferVertexShader = nullptr;
	LPDIRECT3DPIXELSHADER9 pFramebufferPixelShader = nullptr;
	LPDIRECT3DVERTEXDECLARATION9 pFramebufferVertexDecl = nullptr;

	LPDIRECT3DPIXELSHADER9 stencilUploadPS_ = nullptr;
	LPDIRECT3DVERTEXSHADER9 stencilUploadVS_ = nullptr;
	bool stencilUploadFailed_ = false;

	LPDIRECT3DTEXTURE9 nullTex_ = nullptr;

	TextureCacheDX9 *textureCacheDX9_;
	ShaderManagerDX9 *shaderManagerDX9_;
	DrawEngineDX9 *drawEngineD3D9_;
	
	struct OffscreenSurface {
		LPDIRECT3DSURFACE9 surface;
		int last_frame_used;
	};

	std::unordered_map<u64, OffscreenSurface> offscreenSurfaces_;
};

};
