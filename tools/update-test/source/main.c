// Fruit Slicer 3DS update test. A separate app with its own title ID, never released.
// It answers one question for 1.0: can the game update itself from GitHub over the 3DS's Wi-Fi?
//   Part A: ask GitHub for the newest Fruit Slicer 3DS release, show its notes, download its CIA.
//   Part B: install a newer copy of this test app over itself while it runs, then go back to HOME.
#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef TEST_VERSION
#define TEST_VERSION 1
#endif

#define API_URL "https://api.github.com/repos/RealAnxiousGit/fruit-slicer-3ds/releases?per_page=1"
#define USER_AGENT "FruitSlicer3DS-UpdateTest"
#define SELF_CIA "sdmc:/updtest/updtest_v2.cia"
#define SOC_SIZE 0x100000

static PrintConsole sTop, sBottom;
static u32* sSocBuf;

// ---------------------------------------------------------------- small helpers

static void waitA(void) {
	printf("\n  Press A to go on.\n");
	while (aptMainLoop()) {
		hidScanInput();
		if (hidKeysDown() & KEY_A) break;
		gspWaitForVBlank();
	}
}

// libctru hands the whole app region to the heap at start-up, so the OS always reports 0 free.
// What matters is what malloc can still give out: the unused heap plus freed blocks inside it.
extern u32 __ctru_heap_size;
static u32 freeKb(void) {
	struct mallinfo m = mallinfo();
	return (u32)((__ctru_heap_size - m.arena + m.fordblks) / 1024);
}
static u32 sLowKb;
static void noteFree(void) {
	u32 f = freeKb();
	if (f < sLowKb) sLowKb = f;
}

static bool netUp(void) {
	if (sSocBuf) return true;
	u32 wifi = 0;
	ACU_GetWifiStatus(&wifi);
	if (wifi == 0) {
		printf("  Wi-Fi is not connected.\n  Turn Wi-Fi on, then try again.\n");
		return false;
	}
	sSocBuf = (u32*)memalign(0x1000, SOC_SIZE);
	if (!sSocBuf || R_FAILED(socInit(sSocBuf, SOC_SIZE))) {
		printf("  Could not start networking.\n");
		free(sSocBuf);
		sSocBuf = NULL;
		return false;
	}
	return true;
}

static void netDown(void) {
	if (!sSocBuf) return;
	socExit();
	free(sSocBuf);
	sSocBuf = NULL;
}

// ---------------------------------------------------------------- downloads

typedef struct {
	char* text;          // API reply, grown as it arrives
	size_t len, cap;
	u64 bytes;           // file download: bytes seen
	u64 expect;
	Handle cia;          // file download: open AM install, or 0 to only count
	u64 written;         // bytes handed to the installer so far
	Result err;          // the installer's error, if a write failed
	bool failed;
	u64 started;
	int lastPct;
} Sink;

static size_t toText(char* p, size_t sz, size_t n, void* user) {
	Sink* s = (Sink*)user;
	size_t add = sz * n;
	if (s->len + add + 1 > s->cap) {
		size_t cap = (s->len + add + 1) * 2;
		if (cap > 256 * 1024) return 0;   // a release list this big is not expected
		char* t = (char*)realloc(s->text, cap);
		if (!t) return 0;
		s->text = t;
		s->cap = cap;
	}
	memcpy(s->text + s->len, p, add);
	s->len += add;
	s->text[s->len] = 0;
	return add;
}

// The installer gets whole 64 KB blocks, the same way Part B feeds it (which works).
// Network chunks arrive in odd sizes, so they are gathered here first.
static u8 sBlock[64 * 1024] __attribute__((aligned(0x1000)));
static u32 sBlockLen;

static bool flushBlock(Sink* s) {
	if (!sBlockLen) return true;
	u32 w = 0;
	Result r = FSFILE_Write(s->cia, &w, s->written, sBlock, sBlockLen, 0);
	if (R_FAILED(r) || w != sBlockLen) {
		s->err = R_FAILED(r) ? r : -1;
		s->failed = true;
		return false;
	}
	s->written += sBlockLen;
	sBlockLen = 0;
	return true;
}

static size_t toFile(char* p, size_t sz, size_t n, void* user) {
	Sink* s = (Sink*)user;
	size_t add = sz * n;
	if (s->cia) {
		size_t left = add;
		while (left) {
			size_t take = sizeof sBlock - sBlockLen;
			if (take > left) take = left;
			memcpy(sBlock + sBlockLen, p, take);
			sBlockLen += take;
			p += take;
			left -= take;
			if (sBlockLen == sizeof sBlock && !flushBlock(s)) return 0;
		}
	}
	s->bytes += add;
	noteFree();
	int pct = s->expect ? (int)(s->bytes * 100 / s->expect) : 0;
	if (pct / 10 != s->lastPct / 10) {
		double secs = (osGetTime() - s->started) / 1000.0;
		printf("  %3d%%  %llu KB  %.0f KB/s  free %lu KB\n", pct, s->bytes / 1024, secs > 0 ? s->bytes / 1024.0 / secs : 0.0, freeKb());
		s->lastPct = pct;
	}
	return add;
}

