#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <common.h>
#include <pspkernel.h>

extern "C" int sceDmacMemcpy(void *dest, const void *source, unsigned int size);

#define BUF_WIDTH 512
#define SCR_WIDTH 480
#define SCR_HEIGHT 272

unsigned int __attribute__((aligned(16))) list[262144];
unsigned int __attribute__((aligned(16))) clutPattern[] = {
	0x00000000, 0x01010101, 0x02020202, 0x03030303, 0x04040404, 0x05050505, 0x06060606, 0x07070707,
	0x08080808, 0x09090909, 0x0A0A0A0A, 0x0B0B0B0B, 0x0C0C0C0C, 0x0D0D0D0D, 0x0E0E0E0E, 0x0F0F0F0F,
	0x10101010, 0x11111111, 0x12121212, 0x13131313, 0x14141414, 0x15151515, 0x16161616, 0x17171717,
	0x18181818, 0x19191919, 0x1A1A1A1A, 0x1B1B1B1B, 0x1C1C1C1C, 0x1D1D1D1D, 0x1E1E1E1E, 0x1F1F1F1F,
	0x20202020, 0x21212121, 0x22222222, 0x23232323, 0x24242424, 0x25252525, 0x26262626, 0x27272727,
	0x28282828, 0x29292929, 0x2A2A2A2A, 0x2B2B2B2B, 0x2C2C2C2C, 0x2D2D2D2D, 0x2E2E2E2E, 0x2F2F2F2F,
	0x30303030, 0x31313131, 0x32323232, 0x33333333, 0x34343434, 0x35353535, 0x36363636, 0x37373737,
	0x38383838, 0x39393939, 0x3A3A3A3A, 0x3B3B3B3B, 0x3C3C3C3C, 0x3D3D3D3D, 0x3E3E3E3E, 0x3F3F3F3F,
	0x40404040, 0x41414141, 0x42424242, 0x43434343, 0x44444444, 0x45454545, 0x46464646, 0x47474747,
	0x48484848, 0x49494949, 0x4A4A4A4A, 0x4B4B4B4B, 0x4C4C4C4C, 0x4D4D4D4D, 0x4E4E4E4E, 0x4F4F4F4F,
	0x50505050, 0x51515151, 0x52525252, 0x53535353, 0x54545454, 0x55555555, 0x56565656, 0x57575757,
	0x58585858, 0x59595959, 0x5A5A5A5A, 0x5B5B5B5B, 0x5C5C5C5C, 0x5D5D5D5D, 0x5E5E5E5E, 0x5F5F5F5F,
	0x60606060, 0x61616161, 0x62626262, 0x63636363, 0x64646464, 0x65656565, 0x66666666, 0x67676767,
	0x68686868, 0x69696969, 0x6A6A6A6A, 0x6B6B6B6B, 0x6C6C6C6C, 0x6D6D6D6D, 0x6E6E6E6E, 0x6F6F6F6F,
	0x70707070, 0x71717171, 0x72727272, 0x73737373, 0x74747474, 0x75757575, 0x76767676, 0x77777777,
	0x78787878, 0x79797979, 0x7A7A7A7A, 0x7B7B7B7B, 0x7C7C7C7C, 0x7D7D7D7D, 0x7E7E7E7E, 0x7F7F7F7F,
	0x80808080, 0x81818181, 0x82828282, 0x83838383, 0x84848484, 0x85858585, 0x86868686, 0x87878787,
	0x88888888, 0x89898989, 0x8A8A8A8A, 0x8B8B8B8B, 0x8C8C8C8C, 0x8D8D8D8D, 0x8E8E8E8E, 0x8F8F8F8F,
	0x90909090, 0x91919191, 0x92929292, 0x93939393, 0x94949494, 0x95959595, 0x96969696, 0x97979797,
	0x98989898, 0x99999999, 0x9A9A9A9A, 0x9B9B9B9B, 0x9C9C9C9C, 0x9D9D9D9D, 0x9E9E9E9E, 0x9F9F9F9F,
	0xA0A0A0A0, 0xA1A1A1A1, 0xA2A2A2A2, 0xA3A3A3A3, 0xA4A4A4A4, 0xA5A5A5A5, 0xA6A6A6A6, 0xA7A7A7A7,
	0xA8A8A8A8, 0xA9A9A9A9, 0xAAAAAAAA, 0xABABABAB, 0xACACACAC, 0xADADADAD, 0xAEAEAEAE, 0xAFAFAFAF,
	0xB0B0B0B0, 0xB1B1B1B1, 0xB2B2B2B2, 0xB3B3B3B3, 0xB4B4B4B4, 0xB5B5B5B5, 0xB6B6B6B6, 0xB7B7B7B7,
	0xB8B8B8B8, 0xB9B9B9B9, 0xBABABABA, 0xBBBBBBBB, 0xBCBCBCBC, 0xBDBDBDBD, 0xBEBEBEBE, 0xBFBFBFBF,
	0xC0C0C0C0, 0xC1C1C1C1, 0xC2C2C2C2, 0xC3C3C3C3, 0xC4C4C4C4, 0xC5C5C5C5, 0xC6C6C6C6, 0xC7C7C7C7,
	0xC8C8C8C8, 0xC9C9C9C9, 0xCACACACA, 0xCBCBCBCB, 0xCCCCCCCC, 0xCDCDCDCD, 0xCECECECE, 0xCFCFCFCF,
	0xD0D0D0D0, 0xD1D1D1D1, 0xD2D2D2D2, 0xD3D3D3D3, 0xD4D4D4D4, 0xD5D5D5D5, 0xD6D6D6D6, 0xD7D7D7D7,
	0xD8D8D8D8, 0xD9D9D9D9, 0xDADADADA, 0xDBDBDBDB, 0xDCDCDCDC, 0xDDDDDDDD, 0xDEDEDEDE, 0xDFDFDFDF,
	0xE0E0E0E0, 0xE1E1E1E1, 0xE2E2E2E2, 0xE3E3E3E3, 0xE4E4E4E4, 0xE5E5E5E5, 0xE6E6E6E6, 0xE7E7E7E7,
	0xE8E8E8E8, 0xE9E9E9E9, 0xEAEAEAEA, 0xEBEBEBEB, 0xECECECEC, 0xEDEDEDED, 0xEEEEEEEE, 0xEFEFEFEF,
	0xF0F0F0F0, 0xF1F1F1F1, 0xF2F2F2F2, 0xF3F3F3F3, 0xF4F4F4F4, 0xF5F5F5F5, 0xF6F6F6F6, 0xF7F7F7F7,
	0xF8F8F8F8, 0xF9F9F9F9, 0xFAFAFAFA, 0xFBFBFBFB, 0xFCFCFCFC, 0xFDFDFDFD, 0xFEFEFEFE, 0xFFFFFFFF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
};
unsigned char __attribute__((aligned(16))) imageData0[16] = {0};
unsigned char __attribute__((aligned(16))) imageData1[16] = {1};
unsigned char __attribute__((aligned(16))) imageData15[16] = {15};
unsigned char __attribute__((aligned(16))) imageData16[16] = {16};
unsigned char __attribute__((aligned(16))) imageData255[16] = {255};

