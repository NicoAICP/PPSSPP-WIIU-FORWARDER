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

#include "Common/Swap.h"
#include "GPU/Common/StencilCommon.h"

u8 StencilBits5551(const u8 *ptr8, u32 numPixels) {
	const u32_le *ptr = (const u32_le *)ptr8;

	for (u32 i = 0; i < numPixels / 2; ++i) {
		if (ptr[i] & 0x80008000) {
			return 1;
		}
	}
	return 0;
}

u8 StencilBits4444(const u8 *ptr8, u32 numPixels) {
	const u32_le *ptr = (const u32_le *)ptr8;
	u32 bits = 0;

	for (u32 i = 0; i < numPixels / 2; ++i) {
		bits |= ptr[i];
	}

	return ((bits >> 12) & 0xF) | (bits >> 28);
}

u8 StencilBits8888(const u8 *ptr8, u32 numPixels) {
	const u32_le *ptr = (const u32_le *)ptr8;
	u32 bits = 0;

	for (u32 i = 0; i < numPixels; ++i) {
		bits |= ptr[i];
	}

	return bits >> 24;
}