static CURL* newCurl(const char* url) {
	CURL* c = curl_easy_init();
	if (!c) return NULL;
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem");
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 512L);   // give up if under 512 B/s
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);     // for 20 seconds
	curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 64L * 1024);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	return c;
}

// ---------------------------------------------------------------- tiny JSON field finder

// Finds "key": after `from` and copies the string value (JSON escapes for \n and \" handled).
static const char* jsonStr(const char* from, const char* key, char* out, size_t outLen) {
	char pat[64];
	snprintf(pat, sizeof pat, "\"%s\":", key);
	const char* p = strstr(from, pat);
	if (!p) return NULL;
	p += strlen(pat);
	while (*p == ' ') p++;
	if (*p != '"') return NULL;
	p++;
	size_t o = 0;
	while (*p && *p != '"') {
		char ch = *p++;
		if (ch == '\\' && *p) {
			char e = *p++;
			ch = e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? ' ' : e;
		}
		if (ch != '\r' && o + 1 < outLen) out[o++] = ch;
	}
	out[o] = 0;
	return p;
}

static const char* jsonNum(const char* from, const char* key, u64* out) {
	char pat[64];
	snprintf(pat, sizeof pat, "\"%s\":", key);
	const char* p = strstr(from, pat);
	if (!p) return NULL;
	*out = strtoull(p + strlen(pat), NULL, 10);
	return p;
}

// ---------------------------------------------------------------- Part A

static char sTag[64], sCiaUrl[512];
static u64 sCiaSize;

static void partA_check(void) {
	consoleSelect(&sTop);
	consoleClear();
	time_t now = time(NULL);
	struct tm* t = gmtime(&now);
	printf("Part A1: check GitHub\n\n");
	printf("  Console clock (UTC): %04d-%02d-%02d %02d:%02d\n", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
	printf("  (HTTPS needs this date to be right.)\n");
	printf("  Free memory: %lu KB\n", freeKb());
	if (!netUp()) { waitA(); return; }

	Sink s = {0};
	CURL* c = newCurl(API_URL);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toText);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
	u64 t0 = osGetTime();
	CURLcode r = curl_easy_perform(c);
	long code = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(c);
	printf("  Reply in %.1f s, HTTP %ld, %u bytes\n", (osGetTime() - t0) / 1000.0, code, (unsigned)s.len);
	if (r != CURLE_OK || !s.text) {
		printf("\n  FAILED: %s\n", curl_easy_strerror(r));
		free(s.text);
		waitA();
		return;
	}

	static char body[2048];
	jsonStr(s.text, "tag_name", sTag, sizeof sTag);
	jsonStr(s.text, "body", body, sizeof body);
	sCiaUrl[0] = 0;
	sCiaSize = 0;
	const char* a = s.text;
	char name[128];
	while ((a = jsonStr(a, "name", name, sizeof name))) {
		size_t n = strlen(name);
		if (n > 4 && strcmp(name + n - 4, ".cia") == 0) {
			jsonNum(a, "size", &sCiaSize);
			jsonStr(a, "browser_download_url", sCiaUrl, sizeof sCiaUrl);
			break;
		}
	}
	free(s.text);

	printf("\n  Newest release: %s\n", sTag[0] ? sTag : "(none found)");
	printf("  CIA: %s, %llu KB\n", sCiaUrl[0] ? "found" : "MISSING", sCiaSize / 1024);
	consoleSelect(&sBottom);
	consoleClear();
	printf("Changelog (first part):\n\n%.900s\n", body);
	consoleSelect(&sTop);
	printf("\n  PASS if the changelog shows on the\n  bottom screen.\n");
	waitA();
}