typedef struct {
	unsigned short u, v;
	unsigned short x, y, z;
} Vertex;

Vertex vertices1[2] = { {0, 0, 10, 10, 0}, {1, 1, 470, 262, 0} };
Vertex vertices2[2] = { {0, 0, 0, 0, 0}, {480, 272, 480, 272, 0} };

static u32 copybuf[512 * 272];
u16 *copybuf16 = (u16 *)copybuf;
u32 *drawbuf;

extern int HAS_DISPLAY;

void displayBuffer(const char *reason) {
	sceKernelDcacheWritebackInvalidateAll();
	sceDmacMemcpy(copybuf, drawbuf, sizeof(copybuf));
	sceKernelDcacheWritebackInvalidateAll();
	const u32 *buf = copybuf;

	checkpoint(NULL);
	schedf("%s: ", reason);
	// This prevents drawing to the screen, which makes the test faster.
	HAS_DISPLAY = 0;
	for (int y = 0; y < 1; ++y) {
		for (int x = 0; x < 1; ++x) {
			// For the purposes of this test, ignore alpha.
			schedf("%06x", buf[y * 512 + x] & 0x00FFFFFF);
		}
		schedf("\n");
		flushschedf();
	}
	HAS_DISPLAY = 1;

	// Reset.
	memset(copybuf, 0, sizeof(copybuf));
	sceKernelDcacheWritebackInvalidateAll();
	sceDmacMemcpy(drawbuf, copybuf, sizeof(copybuf));
	sceKernelDcacheWritebackInvalidateAll();
}

