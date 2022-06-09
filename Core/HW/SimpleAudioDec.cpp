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

#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Config.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/BufferQueue.h"

#ifdef USE_FFMPEG

extern "C" {
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/samplefmt.h"
}

#endif  // USE_FFMPEG

int SimpleAudio::GetAudioCodecID(int audioType) {
#ifdef USE_FFMPEG
	switch (audioType) {
	case PSP_CODEC_AAC:
		return AV_CODEC_ID_AAC;
	case PSP_CODEC_AT3:
		return AV_CODEC_ID_ATRAC3;
	case PSP_CODEC_AT3PLUS:
		return AV_CODEC_ID_ATRAC3P;
	case PSP_CODEC_MP3:
		return AV_CODEC_ID_MP3;
	default:
		return AV_CODEC_ID_NONE;
	}
#else
	return 0;
#endif // USE_FFMPEG
}

SimpleAudio::SimpleAudio(int audioType, int sample_rate, int channels)
: ctxPtr(0xFFFFFFFF), audioType(audioType), sample_rate_(sample_rate), channels_(channels),
  outSamples(0), srcPos(0), wanted_resample_freq(44100), frame_(0), codec_(0), codecCtx_(0), swrCtx_(0),
  codecOpen_(false) {
	Init();
}

void SimpleAudio::Init() {
#ifdef USE_FFMPEG
	avcodec_register_all();
	av_register_all();
	InitFFmpeg();

	frame_ = av_frame_alloc();

	// Get Audio Codec ctx
	int audioCodecId = GetAudioCodecID(audioType);
	if (!audioCodecId) {
		ERROR_LOG(ME, "This version of FFMPEG does not support Audio codec type: %08x. Update your submodule.", audioType);
		return;
	}
	// Find decoder
	codec_ = avcodec_find_decoder((AVCodecID)audioCodecId);
	if (!codec_) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(ME, "This version of FFMPEG does not support AV_CODEC_ctx for audio (%s). Update your submodule.", GetCodecName(audioType));
		return;
	}
	// Allocate codec context
	codecCtx_ = avcodec_alloc_context3(codec_);
	if (!codecCtx_) {
		ERROR_LOG(ME, "Failed to allocate a codec context");
		return;
	}
	codecCtx_->channels = channels_;
	codecCtx_->channel_layout = channels_ == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
	codecCtx_->sample_rate = sample_rate_;
	codecOpen_ = false;
#endif  // USE_FFMPEG
}

bool SimpleAudio::OpenCodec(int block_align) {
#ifdef USE_FFMPEG
	// Some versions of FFmpeg require this set.  May be set in SetExtraData(), but optional.
	// When decoding, we decode by packet, so we know the size.
	if (codecCtx_->block_align == 0) {
		codecCtx_->block_align = block_align;
	}

	AVDictionary *opts = 0;
	int retval = avcodec_open2(codecCtx_, codec_, &opts);
	if (retval < 0) {
		ERROR_LOG(ME, "Failed to open codec: retval = %i", retval);
	}
	av_dict_free(&opts);
	codecOpen_ = true;
	return retval >= 0;
#else
	return false;
#endif  // USE_FFMPEG
}

void SimpleAudio::SetExtraData(u8 *data, int size, int wav_bytes_per_packet) {
#ifdef USE_FFMPEG
	if (codecCtx_) {
		codecCtx_->extradata = (uint8_t *)av_mallocz(size);
		codecCtx_->extradata_size = size;
		codecCtx_->block_align = wav_bytes_per_packet;
		codecOpen_ = false;

		if (data != nullptr) {
			memcpy(codecCtx_->extradata, data, size);
		}
	}
#endif
}

void SimpleAudio::SetChannels(int channels) {
	if (channels_ == channels) {
		// Do nothing, already set.
		return;
	}
#ifdef USE_FFMPEG

	if (codecOpen_) {
		ERROR_LOG(ME, "Codec already open, cannot change channels");
	} else {
		channels_ = channels;
		codecCtx_->channels = channels_;
		codecCtx_->channel_layout = channels_ == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
	}
#endif
}

