#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adpcm.h"
#include "audio_port.h"
#include "port_api.h"
#include "shop.h"
#if defined(PORT_HAS_VORBIS) && PORT_HAS_VORBIS
#define FS_OGG 1
#include "song_decoder.h"
#else
#define FS_OGG 0
#endif
#if PORT_CUSTOM_MUSIC
#include "custom_music.h"
#endif

namespace audio {
static Clip sSfx[SFX_COUNT];
static Clip sMusicClip;      // only the current track is ever in memory
static s16* sMusicRetired;   // the hardware may still read the old track for a moment
static int sMusicRetiredFrames;
static Clip sFuse;
static int sCurrent = MUSIC_NONE;
static bool sFuseOn;
static bool sReady;

static const char* const kSfxFiles[SFX_COUNT] = {
	"swish0", "swish1", "swish2", "shing",
	"crisp0", "crisp1", "juicy0", "juicy1", "soft0", "soft1", "melon0", "melon1",
	"knock", "shell",
	"throw", "boom",
	"combo3", "combo4", "combo5", "combo6", "combo8", "combo10",
	"strike", "click", "power", "freeze", "fever", "go", "over", "best", "gold",
	"finale_rise", "finale_hit", "finale_burst",
	"coin", "plink", "shopbell",
};
static_assert(sizeof kSfxFiles / sizeof kSfxFiles[0] == SFX_COUNT, "every sound needs a file name");
// How much of the mix one sound may take. A new sound is turned down if the ones still playing have
// used this up, so a long Frenzy stays at the level of a quiet moment instead of piling up to clipping.
static constexpr float kSfxGain = 0.7f;
static constexpr float kMaxPeak = 0.7f;        // what the start of a sound may add at once; music takes the rest
static constexpr float kMaxPower = 0.16f * 0.16f;  // the sound effects together, as RMS squared
static constexpr float kMinShare = 0.35f;      // however busy it gets, a slice stays audible
static constexpr float kPeakFade = 0.05f;      // seconds for a sound's opening hit to fade by about two thirds
static constexpr int kLiveCount = 24;
// Every combo sound hits as hard as the loudest slices, however big the combo. One landing over
// another drops to keep the pair at about that level, but never so far that it gets lost.
static constexpr float kComboLoud = 0.15f;      // RMS of its loudest tenth of a second
static constexpr float kComboMinStack = 0.6f;
static constexpr float kComboMinShare = 0.7f;

struct Live {
	float born, length, peak, power;
	bool combo;
};
static Live sLive[kLiveCount];
static int sLiveNext;
static float sClock;
static float sClipPeak[SFX_COUNT], sClipPower[SFX_COUNT], sClipLoud[SFX_COUNT];

static bool isCombo(int s) { return s >= SFX_COMBO3 && s <= SFX_COMBO10; }

// These run far louder and longer than the rest.
static float clipTrim(Sfx s) {
	switch (s) {
		case SFX_BOOM: return 0.6f;
		case SFX_FINALE_BURST: return 0.75f;
		default: return 1.0f;
	}
}

static void measureClip(int s, const Clip& c) {
	float peak = 0;
	double sum = 0;
	for (u32 i = 0; i < c.samples; i++) {
		float a = c.pcm[i] / 32768.0f;
		sum += a * a;
		if (a < 0) a = -a;
		if (a > peak) peak = a;
	}
	sClipPeak[s] = peak;
	sClipPower[s] = c.samples ? (float)(sum / c.samples) : 0;
	if (!isCombo(s)) return;
	const u32 window = c.rate / 10;
	double power = 0, loudest = 0;
	for (u32 i = 0; i < c.samples; i++) {
		const double a = c.pcm[i] / 32768.0;
		power += a * a;
		if (i >= window) {
			const double old = c.pcm[i - window] / 32768.0;
			power -= old * old;
		}
		if (power > loudest) loudest = power;
	}
	sClipLoud[s] = (float)sqrt(loudest / (c.samples < window ? c.samples : window));
}

// Sounds are cued the moment something happens, so silence at the front of a file would be heard as
// lag. Everything before the sound really starts goes, with a tiny fade in so that no click is added.
static void trimLeadIn(Clip& c) {
	s16 peak = 0;
	for (u32 i = 0; i < c.samples; i++) {
		const s16 a = c.pcm[i] < 0 ? (s16)-(c.pcm[i] + 1) : c.pcm[i];
		if (a > peak) peak = a;
	}
	const s16 gate = peak / 25;
	u32 first = 0;
	while (first < c.samples && c.pcm[first] < gate && c.pcm[first] > -gate) first++;
	const u32 fade = c.rate / 1000;
	if (first >= c.samples) return;
	if (first > fade) {
		c.samples -= first - fade;
		memmove(c.pcm, c.pcm + (first - fade), c.samples * sizeof(s16));
	}
	for (u32 i = 0; i < fade && i < c.samples; i++) c.pcm[i] = (s16)(c.pcm[i] * (float)i / fade);
}

// How much of its full volume a new sound may have, and notes it as playing. Sliders are not part
// of this, so turning sound down keeps the same balance.
static float makeRoom(Sfx s, float gain, float seconds, float minShare) {
	float busyPeak = 0, busyPower = 0;
	for (const Live& l : sLive) {
		const float age = sClock - l.born;
		if (age >= l.length) continue;
		busyPeak += l.peak * expf(-age / kPeakFade);
		busyPower += l.power * (1.0f - age / l.length);
	}
	const float peak = gain * sClipPeak[s], power = gain * gain * sClipPower[s];
	float share = 1;
	if (peak > 0 && busyPeak + peak > kMaxPeak) share = (kMaxPeak - busyPeak) / peak;
	if (power > 0 && busyPower + power > kMaxPower) {
		const float room = kMaxPower > busyPower ? sqrtf((kMaxPower - busyPower) / power) : 0.0f;
		if (room < share) share = room;
	}
	if (share < minShare) share = minShare;
	sLive[sLiveNext] = {sClock, seconds, peak * share, power * share * share, isCombo(s)};
	sLiveNext = (sLiveNext + 1) % kLiveCount;
	return share;
}

static const char* const kTrackFiles[kTrackCount] = {
	"music_menu", "music_play", "music_meadow", "music_lantern", "music_zen", "music_moss", "music_bamboo", "music_clouds",
#define SHOP_TRACK(name, price, line, file, bpm) file,
#include "shop_items.inc"
};
// Beats per minute. The made tracks are composed at these; the recorded ones were measured from the
// files and loop on whole bars. Every track starts on the first beat of a bar.
static constexpr float kTrackBpm[kTrackCount] = {
	62, 84, 56, 68, 50, 44, 48, 40,
#define SHOP_TRACK(name, price, line, file, bpm) bpm,
#include "shop_items.inc"
};
// What each screen plays on Auto: menus, games, and Zen.
static constexpr int kAutoTrack[MUSIC_COUNT] = {0, 1, 4};
static constexpr u32 kReadPerFrame = 24 * 1024;  // whole tracks arrive in under a second

static int sContext = MUSIC_NONE;
static int sForced = -1;  // Beat Slice's track, in place of the Game Music choice
static port::AssetFile* sLoadFile;
static u32 sLoadDone;

#if FS_OGG
// Recorded tracks (the Shop's) are Ogg Vorbis on this port, at full quality and in stereo. The file
// arrives a piece per frame, then decodes a piece per frame, so a new track never stalls a frame.
static constexpr u32 kOggReadPerFrame = 256 * 1024;
static constexpr int kOggFramesPerStep = 16384;  // about a third of a second of music
static u8* sOgg;
static u32 sOggSize;
static songdec::Decoder* sOggDec;
static u32 sOggDecoded;

static void oggDrop() {
	if (sOggDec) songdec::close(sOggDec);
	sOggDec = nullptr;
	free(sOgg);
	sOgg = nullptr;
	sOggSize = 0;
}

// The whole file is here: check it, then make room for all of its samples.
static void oggStartDecode() {
	int ch = 0, rate = 0;
	unsigned frames = 0;
	sOggDec = songdec::open(sOgg, (int)sOggSize, &ch, &rate, &frames);
	if (!sOggDec || ch < 1 || ch > 2 || rate < 8000 || rate > 96000 || frames == 0 || frames > (unsigned)rate * 600) {
		fprintf(stderr, "[audio] skipping a track that can't be decoded\n");
		oggDrop();
		return;
	}
	sMusicClip.samples = frames;
	sMusicClip.rate = (u32)rate;
	sMusicClip.channels = (u32)ch;
	sMusicClip.pcm = audioport::alloc(frames * ch * 2);
	sOggDecoded = 0;
	if (!sMusicClip.pcm) oggDrop();
}
#endif


// Auto shuffles: a track plays to its end, then another the player owns, never the same one twice
// in a row. Zen keeps to the calm tracks. The shuffle's track carries on from menu to menu.
static bool sAutoPick;     // the playing track came from Auto's shuffle
static int sAutoContext = MUSIC_NONE;
static u32 sShuffle = 0x9E3779B9u;

static bool calmTrack(int t) { return t == 0 || (t >= 2 && t <= 7); }

static bool inAutoPool(int context, int t) {
	if (t < 0 || t >= kTrackCount || !trackUnlocked(t)) return false;
	return context != MUSIC_ZEN || calmTrack(t);
}

static int pickAuto(int context, int avoid) {
	int pool[kTrackCount], n = 0;
	for (int t = 0; t < kTrackCount; t++)
		if (t != avoid && inAutoPool(context, t)) pool[n++] = t;
	if (n == 0) return inAutoPool(context, avoid) ? avoid : kAutoTrack[context];
	sShuffle = sShuffle * 1664525u + 1013904223u + (u32)(sClock * 1000);
	return pool[(sShuffle >> 8) % n];
}

static int resolveTrack(int context, bool* autoPick) {
	*autoPick = false;
	if (context == MUSIC_NONE) return -1;
	if (context == MUSIC_PLAY && sForced >= 0) return sForced;
	u8 choice = context == MUSIC_MENU ? gSave.menuTrack : gSave.gameTrack;
	if (choice > 0) return choice - 1;
	*autoPick = true;
	if (sAutoPick && sAutoContext == context && inAutoPool(context, sCurrent)) return sCurrent;
	return pickAuto(context, sCurrent);
}


static float level(u8 slider) { return slider / 100.0f; }

static constexpr u32 kPackedClip = 0x32415346;  // "FSA2": a 3DS song, packed with IMA ADPCM (adpcm.h)
static u32 sLoadPacked;                         // file bytes of a packed song being read, 0 for plain samples

static port::AssetFile* openClip(const char* name, u32 head[3]) {
	char path[64];
	snprintf(path, sizeof path, "audio/%s.pcm", name);
	port::AssetFile* f = port::assetOpen(path);
	if (!f) {
		fprintf(stderr, "[audio] missing %s\n", path);
		return nullptr;
	}
	// A clip with a bad header is skipped on its own, so one broken file can't stall or flood the mix:
	// a sample rate of 0 would never finish, and a huge length would eat the memory every sound shares.
	if (port::assetRead(head, 12, f) != 12 || (head[0] != 0x31415346 && head[0] != kPackedClip) ||  // "FSA1"
	    head[1] == 0 || head[1] > 48000u * 600 || head[2] < 8000 || head[2] > 96000) {
		fprintf(stderr, "[audio] skipping %s: bad header\n", path);
		port::assetClose(f);
		return nullptr;
	}
	return f;
}

// How far a new combo sound drops for the combo sounds still ringing, keeping their sum about level.
static float comboStack() {
	float ringing = 0;
	for (const Live& l : sLive) {
		const float age = sClock - l.born;
		if (l.combo && age < l.length) {
			const float left = 1.0f - age / l.length;
			ringing += left * left;
		}
	}
	const float drop = 1.0f / sqrtf(1.0f + ringing);
	return drop < kComboMinStack ? kComboMinStack : drop;
}

static bool loadClip(Clip& c, const char* name, bool trim = false) {
	u32 head[3];
	port::AssetFile* f = openClip(name, head);
	if (!f) return false;
	c.samples = head[1];
	c.rate = head[2];
	c.pcm = audioport::alloc(c.samples * 2);
	bool ok = c.pcm && port::assetRead(c.pcm, c.samples * 2, f) == c.samples * 2;
	if (ok) {
		if (trim) trimLeadIn(c);
		audioport::ready(c.pcm, c.samples * 2);
	} else if (c.pcm) {
		audioport::release(c.pcm);
		c.pcm = nullptr;
	}
	port::assetClose(f);
	return ok;
}

bool init() {
	sReady = audioport::init();
	return sReady;
}

int loadStepCount() { return SFX_COUNT + 1; }

void loadStep(int i) {
	if (!sReady) return;
	// A clip that fails to load only silences that clip, never the whole game.
	if (i < SFX_COUNT) {
		if (loadClip(sSfx[i], kSfxFiles[i], true)) measureClip(i, sSfx[i]);
	}
	else loadClip(sFuse, "fuse");
}

void shutdown() {
	if (sReady) audioport::shutdown();
	for (auto& c : sSfx)
		if (c.pcm) audioport::release(c.pcm);
	if (sLoadFile) port::assetClose(sLoadFile);
	sLoadFile = nullptr;
#if FS_OGG
	oggDrop();
#endif
	if (sMusicClip.pcm) audioport::release(sMusicClip.pcm);
	if (sMusicRetired) audioport::release(sMusicRetired);
	if (sFuse.pcm) audioport::release(sFuse.pcm);
	sReady = false;
}

bool available() { return sReady; }

void click(float volume) {
	u8 saved = gSave.sfx;
	gSave.sfx = gSave.menuSfx;
	play(SFX_CLICK, volume);
	gSave.sfx = saved;
}

// A track starts as soon as its first half second is in, and the rest keeps arriving while it plays,
// many times faster than it is heard. Waiting for all of it was a delay of several seconds.
static constexpr float kStartAhead = 0.5f;
static bool sStarted;      // the mixer has the current track
static float sStartedAt;
static bool sLoop = true;  // a chosen track loops; Auto's shuffle moves on at the end instead
static float sAskedAt;  // when the current track was asked for, for the test log

static void startMixer(u32 avail) {
	DLOG("[audio] track %d plays %.0f ms after it was asked for (%s)\n", sCurrent, (sClock - sAskedAt) * 1000, sAutoPick ? "shuffle" : "loops");
#ifdef TEST_SHORTMUSIC
	if (sMusicClip.samples > sMusicClip.rate * 6) sMusicClip.samples = sMusicClip.rate * 6;  // test builds: see Auto move on quickly
#endif
	refreshVolumes();
	audioport::musicStart(sMusicClip, sLoop);
	audioport::musicAvail(sMusicClip.pcm, avail, sMusicClip.samples);
	sStarted = true;
	sStartedAt = sClock;
}

// More of the track is here: hand it over, and start the track once there is enough.
static void arrived(u32 fromFrame, u32 avail) {
	if (avail > fromFrame)
		audioport::ready(sMusicClip.pcm + fromFrame * sMusicClip.channels, (avail - fromFrame) * sMusicClip.channels * 2);
	if (sStarted) audioport::musicAvail(sMusicClip.pcm, avail, sMusicClip.samples);
	else if (avail >= sMusicClip.samples || (!PORT_3DS && avail >= sMusicClip.rate * kStartAhead)) startMixer(avail);  // the 3DS waits for all of it
}

// Loading broke off part way: whatever had started stops, and the track is let go.
static void loadFailed() {
	if (sStarted) audioport::musicStop();
	sStarted = false;
	if (sMusicClip.pcm) audioport::release(sMusicClip.pcm);
	sMusicClip.pcm = nullptr;
}

static bool loading() {
#if FS_OGG
	if (sOgg || sOggDec) return true;
#endif
	return sLoadFile != nullptr;
}

// How much of the current track is in so far.
static u32 loadedFrames() {
#if FS_OGG
	if (sOgg) return 0;
	if (sOggDec) return sOggDecoded;
#endif
	if (sLoadFile) return sLoadDone / (sMusicClip.channels * 2);
	return sMusicClip.samples;
}

static void autoNext();

void update(float dt) {
	sClock += dt;
#ifdef TEST_LOUD
	// A worst case for measuring the mix: a big combo and a full swipe of cuts, again and again.
	static float loudNext = 5;
	static int loudCount;
	if (sClock >= loudNext) {
		loudNext += 1.2f;
		play(++loudCount & 1 ? SFX_COMBO8 : SFX_COMBO10);
		for (int i = 0; i < 6; i++) play((Sfx)(SFX_CRISP0 + i), 0.95f, (i - 3) * 0.2f);
		play(SFX_SWISH0, 0.5f);
	}
#endif
#if FS_OGG
	if (sLoadFile && sOgg) {
		u32 left = sOggSize - sLoadDone;
		u32 want = left < kOggReadPerFrame ? left : kOggReadPerFrame;
		size_t got = port::assetRead(sOgg + sLoadDone, want, sLoadFile);
		sLoadDone += got;
		if (got != want || sLoadDone >= sOggSize) {
			port::assetClose(sLoadFile);
			sLoadFile = nullptr;
			if (got == want) oggStartDecode();
			else oggDrop();
		}
	} else if (sOggDec) {
		const u32 from = sOggDecoded;
		u32 left = sMusicClip.samples - sOggDecoded;
		int want = left < (u32)kOggFramesPerStep ? (int)left : kOggFramesPerStep;
		int got = songdec::read(sOggDec, sMusicClip.pcm + sOggDecoded * sMusicClip.channels, (int)sMusicClip.channels, want);
		if (got > 0) sOggDecoded += (u32)got;
		if (got <= 0 || sOggDecoded >= sMusicClip.samples) {
			// The stream's own length can be a touch longer than what decodes: play what is there.
			sMusicClip.samples = sOggDecoded;
			oggDrop();
			if (sMusicClip.samples > 0) arrived(from, sMusicClip.samples);
			else loadFailed();
		} else {
			arrived(from, sOggDecoded);
		}
	} else
#endif
	if (sLoadFile) {
		const u32 frameBytes = sMusicClip.channels * 2, from = sLoadDone / frameBytes;
#if PORT_3DS
		// A cartridge read can take longer than a whole frame, so the 3DS reads music on a background thread
		// that runs while the game waits for the screen, and the track starts playing as soon as enough is in.
		const u32 total = sMusicClip.samples * frameBytes;
		port::streamStart(sLoadFile, sMusicClip.pcm, total, sLoadPacked);
		const long done = port::streamDone(sLoadFile);
		if (done < 0) {
			port::assetClose(sLoadFile);
			sLoadFile = nullptr;
			loadFailed();
		} else {
			sLoadDone = (u32)done;
			if (sLoadDone >= total) {
				port::assetClose(sLoadFile);
				sLoadFile = nullptr;
				arrived(from, sMusicClip.samples);
			} else {
				arrived(from, sLoadDone / frameBytes);
			}
		}
	}
#else
		u32 left = sMusicClip.samples * frameBytes - sLoadDone;
		u32 want = left < kReadPerFrame ? left : kReadPerFrame;
		size_t got = port::assetRead((u8*)sMusicClip.pcm + sLoadDone, want, sLoadFile);
		sLoadDone += got;
		if (got != want || sLoadDone >= sMusicClip.samples * frameBytes) {
			port::assetClose(sLoadFile);
			sLoadFile = nullptr;
			if (got == want) arrived(from, sMusicClip.samples);
			else loadFailed();
		} else {
			arrived(from, sLoadDone / frameBytes);
		}
	}
#endif
	// A track that has played to its end: Auto's shuffle picks the next one; anything else starts over.
	if (sStarted && !loading() && sClock - sStartedAt > 1.0f && audioport::musicTime() < 0) {
		if (sAutoPick) autoNext();
		else startMixer(sMusicClip.samples);
	}
	if (sMusicRetired && --sMusicRetiredFrames <= 0) {
		audioport::release(sMusicRetired);
		sMusicRetired = nullptr;
	}
}

