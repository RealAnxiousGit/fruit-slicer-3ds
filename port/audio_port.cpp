// Nintendo 3DS sound: the DSP through ndsp.
#include <stdio.h>
#include <string.h>

#include "audio_port.h"

namespace audioport {
static constexpr int kMusicChannel = 0;
static constexpr int kFuseChannel = 1;
static constexpr int kFirstSfx = 2;
static constexpr int kSfxChannels = 12;

static ndspWaveBuf sSfxBuf[kSfxChannels];
static ndspWaveBuf sMusicBuf;
static ndspWaveBuf sFuseBuf;
static int sNext;
static float sMusicVolume;
static float sMusicRate = 32000, sMusicSpeed = 1;

bool init() {
	Result rc = ndspInit();
	if (R_FAILED(rc)) {
		// Real hardware needs sdmc:/3ds/dspfirm.cdc; the game stays playable without sound.
		fprintf(stderr, "[audio] ndspInit failed: %08lX\n", (unsigned long)rc);
		return false;
	}
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	return true;
}

void shutdown() {
	ndspChnWaveBufClear(kMusicChannel);
	ndspChnWaveBufClear(kFuseChannel);
	for (int i = 0; i < kSfxChannels; i++) ndspChnWaveBufClear(kFirstSfx + i);
	ndspExit();
}

s16* alloc(u32 bytes) { return (s16*)linearAlloc(bytes); }
void release(s16* p) { linearFree(p); }
void ready(const s16* p, u32 bytes) { DSP_FlushDataCache(p, bytes); }
void settle() { svcSleepThread(10000000); }

void playSfx(const Clip& clip, float left, float right, float rate) {
	int slot = -1;
	for (int i = 0; i < kSfxChannels; i++) {
		int cand = (sNext + i) % kSfxChannels;
		if (sSfxBuf[cand].status != NDSP_WBUF_QUEUED && sSfxBuf[cand].status != NDSP_WBUF_PLAYING) {
			slot = cand;
			break;
		}
	}
	if (slot < 0) slot = sNext;  // every voice busy: steal the oldest
	sNext = (slot + 1) % kSfxChannels;

	int ch = kFirstSfx + slot;
	ndspChnWaveBufClear(ch);
	ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
	ndspChnSetRate(ch, rate);
	ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
	float mix[12] = {left, right};
	ndspChnSetMix(ch, mix);
	ndspWaveBuf& b = sSfxBuf[slot];
	memset(&b, 0, sizeof b);
	b.data_vaddr = clip.pcm;
	b.nsamples = clip.samples;
	ndspChnWaveBufAdd(ch, &b);
}

void musicVolume(float v) {
	sMusicVolume = v;
	float mix[12] = {v, v};
	ndspChnSetMix(kMusicChannel, mix);
}

void musicStart(const Clip& c) {
	ndspChnSetInterp(kMusicChannel, NDSP_INTERP_LINEAR);
	sMusicRate = (float)c.rate;
	ndspChnSetRate(kMusicChannel, sMusicRate * sMusicSpeed);
	ndspChnSetFormat(kMusicChannel, NDSP_FORMAT_MONO_PCM16);
	musicVolume(sMusicVolume);
	memset(&sMusicBuf, 0, sizeof sMusicBuf);
	sMusicBuf.data_vaddr = c.pcm;
	sMusicBuf.nsamples = c.samples;
	sMusicBuf.looping = true;
	ndspChnWaveBufAdd(kMusicChannel, &sMusicBuf);
}

void musicRate(float factor) {
	sMusicSpeed = factor;
	ndspChnSetRate(kMusicChannel, sMusicRate * factor);
}

void musicStop() { ndspChnWaveBufClear(kMusicChannel); }

float musicTime() {
	if (!ndspChnIsPlaying(kMusicChannel) || sMusicRate <= 0) return -1;
	return ndspChnGetSamplePos(kMusicChannel) / sMusicRate;
}

void fuseStart(const Clip& c) {
	ndspChnSetInterp(kFuseChannel, NDSP_INTERP_LINEAR);
	ndspChnSetRate(kFuseChannel, (float)c.rate);
	ndspChnSetFormat(kFuseChannel, NDSP_FORMAT_MONO_PCM16);
	memset(&sFuseBuf, 0, sizeof sFuseBuf);
	sFuseBuf.data_vaddr = c.pcm;
	sFuseBuf.nsamples = c.samples;
	sFuseBuf.looping = true;
	ndspChnWaveBufAdd(kFuseChannel, &sFuseBuf);
}

void fuseMix(float left, float right) {
	float mix[12] = {left, right};
	ndspChnSetMix(kFuseChannel, mix);
}

void fuseStop() { ndspChnWaveBufClear(kFuseChannel); }
}  // namespace audioport
