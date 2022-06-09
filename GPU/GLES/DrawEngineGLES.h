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

#include "Common/Data/Collections/Hashmaps.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"

#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/GLES/FragmentShaderGeneratorGLES.h"

class LinkedShader;
class ShaderManagerGLES;
class TextureCacheGLES;
class FramebufferManagerGLES;
class FramebufferManagerCommon;
class TextureCacheCommon;
class FragmentTestCacheGLES;
struct TransformedVertex;

struct DecVtxFormat;

enum {
	TEX_SLOT_PSP_TEXTURE = 0,
	TEX_SLOT_SHADERBLEND_SRC = 1,
	TEX_SLOT_ALPHATEST = 2,
	TEX_SLOT_CLUT = 3,
	TEX_SLOT_SPLINE_POINTS = 4,
	TEX_SLOT_SPLINE_WEIGHTS_U = 5,
	TEX_SLOT_SPLINE_WEIGHTS_V = 6,
};


// States transitions:
// On creation: DRAWN_NEW
// DRAWN_NEW -> DRAWN_HASHING
// DRAWN_HASHING -> DRAWN_RELIABLE
// DRAWN_HASHING -> DRAWN_UNRELIABLE
// DRAWN_ONCE -> UNRELIABLE
// DRAWN_RELIABLE -> DRAWN_SAFE
// UNRELIABLE -> death
// DRAWN_ONCE -> death
// DRAWN_RELIABLE -> death

enum {
	VAI_FLAG_VERTEXFULLALPHA = 1,
};

// Try to keep this POD.
class VertexArrayInfo {
public:
	VertexArrayInfo() {
		status = VAI_NEW;
		vbo = nullptr;
		ebo = nullptr;
		prim = GE_PRIM_INVALID;
		numDraws = 0;
		numFrames = 0;
		lastFrame = gpuStats.numFlips;
		numVerts = 0;
		drawsUntilNextFullHash = 0;
		flags = 0;
	}

	enum Status : uint8_t {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,  // cache, don't hash
		VAI_UNRELIABLE,  // never cache
	};

	uint64_t hash;
	u32 minihash;

	GLRBuffer *vbo;
	GLRBuffer *ebo;

	// Precalculated parameter for drawRangeElements
	u16 numVerts;
	u16 maxIndex;
	s8 prim;
	Status status;

	// ID information
	int numDraws;
	int numFrames;
	int lastFrame;  // So that we can forget.
	u16 drawsUntilNextFullHash;
	u8 flags;
};

class TessellationDataTransferGLES : public TessellationDataTransfer {
private:
	GLRTexture *data_tex[3]{};
	int prevSizeU = 0, prevSizeV = 0;
	int prevSizeWU = 0, prevSizeWV = 0;
	GLRenderManager *renderManager_;
public:
	TessellationDataTransferGLES(GLRenderManager *renderManager)
			: renderManager_(renderManager) { }
	~TessellationDataTransferGLES() {
		EndFrame();
	}
	// Send spline/bezier's control points and weights to vertex shader through floating point texture.
	void SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) override;
	void EndFrame();  // Queues textures for deletion.
};

// Handles transform, lighting and drawing.
class DrawEngineGLES : public DrawEngineCommon {
public:
	DrawEngineGLES(Draw::DrawContext *draw);
	virtual ~DrawEngineGLES();

	void SetShaderManager(ShaderManagerGLES *shaderManager) {
		shaderManager_ = shaderManager;
	}
	void SetTextureCache(TextureCacheGLES *textureCache) {
		textureCache_ = textureCache;
	}
	void SetFramebufferManager(FramebufferManagerGLES *fbManager) {
		framebufferManager_ = fbManager;
	}
	void SetFragmentTestCache(FragmentTestCacheGLES *testCache) {
		fragmentTestCache_ = testCache;
	}

	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

	void ClearTrackedVertexArrays() override;

	void BeginFrame();
	void EndFrame();


	// So that this can be inlined
	void Flush() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	void FinishDeferred() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	bool IsCodePtrVertexDecoder(const u8 *ptr) const;

	void DispatchFlush() override { Flush(); }

	GLPushBuffer *GetPushVertexBuffer() {
		return frameData_[render_->GetCurFrame()].pushVertex;
	}
	GLPushBuffer *GetPushIndexBuffer() {
		return frameData_[render_->GetCurFrame()].pushIndex;
	}

	void ClearInputLayoutMap();

	bool SupportsHWTessellation() const;

protected:
	bool UpdateUseHWTessellation(bool enable) override;
	void DecimateTrackedVertexArrays();

private:
	void InitDeviceObjects();
	void DestroyDeviceObjects();

	void DoFlush();
	void ApplyDrawState(int prim);
	void ApplyDrawStateLate(bool setStencil, int stencilValue);
	void ResetShaderBlending();

	GLRInputLayout *SetupDecFmtForDraw(LinkedShader *program, const DecVtxFormat &decFmt);

	void *DecodeVertsToPushBuffer(GLPushBuffer *push, uint32_t *bindOffset, GLRBuffer **buf);

	void FreeVertexArray(VertexArrayInfo *vai);

	void MarkUnreliable(VertexArrayInfo *vai);

	struct FrameData {
		GLPushBuffer *pushVertex;
		GLPushBuffer *pushIndex;
	};
	FrameData frameData_[GLRenderManager::MAX_INFLIGHT_FRAMES];

	PrehashMap<VertexArrayInfo *, nullptr> vai_;

	DenseHashMap<uint32_t, GLRInputLayout *, nullptr> inputLayoutMap_;

	GLRInputLayout *softwareInputLayout_ = nullptr;
	GLRenderManager *render_;

	// Other
	ShaderManagerGLES *shaderManager_ = nullptr;
	TextureCacheGLES *textureCache_ = nullptr;
	FramebufferManagerGLES *framebufferManager_ = nullptr;
	FragmentTestCacheGLES *fragmentTestCache_ = nullptr;
	Draw::DrawContext *draw_;

	// Need to preserve the scissor for use when clearing.
	ViewportAndScissor vpAndScissor;

	int bufferDecimationCounter_ = 0;

	int lastRenderStepId_ = -1;

	// Hardware tessellation
	TessellationDataTransferGLES *tessDataTransferGLES;
};
