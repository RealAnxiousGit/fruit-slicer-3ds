// Updates on the 3DS: libcurl with mbedTLS (the console's own HTTPS is too old for GitHub), on a background
// thread. The CIA build installs a new CIA as it downloads, 64 KB at a time, straight into the system's
// installer (AM), as the update test proved on a New 2DS XL; nothing is written to the SD card first. A
// download that fails or comes up short is cancelled, leaving the installed game as it was. The 3DSX build
// downloads beside itself as "<name>.3dsx.new". Its art and music live inside its own file, so the swap waits
// until the game has closed that file, and returning from main then lands in the Homebrew Launcher.
#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "port_api.h"

namespace {
constexpr u32 kSocSize = 0x100000;
u32* sSocBuf;
Thread sThread;
volatile int sResult;  // 0 working, 1 done, -1 failed
volatile u32 sGot;
u64 sExpect;
bool sText;            // a text fetch, not an install
char sUrl[600];
char* sReply;
size_t sReplyLen, sReplyCap;
Handle sCia;
u64 sWritten;
bool sWriteFailed;
u8 sBlock[64 * 1024] __attribute__((aligned(0x1000)));
u32 sBlockLen;
FILE* sOut;            // the 3DSX build's download; null while installing a CIA
char sIncoming[300];
bool sSwapDue;         // a whole new 3DSX is waiting beside the running one

// libctru keeps the launcher's arguments here. The first is the running file, when a launcher passed one.
extern "C" int __system_argc;
extern "C" char** __system_argv;

const char* selfPath() {
	if (__system_argc < 1 || !__system_argv || !__system_argv[0]) return nullptr;
	const char* p = __system_argv[0];
	const size_t n = strlen(p);
	if (strncmp(p, "sdmc:/", 6) != 0 || n < 11 || n > 280 || strcmp(p + n - 5, ".3dsx") != 0) return nullptr;
	return p;
}

bool isHomebrewFile(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	char magic[4] = {};
	const bool read = fread(magic, 1, 4, f) == 4;
	fclose(f);
	return read && memcmp(magic, "3DSX", 4) == 0;
}

bool netUp() {
	if (sSocBuf) return true;
	u32 wifi = 0;
	acInit();
	ACU_GetWifiStatus(&wifi);
	acExit();
	if (wifi == 0) return false;
	sSocBuf = (u32*)memalign(0x1000, kSocSize);
	if (!sSocBuf || R_FAILED(socInit(sSocBuf, kSocSize))) {
		free(sSocBuf);
		sSocBuf = nullptr;
		return false;
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);
	return true;
}

size_t toText(char* p, size_t size, size_t n, void*) {
	const size_t add = size * n;
	if (sReplyLen + add + 1 > sReplyCap) {
		const size_t cap = (sReplyLen + add + 1) * 2;
		if (cap > 512 * 1024) return 0;  // far bigger than a release list
		char* t = (char*)realloc(sReply, cap);
		if (!t) return 0;
		sReply = t;
		sReplyCap = cap;
	}
	memcpy(sReply + sReplyLen, p, add);
	sReplyLen += add;
	sReply[sReplyLen] = 0;
	sGot += add;
	return add;
}

bool flushBlock() {
	if (!sBlockLen) return true;
	u32 w = 0;
	if (sOut) w = fwrite(sBlock, 1, sBlockLen, sOut);
	else if (R_FAILED(FSFILE_Write(sCia, &w, sWritten, sBlock, sBlockLen, 0))) w = 0;
	if (w != sBlockLen) {
		sWriteFailed = true;
		return false;
	}
	sWritten += sBlockLen;
	sBlockLen = 0;
	return true;
}

// The installer takes whole 64 KB blocks; network pieces arrive in odd sizes, so they are gathered first.
size_t toInstaller(char* p, size_t size, size_t n, void*) {
	size_t left = size * n;
	const size_t add = left;
	while (left) {
		size_t take = sizeof sBlock - sBlockLen;
		if (take > left) take = left;
		memcpy(sBlock + sBlockLen, p, take);
		sBlockLen += take;
		p += take;
		left -= take;
		if (sBlockLen == sizeof sBlock && !flushBlock()) return 0;
	}
	sGot += add;
	return add;
}

void work(void*) {
	CURL* c = curl_easy_init();
	bool ok = false;
	if (c) {
		curl_easy_setopt(c, CURLOPT_URL, sUrl);
		curl_easy_setopt(c, CURLOPT_USERAGENT, "FruitSlicer3DS");
		curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem");
		curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
		curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 512L);
		curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
		curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 64L * 1024);
		curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sText ? toText : toInstaller);
		ok = curl_easy_perform(c) == CURLE_OK;
		curl_easy_cleanup(c);
	}
	if (sText) {
		ok = ok && sReply;
	} else if (sOut) {
		ok = ok && flushBlock() && sGot == sExpect && !sWriteFailed;
		ok = fclose(sOut) == 0 && ok;
		sOut = nullptr;
		ok = ok && isHomebrewFile(sIncoming);
		if (ok) sSwapDue = true;
		else remove(sIncoming);
	} else {
		ok = ok && flushBlock() && sGot == sExpect && !sWriteFailed;
		if (ok) ok = R_SUCCEEDED(AM_FinishCiaInstall(sCia));
		else AM_CancelCIAInstall(sCia);
		amExit();
	}
	sResult = ok ? 1 : -1;
}

