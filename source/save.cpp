#include <stdio.h>
#include <string.h>
#if defined(__ANDROID__) || defined(__linux__)
#include <unistd.h>
#endif

#include "common.h"
#include "port_api.h"

Settings gSave;
bool gIsNew3ds = false;
bool gHas3dScreen = true;

// 0.4.0 starts everyone fresh: saves from earlier versions ("FLS1" to "FLS3") are not read.
static constexpr u32 kMagic = 0x34534C46;  // "FLS4"

static u32 checksum(const void* data, size_t size) {
	const u8* p = (const u8*)data;
	u32 h = 2166136261u;
	for (size_t i = 0; i < size; i++) h = (h ^ p[i]) * 16777619u;
	return h;
}

void saveDefaults(bool keepScores, int section) {
	if (!keepScores) {
		memset(gSave.best, 0, sizeof gSave.best);
		gSave.totalFruit = 0;
		gSave.bestCombo = 0;
	}
	if (section < 0 || section == 0) {
		gSave.sfx = 100;
		gSave.music = 75;
		gSave.menuSfx = 80;
		gSave.menuTrack = gSave.gameTrack = 0;
	}
	if (section < 0 || section == 1) {
		// A New 3DS gets the full look. An Old 3DS starts on the settings it can hold 60 with.
		gSave.artStyle = gIsNew3ds ? ART_3D : ART_2D;
		gSave.board = gSave.scene = gSave.blade = 0;
		gSave.effects = gIsNew3ds ? FX_FULL : FX_MEDIUM;
		gSave.depth = 50;
		gSave.shake = 100;
		gSave.bgDetail = gIsNew3ds ? 1 : 0;
		gSave.topFruit = 1;
		gSave.detail = gIsNew3ds ? 1 : 0;
		gSave.sliceStyle = gIsNew3ds ? 1 : 0;
		gSave.transitions = 1;
		PORT_RESET_VISUALS
	}
	if (section < 0 || section == 2) {
		gSave.stylus = PORT_DEFAULT_SENSITIVITY;
		gSave.difficulty = DIFF_NORMAL;
		gSave.fruitSpeed = 50;
		gSave.bombs = 50;
		gSave.forgiveness = 50;
		gSave.frenzyDrain = 50;
		gSave.misses = 3;
	}
}

void saveLoad() {
	saveDefaults(false, -1);
	char file[256], temp[256];
	port::dataPath(file, sizeof file, "save.bin");
	port::dataPath(temp, sizeof temp, "save.tmp");
	FILE* f = fopen(file, "rb");
	if (!f) f = fopen(temp, "rb");  // power was lost between the two steps of saveWrite
	if (!f) return;
	u32 magic = 0, sum = 0;
	if (fread(&magic, 4, 1, f) == 1) {
		if (magic == kMagic) {
			Settings tmp;
			if (fread(&tmp, sizeof tmp, 1, f) == 1 && fread(&sum, 4, 1, f) == 1 && sum == checksum(&tmp, sizeof tmp)) gSave = tmp;
			else fprintf(stderr, "[save] ignoring invalid save file\n");
		}
	}
	fclose(f);
	gSave.board %= kBoardCount;
	gSave.scene %= kSceneCount;
	gSave.blade %= kBladeCount;
	gSave.artStyle &= 1;
	gSave.effects &= 3;
	gSave.difficulty %= DIFF_COUNT;
	gSave.misses = gSave.misses < 1 ? 1 : (gSave.misses > 5 ? 5 : gSave.misses);
	gSave.bgDetail &= 1;
	gSave.topFruit &= 1;
	gSave.detail &= 1;
	gSave.sliceStyle &= 1;
	gSave.transitions &= 1;
	PORT_CHECK_SETTINGS
	gSave.menuTrack %= kTrackCount + 1;
	gSave.gameTrack %= kTrackCount + 1;
	u8* const sliders[] = {&gSave.sfx, &gSave.music, &gSave.menuSfx, &gSave.depth, &gSave.shake, &gSave.stylus, &gSave.fruitSpeed, &gSave.bombs, &gSave.forgiveness, &gSave.frenzyDrain};
	for (u8* v : sliders)
		if (*v > 100) *v = 100;
}

void saveWrite() {
	port::ensureDataDir();
	char file[256], temp[256];
	port::dataPath(file, sizeof file, "save.bin");
	port::dataPath(temp, sizeof temp, "save.tmp");
	// Written beside the old save and swapped in afterwards, so a power cut can't lose both.
	FILE* f = fopen(temp, "wb");
	if (!f) {
		fprintf(stderr, "[save] could not write %s\n", temp);
		return;
	}
	u32 sum = checksum(&gSave, sizeof gSave);
	bool ok = fwrite(&kMagic, 4, 1, f) == 1 && fwrite(&gSave, sizeof gSave, 1, f) == 1 && fwrite(&sum, 4, 1, f) == 1;
#if defined(__ANDROID__) || defined(__linux__)
	// On disk before the swap, so a power cut can't leave an empty save behind it.
	if (ok && (fflush(f) != 0 || fsync(fileno(f)) != 0)) ok = false;
#endif
	if (fclose(f) != 0) ok = false;
	if (!ok) {
		// A full disk or card: the old save stays as it was.
		fprintf(stderr, "[save] could not finish writing %s\n", temp);
		remove(temp);
		return;
	}
	// Where rename can replace a file this is one safe step. Elsewhere the old save has to go first.
	if (rename(temp, file) != 0) {
		remove(file);
		if (rename(temp, file) != 0) fprintf(stderr, "[save] could not replace %s\n", file);
	}
}