// Sound Captions: words for the sounds that tell you something. Shown even with the sound turned off.
static const char* captionFor(Sfx s) {
	switch (s) {
		case SFX_BOOM: return "Boom!";
		case SFX_COMBO3: case SFX_COMBO4: case SFX_COMBO5: case SFX_COMBO6: case SFX_COMBO8: case SFX_COMBO10: return "Combo!";
		case SFX_STRIKE: return "Missed!";
		case SFX_POWER: return "Power-up!";
		case SFX_FREEZE: return "Freeze!";
		case SFX_FEVER: return "Frenzy!";
		case SFX_GOLD: return "Gold!";
		case SFX_COIN: return "Coin!";
		case SFX_PLINK: return "Plink!";
		case SFX_SHOPBELL: return "Ding-a-ling!";
		case SFX_FINALE_RISE: return "Big fruit!";
		case SFX_FINALE_BURST: return "Burst!";
		default: return nullptr;
	}
}

void play(Sfx s, float volume, float pan, float pitch) {
	if (const char* words = captionFor(s)) showCaption(words);
	if (!sReady || gSave.sfx == 0) return;
	const Clip& clip = sSfx[s];
	if (!clip.pcm) return;
	if (!(pitch > 0.05f)) pitch = 1.0f;  // also catches NaN
	const bool combo = isCombo(s);
	float full = combo ? volume * comboStack() * kComboLoud / fmaxf(sClipLoud[s], 1e-3f) : volume * kSfxGain * clipTrim(s);
	if (!isfinite(full) || full <= 0) return;
	float v = full * makeRoom(s, full, clip.samples / ((float)clip.rate * pitch), combo ? kComboMinShare : kMinShare) * level(gSave.sfx);
	audioport::playSfx(clip, v * clampf(1.0f - pan, 0.0f, 1.0f), v * clampf(1.0f + pan, 0.0f, 1.0f), (float)clip.rate * pitch);
}

