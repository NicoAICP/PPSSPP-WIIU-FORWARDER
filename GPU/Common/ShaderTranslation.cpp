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

#include "ppsspp_config.h"

#include <memory>
#include <vector>
#include <sstream>

// DbgNew is not compatible with Glslang
#ifdef DBG_NEW
#undef new
#undef free
#undef malloc
#undef realloc
#endif

// Weird issue
#if PPSSPP_PLATFORM(WINDOWS) && PPSSPP_ARCH(ARM)
#undef free
#endif

#include "Common/Log.h"
#include "Common/StringUtils.h"

#include "GPU/Common/ShaderTranslation.h"
#include "ext/glslang/SPIRV/GlslangToSpv.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "ext/SPIRV-Cross/spirv.hpp"
#include "ext/SPIRV-Cross/spirv_common.hpp"
#include "ext/SPIRV-Cross/spirv_cross.hpp"
#include "ext/SPIRV-Cross/spirv_glsl.hpp"
#ifdef _WIN32
#include "ext/SPIRV-Cross/spirv_hlsl.hpp"
#endif

extern void init_resources(TBuiltInResource &Resources);

static EShLanguage GetLanguage(const Draw::ShaderStage stage) {
	switch (stage) {
	case Draw::ShaderStage::VERTEX: return EShLangVertex;
	case Draw::ShaderStage::CONTROL: return EShLangTessControl;
	case Draw::ShaderStage::EVALUATION: return EShLangTessEvaluation;
	case Draw::ShaderStage::GEOMETRY: return EShLangGeometry;
	case Draw::ShaderStage::FRAGMENT: return EShLangFragment;
	case Draw::ShaderStage::COMPUTE: return EShLangCompute;
	default: return EShLangVertex;
	}
}

void ShaderTranslationInit() {
	// TODO: We have TLS issues on UWP
#if !PPSSPP_PLATFORM(UWP) && !PPSSPP_PLATFORM(WIIU)
	glslang::InitializeProcess();
#endif
}
void ShaderTranslationShutdown() {
#if !PPSSPP_PLATFORM(UWP) && !PPSSPP_PLATFORM(WIIU)
	glslang::FinalizeProcess();
#endif
}

std::string Preprocess(std::string code, ShaderLanguage lang, Draw::ShaderStage stage) {
	// This takes GL up to the version we need.
	return code;
}

struct Builtin {
	const char *needle;
	const char *replacement;
};

static const char *cbufferDecl = R"(
cbuffer data : register(b0) {
	float2 u_texelDelta;
	float2 u_pixelDelta;
	float4 u_time;
	float4 u_setting;
	float u_video;
};
)";

static const char *vulkanPrologue =
R"(#version 430
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
)";

static const char *vulkanUboDecl = R"(
layout (std140, set = 0, binding = 0) uniform Data {
	vec2 u_texelDelta;
	vec2 u_pixelDelta;
	vec4 u_time;
	vec4 u_setting;
	float u_video;
};
)";

static const char *d3d9RegisterDecl = R"(
float4 gl_HalfPixel : register(c0);
float2 u_texelDelta : register(c1);
float2 u_pixelDelta : register(c2);
float4 u_time : register(c3);
float4 u_setting : register(c4);
float u_video : register(c5);
)";

// SPIRV-Cross' HLSL output has some deficiencies we need to work around.
// Also we need to rip out single uniforms and replace them with blocks.
// Should probably do it in the source shader instead and then back translate to old style GLSL, but
// SPIRV-Cross currently won't compile with the Android NDK so I can't be bothered.
std::string Postprocess(std::string code, ShaderLanguage lang, Draw::ShaderStage stage) {
	if (lang != HLSL_D3D11 && lang != HLSL_DX9)
		return code;

	std::stringstream out;

	// Output the uniform buffer.
	if (lang == HLSL_D3D11)
		out << cbufferDecl;
	else if (lang == HLSL_DX9)
		out << d3d9RegisterDecl;

	// Alright, now let's go through it line by line and zap the single uniforms.
	std::string line;
	std::stringstream instream(code);
	while (std::getline(instream, line)) {
		if (line == "uniform sampler2D sampler0;" && lang == HLSL_DX9) {
			out << "sampler2D sampler0 : register(s0);\n";
			continue;
		}
		if (line == "uniform sampler2D sampler1;" && lang == HLSL_DX9) {
			out << "sampler2D sampler1 : register(s1);\n";
			continue;
		}
		if (line.find("uniform float") != std::string::npos) {
			continue;
		}
		out << line << "\n";
	}
	std::string output = out.str();
	return output;
}

