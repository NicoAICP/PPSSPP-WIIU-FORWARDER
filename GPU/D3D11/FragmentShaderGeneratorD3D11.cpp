// Copyright (c) 2017- PPSSPP Project.

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

#include "GPU/Common/ShaderCommon.h"
#include "GPU/D3D11/FragmentShaderGeneratorD3D11.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"

void GenerateFragmentShaderD3D11(const FShaderID &id, char *buffer, ShaderLanguage lang) {
	DX9::GenerateFragmentShaderHLSL(id, buffer, lang);
}