bool begin(bool text, const char* url) {
	if (sThread) {
		if (sResult == 0) return false;  // one at a time
		threadJoin(sThread, U64_MAX);
		threadFree(sThread);
		sThread = nullptr;
	}
	if (!netUp()) return false;
	free(sReply);
	sReply = nullptr;
	sReplyLen = sReplyCap = 0;
	sText = text;
	snprintf(sUrl, sizeof sUrl, "%s", url);
	sResult = 0;
	sGot = 0;
	// Lowest priority, on the game's own core: it only runs in the menus, where there's time to spare.
	sThread = threadCreate(work, nullptr, 128 * 1024, 0x3F, -2, false);
	return sThread != nullptr;
}
}  // namespace

namespace port {
const char* updateRepo() { return "RealAnxiousGit/fruit-slicer-3ds"; }
const char* updateAsset() {
	if (!envIsHomebrew()) return ".cia";
	return selfPath() ? ".3dsx" : nullptr;  // started without its path, it can't know which file to replace
}

// Put the new file in place of the old. The old one is only deleted once the new one has its name.
bool replaceFile(const char* target, const char* incoming) {
	if (strlen(target) > 280) return false;
	FILE* f = fopen(incoming, "rb");
	if (!f) return false;
	fclose(f);
	char old[300];
	snprintf(old, sizeof old, "%s.old", target);
	remove(old);
	if (rename(target, old) != 0) return false;
	if (rename(incoming, target) != 0) {
		rename(old, target);
		return false;
	}
	remove(old);
	return true;
}

void updateTidy() {
	const char* self = envIsHomebrew() ? selfPath() : nullptr;
	if (!self) return;
	char path[300];
	snprintf(path, sizeof path, "%s.new", self);
	remove(path);  // a download the console was switched off before using
	snprintf(path, sizeof path, "%s.old", self);
	remove(path);
}

void updateFinish() {
	if (sSwapDue && selfPath()) replaceFile(selfPath(), sIncoming);
	sSwapDue = false;
}

bool netGetText(const char* url) {
	sExpect = 0;
	return begin(true, url);
}

bool netDownload(const char* url, unsigned long long size) {
	if (!updateAsset()) return false;
	if (sThread && sResult == 0) return false;
	sExpect = size;
	sBlockLen = 0;
	sWritten = 0;
	sWriteFailed = false;
	if (envIsHomebrew()) {
		snprintf(sIncoming, sizeof sIncoming, "%s.new", selfPath());
		sSwapDue = false;
		sOut = fopen(sIncoming, "wb");
		if (!sOut) return false;
		if (!begin(false, url)) {
			fclose(sOut);
			sOut = nullptr;
			remove(sIncoming);
			return false;
		}
		return true;
	}
	if (R_FAILED(amInit())) return false;
	if (R_FAILED(AM_StartCiaInstall(MEDIATYPE_SD, &sCia))) {
		amExit();
		return false;
	}
	if (!begin(false, url)) {
		AM_CancelCIAInstall(sCia);
		amExit();
		return false;
	}
	return true;
}

int netPoll(float* progress) {
	const int r = sResult;
	if (progress) *progress = sExpect ? (float)sGot / (float)sExpect : 0;
	if (r != 0 && sThread) {
		threadJoin(sThread, U64_MAX);
		threadFree(sThread);
		sThread = nullptr;
	}
	return r;
}

char* netText() { return sReply; }

// A CIA is already installed, and a 3DSX swaps in updateFinish: either way, closing the game is all that's left.
void updateRestart() {}
}  // namespace port