bool ConvertToVulkanGLSL(std::string *dest, TranslatedShaderMetadata *destMetadata, std::string src, Draw::ShaderStage stage, std::string *errorMessage) {
	std::stringstream out;

	static struct {
		Draw::ShaderStage stage;
		const char *needle;
		const char *replacement;
	} replacements[] = {
		{ Draw::ShaderStage::VERTEX, "attribute vec4 a_position;", "layout(location = 0) in vec4 a_position;" },
		{ Draw::ShaderStage::VERTEX, "attribute vec2 a_texcoord0;", "layout(location = 2) in vec2 a_texcoord0;"},
		{ Draw::ShaderStage::VERTEX, "varying vec2 v_position;", "layout(location = 0) out vec2 v_position;" },
		{ Draw::ShaderStage::FRAGMENT, "varying vec2 v_position;", "layout(location = 0) in vec2 v_position;" },
		{ Draw::ShaderStage::FRAGMENT, "texture2D(", "texture(" },
		{ Draw::ShaderStage::FRAGMENT, "gl_FragColor", "fragColor0" },
	};

	out << vulkanPrologue;
	if (stage == Draw::ShaderStage::FRAGMENT) {
		out << "layout (location = 0) out vec4 fragColor0;\n";
	}
	// Output the uniform buffer.
	out << vulkanUboDecl;

	// Alright, now let's go through it line by line and zap the single uniforms
	// and perform replacements.
	std::string line;
	std::stringstream instream(src);
	while (std::getline(instream, line)) {
		int vecSize, num;
		if (line.find("uniform bool") != std::string::npos) {
			continue;
		} else if (line.find("uniform sampler2D") == 0) {
			if (line.find("sampler0") != line.npos)
				line = "layout(set = 0, binding = 1) " + line;
			else
				line = "layout(set = 0, binding = 2) " + line;
		} else if (line.find("uniform ") != std::string::npos) {
			continue;
		} else if (2 == sscanf(line.c_str(), "varying vec%d v_texcoord%d;", &vecSize, &num)) {
			if (stage == Draw::ShaderStage::FRAGMENT) {
				line = StringFromFormat("layout(location = %d) in vec%d v_texcoord%d;", num, vecSize, num);
			} else {
				line = StringFromFormat("layout(location = %d) out vec%d v_texcoord%d;", num, vecSize, num);
			}
		}
		for (int i = 0; i < ARRAY_SIZE(replacements); i++) {
			if (replacements[i].stage == stage)
				line = ReplaceAll(line, replacements[i].needle, replacements[i].replacement);
		}
		out << line << "\n";
	}

	// DUMPLOG(src.c_str());
	// INFO_LOG(SYSTEM, "---->");
	// DUMPLOG(LineNumberString(out.str()).c_str());

	*dest = out.str();
	return true;
}