void refreshVolumes() {
	if (!sReady) return;
	audioport::musicVolume(0.6f * level(gSave.music));
}

#if PORT_CUSTOM_MUSIC
// Reads a little-endian number from a WAV header.
static u32 le(const u8* p, int n) {
	u32 v = 0;
	for (int i = n - 1; i >= 0; i--) v = v << 8 | p[i];
	return v;
}

// A player's song: Ogg arrives and decodes like the Shop's tracks; a 16-bit WAV streams straight in.
static void startCustom(int song) {
	port::AssetFile* f = port::fileOpen(custom::path(song));
	if (!f) return;
	if (!custom::isWav(song)) {
		long size = port::assetSize(f);
		if (size > 0 && size < 64L * 1024 * 1024 && (sOgg = (u8*)malloc((size_t)size))) {
			sOggSize = (u32)size;
			sLoadFile = f;
			sLoadDone = 0;
			return;
		}
		port::assetClose(f);
		return;
	}
	// The header, chunk by chunk, up to the samples.
	u8 h[64];
	u32 ch = 0, rate = 0, bits = 0, bytes = 0;
	bool ok = port::assetRead(h, 12, f) == 12 && memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0;
	while (ok) {
		if (port::assetRead(h, 8, f) != 8) {
			ok = false;
			break;
		}
		u32 len = le(h + 4, 4);
		if (memcmp(h, "data", 4) == 0) {
			bytes = len;
			break;
		}
		while (len > 0 && ok) {  // the format, or a chunk the game doesn't need
			u32 n = len < sizeof h ? len : (u32)sizeof h;
			ok = port::assetRead(h, n, f) == n;
			if (ok && rate == 0 && n >= 16 && len >= 16 && (le(h, 2) == 1 || le(h, 2) == 0xFFFE)) {
				ch = le(h + 2, 2);
				rate = le(h + 4, 4);
				bits = le(h + 14, 2);
			}
			len -= n;
		}
		if (ok && (len & 1)) ok = port::assetRead(h, 1, f) == 1;
	}
	if (!ok || bits != 16 || ch < 1 || ch > 2 || rate < 8000 || rate > 96000 || bytes < ch * 2 || bytes / (ch * 2) > rate * 600) {
		fprintf(stderr, "[audio] a custom song isn't a 16-bit WAV the game can play\n");
		port::assetClose(f);
		return;
	}
	sMusicClip.samples = bytes / (ch * 2);
	sMusicClip.rate = rate;
	sMusicClip.channels = ch;
	sMusicClip.pcm = audioport::alloc(sMusicClip.samples * ch * 2);
	if (!sMusicClip.pcm) {
		port::assetClose(f);
		return;
	}
	sLoadFile = f;
	sLoadDone = 0;
}
#endif