static void partA_download(bool install) {
	consoleSelect(&sTop);
	consoleClear();
	printf(install ? "Part A3: download + install the CIA\n\n" : "Part A2: download the CIA (no install)\n\n");
	if (!sCiaUrl[0]) {
		printf("  Run A1 first (press A on the menu).\n");
		waitA();
		return;
	}
	if (!netUp()) { waitA(); return; }

	Sink s = {0};
	s.expect = sCiaSize;
	s.started = osGetTime();
	s.lastPct = -10;
	sBlockLen = 0;
	sLowKb = freeKb();
	if (install) {
		if (R_FAILED(amInit()) || R_FAILED(AM_StartCiaInstall(MEDIATYPE_SD, &s.cia))) {
			printf("  Could not start the installer.\n");
			amExit();
			waitA();
			return;
		}
	}
	CURL* c = newCurl(sCiaUrl);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toFile);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
	CURLcode r = curl_easy_perform(c);
	curl_easy_cleanup(c);
	if (install && r == CURLE_OK) flushBlock(&s);  // the last, partly filled block

	double secs = (osGetTime() - s.started) / 1000.0;
	bool sizeOk = s.bytes == sCiaSize;
	printf("\n  Got %llu of %llu bytes in %.1f s\n", s.bytes, sCiaSize, secs);
	printf("  Size matches GitHub: %s\n", sizeOk ? "YES" : "NO");
	if (r != CURLE_OK) printf("  Network error: %s\n", curl_easy_strerror(r));
	if (s.failed) printf("  Installer write failed: 0x%08lX\n  after %llu bytes\n", s.err, s.written);

	if (install) {
		if (r == CURLE_OK && sizeOk && !s.failed) {
			Result fr = AM_FinishCiaInstall(s.cia);
			printf("  Install: %s (0x%08lX)\n", R_SUCCEEDED(fr) ? "DONE" : "FAILED", fr);
		} else {
			AM_CancelCIAInstall(s.cia);
			printf("  Install cancelled. The game already\n  installed is untouched.\n");
		}
		amExit();
	}
	printf("\n  Lowest free memory seen: %lu KB\n", sLowKb);
	waitA();
}

// ---------------------------------------------------------------- Part B

static bool partB_selfInstall(void) {
	consoleSelect(&sTop);
	consoleClear();
	printf("Part B: install version 2 over myself\n\n");
	FILE* f = fopen(SELF_CIA, "rb");
	if (!f) {
		printf("  %s is missing.\n  Copy updtest_v2.cia into an 'updtest'\n  folder on the SD card first.\n", SELF_CIA);
		waitA();
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	Handle h;
	if (R_FAILED(amInit()) || R_FAILED(AM_StartCiaInstall(MEDIATYPE_SD, &h))) {
		printf("  Could not start the installer.\n");
		fclose(f);
		amExit();
		waitA();
		return false;
	}
	static u8 buf[64 * 1024];
	u64 off = 0;
	bool ok = true;
	int lastPct = -10;
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
		u32 w = 0;
		if (R_FAILED(FSFILE_Write(h, &w, off, buf, n, 0)) || w != n) { ok = false; break; }
		off += n;
		int pct = (int)(off * 100 / size);
		if (pct / 10 != lastPct / 10) { printf("  %3d%%\n", pct); lastPct = pct; }
	}
	fclose(f);
	Result fr = ok ? AM_FinishCiaInstall(h) : (AM_CancelCIAInstall(h), -1);
	amExit();
	if (!ok || R_FAILED(fr)) {
		printf("\n  FAILED (0x%08lX). This version is\n  still installed.\n", fr);
		waitA();
		return false;
	}
	printf("\n  Installed. Going back to HOME.\n  Open the test app again: it should\n  say VERSION 2.\n");
	for (int i = 0; i < 180 && aptMainLoop(); i++) gspWaitForVBlank();
	return true;
}

// ---------------------------------------------------------------- menu

static void drawMenu(void) {
	consoleSelect(&sBottom);
	consoleClear();
	printf("\n  FRUIT SLICER UPDATE TEST\n");
	printf("  ========================\n\n");
	printf("  This is VERSION %d\n\n", TEST_VERSION);
	printf("  A      A1  Check GitHub + changelog\n");
	printf("  X      A2  Download CIA (no install)\n");
	printf("  Y      A3  Download + install CIA\n");
	printf("            (reinstalls Fruit Slicer\n");
	printf("             3DS from GitHub)\n");
	printf("  R      B   Install v2 of this app\n");
	printf("            from the SD card, then\n");
	printf("            go back to HOME\n");
	printf("  START  Quit\n");
	consoleSelect(&sTop);
}

int main(void) {
	gfxInitDefault();
	consoleInit(GFX_TOP, &sTop);
	consoleInit(GFX_BOTTOM, &sBottom);
	romfsInit();
	acInit();
	curl_global_init(CURL_GLOBAL_DEFAULT);

	consoleSelect(&sTop);
	printf("Fruit Slicer update test, VERSION %d\n\nPick a test on the bottom screen.\n", TEST_VERSION);
	drawMenu();
	bool home = false;
	while (aptMainLoop() && !home) {
		hidScanInput();
		u32 k = hidKeysDown();
		if (k & KEY_START) break;
		bool acted = true;
		if (k & KEY_A) partA_check();
		else if (k & KEY_X) partA_download(false);
		else if (k & KEY_Y) partA_download(true);
		else if (k & KEY_R) home = partB_selfInstall();
		else acted = false;
		if (acted) {
			drawMenu();
			consoleSelect(&sTop);
			printf("Done. Pick another test, or START.\n");
		}
		gspWaitForVBlank();
	}

	netDown();
	curl_global_cleanup();
	acExit();
	romfsExit();
	gfxExit();
	return 0;
}
