#pragma once
#include <stddef.h>
#include "common.h"

namespace mods {

static constexpr int kMaxMods = 64;

struct ModInfo {
	char id[128];          // folder name or .zip filename
	char name[64];
	char author[48];
	char version[24];
	char desc[160];
	bool isZip;
	bool enabled;
	bool hasIcon;
	u8* iconPng;           // raw PNG bytes for icon.png (nullptr if missing)
	size_t iconSize;
};

void init();
void rescan();
void saveOrder();

int count();
const ModInfo* get(int index);
u32 iconGen();
void setEnabled(int index, bool on);
void move(int index, int dir);  // dir: -1 = higher priority (up), +1 = lower priority (down)

bool isDirty();
void clearDirty();

// Reads relPath from the highest-priority enabled mod that provides it.
// Caller must free(*outBuf) when true is returned.
bool readAsset(const char* relPath, u8** outBuf, size_t* outSize);
bool readAudioAsFsa1(const char* pcmRelPath, u8** outBuf, size_t* outSize);

// Writes a .zip mod into modsDir and rescans (used by Web Import and Lab tests).
bool importZip(const char* filename, const u8* data, size_t size);

void modsDir(char* out, size_t cap);
void openFolder();  // Desktop: opens folder; Web: triggers .zip upload; 3DS/Android: no-op

}  // namespace mods
