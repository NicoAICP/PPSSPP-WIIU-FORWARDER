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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM64)

#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Common/Arm64Emitter.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "GPU/GPUState.h"
#include "GPU/Common/VertexDecoderCommon.h"

alignas(16) static float bones[16 * 8];  // First four are kept in registers
alignas(16) static float boneMask[4] = {1.0f, 1.0f, 1.0f, 0.0f};

static const float by128 = 1.0f / 128.0f;
static const float by32768 = 1.0f / 32768.0f;

using namespace Arm64Gen;

// Pointers, X regs (X0 - X17 safe to use.)
static const ARM64Reg srcReg = X0;
static const ARM64Reg dstReg = X1;

static const ARM64Reg counterReg = W2;
static const ARM64Reg tempReg1 = W3;
static const ARM64Reg tempRegPtr = X3;
static const ARM64Reg tempReg2 = W4;
static const ARM64Reg tempReg3 = W5;
static const ARM64Reg scratchReg = W6;
static const ARM64Reg scratchReg64 = X6;
static const ARM64Reg scratchReg2 = W7;
static const ARM64Reg scratchReg3 = W8;
static const ARM64Reg fullAlphaReg = W12;
static const ARM64Reg boundsMinUReg = W13;
static const ARM64Reg boundsMinVReg = W14;
static const ARM64Reg boundsMaxUReg = W15;
static const ARM64Reg boundsMaxVReg = W16;

static const ARM64Reg fpScratchReg = S4;
static const ARM64Reg fpScratchReg2 = S5;
static const ARM64Reg fpScratchReg3 = S6;
static const ARM64Reg fpScratchReg4 = S7;

static const ARM64Reg neonScratchRegD = D2;
static const ARM64Reg neonScratchRegQ = Q2;

static const ARM64Reg neonUVScaleReg = D0;
static const ARM64Reg neonUVOffsetReg = D1;

static const ARM64Reg src[3] = {S2, S3, S8};
static const ARM64Reg srcD[3] = {D2, D3, D8};
static const ARM64Reg srcQ[3] = {Q2, Q3, Q8};

static const ARM64Reg srcNEON = Q8;
static const ARM64Reg accNEON = Q9;

static const ARM64Reg neonWeightRegsQ[2] = { Q3, Q2 };  // reverse order to prevent clash with neonScratchReg in Jit_WeightsU*Skin.

// Q4-Q7 is the generated matrix that we multiply things by.
// Q8,Q9 are accumulators/scratch for matrix mul.
// Q10, Q11 are more scratch for matrix mul.
// Q16+ are free-for-all for matrices. In 16 registers, we can fit 4 4x4 matrices.

