#include <algorithm>
#include <cmath>

#include "ShaderUniforms.h"
#include "Common/System/Display.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Math/math_util.h"
#include "Common/Math/lin/vec3.h"
#include "GPU/GPUState.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Math3D.h"

using namespace Lin;

static void ConvertProjMatrixToVulkan(Matrix4x4 &in) {
	const Vec3 trans(gstate_c.vpXOffset, gstate_c.vpYOffset, gstate_c.vpZOffset * 0.5f + 0.5f);
	const Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
	in.translateAndScale(trans, scale);
}

static void ConvertProjMatrixToD3D11(Matrix4x4 &in) {
	const Vec3 trans(gstate_c.vpXOffset, -gstate_c.vpYOffset, gstate_c.vpZOffset * 0.5f + 0.5f);
	const Vec3 scale(gstate_c.vpWidthScale, -gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
	in.translateAndScale(trans, scale);
}

void CalcCullRange(float minValues[4], float maxValues[4], bool flipViewport, bool hasNegZ) {
	// Account for the projection viewport adjustment when viewport is too large.
	auto reverseViewportX = [](float x) {
		float pspViewport = (x - gstate.getViewportXCenter()) * (1.0f / gstate.getViewportXScale());
		return (pspViewport * gstate_c.vpWidthScale) - gstate_c.vpXOffset;
	};
	auto reverseViewportY = [flipViewport](float y) {
		float heightScale = gstate_c.vpHeightScale;
		float yOffset = gstate_c.vpYOffset;
		if (flipViewport) {
			// For D3D11 and GLES non-buffered.
			heightScale = -heightScale;
			yOffset = -yOffset;
		}
		float pspViewport = (y - gstate.getViewportYCenter()) * (1.0f / gstate.getViewportYScale());
		return (pspViewport * heightScale) - yOffset;
	};
	auto reverseViewportZ = [hasNegZ](float z) {
		float vpZScale = gstate.getViewportZScale();
		float vpZCenter = gstate.getViewportZCenter();

		float scale, center;
		if (gstate_c.Supports(GPU_SUPPORTS_ACCURATE_DEPTH)) {
			// These are just the reverse of the formulas in GPUStateUtils.
			float halfActualZRange = vpZScale * (1.0f / gstate_c.vpDepthScale);
			float minz = -((gstate_c.vpZOffset * halfActualZRange) - vpZCenter) - halfActualZRange;

			// In accurate depth mode, we're comparing against a value scaled to (minz, maxz).
			// And minz might be very negative, (e.g. if we're clamping in that direction.)
			scale = halfActualZRange;
			center = minz + halfActualZRange;
		} else {
			// In old-style depth mode, we're comparing against a value scaled to viewport.
			// (and possibly incorrectly clipped against it.)
			scale = vpZScale;
			center = vpZCenter;
		}

		float realViewport = (z - center) * (1.0f / scale);
		return hasNegZ ? realViewport : (realViewport * 0.5f + 0.5f);
	};
	auto sortPair = [](float a, float b) {
		return a > b ? std::make_pair(b, a) : std::make_pair(a, b);
	};

	// The PSP seems to use 0.12.4 for X and Y, and 0.16.0 for Z.
	// Any vertex outside this range (unless depth clamp enabled) is discarded.
	auto x = sortPair(reverseViewportX(0.0f), reverseViewportX(4096.0f));
	auto y = sortPair(reverseViewportY(0.0f), reverseViewportY(4096.0f));
	auto z = sortPair(reverseViewportZ(0.0f), reverseViewportZ(65535.5f));
	// Since we have space in w, use it to pass the depth clamp flag.  We also pass NAN for w "discard".
	float clampEnable = gstate.isDepthClampEnabled() ? 1.0f : 0.0f;

	minValues[0] = x.first;
	minValues[1] = y.first;
	minValues[2] = z.first;
	minValues[3] = clampEnable;
	maxValues[0] = x.second;
	maxValues[1] = y.second;
	maxValues[2] = z.second;
	maxValues[3] = NAN;
}

void BaseUpdateUniforms(UB_VS_FS_Base *ub, uint64_t dirtyUniforms, bool flipViewport, bool useBufferedRendering) {
	if (dirtyUniforms & DIRTY_TEXENV) {
		Uint8x3ToFloat4(ub->texEnvColor, gstate.texenvcolor);
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORREF) {
		Uint8x3ToInt4_Alpha(ub->alphaColorRef, gstate.getColorTestRef(), gstate.getAlphaTestRef() & gstate.getAlphaTestMask());
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORMASK) {
		Uint8x3ToInt4_Alpha(ub->colorTestMask, gstate.getColorTestMask(), gstate.getAlphaTestMask());
	}
	if (dirtyUniforms & DIRTY_FOGCOLOR) {
		Uint8x3ToFloat4(ub->fogColor, gstate.fogcolor);
	}
	if (dirtyUniforms & DIRTY_SHADERBLEND) {
		Uint8x3ToFloat4(ub->blendFixA, gstate.getFixA());
		Uint8x3ToFloat4(ub->blendFixB, gstate.getFixB());
	}
	if (dirtyUniforms & DIRTY_TEXCLAMP) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;

		// First wrap xy, then half texel xy (for clamp.)
		ub->texClamp[0] = widthFactor;
		ub->texClamp[1] = heightFactor;
		ub->texClamp[2] = invW * 0.5f;
		ub->texClamp[3] = invH * 0.5f;
		ub->texClampOffset[0] = gstate_c.curTextureXOffset * invW;
		ub->texClampOffset[1] = gstate_c.curTextureYOffset * invH;
	}

	if (dirtyUniforms & DIRTY_PROJMATRIX) {
		Matrix4x4 flippedMatrix;
		memcpy(&flippedMatrix, gstate.projMatrix, 16 * sizeof(float));

		const bool invertedY = gstate_c.vpHeight < 0;
		if (invertedY) {
			flippedMatrix[1] = -flippedMatrix[1];
			flippedMatrix[5] = -flippedMatrix[5];
			flippedMatrix[9] = -flippedMatrix[9];
			flippedMatrix[13] = -flippedMatrix[13];
		}
		const bool invertedX = gstate_c.vpWidth < 0;
		if (invertedX) {
			flippedMatrix[0] = -flippedMatrix[0];
			flippedMatrix[4] = -flippedMatrix[4];
			flippedMatrix[8] = -flippedMatrix[8];
			flippedMatrix[12] = -flippedMatrix[12];
		}
		if (flipViewport) {
			ConvertProjMatrixToD3D11(flippedMatrix);
		} else {
			ConvertProjMatrixToVulkan(flippedMatrix);
		}

		if (!useBufferedRendering && g_display_rotation != DisplayRotation::ROTATE_0) {
			flippedMatrix = flippedMatrix * g_display_rot_matrix;
		}
		CopyMatrix4x4(ub->proj, flippedMatrix.getReadPtr());
	}

	if (dirtyUniforms & DIRTY_PROJTHROUGHMATRIX) {
		Matrix4x4 proj_through;
		if (flipViewport) {
			proj_through.setOrthoD3D(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0, 0, 1);
		} else {
			proj_through.setOrthoVulkan(0.0f, gstate_c.curRTWidth, 0, gstate_c.curRTHeight, 0, 1);
		}
		if (!useBufferedRendering && g_display_rotation != DisplayRotation::ROTATE_0) {
			proj_through = proj_through * g_display_rot_matrix;
		}
		CopyMatrix4x4(ub->proj_through, proj_through.getReadPtr());
	}

	// Transform
	if (dirtyUniforms & DIRTY_WORLDMATRIX) {
		ConvertMatrix4x3To3x4Transposed(ub->world, gstate.worldMatrix);
	}
	if (dirtyUniforms & DIRTY_VIEWMATRIX) {
		ConvertMatrix4x3To3x4Transposed(ub->view, gstate.viewMatrix);
	}
	if (dirtyUniforms & DIRTY_TEXMATRIX) {
		ConvertMatrix4x3To3x4Transposed(ub->tex, gstate.tgenMatrix);
	}

	if (dirtyUniforms & DIRTY_FOGCOEF) {
		float fogcoef[2] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
		};
		// The PSP just ignores infnan here (ignoring IEEE), so take it down to a valid float.
		// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
		if (my_isnanorinf(fogcoef[0])) {
			// Not really sure what a sensible value might be, but let's try 64k.
			fogcoef[0] = std::signbit(fogcoef[0]) ? -65535.0f : 65535.0f;
		}
		if (my_isnanorinf(fogcoef[1])) {
			fogcoef[1] = std::signbit(fogcoef[1]) ? -65535.0f : 65535.0f;
		}
		CopyFloat2(ub->fogCoef, fogcoef);
	}

	if (dirtyUniforms & DIRTY_STENCILREPLACEVALUE) {
		ub->stencil = (float)gstate.getStencilTestRef() / 255.0;
	}

	// Note - this one is not in lighting but in transformCommon as it has uses beyond lighting
	if (dirtyUniforms & DIRTY_MATAMBIENTALPHA) {
		Uint8x3ToFloat4_AlphaUint8(ub->matAmbient, gstate.materialambient, gstate.getMaterialAmbientA());
	}

	// Texturing
	if (dirtyUniforms & DIRTY_UVSCALEOFFSET) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;
		if (gstate_c.bezier || gstate_c.spline) {
			// When we are generating UV coordinates through the bezier/spline, we need to apply the scaling.
			// However, this is missing a check that we're not getting our UV:s supplied for us in the vertices.
			ub->uvScaleOffset[0] = gstate_c.uv.uScale * widthFactor;
			ub->uvScaleOffset[1] = gstate_c.uv.vScale * heightFactor;
			ub->uvScaleOffset[2] = gstate_c.uv.uOff * widthFactor;
			ub->uvScaleOffset[3] = gstate_c.uv.vOff * heightFactor;
		} else {
			ub->uvScaleOffset[0] = widthFactor;
			ub->uvScaleOffset[1] = heightFactor;
			ub->uvScaleOffset[2] = 0.0f;
			ub->uvScaleOffset[3] = 0.0f;
		}
	}

	if (dirtyUniforms & DIRTY_DEPTHRANGE) {
		// Same formulas as D3D9 now. Should work for both Vulkan and D3D11.

		// Depth is [0, 1] mapping to [minz, maxz], not too hard.
		float vpZScale = gstate.getViewportZScale();
		float vpZCenter = gstate.getViewportZCenter();

		// These are just the reverse of the formulas in GPUStateUtils.
		float halfActualZRange = vpZScale / gstate_c.vpDepthScale;
		float minz = -((gstate_c.vpZOffset * halfActualZRange) - vpZCenter) - halfActualZRange;
		float viewZScale = halfActualZRange * 2.0f;
		// Account for the half pixel offset.
		float viewZCenter = minz + (DepthSliceFactor() / 256.0f) * 0.5f;
		float viewZInvScale;

		if (viewZScale != 0.0) {
			viewZInvScale = 1.0f / viewZScale;
		} else {
			viewZInvScale = 0.0;
		}

		float data[4] = { viewZScale, viewZCenter, viewZCenter, viewZInvScale };
		ub->depthRange[0] = viewZScale;
		ub->depthRange[1] = viewZCenter;
		ub->depthRange[2] = viewZCenter;
		ub->depthRange[3] = viewZInvScale;
	}

	if (dirtyUniforms & DIRTY_CULLRANGE) {
		CalcCullRange(ub->cullRangeMin, ub->cullRangeMax, flipViewport, false);
	}

	if (dirtyUniforms & DIRTY_BEZIERSPLINE) {
		ub->spline_counts = gstate_c.spline_num_points_u;
	}

	if (dirtyUniforms & DIRTY_DEPAL) {
		int indexMask = gstate.getClutIndexMask();
		int indexShift = gstate.getClutIndexShift();
		int indexOffset = gstate.getClutIndexStartPos() >> 4;
		int format = gstate_c.depalFramebufferFormat;
		uint32_t val = BytesToUint32(indexMask, indexShift, indexOffset, format);
		// Poke in a bilinear filter flag in the top bit.
		val |= gstate.isMagnifyFilteringEnabled() << 31;
		ub->depal_mask_shift_off_fmt = val;
	}
}