SimpleAudio::~SimpleAudio() {
#ifdef USE_FFMPEG
	swr_free(&swrCtx_);
	av_frame_free(&frame_);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55, 52, 0)
	avcodec_free_context(&codecCtx_);
#else
	// Future versions may add other things to free, but avcodec_free_context didn't exist yet here.
	avcodec_close(codecCtx_);
	av_freep(&codecCtx_->extradata);
	av_freep(&codecCtx_->subtitle_header);
	av_freep(&codecCtx_);
#endif
	codec_ = 0;
#endif  // USE_FFMPEG
}

bool SimpleAudio::IsOK() const {
#ifdef USE_FFMPEG
	return codec_ != 0;
#else
	return 0;
#endif
}

bool SimpleAudio::Decode(void *inbuf, int inbytes, uint8_t *outbuf, int *outbytes) {
#ifdef USE_FFMPEG
	if (!codecOpen_) {
		OpenCodec(inbytes);
	}

	AVPacket packet;
	av_init_packet(&packet);
	packet.data = static_cast<uint8_t *>(inbuf);
	packet.size = inbytes;

	int got_frame = 0;
	av_frame_unref(frame_);

	*outbytes = 0;
	srcPos = 0;
	int len = avcodec_decode_audio4(codecCtx_, frame_, &got_frame, &packet);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
	av_packet_unref(&packet);
#else
	av_free_packet(&packet);
#endif

	if (len < 0) {
		ERROR_LOG(ME, "Error decoding Audio frame (%i bytes): %i (%08x)", inbytes, len, len);
		return false;
	}
	
	// get bytes consumed in source
	srcPos = len;

	if (got_frame) {
		// Initializing the sample rate convert. We will use it to convert float output into int.
		int64_t wanted_channel_layout = AV_CH_LAYOUT_STEREO; // we want stereo output layout
		int64_t dec_channel_layout = frame_->channel_layout; // decoded channel layout

		if (!swrCtx_) {
			swrCtx_ = swr_alloc_set_opts(
				swrCtx_,
				wanted_channel_layout,
				AV_SAMPLE_FMT_S16,
				wanted_resample_freq,
				dec_channel_layout,
				codecCtx_->sample_fmt,
				codecCtx_->sample_rate,
				0,
				NULL);

			if (!swrCtx_ || swr_init(swrCtx_) < 0) {
				ERROR_LOG(ME, "swr_init: Failed to initialize the resampling context");
				avcodec_close(codecCtx_);
				codec_ = 0;
				return false;
			}
		}

		// convert audio to AV_SAMPLE_FMT_S16
		int swrRet = swr_convert(swrCtx_, &outbuf, frame_->nb_samples, (const u8 **)frame_->extended_data, frame_->nb_samples);
		if (swrRet < 0) {
			ERROR_LOG(ME, "swr_convert: Error while converting: %d", swrRet);
			return false;
		}
		// output samples per frame, we should *2 since we have two channels
		outSamples = swrRet * 2;

		// each sample occupies 2 bytes
		*outbytes = outSamples * 2;

		// Save outbuf into pcm audio, you can uncomment this line to save and check the decoded audio into pcm file.
		// SaveAudio("dump.pcm", outbuf, *outbytes);
	}
	return true;
#else
	// Zero bytes output. No need to memset.
	*outbytes = 0;
	return true;
#endif  // USE_FFMPEG
}

int SimpleAudio::GetOutSamples() {
	return outSamples;
}

int SimpleAudio::GetSourcePos() {
	return srcPos;
}

void AudioClose(SimpleAudio **ctx) {
#ifdef USE_FFMPEG
	delete *ctx;
	*ctx = 0;
#endif  // USE_FFMPEG
}


static const char *const codecNames[4] = {
	"AT3+", "AT3", "MP3", "AAC",
};

const char *GetCodecName(int codec) {
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return codecNames[codec - PSP_CODEC_AT3PLUS];
	} else {
		return "(unk)";
	}
};

bool IsValidCodec(int codec){
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return true;
	}
	return false;
}


// sceAu module starts from here

