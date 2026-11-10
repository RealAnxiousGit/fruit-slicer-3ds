#include <stddef.h>
#include <stdio.h>
#include <string.h>
#if defined(__ANDROID__) || defined(__linux__)
#include <unistd.h>
#endif

#include "common.h"
#include "port_api.h"
#include "shop.h"

Settings gSave;
bool gIsNew3ds = false;
bool gHas3dScreen = true;

// Save files:
//   "FLS5" (1.0 on): magic, u16 version, u16 size, then `size` bytes of Settings, then a checksum.
//     The size lets a newer game read an older, shorter save and fill the rest with defaults.
//   "FLS4" (0.4.0): magic, the 0.4 settings exactly as they were in memory, then a checksum.
//     Upgraded field by field; keep this reader in every future version.
//   Earlier saves ("FLS1" to "FLS3") were already left behind by 0.4.0.
static constexpr u32 kMagic4 = 0x34534C46;  // "FLS4"
static constexpr u32 kMagic5 = 0x35534C46;  // "FLS5"
static constexpr u16 kVersion = 1;
static bool sKeep04Copy = false;  // the save on disk is a 0.4 one; copy it aside before replacing it

// The 0.4.0 layout. Universal builds added `u8 renderer` (and 3 bytes of padding) after it.
struct SettingsV4 {
	u32 best[7];
	u32 totalFruit, bestCombo;
	u8 sfx, music, menuSfx, menuTrack, gameTrack;
	u8 artStyle, board, scene, blade, effects, depth, shake, bgDetail, topFruit, detail, sliceStyle, transitions;
	u8 stylus, difficulty, fruitSpeed, bombs, forgiveness, frenzyDrain, misses;
};
static_assert(sizeof(SettingsV4) == 60, "0.4 save layout changed");
// Fields never move once shipped: these pin the 1.0 layout.
static_assert(offsetof(Settings, trail) == 486 && offsetof(Settings, modePlays) == 488 && offsetof(Settings, portBlock) == 552, "a saved field moved");
static_assert(offsetof(Settings, portBlock) % 4 == 0 && offsetof(Settings, bestMixer) == offsetof(Settings, portBlock) + 32, "the port block must start 4-byte aligned and hold 32 bytes: resize reserved[] or the port fields");

static u32 checksum(const void* data, size_t size) {
	const u8* p = (const u8*)data;
	u32 h = 2166136261u;
	for (size_t i = 0; i < size; i++) h = (h ^ p[i]) * 16777619u;
	return h;
}

void saveResetScores() {
	memset(gSave.best, 0, sizeof gSave.best);
	memset(gSave.bestAssisted, 0, sizeof gSave.bestAssisted);
	memset(gSave.bestMixer, 0, sizeof gSave.bestMixer);
	gSave.totalFruit = 0;
	gSave.bestCombo = 0;
	gSave.playSeconds = 0;
	memset(gSave.fruitSliced, 0, sizeof gSave.fruitSliced);
	gSave.dailyDay = 0;
	gSave.dailyFirstDay = 0;
	memset(gSave.dailyScore, 0, sizeof gSave.dailyScore);
	gSave.dailyStreak = gSave.dailyBestStreak = 0;
	memset(gSave.beatBest, 0, sizeof gSave.beatBest);
}

