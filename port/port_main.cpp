// Nintendo 3DS entry point: system services, buttons and the stylus.
#include <stdio.h>
#include <sys/stat.h>

#include "app.h"
#include "common.h"
#include "port_api.h"

namespace port {
struct AssetFile {
	FILE* f;
};
static AssetFile sAssets[8];

AssetFile* assetOpen(const char* path) {
	char full[96];
	snprintf(full, sizeof full, "romfs:/%s", path);
	for (auto& a : sAssets) {
		if (a.f) continue;
		a.f = fopen(full, "rb");
		return a.f ? &a : nullptr;
	}
	return nullptr;
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
	fclose(a->f);
	a->f = nullptr;
}

void dataPath(char* out, size_t size, const char* name) { snprintf(out, size, "sdmc:/3ds/FruitSlicer/%s", name); }
void ensureDataDir() {
	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/FruitSlicer", 0777);
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
