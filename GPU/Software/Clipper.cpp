// Copyright (c) 2013- PPSSPP Project.

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

#include <algorithm>

#include "GPU/GPUState.h"

#include "GPU/Software/Clipper.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/RasterizerRectangle.h"

#include "Common/Profiler/Profiler.h"

namespace Clipper {

enum {
	SKIP_FLAG = -1,
	CLIP_POS_X_BIT = 0x01,
	CLIP_NEG_X_BIT = 0x02,
	CLIP_POS_Y_BIT = 0x04,
	CLIP_NEG_Y_BIT = 0x08,
	CLIP_POS_Z_BIT = 0x10,
	CLIP_NEG_Z_BIT = 0x20,
};

static inline int CalcClipMask(const ClipCoords& v)
{
	int mask = 0;
	// This checks `x / w` compared to 1 or -1, skipping the division.
	if (v.x > v.w) mask |= CLIP_POS_X_BIT;
	if (v.x < -v.w) mask |= CLIP_NEG_X_BIT;
	if (v.y > v.w) mask |= CLIP_POS_Y_BIT;
	if (v.y < -v.w) mask |= CLIP_NEG_Y_BIT;
	if (v.z > v.w) mask |= CLIP_POS_Z_BIT;
	if (v.z < -v.w) mask |= CLIP_NEG_Z_BIT;
	return mask;
}

inline bool different_signs(float x, float y) {
	return ((x <= 0 && y > 0) || (x > 0 && y <= 0));
}

inline float clip_dotprod(const VertexData &vert, float A, float B, float C, float D) {
	return (vert.clippos.x * A + vert.clippos.y * B + vert.clippos.z * C + vert.clippos.w * D);
}

#define POLY_CLIP( PLANE_BIT, A, B, C, D )							\
{																	\
	if (mask & PLANE_BIT) {											\
		int idxPrev = inlist[0];									\
		float dpPrev = clip_dotprod(*Vertices[idxPrev], A, B, C, D );\
		int outcount = 0;											\
																	\
		inlist[n] = inlist[0];										\
		for (int j = 1; j <= n; j++) { 								\
			int idx = inlist[j];									\
			float dp = clip_dotprod(*Vertices[idx], A, B, C, D );	\
			if (dpPrev >= 0) {										\
				outlist[outcount++] = idxPrev;						\
			}														\
																	\
			if (different_signs(dp, dpPrev)) {						\
				if (dp < 0) {										\
					float t = dp / (dp - dpPrev);					\
					Vertices[numVertices++]->Lerp(t, *Vertices[idx], *Vertices[idxPrev]);		\
				} else {											\
					float t = dpPrev / (dpPrev - dp);				\
					Vertices[numVertices++]->Lerp(t, *Vertices[idxPrev], *Vertices[idx]);		\
				}													\
				outlist[outcount++] = numVertices - 1;				\
			}														\
																	\
			idxPrev = idx;											\
			dpPrev = dp;											\
		}															\
																	\
		if (outcount < 3)											\
			continue;												\
																	\
		{															\
			int *tmp = inlist;										\
			inlist = outlist;										\
			outlist = tmp;											\
			n = outcount;											\
		}															\
	}																\
}

#define CLIP_LINE(PLANE_BIT, A, B, C, D)						\
{																\
	if (mask & PLANE_BIT) {										\
		float dp0 = clip_dotprod(*Vertices[0], A, B, C, D );	\
		float dp1 = clip_dotprod(*Vertices[1], A, B, C, D );	\
		int numVertices = 0;												\
																\
		if (mask0 & PLANE_BIT) {								\
			if (dp0 < 0) {										\
				float t = dp1 / (dp1 - dp0);					\
				Vertices[0]->Lerp(t, *Vertices[1], *Vertices[0]); \
			}													\
		}														\
		dp0 = clip_dotprod(*Vertices[0], A, B, C, D );			\
																\
		if (mask1 & PLANE_BIT) {								\
			if (dp1 < 0) {										\
				float t = dp1 / (dp1- dp0);						\
				Vertices[1]->Lerp(t, *Vertices[1], *Vertices[0]);	\
			}													\
		}														\
	}															\
}

static void RotateUVThrough(const VertexData &tl, const VertexData &br, VertexData &tr, VertexData &bl) {
	const int x1 = tl.screenpos.x;
	const int x2 = br.screenpos.x;
	const int y1 = tl.screenpos.y;
	const int y2 = br.screenpos.y;

	if ((x1 < x2 && y1 > y2) || (x1 > x2 && y1 < y2)) {
		std::swap(bl.texturecoords, tr.texturecoords);
	}
}

void ProcessRect(const VertexData& v0, const VertexData& v1)
{
	if (!gstate.isModeThrough()) {
		VertexData buf[4];
		buf[0].clippos = ClipCoords(v0.clippos.x, v0.clippos.y, v1.clippos.z, v1.clippos.w);
		buf[0].texturecoords = v0.texturecoords;

		buf[1].clippos = ClipCoords(v0.clippos.x, v1.clippos.y, v1.clippos.z, v1.clippos.w);
		buf[1].texturecoords = Vec2<float>(v0.texturecoords.x, v1.texturecoords.y);

		buf[2].clippos = ClipCoords(v1.clippos.x, v0.clippos.y, v1.clippos.z, v1.clippos.w);
		buf[2].texturecoords = Vec2<float>(v1.texturecoords.x, v0.texturecoords.y);

		buf[3] = v1;

		// Color and depth values of second vertex are used for the whole rectangle
		buf[0].color0 = buf[1].color0 = buf[2].color0 = buf[3].color0;
		buf[0].color1 = buf[1].color1 = buf[2].color1 = buf[3].color1;
		buf[0].fogdepth = buf[1].fogdepth = buf[2].fogdepth = buf[3].fogdepth;

		VertexData* topleft = &buf[0];
		VertexData* topright = &buf[1];
		VertexData* bottomleft = &buf[2];
		VertexData* bottomright = &buf[3];

		for (int i = 0; i < 4; ++i) {
			if (buf[i].clippos.x < topleft->clippos.x && buf[i].clippos.y < topleft->clippos.y)
				topleft = &buf[i];
			if (buf[i].clippos.x > topright->clippos.x && buf[i].clippos.y < topright->clippos.y)
				topright = &buf[i];
			if (buf[i].clippos.x < bottomleft->clippos.x && buf[i].clippos.y > bottomleft->clippos.y)
				bottomleft = &buf[i];
			if (buf[i].clippos.x > bottomright->clippos.x && buf[i].clippos.y > bottomright->clippos.y)
				bottomright = &buf[i];
		}

		// Four triangles to do backfaces as well. Two of them will get backface culled.
		ProcessTriangle(*topleft, *topright, *bottomright, buf[3]);
		ProcessTriangle(*bottomright, *topright, *topleft, buf[3]);
		ProcessTriangle(*bottomright, *bottomleft, *topleft, buf[3]);
		ProcessTriangle(*topleft, *bottomleft, *bottomright, buf[3]);
	} else {
		// through mode handling

		if (Rasterizer::RectangleFastPath(v0, v1)) {
			return;
		}

		VertexData buf[4];
		buf[0].screenpos = ScreenCoords(v0.screenpos.x, v0.screenpos.y, v1.screenpos.z);
		buf[0].texturecoords = v0.texturecoords;

		buf[1].screenpos = ScreenCoords(v0.screenpos.x, v1.screenpos.y, v1.screenpos.z);
		buf[1].texturecoords = Vec2<float>(v0.texturecoords.x, v1.texturecoords.y);

		buf[2].screenpos = ScreenCoords(v1.screenpos.x, v0.screenpos.y, v1.screenpos.z);
		buf[2].texturecoords = Vec2<float>(v1.texturecoords.x, v0.texturecoords.y);

		buf[3] = v1;

		// Color and depth values of second vertex are used for the whole rectangle
		buf[0].color0 = buf[1].color0 = buf[2].color0 = buf[3].color0;
		buf[0].color1 = buf[1].color1 = buf[2].color1 = buf[3].color1;  // is color1 ever used in through mode?
		buf[0].clippos.w = buf[1].clippos.w = buf[2].clippos.w = buf[3].clippos.w = 1.0f;
		buf[0].fogdepth = buf[1].fogdepth = buf[2].fogdepth = buf[3].fogdepth = 1.0f;

		VertexData* topleft = &buf[0];
		VertexData* topright = &buf[1];
		VertexData* bottomleft = &buf[2];
		VertexData* bottomright = &buf[3];

		// DrawTriangle always culls, so sort out the drawing order.
		for (int i = 0; i < 4; ++i) {
			if (buf[i].screenpos.x < topleft->screenpos.x && buf[i].screenpos.y < topleft->screenpos.y)
				topleft = &buf[i];
			if (buf[i].screenpos.x > topright->screenpos.x && buf[i].screenpos.y < topright->screenpos.y)
				topright = &buf[i];
			if (buf[i].screenpos.x < bottomleft->screenpos.x && buf[i].screenpos.y > bottomleft->screenpos.y)
				bottomleft = &buf[i];
			if (buf[i].screenpos.x > bottomright->screenpos.x && buf[i].screenpos.y > bottomright->screenpos.y)
				bottomright = &buf[i];
		}

		RotateUVThrough(v0, v1, *topright, *bottomleft);

		if (gstate.isModeClear()) {
			Rasterizer::ClearRectangle(v0, v1);
		} else {
			// Four triangles to do backfaces as well. Two of them will get backface culled.
			Rasterizer::DrawTriangle(*topleft, *topright, *bottomright);
			Rasterizer::DrawTriangle(*bottomright, *topright, *topleft);
			Rasterizer::DrawTriangle(*bottomright, *bottomleft, *topleft);
			Rasterizer::DrawTriangle(*topleft, *bottomleft, *bottomright);
		}
	}
}

void ProcessPoint(VertexData& v0)
{
	// Points need no clipping. Will be bounds checked in the rasterizer (which seems backwards?)
	Rasterizer::DrawPoint(v0);
}

void ProcessLine(VertexData& v0, VertexData& v1)
{
	if (gstate.isModeThrough()) {
		// Actually, should clip this one too so we don't need to do bounds checks in the rasterizer.
		Rasterizer::DrawLine(v0, v1);
		return;
	}

	VertexData *Vertices[2] = {&v0, &v1};

	int mask0 = CalcClipMask(v0.clippos);
	int mask1 = CalcClipMask(v1.clippos);
	int mask = mask0 | mask1;

	if (mask0 & mask1) {
		// Even if clipping is disabled, we can discard if the line is entirely outside.
		return;
	}

	if (mask) {
		CLIP_LINE(CLIP_POS_X_BIT, -1,  0,  0, 1);
		CLIP_LINE(CLIP_NEG_X_BIT,  1,  0,  0, 1);
		CLIP_LINE(CLIP_POS_Y_BIT,  0, -1,  0, 1);
		CLIP_LINE(CLIP_NEG_Y_BIT,  0,  1,  0, 1);
		CLIP_LINE(CLIP_POS_Z_BIT,  0,  0,  0, 1);
		CLIP_LINE(CLIP_NEG_Z_BIT,  0,  0,  1, 1);
	}

	VertexData data[2] = { *Vertices[0], *Vertices[1] };
	data[0].screenpos = TransformUnit::ClipToScreen(data[0].clippos);
	data[1].screenpos = TransformUnit::ClipToScreen(data[1].clippos);
	Rasterizer::DrawLine(data[0], data[1]);
}

void ProcessTriangle(VertexData& v0, VertexData& v1, VertexData& v2, const VertexData &provoking) {
	if (gstate.isModeThrough()) {
		// In case of cull reordering, make sure the right color is on the final vertex.
		if (gstate.getShadeMode() == GE_SHADE_FLAT) {
			VertexData corrected2 = v2;
			corrected2.color0 = provoking.color0;
			corrected2.color1 = provoking.color1;
			Rasterizer::DrawTriangle(v0, v1, corrected2);
		} else {
			Rasterizer::DrawTriangle(v0, v1, v2);
		}
		return;
	}

	enum { NUM_CLIPPED_VERTICES = 33, NUM_INDICES = NUM_CLIPPED_VERTICES + 3 };

	VertexData* Vertices[NUM_INDICES];
	VertexData ClippedVertices[NUM_CLIPPED_VERTICES];
	for (int i = 0; i < NUM_CLIPPED_VERTICES; ++i)
		Vertices[i+3] = &ClippedVertices[i];

	// TODO: Change logic when it's a backface (why? In what way?)
	Vertices[0] = &v0;
	Vertices[1] = &v1;
	Vertices[2] = &v2;

	int indices[NUM_INDICES] = { 0, 1, 2, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG,
									SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG,
									SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG, SKIP_FLAG };
	int numIndices = 3;

	int mask = 0;
	mask |= CalcClipMask(v0.clippos);
	mask |= CalcClipMask(v1.clippos);
	mask |= CalcClipMask(v2.clippos);

	if (mask) {
		for (int i = 0; i < 3; i += 3) {
			int vlist[2][2*6+1];
			int *inlist = vlist[0], *outlist = vlist[1];
			int n = 3;
			int numVertices = 3;

			inlist[0] = 0;
			inlist[1] = 1;
			inlist[2] = 2;

			// mark this triangle as unused in case it should be completely clipped
			indices[0] = SKIP_FLAG;
			indices[1] = SKIP_FLAG;
			indices[2] = SKIP_FLAG;

			// The PSP doesn't clip on the sides (so, commented out) but it does appear to have a Z clipper.
			// POLY_CLIP(CLIP_POS_X_BIT, -1,  0,  0, 1);
			// POLY_CLIP(CLIP_NEG_X_BIT,  1,  0,  0, 1);
			// POLY_CLIP(CLIP_POS_Y_BIT,  0, -1,  0, 1);
			// POLY_CLIP(CLIP_NEG_Y_BIT,  0,  1,  0, 1);
			POLY_CLIP(CLIP_POS_Z_BIT,  0,  0,  0, 1);
			POLY_CLIP(CLIP_NEG_Z_BIT,  0,  0,  1, 1);

			// transform the poly in inlist into triangles
			indices[0] = inlist[0];
			indices[1] = inlist[1];
			indices[2] = inlist[2];
			for (int j = 3; j < n; ++j) {
				indices[numIndices++] = inlist[0];
				indices[numIndices++] = inlist[j - 1];
				indices[numIndices++] = inlist[j];
			}
		}
	} else if (CalcClipMask(v0.clippos) & CalcClipMask(v1.clippos) & CalcClipMask(v2.clippos))  {
		// If clipping is disabled, only discard the current primitive
		// if all three vertices lie outside one of the clipping planes
		return;
	}

	for (int i = 0; i + 3 <= numIndices; i += 3) {
		if (indices[i] != SKIP_FLAG) {
			VertexData data[3] = { *Vertices[indices[i]], *Vertices[indices[i+1]], *Vertices[indices[i+2]] };
			data[0].screenpos = TransformUnit::ClipToScreen(data[0].clippos);
			data[1].screenpos = TransformUnit::ClipToScreen(data[1].clippos);
			data[2].screenpos = TransformUnit::ClipToScreen(data[2].clippos);

			if (gstate.getShadeMode() == GE_SHADE_FLAT) {
				// So that the order of clipping doesn't matter...
				data[2].color0 = provoking.color0;
				data[2].color1 = provoking.color1;
			}

			Rasterizer::DrawTriangle(data[0], data[1], data[2]);
		}
	}
}

} // namespace