void LightUpdateUniforms(UB_VS_Lights *ub, uint64_t dirtyUniforms) {
	// Lighting
	if (dirtyUniforms & DIRTY_AMBIENT) {
		Uint8x3ToFloat4_AlphaUint8(ub->ambientColor, gstate.ambientcolor, gstate.getAmbientA());
	}
	if (dirtyUniforms & DIRTY_MATDIFFUSE) {
		Uint8x3ToFloat4(ub->materialDiffuse, gstate.materialdiffuse);
	}
	if (dirtyUniforms & DIRTY_MATSPECULAR) {
		Uint8x3ToFloat4_Alpha(ub->materialSpecular, gstate.materialspecular, std::max(0.0f, getFloat24(gstate.materialspecularcoef)));
	}
	if (dirtyUniforms & DIRTY_MATEMISSIVE) {
		Uint8x3ToFloat4(ub->materialEmissive, gstate.materialemissive);
	}
	for (int i = 0; i < 4; i++) {
		if (dirtyUniforms & (DIRTY_LIGHT0 << i)) {
			if (gstate.isDirectionalLight(i)) {
				// Prenormalize
				float x = getFloat24(gstate.lpos[i * 3 + 0]);
				float y = getFloat24(gstate.lpos[i * 3 + 1]);
				float z = getFloat24(gstate.lpos[i * 3 + 2]);
				float len = sqrtf(x*x + y*y + z*z);
				if (len == 0.0f)
					len = 1.0f;
				else
					len = 1.0f / len;
				float vec[3] = { x * len, y * len, z * len };
				CopyFloat3To4(ub->lpos[i], vec);
			} else {
				ExpandFloat24x3ToFloat4(ub->lpos[i], &gstate.lpos[i * 3]);
			}
			ExpandFloat24x3ToFloat4(ub->ldir[i], &gstate.ldir[i * 3]);
			ExpandFloat24x3ToFloat4(ub->latt[i], &gstate.latt[i * 3]);
			float lightAngle_spotCoef[2] = { getFloat24(gstate.lcutoff[i]), getFloat24(gstate.lconv[i]) };
			CopyFloat2To4(ub->lightAngle_SpotCoef[i], lightAngle_spotCoef);
			Uint8x3ToFloat4(ub->lightAmbient[i], gstate.lcolor[i * 3]);
			Uint8x3ToFloat4(ub->lightDiffuse[i], gstate.lcolor[i * 3 + 1]);
			Uint8x3ToFloat4(ub->lightSpecular[i], gstate.lcolor[i * 3 + 2]);
		}
	}
}

void BoneUpdateUniforms(UB_VS_Bones *ub, uint64_t dirtyUniforms) {
	for (int i = 0; i < 8; i++) {
		if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
			ConvertMatrix4x3To3x4Transposed(ub->bones[i], gstate.boneMatrix + 12 * i);
		}
	}
}