static void startTrack(int track, bool autoPick = false, int context = MUSIC_NONE) {
	// The same track again keeps playing; at its end it loops or moves on as asked now.
	sAutoPick = autoPick;
	sAutoContext = context;
	sLoop = !autoPick;
	DLOG("[audio] asked for track %d (now %d)\n", track, sCurrent);
	if (!sReady || track == sCurrent) return;
	sCurrent = track;
	sAskedAt = sClock;
	sStarted = false;
	audioport::musicStop();
#if FS_OGG
	if (sOgg || sOggDec) {
		// Still arriving or decoding, so the mixer never saw this one.
		oggDrop();
		if (sLoadFile) port::assetClose(sLoadFile);
		sLoadFile = nullptr;
		if (sMusicClip.pcm) audioport::release(sMusicClip.pcm);
		sMusicClip.pcm = nullptr;
	}
#endif
	if (sLoadFile) {
		// Still arriving, so the hardware never saw this one.
		port::assetClose(sLoadFile);
		sLoadFile = nullptr;
		if (sMusicClip.pcm) audioport::release(sMusicClip.pcm);
		sMusicClip.pcm = nullptr;
	}
	if (sMusicClip.pcm) {
		if (sMusicRetired) {
			audioport::settle();  // two swaps in a row: let the hardware finish with the older one
			audioport::release(sMusicRetired);
		}
		sMusicRetired = sMusicClip.pcm;
		sMusicRetiredFrames = 6;
		sMusicClip.pcm = nullptr;
	}
	if (track < 0) return;
#if PORT_CUSTOM_MUSIC
	if (track >= kTrackCount) {
		startCustom(track - kTrackCount);
		return;
	}
#endif
	if (track >= kTrackCount) return;
#if FS_OGG
	{
		char path[64];
		snprintf(path, sizeof path, "audio/%s.ogg", kTrackFiles[track]);
		if (port::AssetFile* ogg = port::assetOpen(path)) {
			long size = port::assetSize(ogg);
			if (size > 0 && size < 32L * 1024 * 1024 && (sOgg = (u8*)malloc((size_t)size))) {
				sOggSize = (u32)size;
				sLoadFile = ogg;
				sLoadDone = 0;
				return;
			}
			port::assetClose(ogg);
		}
	}
#endif
	u32 head[3];
	port::AssetFile* f = openClip(kTrackFiles[track], head);
	if (!f) return;
	sMusicClip.samples = head[1];
	sMusicClip.rate = head[2];
	sMusicClip.channels = 1;
	sLoadPacked = head[0] == kPackedClip ? adpcmFileBytes(sMusicClip.samples) : 0;
	sMusicClip.pcm = audioport::alloc(sMusicClip.samples * 2 + (sLoadPacked ? kAdpcmRoom : 0));
	if (!sMusicClip.pcm) {
		port::assetClose(f);
		return;
	}
	sLoadFile = f;
	sLoadDone = 0;
}