static const JitLookup jitLookup[] = {
	{&VertexDecoder::Step_WeightsU8, &VertexDecoderJitCache::Jit_WeightsU8},
	{&VertexDecoder::Step_WeightsU16, &VertexDecoderJitCache::Jit_WeightsU16},
	{&VertexDecoder::Step_WeightsFloat, &VertexDecoderJitCache::Jit_WeightsFloat},
	{&VertexDecoder::Step_WeightsU8Skin, &VertexDecoderJitCache::Jit_WeightsU8Skin},
	{&VertexDecoder::Step_WeightsU16Skin, &VertexDecoderJitCache::Jit_WeightsU16Skin},
	{&VertexDecoder::Step_WeightsFloatSkin, &VertexDecoderJitCache::Jit_WeightsFloatSkin},

	{&VertexDecoder::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},
	{&VertexDecoder::Step_TcU8ToFloat, &VertexDecoderJitCache::Jit_TcU8ToFloat},
	{&VertexDecoder::Step_TcU16ToFloat, &VertexDecoderJitCache::Jit_TcU16ToFloat},

	{&VertexDecoder::Step_TcU8Prescale, &VertexDecoderJitCache::Jit_TcU8Prescale},
	{&VertexDecoder::Step_TcU16Prescale, &VertexDecoderJitCache::Jit_TcU16Prescale},
	{&VertexDecoder::Step_TcFloatPrescale, &VertexDecoderJitCache::Jit_TcFloatPrescale},

	{&VertexDecoder::Step_TcFloatThrough, &VertexDecoderJitCache::Jit_TcFloatThrough},
	{&VertexDecoder::Step_TcU16ThroughToFloat, &VertexDecoderJitCache::Jit_TcU16ThroughToFloat},

	{&VertexDecoder::Step_NormalS8, &VertexDecoderJitCache::Jit_NormalS8},
	{&VertexDecoder::Step_NormalS16, &VertexDecoderJitCache::Jit_NormalS16},
	{&VertexDecoder::Step_NormalFloat, &VertexDecoderJitCache::Jit_NormalFloat},

	{&VertexDecoder::Step_NormalS8Skin, &VertexDecoderJitCache::Jit_NormalS8Skin},
	{&VertexDecoder::Step_NormalS16Skin, &VertexDecoderJitCache::Jit_NormalS16Skin},
	{&VertexDecoder::Step_NormalFloatSkin, &VertexDecoderJitCache::Jit_NormalFloatSkin},

	{&VertexDecoder::Step_Color8888, &VertexDecoderJitCache::Jit_Color8888},
	{&VertexDecoder::Step_Color4444, &VertexDecoderJitCache::Jit_Color4444},
	{&VertexDecoder::Step_Color565, &VertexDecoderJitCache::Jit_Color565},
	{&VertexDecoder::Step_Color5551, &VertexDecoderJitCache::Jit_Color5551},

	{&VertexDecoder::Step_PosS8Through, &VertexDecoderJitCache::Jit_PosS8Through},
	{&VertexDecoder::Step_PosS16Through, &VertexDecoderJitCache::Jit_PosS16Through},
	{&VertexDecoder::Step_PosFloatThrough, &VertexDecoderJitCache::Jit_PosFloat},

	{&VertexDecoder::Step_PosS8, &VertexDecoderJitCache::Jit_PosS8},
	{&VertexDecoder::Step_PosS16, &VertexDecoderJitCache::Jit_PosS16},
	{&VertexDecoder::Step_PosFloat, &VertexDecoderJitCache::Jit_PosFloat},

	{&VertexDecoder::Step_PosS8Skin, &VertexDecoderJitCache::Jit_PosS8Skin},
	{&VertexDecoder::Step_PosS16Skin, &VertexDecoderJitCache::Jit_PosS16Skin},
	{&VertexDecoder::Step_PosFloatSkin, &VertexDecoderJitCache::Jit_PosFloatSkin},

	/*
	{&VertexDecoder::Step_NormalS8Morph, &VertexDecoderJitCache::Jit_NormalS8Morph},
	{&VertexDecoder::Step_NormalS16Morph, &VertexDecoderJitCache::Jit_NormalS16Morph},
	{&VertexDecoder::Step_NormalFloatMorph, &VertexDecoderJitCache::Jit_NormalFloatMorph},

	{&VertexDecoder::Step_PosS8Morph, &VertexDecoderJitCache::Jit_PosS8Morph},
	{&VertexDecoder::Step_PosS16Morph, &VertexDecoderJitCache::Jit_PosS16Morph},
	{&VertexDecoder::Step_PosFloatMorph, &VertexDecoderJitCache::Jit_PosFloatMorph},

	{&VertexDecoder::Step_Color8888Morph, &VertexDecoderJitCache::Jit_Color8888Morph},
	{&VertexDecoder::Step_Color4444Morph, &VertexDecoderJitCache::Jit_Color4444Morph},
	{&VertexDecoder::Step_Color565Morph, &VertexDecoderJitCache::Jit_Color565Morph},
	{&VertexDecoder::Step_Color5551Morph, &VertexDecoderJitCache::Jit_Color5551Morph},
	*/
};


JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec, int32_t *jittedSize) {
	dec_ = &dec;

	BeginWrite();
	const u8 *start = AlignCode16();

	bool prescaleStep = false;
	bool skinning = false;

	bool log = false;

	// Look for prescaled texcoord steps
	for (int i = 0; i < dec.numSteps_; i++) {
		if (dec.steps_[i] == &VertexDecoder::Step_TcU8Prescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcU16Prescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcFloatPrescale) {
			prescaleStep = true;
		}
		if (dec.steps_[i] == &VertexDecoder::Step_WeightsU8Skin ||
			dec.steps_[i] == &VertexDecoder::Step_WeightsU16Skin ||
			dec.steps_[i] == &VertexDecoder::Step_WeightsFloatSkin) {
			skinning = true;
		}
	}

	// Not used below, but useful for logging.
	(void)skinning;

	// if (skinning) log = true;

	uint64_t regs_to_save = Arm64Gen::ALL_CALLEE_SAVED;
	uint64_t regs_to_save_fp = Arm64Gen::ALL_CALLEE_SAVED_FP;
	fp.ABI_PushRegisters(regs_to_save, regs_to_save_fp);

	// Keep the scale/offset in a few fp registers if we need it.
	if (prescaleStep) {
		MOVP2R(X3, &gstate_c.uv);
		fp.LDR(64, INDEX_UNSIGNED, neonUVScaleReg, X3, 0);
		fp.LDR(64, INDEX_UNSIGNED, neonUVOffsetReg, X3, 8);
		if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_8BIT) {
			fp.MOVI2FDUP(neonScratchRegD, by128, scratchReg);
			fp.FMUL(32, neonUVScaleReg, neonUVScaleReg, neonScratchRegD);
		} else if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_16BIT) {
			fp.MOVI2FDUP(neonScratchRegD, by32768, scratchReg);
			fp.FMUL(32, neonUVScaleReg, neonUVScaleReg, neonScratchRegD);
		}
	}

	// Add code to convert matrices to 4x4.
	// Later we might want to do this when the matrices are loaded instead.
	int boneCount = 0;
	if (dec.weighttype && g_Config.bSoftwareSkinning) {
		// Copying from R3 to R4
		MOVP2R(X3, gstate.boneMatrix);
		MOVP2R(X4, bones);
		MOVP2R(X5, boneMask);
		fp.LDR(128, INDEX_UNSIGNED, Q3, X5, 0);
		for (int i = 0; i < dec.nweights; i++) {
			// Note that INDEX_UNSIGNED does not support offsets not aligned to the data size so we must use POST.
			fp.LDR(128, INDEX_POST, Q4, X3, 12);  // Load 128 bits even though we just want 96
			fp.LDR(128, INDEX_POST, Q5, X3, 12);
			fp.LDR(128, INDEX_POST, Q6, X3, 12);
			fp.LDR(128, INDEX_POST, Q7, X3, 12);
			// First four matrices are in registers Q16+.
			if (i < 4) {
				fp.FMUL(32, (ARM64Reg)(Q16 + i * 4), Q4, Q3);
				fp.FMUL(32, (ARM64Reg)(Q17 + i * 4), Q5, Q3);
				fp.FMUL(32, (ARM64Reg)(Q18 + i * 4), Q6, Q3);
				fp.FMUL(32, (ARM64Reg)(Q19 + i * 4), Q7, Q3);
				ADDI2R(X4, X4, 16 * 4);
			} else {
				fp.FMUL(32, Q4, Q4, Q3);
				fp.FMUL(32, Q5, Q5, Q3);
				fp.FMUL(32, Q6, Q6, Q3);
				fp.FMUL(32, Q7, Q7, Q3);
				fp.STR(128, INDEX_UNSIGNED, Q4, X4, 0);
				fp.STR(128, INDEX_UNSIGNED, Q5, X4, 16);
				fp.STR(128, INDEX_UNSIGNED, Q6, X4, 32);
				fp.STR(128, INDEX_UNSIGNED, Q7, X4, 48);
				ADDI2R(X4, X4, 16 * 4);
			}
		}
	}

	if (dec.col) {
		// Or LDB and skip the conditional?  This is probably cheaper.
		MOVI2R(fullAlphaReg, 0xFF);
	}

	if (dec.tc && dec.throughmode) {
		// TODO: Smarter, only when doing bounds.
		MOVP2R(scratchReg64, &gstate_c.vertBounds.minU);
		LDRH(INDEX_UNSIGNED, boundsMinUReg, scratchReg64, offsetof(KnownVertexBounds, minU));
		LDRH(INDEX_UNSIGNED, boundsMaxUReg, scratchReg64, offsetof(KnownVertexBounds, maxU));
		LDRH(INDEX_UNSIGNED, boundsMinVReg, scratchReg64, offsetof(KnownVertexBounds, minV));
		LDRH(INDEX_UNSIGNED, boundsMaxVReg, scratchReg64, offsetof(KnownVertexBounds, maxV));
	}

	const u8 *loopStart = GetCodePtr();
	for (int i = 0; i < dec.numSteps_; i++) {
		if (!CompileStep(dec, i)) {
			EndWrite();
			// Reset the code ptr (effectively undoing what we generated) and return zero to indicate that we failed.
			ResetCodePtr(GetOffset(start));
			char temp[1024] = {0};
			dec.ToString(temp);
			ERROR_LOG(G3D, "Could not compile vertex decoder, failed at step %d: %s", i, temp);
			return nullptr;
		}
	}

	ADDI2R(srcReg, srcReg, dec.VertexSize(), scratchReg);
	ADDI2R(dstReg, dstReg, dec.decFmt.stride, scratchReg);
	SUBS(counterReg, counterReg, 1);
	B(CC_NEQ, loopStart);

	if (dec.col) {
		MOVP2R(tempRegPtr, &gstate_c.vertexFullAlpha);
		CMP(fullAlphaReg, 0);
		FixupBranch skip = B(CC_NEQ);
		STRB(INDEX_UNSIGNED, fullAlphaReg, tempRegPtr, 0);
		SetJumpTarget(skip);
	}

	if (dec.tc && dec.throughmode) {
		// TODO: Smarter, only when doing bounds.
		MOVP2R(scratchReg64, &gstate_c.vertBounds.minU);
		STRH(INDEX_UNSIGNED, boundsMinUReg, scratchReg64, offsetof(KnownVertexBounds, minU));
		STRH(INDEX_UNSIGNED, boundsMaxUReg, scratchReg64, offsetof(KnownVertexBounds, maxU));
		STRH(INDEX_UNSIGNED, boundsMinVReg, scratchReg64, offsetof(KnownVertexBounds, minV));
		STRH(INDEX_UNSIGNED, boundsMaxVReg, scratchReg64, offsetof(KnownVertexBounds, maxV));
	}

	fp.ABI_PopRegisters(regs_to_save, regs_to_save_fp);

	RET();

	FlushIcache();

	if (log) {
		char temp[1024] = { 0 };
		dec.ToString(temp);
		INFO_LOG(JIT, "=== %s (%d bytes) ===", temp, (int)(GetCodePtr() - start));
		std::vector<std::string> lines = DisassembleArm64(start, (int)(GetCodePtr() - start));
		for (auto line : lines) {
			INFO_LOG(JIT, "%s", line.c_str());
		}
		INFO_LOG(JIT, "==========");
	}

	*jittedSize = (int)(GetCodePtr() - start);
	EndWrite();
	return (JittedVertexDecoder)start;
}