void drawTexFlush(int width, int height, int stride, int texfmt, const void *tex, const void *clut, int clutfmt, int blocks, const void *verts, int indexOffset) {
	sceGuStart(GU_DIRECT, list);

	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(texfmt, 0, 0, GU_FALSE);
	sceGuTexFunc(GU_TFX_DECAL, GU_TCC_RGB);
	sceGuTexImage(0, width, height, stride, tex);

	sceGuClutLoad(blocks, clut);
	sceGuClutMode(clutfmt, 0, 0xFF, indexOffset);
	sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, verts);

	sceGuFinish();
	sceGuSync(0, 0);
}

void drawWithIndexOffsetsFmt(int clutfmt) {
	static const int offsets[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15, 16, 17, 21, 30, 31};
	for (size_t i = 0; i < ARRAY_SIZE(offsets); ++i) {
		char temp[128];
		snprintf(temp, sizeof(temp), "  Index offset %d (0)", offsets[i]);

		drawTexFlush(1, 1, 16, GU_PSM_T32, imageData0, clutPattern, clutfmt, 0xFF, vertices2, offsets[i]);
		sceDisplayWaitVblank();
		displayBuffer(temp);

		snprintf(temp, sizeof(temp), "  Index offset %d (1)", offsets[i]);

		drawTexFlush(1, 1, 16, GU_PSM_T32, imageData1, clutPattern, clutfmt, 0xFF, vertices2, offsets[i]);
		sceDisplayWaitVblank();
		displayBuffer(temp);

		snprintf(temp, sizeof(temp), "  Index offset %d (15)", offsets[i]);

		drawTexFlush(1, 1, 16, GU_PSM_T32, imageData15, clutPattern, clutfmt, 0xFF, vertices2, offsets[i]);
		sceDisplayWaitVblank();
		displayBuffer(temp);

		snprintf(temp, sizeof(temp), "  Index offset %d (16)", offsets[i]);

		drawTexFlush(1, 1, 16, GU_PSM_T32, imageData16, clutPattern, clutfmt, 0xFF, vertices2, offsets[i]);
		sceDisplayWaitVblank();
		displayBuffer(temp);

		snprintf(temp, sizeof(temp), "  Index offset %d (255)", offsets[i]);

		drawTexFlush(1, 1, 16, GU_PSM_T32, imageData255, clutPattern, clutfmt, 0xFF, vertices2, offsets[i]);
		sceDisplayWaitVblank();
		displayBuffer(temp);

		if (i < ARRAY_SIZE(offsets) - 1) {
			schedf("\n");
		}
	}
}

void drawWithIndexOffsets() {
	sceDisplaySetMode(0, SCR_WIDTH, SCR_HEIGHT);

	sceGuStart(GU_DIRECT, list);
	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	sceGuFinish();
	sceGuSync(0, 0);

	checkpointNext("Index offsets 8888:");
	drawWithIndexOffsetsFmt(GU_PSM_8888);
	schedf("\n");

	checkpointNext("Index offsets 4444:");
	drawWithIndexOffsetsFmt(GU_PSM_4444);
}

void init() {
	void *fbp0 = 0;

	drawbuf = (u32 *)sceGeEdramGetAddr();

	sceGuInit();
	sceGuStart(GU_DIRECT, list);
	sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_WIDTH);
	sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, fbp0, BUF_WIDTH);
	sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuFinish();
	sceGuSync(0, 0);

	sceDisplayWaitVblankStart();
	sceGuDisplay(1);

	memset(copybuf, 0, sizeof(copybuf));
	sceKernelDcacheWritebackInvalidateAll();
	sceDmacMemcpy(drawbuf, copybuf, sizeof(copybuf));
	sceKernelDcacheWritebackInvalidateAll();
}

extern "C" int main(int argc, char *argv[]) {
	init();
	drawWithIndexOffsets();
	sceGuTerm();

	return 0;
}