void saveDefaults(bool keepScores, int section) {
	if (!keepScores) saveResetScores();
	if (section == SEC_ALL || section == SEC_AUDIO) {
		gSave.sfx = 100;
		gSave.music = 75;
		gSave.menuSfx = 80;
		gSave.menuTrack = gSave.gameTrack = 0;
	}
	if (section == SEC_ALL || section == SEC_VISUALS) {
		// A New 3DS gets the full look. An Old 3DS starts on the settings it can hold 60 with.
		gSave.artStyle = gIsNew3ds ? ART_3D : ART_2D;
		gSave.board = gSave.scene = gSave.blade = 0;
		gSave.depth = 50;
		gSave.bgDetail = gIsNew3ds ? 1 : 0;
#if !PORT_HAS_DISPLAY  // otherwise they belong to the Display section's graphics preset
		gSave.effects = gIsNew3ds ? FX_FULL : FX_MEDIUM;
		gSave.detail = gIsNew3ds ? 1 : 0;
#endif
		gSave.sliceStyle = gIsNew3ds ? 1 : 0;
		gSave.transitions = 1;
		gSave.seasonalOff = 0;
		PORT_RESET_VISUALS
	}
	if (section == SEC_ALL || section == SEC_GAMEPLAY) {
		gSave.stylus = PORT_DEFAULT_SENSITIVITY;
		gSave.difficulty = DIFF_NORMAL;
		gSave.fruitSpeed = 50;
		gSave.bombs = 50;
		gSave.forgiveness = 50;
		gSave.frenzyDrain = 50;
		gSave.misses = 3;
	}
	if (section == SEC_ALL || section == SEC_CONTROLS) {
		gSave.stickStyle = 0;
		gSave.stickSpeed = 50;
		gSave.snapAssist = 1;
		gSave.vibration = 1;
	}
	if (section == SEC_ALL || section == SEC_ACCESS) {
		gSave.shake = 100;
		gSave.uiScale = 0;
		gSave.colorBlind = 0;
		gSave.highContrast = gSave.largeText = gSave.calmFx = 0;
		gSave.sliceAssist = gSave.captions = gSave.sliceNoHold = 0;
		gSave.gameSpeed = 0;
	}
	if (section == SEC_ALL || section == SEC_DISPLAY) {
		PORT_RESET_DISPLAY
	}
}

// Copies a 0.4 save into the 1.0 settings. Everything new keeps its default.
static void upgradeFrom04(const u8* data, size_t size) {
	SettingsV4 v;
	memcpy(&v, data, sizeof v);
	for (int i = 0; i < 7; i++) gSave.best[i] = v.best[i];
	gSave.totalFruit = v.totalFruit;
	gSave.bestCombo = v.bestCombo;
	gSave.sfx = v.sfx;
	gSave.music = v.music;
	gSave.menuSfx = v.menuSfx;
	gSave.menuTrack = v.menuTrack;
	gSave.gameTrack = v.gameTrack;
	gSave.artStyle = v.artStyle;
	gSave.board = v.board;
	gSave.scene = v.scene;
	gSave.blade = v.blade;
	gSave.effects = v.effects;
	gSave.depth = v.depth;
	gSave.shake = v.shake;
	gSave.bgDetail = v.bgDetail;
	gSave.detail = v.detail;
	gSave.sliceStyle = v.sliceStyle;
	gSave.transitions = v.transitions;
	gSave.stylus = v.stylus;
	gSave.difficulty = v.difficulty;
	gSave.fruitSpeed = v.fruitSpeed;
	gSave.bombs = v.bombs;
	gSave.forgiveness = v.forgiveness;
	gSave.frenzyDrain = v.frenzyDrain;
	gSave.misses = v.misses;
	gSave.practiceDone = 1;  // a 0.4 player already knows how to slice
	(void)size;
	PORT_UPGRADE_V4(data, size)
}

// Reads one save file into gSave. Returns false, leaving gSave as it was, if it isn't a good save.
static bool readSave(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	static u8 buf[sizeof(Settings) + 4096];
	size_t n = fread(buf, 1, sizeof buf, f);
	fclose(f);
	u32 magic = 0, sum = 0;
	if (n < 8) return false;
	memcpy(&magic, buf, 4);
	if (magic == kMagic5) {
		u16 size = 0;
		memcpy(&size, buf + 6, 2);
		if (n < 8u + size + 4u) return false;
		memcpy(&sum, buf + 8 + size, 4);
		if (sum != checksum(buf + 8, size)) return false;
		// A shorter save (an older 1.x) keeps the defaults for what it lacks; a longer one is cut.
		memcpy((void*)&gSave, buf + 8, size < sizeof(Settings) ? size : sizeof(Settings));
		return true;
	}
	if (magic == kMagic4) {
		size_t payload = n - 8;  // 60 bytes on the 3DS, 64 on the universal build
		if (payload < sizeof(SettingsV4)) return false;
		memcpy(&sum, buf + 4 + payload, 4);
		if (sum != checksum(buf + 4, payload)) return false;
		upgradeFrom04(buf + 4, payload);
		sKeep04Copy = true;
		return true;
	}
	return false;
}