bool VertexDecoderJitCache::CompileStep(const VertexDecoder &dec, int step) {
	// See if we find a matching JIT function
	for (size_t i = 0; i < ARRAY_SIZE(jitLookup); i++) {
		if (dec.steps_[step] == jitLookup[i].func) {
			((*this).*jitLookup[i].jitFunc)();
			return true;
		}
	}
	return false;
}

void VertexDecoderJitCache::Jit_ApplyWeights() {
	// We construct a matrix in Q4-Q7
	if (dec_->nweights >= 4) {
		MOVP2R(scratchReg64, bones + 16 * 4);
	}
	for (int i = 0; i < dec_->nweights; i++) {
		switch (i) {
		case 0:
			fp.FMUL(32, Q4, Q16, neonWeightRegsQ[0], 0);
			fp.FMUL(32, Q5, Q17, neonWeightRegsQ[0], 0);
			fp.FMUL(32, Q6, Q18, neonWeightRegsQ[0], 0);
			fp.FMUL(32, Q7, Q19, neonWeightRegsQ[0], 0);
			break;
		case 1:
			fp.FMLA(32, Q4, Q20, neonWeightRegsQ[0], 1);
			fp.FMLA(32, Q5, Q21, neonWeightRegsQ[0], 1);
			fp.FMLA(32, Q6, Q22, neonWeightRegsQ[0], 1);
			fp.FMLA(32, Q7, Q23, neonWeightRegsQ[0], 1);
			break;
		case 2:
			fp.FMLA(32, Q4, Q24, neonWeightRegsQ[0], 2);
			fp.FMLA(32, Q5, Q25, neonWeightRegsQ[0], 2);
			fp.FMLA(32, Q6, Q26, neonWeightRegsQ[0], 2);
			fp.FMLA(32, Q7, Q27, neonWeightRegsQ[0], 2);
			break;
		case 3:
			fp.FMLA(32, Q4, Q28, neonWeightRegsQ[0], 3);
			fp.FMLA(32, Q5, Q29, neonWeightRegsQ[0], 3);
			fp.FMLA(32, Q6, Q30, neonWeightRegsQ[0], 3);
			fp.FMLA(32, Q7, Q31, neonWeightRegsQ[0], 3);
			break;
		default:
			// Matrices 4+ need to be loaded from memory.
			fp.LDP(128, INDEX_SIGNED, Q8, Q9, scratchReg64, 0);
			fp.LDP(128, INDEX_SIGNED, Q10, Q11, scratchReg64, 2 * 16);
			fp.FMLA(32, Q4, Q8, neonWeightRegsQ[i >> 2], i & 3);
			fp.FMLA(32, Q5, Q9, neonWeightRegsQ[i >> 2], i & 3);
			fp.FMLA(32, Q6, Q10, neonWeightRegsQ[i >> 2], i & 3);
			fp.FMLA(32, Q7, Q11, neonWeightRegsQ[i >> 2], i & 3);
			ADDI2R(scratchReg64, scratchReg64, 4 * 16);
			break;
		}
	}
}