static void startFor(int context) {
	bool autoPick;
	const int track = resolveTrack(context, &autoPick);
	startTrack(track, autoPick, context);
}

// The shuffle's track has ended: on to another.
static void autoNext() {
	const int next = pickAuto(sAutoContext, sCurrent);
	if (next == sCurrent) startMixer(sMusicClip.samples);
	else startTrack(next, true, sAutoContext);
}

void music(Music m) {
	sContext = m;
	sForced = -1;
	startFor(m);
}

void preview(Music m) { startFor(m); }

void musicBeat(int track) {
	if (track < 0 || track >= trackCount()) return;
	sContext = MUSIC_PLAY;
	sForced = track;
	if (!sReady) return;
	if (track == sCurrent && sStarted) {
		// Already here: back to its first beat.
		sAutoPick = false;
		sLoop = true;
		audioport::musicStop();
		startMixer(loadedFrames());
		return;
	}
	startTrack(track, false, MUSIC_PLAY);  // it starts from the top as soon as its start is in
}

int currentTrack() { return sCurrent; }
bool musicPending() { return sReady && sCurrent >= 0 && !sStarted; }

float musicTime() {
	if (!sReady || !sMusicClip.pcm || !sStarted) return -1;
	return audioport::musicTime();
}

int trackCount() {
#if PORT_CUSTOM_MUSIC
	return kTrackCount + custom::count();
#else
	return kTrackCount;
#endif
}

