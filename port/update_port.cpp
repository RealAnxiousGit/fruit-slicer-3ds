// Updates on the 3DS: libcurl with mbedTLS (the console's own HTTPS is too old for GitHub), on a background
// thread. The CIA build installs a new CIA as it downloads, 64 KB at a time, straight into the system's
// installer (AM), as the update test proved on a New 2DS XL; nothing is written to the SD card first. A
// download that fails or comes up short is cancelled, leaving the installed game as it was. The 3DSX build
// can't install itself, so it only says that a new version is out.
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
	if (R_FAILED(FSFILE_Write(sCia, &w, sWritten, sBlock, sBlockLen, 0)) || w != sBlockLen) {
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
const char* updateAsset() { return envIsHomebrew() ? nullptr : ".cia"; }

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

// The new version is already installed; closing the game goes back to the HOME Menu, where it opens.
void updateRestart() {}
}  // namespace port
