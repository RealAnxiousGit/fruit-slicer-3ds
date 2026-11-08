// Nintendo 3DS entry point: system services, buttons and the stylus.
#include <stdio.h>
#include <sys/stat.h>

#include "app.h"
#include "common.h"
#include "port_api.h"

namespace port {
struct AssetFile {
	FILE* f;
	// A stream (music): a lower-priority thread reads into `out` while the game waits for the screen.
	Thread reader;
	bool streaming;
	u8* out;
	size_t want;
	volatile size_t done;
	volatile bool failed, stop;
};
static AssetFile sAssets[8];

AssetFile* assetOpen(const char* path) {
	char full[96];
	snprintf(full, sizeof full, "romfs:/%s", path);
	for (auto& a : sAssets) {
		if (a.f) continue;
		a = {};
		a.f = fopen(full, "rb");
		return a.f ? &a : nullptr;
	}
	return nullptr;
}

static constexpr size_t kStreamChunk = 16 * 1024;  // small, so stopping a stream never waits long

static void streamMain(void* arg) {
	AssetFile* a = (AssetFile*)arg;
	while (!a->stop && a->done < a->want) {
		size_t n = a->want - a->done;
		if (n > kStreamChunk) n = kStreamChunk;
		size_t got = fread(a->out + a->done, 1, n, a->f);
		__sync_synchronize();  // the bytes are in before the count says so
		a->done = a->done + got;
		if (got != n) {
			a->failed = true;
			break;
		}
	}
}

void streamStart(AssetFile* a, void* out, size_t bytes) {
	if (a->streaming) return;
	a->streaming = true;
	a->out = (u8*)out;
	a->want = bytes;
	a->done = 0;
	a->failed = a->stop = false;
	// Just below the game's own priority, on the game's core: it only runs while the game is waiting.
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	a->reader = threadCreate(streamMain, a, 16 * 1024, prio < 0x3F ? prio + 1 : 0x3F, -2, false);
}

long streamDone(AssetFile* a) {
	if (!a->streaming) return 0;
	if (!a->reader && !a->failed && a->done < a->want) {
		// No thread to spare: read a piece a frame, as before.
		size_t n = a->want - a->done;
		if (n > 24 * 1024) n = 24 * 1024;
		size_t got = fread(a->out + a->done, 1, n, a->f);
		a->done = a->done + got;
		if (got != n) a->failed = true;
	}
	return a->failed ? -1 : (long)a->done;
}
size_t assetRead(void* out, size_t bytes, AssetFile* a) { return fread(out, 1, bytes, a->f); }
long assetSize(AssetFile* a) {
	long at = ftell(a->f);
	fseek(a->f, 0, SEEK_END);
	long n = ftell(a->f);
	fseek(a->f, at, SEEK_SET);
	return n;
}
void assetClose(AssetFile* a) {
	if (a->reader) {
		// The reader is still writing into memory the caller is about to free: stop it first.
		a->stop = true;
		threadJoin(a->reader, U64_MAX);
		threadFree(a->reader);
		a->reader = nullptr;
	}
	a->streaming = false;
	fclose(a->f);
	a->f = nullptr;
}

// The Lab keeps its own folder, so a test run never touches a player's save.
#ifdef LAB
#define DATA_DIR "sdmc:/3ds/FruitSlicerLab"
#else
#define DATA_DIR "sdmc:/3ds/FruitSlicer"
#endif
void dataPath(char* out, size_t size, const char* name) { snprintf(out, size, DATA_DIR "/%s", name); }
void ensureDataDir() {
	mkdir("sdmc:/3ds", 0777);
	mkdir(DATA_DIR, 0777);
#ifdef LAB
	remove(DATA_DIR "/save.bin");  // every Lab run starts fresh
#endif
}
bool running() { return aptMainLoop(); }
}  // namespace port

static aptHookCookie sAptCookie;

static void onApt(APT_HookType hook, void*) {
	if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP) app::notifySuspended();
}

int main() {
	romfsInit();
#ifdef AUTOTEST
	consoleDebugInit(debugDevice_SVC);  // stderr goes to svcOutputDebugString, visible in Azahar's log
#endif
	osSetSpeedupEnable(true);  // New 3DS: 804 MHz + L2 cache; no effect on Old 3DS
	APT_CheckNew3DS(&gIsNew3ds);
	cfguInit();
	u8 model = CFG_MODEL_3DS;
	if (R_SUCCEEDED(CFGU_GetSystemModel(&model))) gHas3dScreen = model != CFG_MODEL_2DS && model != CFG_MODEL_N2DSXL;

	if (!app::init()) {
		// Without romfs there is nothing to draw; bail out instead of crashing.
		cfguExit();
		romfsExit();
		return 1;
	}
	hidSetRepeatParameters(20, 5);
	// The New 3DS C-Stick comes through ir:rst. An Old 3DS (or a Circle Pad Pro) just goes without it.
	bool cstick = gIsNew3ds && R_SUCCEEDED(irrstInit());
	aptHook(&sAptCookie, onApt, nullptr);
	gRng.s = (u32)osGetTime() | 1;

	app::showLoading();

	while (aptMainLoop()) {
		hidScanInput();
		if (cstick) irrstScanInput();
		Input in{};
		in.down = hidKeysDown();
		in.held = hidKeysHeld();
		in.repeat = hidKeysDownRepeat();
		if (in.held & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			in.touching = true;
			in.tx = tp.px;
			in.ty = tp.py;
		}
		// Stick slicing: the Circle Pad, plus the C-Stick on a New 3DS. A, L or R slices.
		circlePosition cp;
		hidCircleRead(&cp);
		float sx = cp.dx / 150.0f, sy = -cp.dy / 150.0f;
		if (cstick) {
			circlePosition cs;
			irrstCstickRead(&cs);
			sx += cs.dx / 150.0f;
			sy -= cs.dy / 150.0f;
		}
		in.stickX = sx < -1 ? -1 : (sx > 1 ? 1 : sx);
		in.stickY = sy < -1 ? -1 : (sy > 1 ? 1 : sy);
		in.stickSlice = (in.held & (KEY_A | KEY_L | KEY_R)) != 0;
		if (!app::frame(in)) break;
	}

	if (cstick) irrstExit();
	aptUnhook(&sAptCookie);
	app::shutdown();
	cfguExit();
	romfsExit();
	return 0;
}