void VertexDecoderJitCache::Jit_WeightsU8() {
	// Basic implementation - a byte at a time. TODO: Optimize
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LDRB(INDEX_UNSIGNED, tempReg1, srcReg, dec_->weightoff + j);
		STRB(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.w0off + j);
	}
	while (j & 3) {
		STRB(INDEX_UNSIGNED, WZR, dstReg, dec_->decFmt.w0off + j);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU16() {
	// Basic implementation - a short at a time. TODO: Optimize
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LDRH(INDEX_UNSIGNED, tempReg1, srcReg, dec_->weightoff + j * 2);
		STRH(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.w0off + j * 2);
	}
	while (j & 3) {
		STRH(INDEX_UNSIGNED, WZR, dstReg, dec_->decFmt.w0off + j * 2);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsFloat() {
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LDR(INDEX_UNSIGNED, tempReg1, srcReg, dec_->weightoff + j * 4);
		STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.w0off + j * 4);
	}
	while (j & 3) {  // Zero additional weights rounding up to 4.
		STR(INDEX_UNSIGNED, WZR, dstReg, dec_->decFmt.w0off + j * 4);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU8Skin() {
	// Weight is first so srcReg is correct.
	switch (dec_->nweights) {
	case 1: fp.LDR(8, INDEX_UNSIGNED, neonScratchRegD, srcReg, 0); break;
	case 2: fp.LDR(16, INDEX_UNSIGNED, neonScratchRegD, srcReg, 0); break;
	default:
		// For 3, we over read, for over 4, we read more later.
		fp.LDR(32, INDEX_UNSIGNED, neonScratchRegD, srcReg, 0);
		break;
	}

	fp.UXTL(8, neonScratchRegQ, neonScratchRegD);
	fp.UXTL(16, neonScratchRegQ, neonScratchRegD);
	fp.UCVTF(32, neonWeightRegsQ[0], neonScratchRegQ, 7);

	if (dec_->nweights > 4) {
		switch (dec_->nweights) {
		case 5: fp.LDR(8, INDEX_UNSIGNED, neonScratchRegD, srcReg, 4); break;
		case 6: fp.LDR(16, INDEX_UNSIGNED, neonScratchRegD, srcReg, 4); break;
		case 7:
		case 8:
			fp.LDR(32, INDEX_UNSIGNED, neonScratchRegD, srcReg, 4);
			break;
		}
		fp.UXTL(8, neonScratchRegQ, neonScratchRegD);
		fp.UXTL(16, neonScratchRegQ, neonScratchRegD);
		fp.UCVTF(32, neonWeightRegsQ[1], neonScratchRegQ, 7);
	}
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsU16Skin() {
	switch (dec_->nweights) {
	case 1: fp.LDR(16, INDEX_UNSIGNED, neonScratchRegD, srcReg, 0); break;
	case 2: fp.LDR(32, INDEX_UNSIGNED, neonScratchRegD, srcReg, 0); break;
	default:
		// For 3, we over read, for over 4, we read more later.
		fp.LDR(64, INDEX_UNSIGNED, neonScratchRegD, srcReg, 0);
		break;
	}
	fp.UXTL(16, neonScratchRegQ, neonScratchRegD);
	fp.UCVTF(32, neonWeightRegsQ[0], neonScratchRegQ, 15);

	if (dec_->nweights > 4) {
		switch (dec_->nweights) {
		case 5: fp.LDR(16, INDEX_UNSIGNED, neonScratchRegD, srcReg, 8); break;
		case 6: fp.LDR(32, INDEX_UNSIGNED, neonScratchRegD, srcReg, 8); break;
		case 7:
		case 8:
			fp.LDR(64, INDEX_UNSIGNED, neonScratchRegD, srcReg, 8);
			break;
		}
		fp.UXTL(16, neonScratchRegQ, neonScratchRegD);
		fp.UCVTF(32, neonWeightRegsQ[1], neonScratchRegQ, 15);
	}
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsFloatSkin() {
	switch (dec_->nweights) {
	case 1:
		fp.LDR(32, INDEX_UNSIGNED, neonWeightRegsQ[0], srcReg, 0);
		break;
	case 2:
		fp.LDR(64, INDEX_UNSIGNED, neonWeightRegsQ[0], srcReg, 0);
		break;
	case 3:
	case 4:
		fp.LDR(128, INDEX_UNSIGNED, neonWeightRegsQ[0], srcReg, 0);
		break;

	case 5:
		fp.LDR(128, INDEX_UNSIGNED, neonWeightRegsQ[0], srcReg, 0);
		fp.LDR(32, INDEX_UNSIGNED, neonWeightRegsQ[1], srcReg, 16);
		break;
	case 6:
		fp.LDR(128, INDEX_UNSIGNED, neonWeightRegsQ[0], srcReg, 0);
		fp.LDR(64, INDEX_UNSIGNED, neonWeightRegsQ[1], srcReg, 16);
		break;
	case 7:
	case 8:
		fp.LDP(128, INDEX_SIGNED, neonWeightRegsQ[0], neonWeightRegsQ[1], srcReg, 0);
		break;
	}
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_Color8888() {
	LDR(INDEX_UNSIGNED, tempReg1, srcReg, dec_->coloff);

	// Set flags to determine if alpha != 0xFF.
	ORN(tempReg2, WZR, tempReg1, ArithOption(tempReg1, ST_ASR, 24));
	CMP(tempReg2, 0);

	// Clear fullAlphaReg when the inverse was not 0.
	// fullAlphaReg = tempReg2 == 0 ? fullAlphaReg : 0 + 1;
	CSEL(fullAlphaReg, fullAlphaReg, WZR, CC_EQ);

	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color4444() {
	LDRH(INDEX_UNSIGNED, tempReg1, srcReg, dec_->coloff);

	// Spread out the components.
	ANDI2R(tempReg2, tempReg1, 0x000F, scratchReg);
	ANDI2R(tempReg3, tempReg1, 0x00F0, scratchReg);
	ORR(tempReg2, tempReg2, tempReg3, ArithOption(tempReg3, ST_LSL, 4));
	ANDI2R(tempReg3, tempReg1, 0x0F00, scratchReg);
	ORR(tempReg2, tempReg2, tempReg3, ArithOption(tempReg3, ST_LSL, 8));
	ANDI2R(tempReg3, tempReg1, 0xF000, scratchReg);
	ORR(tempReg2, tempReg2, tempReg3, ArithOption(tempReg3, ST_LSL, 12));

	// And expand to 8 bits.
	ORR(tempReg1, tempReg2, tempReg2, ArithOption(tempReg2, ST_LSL, 4));

	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.c0off);

	// Set flags to determine if alpha != 0xFF.
	ORN(tempReg2, WZR, tempReg1, ArithOption(tempReg1, ST_ASR, 24));
	CMP(tempReg2, 0);

	// Clear fullAlphaReg when the inverse was not 0.
	// fullAlphaReg = tempReg2 == 0 ? fullAlphaReg : 0 + 1;
	CSEL(fullAlphaReg, fullAlphaReg, WZR, CC_EQ);
}

void VertexDecoderJitCache::Jit_Color565() {
	LDRH(INDEX_UNSIGNED, tempReg1, srcReg, dec_->coloff);

	// Spread out R and B first.  This puts them in 0x001F001F.
	ANDI2R(tempReg2, tempReg1, 0x001F, scratchReg);
	ANDI2R(tempReg3, tempReg1, 0xF800, scratchReg);
	ORR(tempReg2, tempReg2, tempReg3, ArithOption(tempReg3, ST_LSL, 5));

	// Expand 5 -> 8.
	LSL(tempReg3, tempReg2, 3);
	ORR(tempReg2, tempReg3, tempReg2, ArithOption(tempReg2, ST_LSR, 2));
	ANDI2R(tempReg2, tempReg2, 0xFFFF00FF, scratchReg);

	// Now finally G.  We start by shoving it into a wall.
	LSR(tempReg1, tempReg1, 5);
	ANDI2R(tempReg1, tempReg1, 0x003F, scratchReg);
	LSL(tempReg3, tempReg1, 2);
	// Don't worry, shifts into a wall.
	ORR(tempReg3, tempReg3, tempReg1, ArithOption(tempReg1, ST_LSR, 4));
	ORR(tempReg2, tempReg2, tempReg3, ArithOption(tempReg3, ST_LSL, 8));

	// Add in full alpha.  No need to update fullAlphaReg.
	ORRI2R(tempReg1, tempReg2, 0xFF000000, scratchReg);

	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color5551() {
	LDRSH(INDEX_UNSIGNED, tempReg1, srcReg, dec_->coloff);

	ANDI2R(tempReg2, tempReg1, 0x001F, scratchReg);
	ANDI2R(tempReg3, tempReg1, 0x03E0, scratchReg);
	ORR(tempReg2, tempReg2, tempReg3, ArithOption(tempReg3, ST_LSL, 3));
	ANDI2R(tempReg3, tempReg1, 0x7C00, scratchReg);
	ORR(tempReg2, tempReg2, tempReg3, ArithOption(tempReg3, ST_LSL, 6));

	// Expand 5 -> 8.
	LSR(tempReg3, tempReg2, 2);
	// Clean up the bits that were shifted right.
	ANDI2R(tempReg3, tempReg3, ~0x000000F8);
	ANDI2R(tempReg3, tempReg3, ~0x0000F800);
	ORR(tempReg2, tempReg3, tempReg2, ArithOption(tempReg2, ST_LSL, 3));

	// Now we just need alpha.  Since we loaded as signed, it'll be extended.
	ANDI2R(tempReg1, tempReg1, 0xFF000000, scratchReg);
	ORR(tempReg2, tempReg2, tempReg1);
	
	// Set flags to determine if alpha != 0xFF.
	ORN(tempReg3, WZR, tempReg1, ArithOption(tempReg1, ST_ASR, 24));
	CMP(tempReg3, 0);

	STR(INDEX_UNSIGNED, tempReg2, dstReg, dec_->decFmt.c0off);

	// Clear fullAlphaReg when the inverse was not 0.
	// fullAlphaReg = tempReg3 == 0 ? fullAlphaReg : 0 + 1;
	CSEL(fullAlphaReg, fullAlphaReg, WZR, CC_EQ);
}

void VertexDecoderJitCache::Jit_TcU16ThroughToFloat() {
	LDRH(INDEX_UNSIGNED, tempReg1, srcReg, dec_->tcoff);
	LDRH(INDEX_UNSIGNED, tempReg2, srcReg, dec_->tcoff + 2);

	auto updateSide = [&](ARM64Reg src, CCFlags cc, ARM64Reg dst) {
		CMP(src, dst);
		CSEL(dst, src, dst, cc);
	};

	updateSide(tempReg1, CC_LT, boundsMinUReg);
	updateSide(tempReg1, CC_GT, boundsMaxUReg);
	updateSide(tempReg2, CC_LT, boundsMinVReg);
	updateSide(tempReg2, CC_GT, boundsMaxVReg);

	fp.LDUR(32, neonScratchRegD, srcReg, dec_->tcoff);
	fp.UXTL(16, neonScratchRegQ, neonScratchRegD); // Widen to 32-bit
	fp.UCVTF(32, neonScratchRegD, neonScratchRegD);
	fp.STUR(64, neonScratchRegD, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloatThrough() {
	LDP(INDEX_SIGNED, tempReg1, tempReg2, srcReg, dec_->tcoff);
	STP(INDEX_SIGNED, tempReg1, tempReg2, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloat() {
	LDP(INDEX_SIGNED, tempReg1, tempReg2, srcReg, dec_->tcoff);
	STP(INDEX_SIGNED, tempReg1, tempReg2, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU8Prescale() {
	fp.LDUR(16, neonScratchRegD, srcReg, dec_->tcoff);
	fp.UXTL(8, neonScratchRegQ, neonScratchRegD); // Widen to 16-bit
	fp.UXTL(16, neonScratchRegQ, neonScratchRegD); // Widen to 32-bit
	fp.UCVTF(32, neonScratchRegD, neonScratchRegD);
	fp.FMUL(32, neonScratchRegD, neonScratchRegD, neonUVScaleReg);  // TODO: FMLA
	fp.FADD(32, neonScratchRegD, neonScratchRegD, neonUVOffsetReg);
	fp.STUR(64, neonScratchRegD, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU8ToFloat() {
	fp.LDUR(16, neonScratchRegD, srcReg, dec_->tcoff);
	fp.UXTL(8, neonScratchRegQ, neonScratchRegD); // Widen to 16-bit
	fp.UXTL(16, neonScratchRegQ, neonScratchRegD); // Widen to 32-bit
	fp.UCVTF(32, neonScratchRegD, neonScratchRegD, 7);
	fp.STUR(64, neonScratchRegD, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU16Prescale() {
	fp.LDUR(32, neonScratchRegD, srcReg, dec_->tcoff);
	fp.UXTL(16, neonScratchRegQ, neonScratchRegD); // Widen to 32-bit
	fp.UCVTF(32, neonScratchRegD, neonScratchRegD);
	fp.FMUL(32, neonScratchRegD, neonScratchRegD, neonUVScaleReg);  // TODO: FMLA
	fp.FADD(32, neonScratchRegD, neonScratchRegD, neonUVOffsetReg);
	fp.STUR(64, neonScratchRegD, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU16ToFloat() {
	fp.LDUR(32, neonScratchRegD, srcReg, dec_->tcoff);
	fp.UXTL(16, neonScratchRegQ, neonScratchRegD); // Widen to 32-bit
	fp.UCVTF(32, neonScratchRegD, neonScratchRegD, 15);
	fp.STUR(64, neonScratchRegD, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloatPrescale() {
	fp.LDUR(64, neonScratchRegD, srcReg, dec_->tcoff);
	fp.FMUL(32, neonScratchRegD, neonScratchRegD, neonUVScaleReg);  // TODO: FMLA
	fp.FADD(32, neonScratchRegD, neonScratchRegD, neonUVOffsetReg);
	fp.STUR(64, neonScratchRegD, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_PosS8() {
	Jit_AnyS8ToFloat(dec_->posoff);
	fp.STUR(128, srcQ[0], dstReg, dec_->decFmt.posoff);
}

void VertexDecoderJitCache::Jit_PosS16() {
	Jit_AnyS16ToFloat(dec_->posoff);
	fp.STUR(128, srcQ[0], dstReg, dec_->decFmt.posoff);
}

void VertexDecoderJitCache::Jit_PosFloat() {
	// Only need to copy 12 bytes, but copying 16 should be okay (and is faster.)
	if ((dec_->posoff & 7) == 0 && (dec_->decFmt.posoff & 7) == 0) {
		LDP(INDEX_SIGNED, EncodeRegTo64(tempReg1), EncodeRegTo64(tempReg2), srcReg, dec_->posoff);
		STP(INDEX_SIGNED, EncodeRegTo64(tempReg1), EncodeRegTo64(tempReg2), dstReg, dec_->decFmt.posoff);
	} else {
		LDP(INDEX_SIGNED, tempReg1, tempReg2, srcReg, dec_->posoff);
		STP(INDEX_SIGNED, tempReg1, tempReg2, dstReg, dec_->decFmt.posoff);
		LDR(INDEX_UNSIGNED, tempReg3, srcReg, dec_->posoff + 8);
		STR(INDEX_UNSIGNED, tempReg3, dstReg, dec_->decFmt.posoff + 8);
	}
}

void VertexDecoderJitCache::Jit_PosS8Through() {
	LDRSB(INDEX_UNSIGNED, tempReg1, srcReg, dec_->posoff);
	LDRSB(INDEX_UNSIGNED, tempReg2, srcReg, dec_->posoff + 1);
	LDRSB(INDEX_UNSIGNED, tempReg3, srcReg, dec_->posoff + 2);  // signed?
	fp.SCVTF(fpScratchReg, tempReg1);
	fp.SCVTF(fpScratchReg2, tempReg2);
	fp.SCVTF(fpScratchReg3, tempReg3);
	STR(INDEX_UNSIGNED, fpScratchReg, dstReg, dec_->decFmt.posoff);
	STR(INDEX_UNSIGNED, fpScratchReg2, dstReg, dec_->decFmt.posoff + 4);
	STR(INDEX_UNSIGNED, fpScratchReg3, dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosS16Through() {
	// Start with X and Y (which is signed.)
	fp.LDUR(32, src[0], srcReg, dec_->posoff);
	fp.SXTL(16, srcD[0], src[0]);
	fp.SCVTF(32, srcD[0], srcD[0]);
	fp.STUR(64, src[0], dstReg, dec_->decFmt.posoff);
	// Now load in Z (which is unsigned.)
	LDRH(INDEX_UNSIGNED, tempReg3, srcReg, dec_->posoff + 4);
	fp.SCVTF(src[1], tempReg3);
	STR(INDEX_UNSIGNED, src[1], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_NormalS8() {
	LDURH(tempReg1, srcReg, dec_->nrmoff);
	LDRB(INDEX_UNSIGNED, tempReg3, srcReg, dec_->nrmoff + 2);
	ORR(tempReg1, tempReg1, tempReg3, ArithOption(tempReg3, ST_LSL, 16));
	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.nrmoff);
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16() {
	// NOTE: Not LDRH, we just copy the raw bytes here.
	LDUR(tempReg1, srcReg, dec_->nrmoff);
	LDRH(INDEX_UNSIGNED, tempReg2, srcReg, dec_->nrmoff + 4);
	STP(INDEX_SIGNED, tempReg1, tempReg2, dstReg, dec_->decFmt.nrmoff);
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	// Only need to copy 12 bytes, but copying 16 should be okay (and is faster.)
	if ((dec_->posoff & 7) == 0 && (dec_->decFmt.posoff & 7) == 0) {
		LDP(INDEX_SIGNED, EncodeRegTo64(tempReg1), EncodeRegTo64(tempReg2), srcReg, dec_->nrmoff);
		STP(INDEX_SIGNED, EncodeRegTo64(tempReg1), EncodeRegTo64(tempReg2), dstReg, dec_->decFmt.nrmoff);
	} else {
		LDP(INDEX_SIGNED, tempReg1, tempReg2, srcReg, dec_->nrmoff);
		STP(INDEX_SIGNED, tempReg1, tempReg2, dstReg, dec_->decFmt.nrmoff);
		LDR(INDEX_UNSIGNED, tempReg3, srcReg, dec_->nrmoff + 8);
		STR(INDEX_UNSIGNED, tempReg3, dstReg, dec_->decFmt.nrmoff + 8);
	}
}

void VertexDecoderJitCache::Jit_NormalS8Skin() {
	Jit_AnyS8ToFloat(dec_->nrmoff);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalS16Skin() {
	Jit_AnyS16ToFloat(dec_->nrmoff);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalFloatSkin() {
	fp.LDUR(128, srcQ[0], srcReg, dec_->nrmoff);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_PosS8Skin() {
	Jit_AnyS8ToFloat(dec_->posoff);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosS16Skin() {
	Jit_AnyS16ToFloat(dec_->posoff);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosFloatSkin() {
	fp.LDUR(128, srcQ[0], srcReg, dec_->posoff);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_AnyS8ToFloat(int srcoff) {
	fp.LDUR(32, src[0], srcReg, srcoff);
	fp.SXTL(8, srcD[0], src[0]);
	fp.SXTL(16, srcQ[0], srcD[0]);
	fp.SCVTF(32, srcQ[0], srcQ[0], 7);
}

void VertexDecoderJitCache::Jit_AnyS16ToFloat(int srcoff) {
	fp.LDUR(64, src[0], srcReg, srcoff);
	fp.SXTL(16, srcQ[0], srcD[0]);
	fp.SCVTF(32, srcQ[0], srcQ[0], 15);
}

void VertexDecoderJitCache::Jit_WriteMatrixMul(int outOff, bool pos) {
	// Multiply with the matrix sitting in Q4-Q7.
	fp.FMUL(32, accNEON, Q4, srcQ[0], 0);
	fp.FMLA(32, accNEON, Q5, srcQ[0], 1);
	fp.FMLA(32, accNEON, Q6, srcQ[0], 2);
	if (pos) {
		fp.FADD(32, accNEON, accNEON, Q7);
	}
	fp.STUR(128, accNEON, dstReg, outOff);
}

#endif // PPSSPP_ARCH(ARM64)
