// What every port provides to the shared game, besides render.h, audio_port.h and model3d_internal.h.
#pragma once
#include <stddef.h>

namespace port {
struct AssetFile;
// Read-only game files, by path inside the assets ("audio/click.pcm").
AssetFile* assetOpen(const char* path);
size_t assetRead(void* out, size_t bytes, AssetFile* f);
long assetSize(AssetFile* f);
void assetClose(AssetFile* f);  // also stops a stream on it first
// 3DS only: music arrives on a background thread, so a slow cartridge read never holds up a frame.
// streamStart begins reading `bytes` into `out` (once per file; later calls do nothing), and streamDone
// gives how many bytes have arrived so far, or -1 if a read failed.
void streamStart(AssetFile* f, void* out, size_t bytes);
long streamDone(AssetFile* f);
// Any file by its whole path, read the same way: the player's own songs (Windows, Linux and Android only).
AssetFile* fileOpen(const char* fullPath);

// Writable files: saves, and anything a player might want to send in.
void dataPath(char* out, size_t size, const char* name);
void ensureDataDir();

// False once the system or player wants the game closed.
bool running();

// Updates (update.cpp). One network job at a time, in the background; each start replaces the last.
const char* updateRepo();   // the game's GitHub repository, "owner/name"
const char* updateAsset();  // the end of the release file name this build installs, or null if it can't
bool netGetText(const char* url);                            // false if there's no network to use
bool netDownload(const char* url, unsigned long long size);  // fetches a release file and installs it
int netPoll(float* progress);  // 0 still working, 1 done, -1 failed
char* netText();               // a finished text fetch, kept until the next start
void updateRestart();          // once the game has closed, start the new version (3DS: back to HOME)

// Opens a web page in the system browser (Windows, Linux and Android only: the main menu's link icons).
void openLink(const char* url);
}  // namespace port
