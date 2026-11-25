#include "mods.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#if !PORT_3DS && !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include <SDL3/SDL.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "port_api.h"
#include "tinfl.inc"

namespace mods {

static constexpr size_t kMaxModFile = 16u * 1024u * 1024u;
static ModInfo sMods[kMaxMods];
static int sCount = 0;
static u32 sIconGen = 1;
static bool sDirty = false;
static bool sInited = false;

static void makeDir(const char* path) {
#if defined(_WIN32)
	_mkdir(path);
#else
	mkdir(path, 0755);
#endif
}

static bool ciEq(const char* a, const char* b) {
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

static bool endsWithCi(const char* s, const char* suf) {
	size_t ls = strlen(s), l2 = strlen(suf);
	return ls >= l2 && ciEq(s + ls - l2, suf);
}

static bool fileExists(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

static u16 rd16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

static bool readDiskFile(const char* fullPath, u8** outBuf, size_t* outSize) {
	FILE* f = fopen(fullPath, "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || (size_t)sz > kMaxModFile) {
		fclose(f);
		return false;
	}
	u8* buf = (u8*)malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return false;
	}
	size_t got = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	if (got != (size_t)sz) {
		free(buf);
		return false;
	}
	buf[sz] = 0;
	*outBuf = buf;
	*outSize = (size_t)sz;
	return true;
}

static bool readZipEntry(const char* zipPath, const char* relPath, u8** outBuf, size_t* outSize) {
	u8* zdata = nullptr;
	size_t zsize = 0;
	if (!readDiskFile(zipPath, &zdata, &zsize)) return false;
	if (zsize < 22) {
		free(zdata);
		return false;
	}

	// Locate End of Central Directory (PK\x05\x06)
	long eocd = -1;
	long minOff = (long)zsize - 65557;
	if (minOff < 0) minOff = 0;
	for (long i = (long)zsize - 22; i >= minOff; i--) {
		if (zdata[i] == 0x50 && zdata[i + 1] == 0x4b && zdata[i + 2] == 0x05 && zdata[i + 3] == 0x06) {
			eocd = i;
			break;
		}
	}
	if (eocd < 0) {
		free(zdata);
		return false;
	}

	u32 cdSize = rd32(zdata + eocd + 12);
	u32 cdOff = rd32(zdata + eocd + 16);
	if ((size_t)cdOff + (size_t)cdSize > zsize) {
		free(zdata);
		return false;
	}

	// Normalize target path (strip leading '/')
	while (*relPath == '/' || *relPath == '\\') relPath++;

	bool found = false;
	size_t pos = cdOff;
	size_t cdEnd = (size_t)cdOff + (size_t)cdSize;
	while (pos + 46 <= cdEnd) {
		if (rd32(zdata + pos) != 0x02014b50u) break;
		u16 method = rd16(zdata + pos + 10);
		u32 compSize = rd32(zdata + pos + 20);
		u32 uncompSize = rd32(zdata + pos + 24);
		u16 nameLen = rd16(zdata + pos + 28);
		u16 extraLen = rd16(zdata + pos + 30);
		u16 cmtLen = rd16(zdata + pos + 32);
		u32 lfhOff = rd32(zdata + pos + 42);
		if (pos + 46 + nameLen > cdEnd) break;

		char ename[256];
		size_t copyLen = nameLen < sizeof(ename) - 1 ? nameLen : sizeof(ename) - 1;
		memcpy(ename, zdata + pos + 46, copyLen);
		ename[copyLen] = 0;
		for (size_t k = 0; k < copyLen; k++) if (ename[k] == '\\') ename[k] = '/';

		if (ciEq(ename, relPath) && uncompSize > 0 && uncompSize <= kMaxModFile) {
			if ((size_t)lfhOff + 30 <= zsize && rd32(zdata + lfhOff) == 0x04034b50u) {
				u16 lName = rd16(zdata + lfhOff + 26);
				u16 lExtra = rd16(zdata + lfhOff + 28);
				size_t dataOff = (size_t)lfhOff + 30 + lName + lExtra;
				if (dataOff + compSize <= zsize) {
					u8* out = (u8*)malloc(uncompSize + 1);
					if (out) {
						bool ok = false;
						if (method == 0 && compSize == uncompSize) {
							memcpy(out, zdata + dataOff, uncompSize);
							ok = true;
						} else if (method == 8) {
							ok = tinfl::inflateRaw(zdata + dataOff, compSize, out, uncompSize);
						}
						if (ok) {
							out[uncompSize] = 0;
							*outBuf = out;
							*outSize = uncompSize;
							found = true;
						} else {
							free(out);
						}
					}
				}
			}
			break;
		}
		pos += 46 + nameLen + extraLen + cmtLen;
	}
	free(zdata);
	return found;
}

static bool parseModTxt(const char* txt, size_t len, ModInfo& m) {
	m.name[0] = 0;
	snprintf(m.author, sizeof m.author, "Unknown");
	snprintf(m.version, sizeof m.version, "1.0");
	snprintf(m.desc, sizeof m.desc, "Custom Fruit Slicer mod.");

	size_t i = 0;
	while (i < len) {
		size_t end = i;
		while (end < len && txt[end] != '\n' && txt[end] != '\r') end++;
		if (end > i) {
			char line[256];
			size_t l = end - i;
			if (l >= sizeof(line)) l = sizeof(line) - 1;
			memcpy(line, txt + i, l);
			line[l] = 0;
			char* eq = strchr(line, '=');
			if (eq) {
				*eq = 0;
				const char* k = line;
				const char* v = eq + 1;
				while (*k == ' ' || *k == '\t') k++;
				while (*v == ' ' || *v == '\t') v++;
				if (ciEq(k, "name") && v[0]) snprintf(m.name, sizeof m.name, "%s", v);
				else if (ciEq(k, "author") && v[0]) snprintf(m.author, sizeof m.author, "%s", v);
				else if (ciEq(k, "version") && v[0]) snprintf(m.version, sizeof m.version, "%s", v);
				else if (ciEq(k, "description") && v[0]) snprintf(m.desc, sizeof m.desc, "%s", v);
			}
		}
		i = end + 1;
	}
	return m.name[0] != 0;
}

static bool isValidPng(const u8* data, size_t size) {
	static const u8 kSig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
	return data && size >= 24 && memcmp(data, kSig, 8) == 0;
}

static void freeModIcons() {
	for (int i = 0; i < sCount; i++) {
		if (sMods[i].iconPng) {
			free(sMods[i].iconPng);
			sMods[i].iconPng = nullptr;
		}
		sMods[i].iconSize = 0;
		sMods[i].hasIcon = false;
	}
}

void modsDir(char* out, size_t cap) {
	port::dataPath(out, cap, "mods");
}

static void writeSeedFiles() {
	char dir[480], path[512];
	modsDir(dir, sizeof dir);
	makeDir(dir);

	snprintf(path, sizeof path, "%s/README.md", dir);
	if (!fileExists(path)) {
		if (FILE* f = fopen(path, "wb")) {
			static const char kReadme[] =
				"# Fruit Slicer 2.1.0 Mods Folder\n\n"
				"Drop extracted mod folders or `.zip` archives into this folder.\n"
				"(`modTemplate.zip` is provided as a starter reference and is ignored by the game.)\n\n"
				"## Required `mod.txt` (`key=value`)\n"
				"```\n"
				"name=My Mod Name\n"
				"author=YourName\n"
				"version=1.0.0\n"
				"description=Short description shown in the Mods menu.\n"
				"```\n\n"
				"## Optional `icon.png`\n"
				"Place a 64x64 or 128x128 `icon.png` in the mod root. Missing icons show a black `?` tile.\n\n"
				"## 3DS LayeredFS Fallback\n"
				"If using Luma3DS LayeredFS instead of runtime `sdmc:/3ds/FruitSlicer/mods/`, place romfs\n"
				"overrides in `luma/titles/000400000F51CE00/romfs/`.\n";
			fwrite(kReadme, 1, sizeof(kReadme) - 1, f);
			fclose(f);
		}
	}

	snprintf(path, sizeof path, "%s/modTemplate.zip", dir);
	if (!fileExists(path)) {
		if (port::AssetFile* a = port::assetOpenRaw("modTemplate.zip")) {
			long sz = port::assetSize(a);
			if (sz > 0 && sz < 2 * 1024 * 1024) {
				u8* buf = (u8*)malloc((size_t)sz);
				if (buf && port::assetRead(buf, (size_t)sz, a) == (size_t)sz) {
					if (FILE* f = fopen(path, "wb")) {
						fwrite(buf, 1, (size_t)sz, f);
						fclose(f);
					}
				}
				free(buf);
			}
			port::assetClose(a);
		}
	}
}

void init() {
	if (sInited) return;
	sInited = true;
	port::ensureDataDir();
	writeSeedFiles();
	rescan();
}

void rescan() {
	freeModIcons();
	sCount = 0;

	char dir[480];
	modsDir(dir, sizeof dir);
	makeDir(dir);

	ModInfo found[kMaxMods];
	int foundN = 0;

	auto tryAdd = [&](const char* entryName, bool isDir) {
		if (foundN >= kMaxMods) return;
		if (entryName[0] == '.') return;
		if (ciEq(entryName, "modTemplate.zip") || ciEq(entryName, "README.md") || ciEq(entryName, "mods.txt")) return;
		if (!isDir && !endsWithCi(entryName, ".zip")) return;

		char full[512];
		snprintf(full, sizeof full, "%s/%s", dir, entryName);

		u8* txtBuf = nullptr;
		size_t txtSize = 0;
		bool okTxt = false;
		if (isDir) {
			char txtPath[540];
			snprintf(txtPath, sizeof txtPath, "%s/mod.txt", full);
			okTxt = readDiskFile(txtPath, &txtBuf, &txtSize);
		} else {
			okTxt = readZipEntry(full, "mod.txt", &txtBuf, &txtSize);
		}
		if (!okTxt) return;

		ModInfo m{};
		snprintf(m.id, sizeof m.id, "%s", entryName);
		m.isZip = !isDir;
		m.enabled = false;
		const bool validMeta = parseModTxt((const char*)txtBuf, txtSize, m);
		free(txtBuf);
		if (!validMeta) return;

		u8* icBuf = nullptr;
		size_t icSize = 0;
		bool okIc = false;
		if (isDir) {
			char icPath[540];
			snprintf(icPath, sizeof icPath, "%s/icon.png", full);
			okIc = readDiskFile(icPath, &icBuf, &icSize);
		} else {
			okIc = readZipEntry(full, "icon.png", &icBuf, &icSize);
		}
		if (okIc && isValidPng(icBuf, icSize)) {
			m.hasIcon = true;
			m.iconPng = icBuf;
			m.iconSize = icSize;
		} else {
			if (icBuf) free(icBuf);
			m.hasIcon = false;
		}
		found[foundN++] = m;
	};

#if defined(_WIN32)
	char pat[512];
	snprintf(pat, sizeof pat, "%s/*", dir);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pat, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			tryAdd(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
#else
	if (DIR* d = opendir(dir)) {
		while (struct dirent* e = readdir(d)) {
			char full[512];
			snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
			struct stat st{};
			if (stat(full, &st) == 0) {
				tryAdd(e->d_name, S_ISDIR(st.st_mode));
			}
		}
		closedir(d);
	}
#endif

	// Apply saved priority order & enabled flags from mods/mods.txt
	bool used[kMaxMods] = {};
	char orderPath[512];
	snprintf(orderPath, sizeof orderPath, "%s/mods.txt", dir);
	if (FILE* f = fopen(orderPath, "rb")) {
		char line[200];
		while (fgets(line, sizeof line, f)) {
			size_t l = strlen(line);
			while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
			if (l < 3) continue;
			bool en = (line[0] == '1');
			const char* id = line + 2;
			for (int i = 0; i < foundN; i++) {
				if (!used[i] && strcmp(found[i].id, id) == 0) {
					found[i].enabled = en;
					sMods[sCount++] = found[i];
					used[i] = true;
					break;
				}
			}
		}
		fclose(f);
	}
	for (int i = 0; i < foundN; i++) {
		if (!used[i] && sCount < kMaxMods) {
			sMods[sCount++] = found[i];
		}
	}
	sIconGen++;
}

void saveOrder() {
	char dir[480], path[512];
	modsDir(dir, sizeof dir);
	makeDir(dir);
	snprintf(path, sizeof path, "%s/mods.txt", dir);
	if (FILE* f = fopen(path, "wb")) {
		for (int i = 0; i < sCount; i++) {
			fprintf(f, "%d %s\n", sMods[i].enabled ? 1 : 0, sMods[i].id);
		}
		fclose(f);
	}
}

int count() { return sCount; }
const ModInfo* get(int index) { return (index >= 0 && index < sCount) ? &sMods[index] : nullptr; }
u32 iconGen() { return sIconGen; }

void setEnabled(int index, bool on) {
	if (index < 0 || index >= sCount || sMods[index].enabled == on) return;
	sMods[index].enabled = on;
	sDirty = true;
	saveOrder();
}

void move(int index, int dir) {
	int dst = index + dir;
	if (index < 0 || index >= sCount || dst < 0 || dst >= sCount || index == dst) return;
	ModInfo tmp = sMods[index];
	sMods[index] = sMods[dst];
	sMods[dst] = tmp;
	sDirty = true;
	saveOrder();
}

bool isDirty() { return sDirty; }
void clearDirty() { sDirty = false; }

bool readAsset(const char* relPath, u8** outBuf, size_t* outSize) {
	if (!sInited || !relPath || !relPath[0]) return false;
	while (*relPath == '/') relPath++;

	char dir[480];
	modsDir(dir, sizeof dir);
	for (int i = 0; i < sCount; i++) {
		if (!sMods[i].enabled) continue;
		char full[512];
		snprintf(full, sizeof full, "%s/%s", dir, sMods[i].id);
		if (sMods[i].isZip) {
			if (readZipEntry(full, relPath, outBuf, outSize)) return true;
		} else {
			char fileP[540];
			snprintf(fileP, sizeof fileP, "%s/%s", full, relPath);
			if (readDiskFile(fileP, outBuf, outSize)) return true;
		}
	}
	return false;
}

bool readAudioAsFsa1(const char* pcmRelPath, u8** outBuf, size_t* outSize) {
	if (readAsset(pcmRelPath, outBuf, outSize)) return true;
	if (!endsWithCi(pcmRelPath, ".pcm")) return false;
	char wavPath[256];
	size_t l = strlen(pcmRelPath);
	if (l >= sizeof(wavPath)) return false;
	memcpy(wavPath, pcmRelPath, l - 4);
	memcpy(wavPath + l - 4, ".wav", 5);
	u8* w = nullptr;
	size_t wsz = 0;
	if (!readAsset(wavPath, &w, &wsz)) return false;
	if (wsz < 44 || memcmp(w, "RIFF", 4) != 0 || memcmp(w + 8, "WAVE", 4) != 0) {
		free(w);
		return false;
	}
	u16 fmt = 0, ch = 0, bps = 0;
	u32 rate = 0;
	const u8* pcm = nullptr;
	u32 pcmBytes = 0;
	size_t pos = 12;
	while (pos + 8 <= wsz) {
		u32 cid = rd32(w + pos);
		u32 csz = rd32(w + pos + 4);
		pos += 8;
		if (pos + csz > wsz) break;
		if (cid == 0x20746d66u && csz >= 16) {  // "fmt "
			fmt = rd16(w + pos);
			ch = rd16(w + pos + 2);
			rate = rd32(w + pos + 4);
			bps = rd16(w + pos + 14);
		} else if (cid == 0x61746164u) {  // "data"
			pcm = w + pos;
			pcmBytes = csz;
			break;
		}
		pos += csz + (csz & 1u);
	}
	if (fmt != 1 || (ch != 1 && ch != 2) || bps != 16 || rate < 8000 || rate > 96000 || !pcm || pcmBytes < (u32)(ch * 2)) {
		free(w);
		return false;
	}
	u32 frames = pcmBytes / (ch * 2);
	u8* out = (u8*)malloc(12 + (size_t)frames * 2);
	if (!out) {
		free(w);
		return false;
	}
	u32 hdr[3] = {0x31415346u, frames, rate};  // "FSA1"
	memcpy(out, hdr, 12);
	s16* dst = (s16*)(out + 12);
	const s16* src = (const s16*)pcm;
	if (ch == 1) {
		memcpy(dst, src, (size_t)frames * 2);
	} else {
		for (u32 i = 0; i < frames; i++) {
			dst[i] = (s16)(((int)src[i * 2] + (int)src[i * 2 + 1]) / 2);
		}
	}
	free(w);
	*outBuf = out;
	*outSize = 12 + (size_t)frames * 2;
	return true;
}

bool importZip(const char* filename, const u8* data, size_t size) {
	if (!filename || !filename[0] || !data || size < 22 || size > kMaxModFile) return false;
	const char* base = strrchr(filename, '/');
	if (base) filename = base + 1;
	const char* b2 = strrchr(filename, '\\');
	if (b2) filename = b2 + 1;
	if (!endsWithCi(filename, ".zip") || ciEq(filename, "modTemplate.zip")) return false;

	char dir[480], path[512];
	modsDir(dir, sizeof dir);
	makeDir(dir);
	snprintf(path, sizeof path, "%s/%s", dir, filename);
	FILE* f = fopen(path, "wb");
	if (!f) return false;
	size_t wr = fwrite(data, 1, size, f);
	fclose(f);
	if (wr != size) return false;
	rescan();
	return true;
}

#ifdef __EMSCRIPTEN__
extern "C" {
EMSCRIPTEN_KEEPALIVE void fs_web_import_zip(const char* name, const u8* data, int size) {
	if (name && data && size > 0) {
		importZip(name, data, (size_t)size);
		EM_ASM({ FS.syncfs(false, function() {}); });
	}
}
}
#endif

void openFolder() {
	char dir[480];
	modsDir(dir, sizeof dir);
	makeDir(dir);
#if defined(_WIN32)
	ShellExecuteA(nullptr, "open", dir, nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__EMSCRIPTEN__)
	EM_ASM({
		var inp = document.createElement('input');
		inp.type = 'file';
		inp.accept = '.zip';
		inp.onchange = function(ev) {
			var f = ev.target.files[0];
			if (!f) return;
			var rd = new FileReader();
			rd.onload = function() {
				var u8 = new Uint8Array(rd.result);
				var ptr = _malloc(u8.length);
				HEAPU8.set(u8, ptr);
				var nlen = lengthBytesUTF8(f.name) + 1;
				var nptr = _malloc(nlen);
				stringToUTF8(f.name, nptr, nlen);
				_fs_web_import_zip(nptr, ptr, u8.length);
				_free(ptr);
				_free(nptr);
			};
			rd.readAsArrayBuffer(f);
		};
		inp.click();
	});
#elif !PORT_3DS && !defined(__ANDROID__)
	char url[520];
	snprintf(url, sizeof url, "file://%s", dir);
	SDL_OpenURL(url);
#endif
}

}  // namespace mods