bool TranslateShader(std::string *dest, ShaderLanguage destLang, TranslatedShaderMetadata *destMetadata, std::string src, ShaderLanguage srcLang, Draw::ShaderStage stage, std::string *errorMessage) {
	if (srcLang != GLSL_300 && srcLang != GLSL_140)
		return false;

	if ((srcLang == GLSL_140 || srcLang == GLSL_300) && destLang == GLSL_VULKAN) {
		// Let's just mess about at the string level, no need to recompile.
		bool result = ConvertToVulkanGLSL(dest, destMetadata, src, stage, errorMessage);
		return result;
	}
	if (errorMessage) {
		*errorMessage = "";
	}

#if PPSSPP_PLATFORM(UWP) || PPSSPP_PLATFORM(WIIU)
	return false;
#endif

	glslang::TProgram program;
	const char *shaderStrings[1];

	TBuiltInResource Resources;
	init_resources(Resources);

	// Don't enable SPIR-V and Vulkan rules when parsing GLSL. Our postshaders are written in oldschool GLES 2.0.
	EShMessages messages = EShMessages::EShMsgDefault;

	EShLanguage shaderStage = GetLanguage(stage);
	glslang::TShader shader(shaderStage);

	std::string preprocessed = Preprocess(src, srcLang, stage);

	shaderStrings[0] = src.c_str();
	shader.setStrings(shaderStrings, 1);
	if (!shader.parse(&Resources, 100, EProfile::ECompatibilityProfile, false, false, messages)) {
		ERROR_LOG(G3D, "%s", shader.getInfoLog());
		ERROR_LOG(G3D, "%s", shader.getInfoDebugLog());
		if (errorMessage) {
			*errorMessage = shader.getInfoLog();
			(*errorMessage) += shader.getInfoDebugLog();
		}
		return false; // something didn't work
	}

	// Note that program does not take ownership of &shader, so this is fine.
	program.addShader(&shader);

	if (!program.link(messages)) {
		ERROR_LOG(G3D, "%s", shader.getInfoLog());
		ERROR_LOG(G3D, "%s", shader.getInfoDebugLog());
		if (errorMessage) {
			*errorMessage = shader.getInfoLog();
			(*errorMessage) += shader.getInfoDebugLog();
		}
		return false;
	}

	std::vector<unsigned int> spirv;
	// Can't fail, parsing worked, "linking" worked.
	glslang::SpvOptions options;
	options.disableOptimizer = false;
	options.optimizeSize = false;
	options.generateDebugInfo = false;
	glslang::GlslangToSpv(*program.getIntermediate(shaderStage), spirv, &options);

	// For whatever reason, with our config, the above outputs an invalid SPIR-V version, 0.
	// Patch it up so spirv-cross accepts it.
	spirv[1] = glslang::EShTargetSpv_1_0;


	// Alright, step 1 done. Now let's take this SPIR-V shader and output in our desired format.

	switch (destLang) {
#ifdef _WIN32
	case HLSL_DX9:
	{
		spirv_cross::CompilerHLSL hlsl(spirv);
		spirv_cross::CompilerHLSL::Options options{};
		options.shader_model = 30;
		spirv_cross::CompilerGLSL::Options options_common{};
		options_common.vertex.fixup_clipspace = true;
		hlsl.set_hlsl_options(options);
		hlsl.set_common_options(options_common);
		std::string raw = hlsl.compile();
		*dest = Postprocess(raw, destLang, stage);
		return true;
	}
	case HLSL_D3D11:
	{
		spirv_cross::CompilerHLSL hlsl(spirv);
		spirv_cross::ShaderResources resources = hlsl.get_shader_resources();

		int i = 0;
		for (auto &resource : resources.sampled_images) {
			// int location = hlsl.get_decoration(resource.id, spv::DecorationLocation);
			hlsl.set_decoration(resource.id, spv::DecorationLocation, i);
			i++;
		}
		spirv_cross::CompilerHLSL::Options options{};
		options.shader_model = 50;
		spirv_cross::CompilerGLSL::Options options_common{};
		options_common.vertex.fixup_clipspace = true;
		hlsl.set_hlsl_options(options);
		hlsl.set_common_options(options_common);
		std::string raw = hlsl.compile();
		*dest = Postprocess(raw, destLang, stage);
		return true;
	}
#endif
	case GLSL_140:
	{
		spirv_cross::CompilerGLSL glsl(std::move(spirv));
		// The SPIR-V is now parsed, and we can perform reflection on it.
		spirv_cross::ShaderResources resources = glsl.get_shader_resources();
		// Get all sampled images in the shader.
		for (auto &resource : resources.sampled_images) {
			unsigned set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
			unsigned binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
			printf("Image %s at set = %u, binding = %u\n", resource.name.c_str(), set, binding);
			// Modify the decoration to prepare it for GLSL.
			glsl.unset_decoration(resource.id, spv::DecorationDescriptorSet);
			// Some arbitrary remapping if we want.
			glsl.set_decoration(resource.id, spv::DecorationBinding, set * 16 + binding);
		}
		// Set some options.
		spirv_cross::CompilerGLSL::Options options;
		options.version = 140;
		options.es = true;
		glsl.set_common_options(options);

		// Compile to GLSL, ready to give to GL driver.
		*dest = glsl.compile();
		return true;
	}
	case GLSL_300:
	{
		spirv_cross::CompilerGLSL glsl(std::move(spirv));
		// The SPIR-V is now parsed, and we can perform reflection on it.
		spirv_cross::ShaderResources resources = glsl.get_shader_resources();
		// Set some options.
		spirv_cross::CompilerGLSL::Options options;
		options.version = gl_extensions.GLSLVersion();
		// macOS OpenGL 4.1 implementation does not support GL_ARB_shading_language_420pack.
		// Prevent explicit binding location emission enabled in SPIRV-Cross by default.
		options.enable_420pack_extension = gl_extensions.ARB_shading_language_420pack;
		glsl.set_common_options(options);
		// Compile to GLSL, ready to give to GL driver.
		*dest = glsl.compile();
		return true;
	}
	default:
		return false;
	}
}