// 1.1 changed how Zen and Combo Rush score, so bests set under the old rules can't be compared with
// new ones. They are cleared once; every other score stays.
void saveRetireOldBests() {
	if (gSave.scoreRules >= 1) return;
	gSave.best[MODE_ZEN] = gSave.bestAssisted[MODE_ZEN] = 0;
	gSave.best[MODE_COMBO] = gSave.bestAssisted[MODE_COMBO] = 0;
	gSave.scoreRules = 1;
}

void saveLoad() {
	memset((void*)&gSave, 0, sizeof gSave);
	saveDefaults(false, SEC_ALL);
	char file[256], temp[256];
	port::dataPath(file, sizeof file, "save.bin");
	port::dataPath(temp, sizeof temp, "save.tmp");
	// save.tmp is a new save that wasn't swapped in, after a power cut. Use it only when
	// save.bin itself is missing or damaged.
	if (!readSave(file) && !readSave(temp)) {
		FILE* f = fopen(file, "rb");
		if (f) {
			fclose(f);
			fprintf(stderr, "[save] ignoring invalid save file\n");
		}
	}
	saveRetireOldBests();
	gSave.board %= kBoardCount;
	gSave.scene %= kSceneCount;
	gSave.blade %= kBladeCount;
	gSave.artStyle &= 1;
	gSave.effects &= 3;
	gSave.difficulty %= DIFF_COUNT;
	gSave.misses = gSave.misses < 1 ? 1 : (gSave.misses > 5 ? 5 : gSave.misses);
	gSave.bgDetail &= 1;
	gSave.detail &= 1;
	gSave.sliceStyle &= 1;
	gSave.transitions &= 1;
	gSave.seasonalOff &= 1;
	gSave.stickStyle &= 1;
	if (gSave.snapAssist > 2) gSave.snapAssist = 1;
	gSave.vibration &= 1;
	if (gSave.uiScale > 2) gSave.uiScale = 0;
	gSave.colorBlind &= 3;
	gSave.highContrast &= 1;
	gSave.largeText &= 1;
	gSave.calmFx &= 1;
	gSave.sliceAssist &= 1;
	gSave.captions &= 1;
	gSave.sliceNoHold &= 1;
	if (gSave.gameSpeed > 2) gSave.gameSpeed = 0;
	if (gSave.updConsent > 2) gSave.updConsent = 0;
	gSave.practiceDone &= 1;
	if (gSave.trail >= kTrailCount) gSave.trail = 0;
	gSave.updSkipped[sizeof gSave.updSkipped - 1] = 0;
	gSave.ownedThemes |= 1;  // the first of each is always owned
	gSave.ownedTrails |= 1;
	gSave.ownedTracks |= 1;
	PORT_CHECK_SETTINGS
	gSave.menuTrack %= kTrackCount + 1;
	gSave.gameTrack %= kTrackCount + 1;
#if !PORT_CUSTOM_MUSIC
	gSave.beatTrack %= kTrackCount + 1;  // custom songs are checked when Beat Slice starts, as the folder can change
#endif
	shopCheckSave();
	u8* const sliders[] = {&gSave.sfx, &gSave.music, &gSave.menuSfx, &gSave.depth, &gSave.shake, &gSave.stylus, &gSave.fruitSpeed, &gSave.bombs, &gSave.forgiveness, &gSave.frenzyDrain, &gSave.stickSpeed};
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
	const u16 head[2] = {kVersion, (u16)sizeof gSave};
	bool ok = fwrite(&kMagic5, 4, 1, f) == 1 && fwrite(head, 4, 1, f) == 1 && fwrite(&gSave, sizeof gSave, 1, f) == 1 && fwrite(&sum, 4, 1, f) == 1;
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
	if (sKeep04Copy) {
		// The first 1.0 save replaces a 0.4 one. Keep that one too, in case the player goes back to 0.4.
		char old[256];
		port::dataPath(old, sizeof old, "save04.bak");
		remove(old);
		rename(file, old);
		sKeep04Copy = false;
	}
	// Where rename can replace a file this is one safe step. Elsewhere the old save has to go first.
	if (rename(temp, file) != 0) {
		remove(file);
		if (rename(temp, file) != 0) fprintf(stderr, "[save] could not replace %s\n", file);
	}
}