AuCtx::AuCtx() {
	decoder = NULL;
	startPos = 0;
	endPos = 0;
	LoopNum = -1;
	AuBuf = 0;
	AuBufSize = 2048;
	PCMBuf = 0;
	PCMBufSize = 2048;
	AuBufAvailable = 0;
	SumDecodedSamples = 0;
	askedReadSize = 0;
	audioType = 0;
	FrameNum = 0;
};

AuCtx::~AuCtx(){
	if (decoder){
		AudioClose(&decoder);
		decoder = NULL;
	}
};

size_t AuCtx::FindNextMp3Sync() {
	if (audioType != PSP_CODEC_MP3) {
		return 0;
	}

	for (size_t i = 0; i < sourcebuff.size() - 2; ++i) {
		if ((sourcebuff[i] & 0xFF) == 0xFF && (sourcebuff[i + 1] & 0xC0) == 0xC0) {
			return i;
		}
	}
	return 0;
}

// return output pcm size, <0 error
u32 AuCtx::AuDecode(u32 pcmAddr) {
	if (!Memory::IsValidAddress(pcmAddr)){
		ERROR_LOG(ME, "%s: output bufferAddress %08x is invalctx", __FUNCTION__, pcmAddr);
		return -1;
	}

	auto outbuf = Memory::GetPointer(PCMBuf);
	int outpcmbufsize = 0;

	// Decode a single frame in sourcebuff and output into PCMBuf.
	if (!sourcebuff.empty()) {
		// FFmpeg doesn't seem to search for a sync for us, so let's do that.
		int nextSync = (int)FindNextMp3Sync();
		decoder->Decode(&sourcebuff[nextSync], (int)sourcebuff.size() - nextSync, outbuf, &outpcmbufsize);		
		ToLEndian((u16*)outbuf, outpcmbufsize / 2);

		if (outpcmbufsize == 0) {
			// Nothing was output, hopefully we're at the end of the stream.
			AuBufAvailable = 0;
			sourcebuff.clear();
		} else {
			// Update our total decoded samples, but don't count stereo.
			SumDecodedSamples += decoder->GetOutSamples() / 2;
			// get consumed source length
			int srcPos = decoder->GetSourcePos() + nextSync;
			// remove the consumed source
			if (srcPos > 0)
				sourcebuff.erase(sourcebuff.begin(), sourcebuff.begin() + srcPos);
			// reduce the available Aubuff size
			// (the available buff size is now used to know if we can read again from file and how many to read)
			AuBufAvailable -= srcPos;
		}
	}

	bool end = readPos - AuBufAvailable >= (int64_t)endPos;
	if (end && LoopNum != 0) {
		// When looping, start the sum back off at zero and reset readPos to the start.
		SumDecodedSamples = 0;
		readPos = startPos;
		if (LoopNum > 0)
			LoopNum--;
	}

	if (outpcmbufsize == 0 && !end) {
		outpcmbufsize = MaxOutputSample * 4;
		memset(outbuf, 0, PCMBufSize);
	} else if ((u32)outpcmbufsize < PCMBufSize) {
		// TODO: We probably should use a rolling buffer instead.
		memset(outbuf + outpcmbufsize, 0, PCMBufSize - outpcmbufsize);
	}

	Memory::Write_U32(PCMBuf, pcmAddr);
	return outpcmbufsize;
}

u32 AuCtx::AuGetLoopNum()
{
	return LoopNum;
}

u32 AuCtx::AuSetLoopNum(int loop)
{
	LoopNum = loop;
	return 0;
}

// return 1 to read more data stream, 0 don't read
int AuCtx::AuCheckStreamDataNeeded() {
	// If we would ask for bytes, then some are needed.
	if (AuStreamBytesNeeded() != 0) {
		return 1;
	}
	return 0;
}

int AuCtx::AuStreamBytesNeeded() {
	if (audioType == PSP_CODEC_MP3) {
		// The endPos and readPos are not considered, except when you've read to the end.
		if (readPos >= endPos)
			return 0;
		// Account for the workarea.
		int offset = AuStreamWorkareaSize();
		return (int)AuBufSize - AuBufAvailable - offset;
	}

	// TODO: Untested.  Maybe similar to MP3.
	return std::min((int)AuBufSize - AuBufAvailable, (int)endPos - readPos);
}