float trackBeat(int track) {
#if PORT_CUSTOM_MUSIC
	float bpm, first;
	if (track >= kTrackCount && custom::beat(track - kTrackCount, &bpm, &first)) return 60.0f / bpm;
#endif
	return 60.0f / kTrackBpm[track >= 0 && track < kTrackCount ? track : 1];
}

bool trackHasBeat(int track) {
	if (track >= 0 && track < kTrackCount) return kTrackBpm[track] > 0;
#if PORT_CUSTOM_MUSIC
	float bpm, first;
	return track >= kTrackCount && custom::beat(track - kTrackCount, &bpm, &first);
#else
	return false;
#endif
}

float trackFirst(int track) {
#if PORT_CUSTOM_MUSIC
	float bpm, first;
	if (track >= kTrackCount && custom::beat(track - kTrackCount, &bpm, &first)) return first;
#endif
	(void)track;
	return 0;
}

float musicLength() {
	if (!sReady || !sMusicClip.pcm || sLoadFile || sMusicClip.rate == 0) return -1;
#if FS_OGG
	if (sOgg || sOggDec) return -1;
#endif
	return (float)sMusicClip.samples / sMusicClip.rate;
}

void reapply() { startFor(sContext); }

void musicSpeed(float factor) {
	static float now = 1;
	if (!sReady || factor == now) return;
	now = factor;
	audioport::musicRate(factor);
}

void fuse(bool on, float pan) {
	if (!sReady || !sFuse.pcm) return;
	if (on && gSave.sfx > 0) {
		float v = 0.2f * level(gSave.sfx);
		audioport::fuseMix(v * clampf(1.0f - pan, 0.0f, 1.0f), v * clampf(1.0f + pan, 0.0f, 1.0f));
		if (!sFuseOn) {
			audioport::fuseStart(sFuse);
			sFuseOn = true;
		}
	} else if (sFuseOn) {
		audioport::fuseStop();
		sFuseOn = false;
	}
}
}  // namespace audio
