// Update check: see update.h. The port does the network and the installing (port_api.h); this file decides
// when to ask, reads GitHub's answer, and keeps track of what the player chose.
#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_api.h"
#include "progress.h"

namespace update {
static State sState = IDLE;
static bool sManual;
static bool sDownloadFailed;
static char sVersion[24];   // this build's version
static char sTag[24];       // the newer release's tag
static char sNotes[4096];
static char sUrl[512];
static unsigned long long sSize;
static float sProgress;

// ---------------------------------------------------------------- versions
// "1.0.0", "v0.4.0-alpha", "1.1.0-beta.2": the numbers first; then a release with no suffix is newer
// than one with, and suffixes compare as text (alpha, beta, rc).
static int compareVersions(const char* a, const char* b) {
	if (*a == 'v' || *a == 'V') a++;
	if (*b == 'v' || *b == 'V') b++;
	for (int part = 0; part < 3; part++) {
		char* ea;
		char* eb;
		const long na = strtol(a, &ea, 10), nb = strtol(b, &eb, 10);
		if (na != nb) return na < nb ? -1 : 1;
		a = *ea == '.' ? ea + 1 : ea;
		b = *eb == '.' ? eb + 1 : eb;
	}
	const char* sa = strchr(a, '-');
	const char* sb = strchr(b, '-');
	if (!sa && !sb) return 0;
	if (!sa) return 1;
	if (!sb) return -1;
	const int c = strcmp(sa, sb);
	return c < 0 ? -1 : c > 0 ? 1 : 0;
}

// ---------------------------------------------------------------- GitHub's answer
// A small reader for the few fields needed. Strings keep plain letters only: the game's lettering has no
// others, and the changelog's Markdown marks are dropped.
static const char* findKey(const char* from, const char* key) {
	char pat[48];
	snprintf(pat, sizeof pat, "\"%s\"", key);
	const char* p = strstr(from, pat);
	if (!p) return nullptr;
	p += strlen(pat);
	while (*p == ' ' || *p == ':' || *p == '\n' || *p == '\r' || *p == '\t') p++;
	return p;
}

static const char* jsonStr(const char* from, const char* key, char* out, size_t outLen) {
	const char* p = findKey(from, key);
	if (!p || *p != '"') return nullptr;
	p++;
	size_t o = 0;
	while (*p && *p != '"') {
		char ch = *p++;
		if (ch == '\\' && *p) {
			const char e = *p++;
			if (e == 'u') {  // \uXXXX: not in the lettering
				for (int i = 0; i < 4 && *p; i++) p++;
				ch = '?';
			} else {
				ch = e == 'n' ? '\n' : e == 't' ? ' ' : e == 'r' ? '\r' : e;
			}
		}
		if (ch == '\r' || (unsigned char)ch >= 0x80) continue;
		if (o + 1 < outLen) out[o++] = ch;
	}
	out[o] = 0;
	return p;
}

static bool jsonTrue(const char* from, const char* key) {
	const char* p = findKey(from, key);
	return p && strncmp(p, "true", 4) == 0;
}

// The changelog as lines of plain text: headings, bullets and emphasis lose their marks.
static void plainNotes(char* s) {
	char* o = s;
	bool lineStart = true;
	for (const char* p = s; *p; p++) {
		if (lineStart) {
			while (*p == '#' || *p == ' ') p++;
			// Table rows (the download table) mean nothing inside the game.
			while (*p == '|') {
				while (*p && *p != '\n') p++;
				while (*p == '\n') p++;
			}
			if ((*p == '-' || *p == '*') && p[1] == ' ') {
				*o++ = '-';
				*o++ = ' ';
				p += 2;
			}
			if (!*p) break;
		}
		lineStart = *p == '\n';
		// A link keeps its words and loses its address: "[Fruit Slicer 3DS](https://...)" reads "Fruit Slicer 3DS".
		if (*p == ']' && p[1] == '(') {
			const char* close = strchr(p, ')');
			if (close) {
				p = close;
				continue;
			}
		}
		if (*p == '*' || *p == '`' || *p == '_' || *p == '[' || (*p == '!' && p[1] == '[')) continue;
		*o++ = *p;
	}
	*o = 0;
}

// Picks the newest release this player should be offered, from GitHub's list (newest first).
static bool readReleases(char* text) {
	const bool onPre = strchr(sVersion, '-') != nullptr;  // pre-releases only go to pre-release players
	const char* suffix = port::updateAsset();
	char* p = text;
	while ((p = strstr(p, "\"tag_name\""))) {
		char* end = strstr(p + 1, "\"tag_name\"");
		const char keep = end ? *end : 0;
		if (end) *end = 0;  // look only inside this release
		char tag[sizeof sTag];
		bool take = jsonStr(p, "tag_name", tag, sizeof tag) && !jsonTrue(p, "draft") && (onPre || !jsonTrue(p, "prerelease")) && compareVersions(tag, sVersion) > 0;
		if (take) {
			snprintf(sTag, sizeof sTag, "%s", tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag);
			sNotes[0] = 0;
			jsonStr(p, "body", sNotes, sizeof sNotes);
			plainNotes(sNotes);
			sUrl[0] = 0;
			sSize = 0;
			const char* a = p;
			char name[160];
			while (suffix && (a = jsonStr(a, "name", name, sizeof name))) {
				const size_t n = strlen(name), k = strlen(suffix);
				if (n > k && strcmp(name + n - k, suffix) == 0) {
					const char* s = findKey(a, "size");
					if (s) sSize = strtoull(s, nullptr, 10);
					jsonStr(a, "browser_download_url", sUrl, sizeof sUrl);
					break;
				}
			}
		}
		if (end) *end = keep;
		if (take) return true;
		if (!end) break;
		p = end;
	}
	return false;
}

// ---------------------------------------------------------------- the check
static void startCheck(bool manual) {
	char url[160];
	snprintf(url, sizeof url, "https://api.github.com/repos/%s/releases?per_page=10", port::updateRepo());
	sManual = manual;
	sDownloadFailed = false;
	if (port::netGetText(url)) {
		sState = CHECKING;
	} else {
		sState = manual ? FAILED : IDLE;
	}
}

void boot(const char* version) {
	snprintf(sVersion, sizeof sVersion, "%s", version);
#ifdef AUTOTEST
	return;  // test runs never reach the network
#endif
	if (gSave.updConsent == 0) sState = ASK;
	else if (gSave.updConsent == 1 && gSave.updLastDay != progress::today()) startCheck(false);  // at most once a day
}

void tick() {
	if (sState == CHECKING) {
		float p;
		const int r = port::netPoll(&p);
		if (r == 0) return;
		if (r < 0) {
			sState = sManual ? FAILED : IDLE;
			return;
		}
		gSave.updLastDay = progress::today();
		saveWrite();
		char* text = port::netText();
		if (!text || !readReleases(text)) {
			sState = sManual ? LATEST : IDLE;
		} else if (!sManual && strncmp(sTag, gSave.updSkipped, sizeof gSave.updSkipped - 1) == 0) {
			sState = IDLE;  // Later was chosen for this one
		} else {
			sState = sUrl[0] && sSize > 0 ? OFFER : NOTE;
		}
	} else if (sState == DOWNLOADING) {
		const int r = port::netPoll(&sProgress);
		if (r > 0) sState = READY;
		else if (r < 0) {
			sState = FAILED;
			sDownloadFailed = true;
		}
	}
}

State state() { return sState; }
bool wantsSign() { return sState != IDLE && sState != CHECKING; }
bool manual() { return sManual; }
const char* newVersion() { return sTag; }
const char* notes() { return sNotes; }
float progress() { return sProgress; }
bool failedDownload() { return sDownloadFailed; }

void consent(bool yes) {
	gSave.updConsent = yes ? 1 : 2;
	saveWrite();
	sState = IDLE;
	if (yes) startCheck(false);
}

void checkNow() {
	if (sState == DOWNLOADING || sState == READY) return;
	startCheck(true);
}

void later() {
	snprintf(gSave.updSkipped, sizeof gSave.updSkipped, "%.*s", (int)sizeof gSave.updSkipped - 1, sTag);
	saveWrite();
	sState = IDLE;
}

void install() {
	sProgress = 0;
	sDownloadFailed = false;
	if (port::netDownload(sUrl, sSize)) {
		sState = DOWNLOADING;
	} else {
		sState = FAILED;
		sDownloadFailed = true;
	}
}

void dismiss() { sState = IDLE; }

void restart() {
	port::updateRestart();
	sState = IDLE;
}
}  // namespace update