int AuCtx::AuStreamWorkareaSize() {
	// Note that this is 31 bytes more than the max layer 3 frame size.
	if (audioType == PSP_CODEC_MP3)
		return 0x05c0;
	return 0;
}

// check how many bytes we have read from source file
u32 AuCtx::AuNotifyAddStreamData(int size) {
	int offset = AuStreamWorkareaSize();

	if (askedReadSize != 0) {
		// Old save state, numbers already adjusted.
		int diffsize = size - askedReadSize;
		// Notify the real read size
		if (diffsize != 0) {
			readPos += diffsize;
			AuBufAvailable += diffsize;
		}
		askedReadSize = 0;
	} else {
		readPos += size;
		AuBufAvailable += size;
	}

	if (Memory::IsValidRange(AuBuf, size)) {
		sourcebuff.resize(sourcebuff.size() + size);
		Memory::MemcpyUnchecked(&sourcebuff[sourcebuff.size() - size], AuBuf + offset, size);
	}

	return 0;
}

// read from stream position srcPos of size bytes into buff
// buff, size and srcPos are all pointers
u32 AuCtx::AuGetInfoToAddStreamData(u32 bufPtr, u32 sizePtr, u32 srcPosPtr) {
	int readsize = AuStreamBytesNeeded();
	int offset = AuStreamWorkareaSize();

	// we can recharge AuBuf from its beginning
	if (readsize != 0) {
		if (Memory::IsValidAddress(bufPtr))
			Memory::Write_U32(AuBuf + offset, bufPtr);
		if (Memory::IsValidAddress(sizePtr))
			Memory::Write_U32(readsize, sizePtr);
		if (Memory::IsValidAddress(srcPosPtr))
			Memory::Write_U32(readPos, srcPosPtr);
	} else {
		if (Memory::IsValidAddress(bufPtr))
			Memory::Write_U32(0, bufPtr);
		if (Memory::IsValidAddress(sizePtr))
			Memory::Write_U32(0, sizePtr);
		if (Memory::IsValidAddress(srcPosPtr))
			Memory::Write_U32(0, srcPosPtr);
	}

	// Just for old save states.
	askedReadSize = 0;
	return 0;
}

u32 AuCtx::AuResetPlayPositionByFrame(int frame) {
	// Note: this doesn't correctly handle padding or slot size, but the PSP doesn't either.
	uint32_t bytesPerSecond = (MaxOutputSample / 8) * BitRate * 1000;
	readPos = startPos + (frame * bytesPerSecond) / SamplingRate;
	// Not sure why, but it seems to consistently seek 1 before, maybe in case it's off slightly.
	if (frame != 0)
		readPos -= 1;
	SumDecodedSamples = frame * MaxOutputSample;
	AuBufAvailable = 0;
	sourcebuff.clear();
	return 0;
}

u32 AuCtx::AuResetPlayPosition() {
	readPos = startPos;
	SumDecodedSamples = 0;
	AuBufAvailable = 0;
	sourcebuff.clear();
	return 0;
}

void AuCtx::DoState(PointerWrap &p) {
	auto s = p.Section("AuContext", 0, 1);
	if (!s)
		return;

	Do(p, startPos);
	Do(p, endPos);
	Do(p, AuBuf);
	Do(p, AuBufSize);
	Do(p, PCMBuf);
	Do(p, PCMBufSize);
	Do(p, freq);
	Do(p, SumDecodedSamples);
	Do(p, LoopNum);
	Do(p, Channels);
	Do(p, MaxOutputSample);
	Do(p, readPos);
	Do(p, audioType);
	Do(p, BitRate);
	Do(p, SamplingRate);
	Do(p, askedReadSize);
	int dummy = 0;
	Do(p, dummy);
	Do(p, FrameNum);

	if (p.mode == p.MODE_READ) {
		decoder = new SimpleAudio(audioType);
		AuBufAvailable = 0; // reset to read from file at position readPos
	}
}
