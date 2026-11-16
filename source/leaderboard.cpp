// Cross-platform online leaderboards (/v1/scores over HTTPS/TLS 1.2).
// Port network helpers live in port::lbRequest / port::lbPoll / port::lbText.
#include "leaderboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_api.h"

namespace lb {

static constexpr const char* kDefaultBaseUrl = "https://fruit-slicer-lb.realanxiousgit.workers.dev";

static BoardView sView = {};

struct PendingReq {
	bool active;
	int mode;
	bool endless;
	bool submit;
	u32 score;
	bool testFlag;
};
static PendingReq sPending = {};

const char* modeSlug(int mode, bool endless) {
	if (mode == MODE_ORCHARD && endless) return "orchard_endless";
	switch (mode) {
		case MODE_CLASSIC: return "classic";
		case MODE_ARCADE: return "arcade";
		case MODE_ZEN: return "zen";
		case MODE_FRENZY: return "frenzy";
		case MODE_SUDDEN: return "sudden_death";
		case MODE_MINEFIELD: return "minefield";
		case MODE_COMBO: return "combo_rush";
		case MODE_DAILY: return "daily";
		case MODE_BEAT: return "beat_slice";
		case MODE_ORCHARD: return "orchard_blast";
		case MODE_BOSSRUSH: return "boss_rush";
		case MODE_RECIPE: return "recipe";
		case MODE_SHARP: return "sharpshooter";
		case MODE_JUGGLE: return "juggle";
		default: return nullptr;
	}
}

const char* platformTag() {
#if PORT_3DS
	return "3ds";
#elif PORT_ANDROID
	return "android";
#elif defined(_WIN32)
	return "win";
#else
	return "linux";
#endif
}

static bool allowedNameChar(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
}

bool sanitizeName(const char* in, char* out, size_t size) {
	if (!out || size == 0) return false;
	char tmp[8];
	int n = 0;
	if (in) {
		bool started = false;
		for (int i = 0; in[i] && n < 6; i++) {
			if (!allowedNameChar(in[i])) continue;
			if (!started && in[i] == ' ') continue;
			started = true;
			tmp[n++] = in[i];
		}
	}
	while (n > 0 && tmp[n - 1] == ' ') n--;
	const int cap = (int)size - 1;
	const int copy = n < cap ? n : cap;
	for (int i = 0; i < copy; i++) out[i] = tmp[i];
	out[copy] = 0;
	return copy > 0;
}

void cleanName(const char* in, char* out, size_t size) {
	if (!sanitizeName(in, out, size)) snprintf(out, size, "PLAYER");
}

void formatPlayerId(u64 id, char* out, size_t size) {
	const unsigned long long v = (unsigned long long)(id ? id : 1ULL);
	snprintf(out, size, "%016llx", v);
}

static void trimUrl(char* s) {
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '/')) {
		s[--n] = 0;
	}
}

void baseUrl(char* out, size_t size) {
	if (!out || size == 0) return;
	out[0] = 0;
	const char* env = getenv("FRUITSLICER_LB_URL");
	if (env && (strncmp(env, "http://", 7) == 0 || strncmp(env, "https://", 8) == 0)) {
		snprintf(out, size, "%s", env);
		trimUrl(out);
		return;
	}
	char path[512];
	port::dataPath(path, sizeof path, "lb_url.txt");
	if (FILE* f = fopen(path, "r")) {
		char buf[256] = {};
		if (fgets(buf, sizeof buf, f)) {
			trimUrl(buf);
			if (strncmp(buf, "http://", 7) == 0 || strncmp(buf, "https://", 8) == 0) {
				snprintf(out, size, "%s", buf);
			}
		}
		fclose(f);
		if (out[0]) return;
	}
	snprintf(out, size, "%s", kDefaultBaseUrl);
}

bool buildSubmitJson(int mode, bool endless, u32 score, const char* name, u64 playerId, bool testFlag, char* out, size_t size) {
	const char* slug = modeSlug(mode, endless);
	if (!slug || !out || size < 64) return false;
	char safeName[8];
	char pid[24];
	cleanName(name, safeName, sizeof safeName);
	formatPlayerId(playerId, pid, sizeof pid);
	const int n = snprintf(out, size,
	    "{\"mode\":\"%s\",\"player_id\":\"%s\",\"name\":\"%s\",\"score\":%u,\"platform\":\"%s\"%s}",
	    slug, pid, safeName, (unsigned)score, platformTag(), testFlag ? ",\"test\":1" : "");
	return n > 0 && (size_t)n < size;
}

// ---------------------------------------------------------------- minimal scoped JSON parser
static const char* skipWs(const char* p, const char* end) {
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
	return p;
}

static const char* skipString(const char* p, const char* end) {
	if (p >= end || *p != '"') return nullptr;
	p++;
	while (p < end) {
		if (*p == '\\') {
			p += (p + 1 < end) ? 2 : 1;
			continue;
		}
		if (*p == '"') return p + 1;
		p++;
	}
	return nullptr;
}

static const char* skipValue(const char* p, const char* end) {
	p = skipWs(p, end);
	if (p >= end) return nullptr;
	if (*p == '"') return skipString(p, end);
	if (*p == '{' || *p == '[') {
		const char open = *p;
		const char close = (open == '{') ? '}' : ']';
		int depth = 1;
		p++;
		while (p < end && depth > 0) {
			if (*p == '"') {
				p = skipString(p, end);
				if (!p) return nullptr;
			} else {
				if (*p == open) depth++;
				else if (*p == close) depth--;
				p++;
			}
		}
		return depth == 0 ? p : nullptr;
	}
	while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
	return p;
}

// Finds `key` at the top level of JSON object [objStart, objEnd). Returns pointer to the value start.
static const char* findKey(const char* objStart, const char* objEnd, const char* key) {
	const char* p = skipWs(objStart, objEnd);
	if (!p || p >= objEnd || *p != '{') return nullptr;
	p++;
	const size_t keyLen = strlen(key);
	while (p < objEnd) {
		p = skipWs(p, objEnd);
		if (p >= objEnd || *p == '}') return nullptr;
		if (*p == ',') {
			p++;
			continue;
		}
		if (*p != '"') return nullptr;
		const char* kStart = p + 1;
		const char* afterK = skipString(p, objEnd);
		if (!afterK) return nullptr;
		const size_t len = (size_t)((afterK - 1) - kStart);
		p = skipWs(afterK, objEnd);
		if (p >= objEnd || *p != ':') return nullptr;
		p = skipWs(p + 1, objEnd);
		if (len == keyLen && memcmp(kStart, key, keyLen) == 0) return p;
		p = skipValue(p, objEnd);
		if (!p) return nullptr;
	}
	return nullptr;
}

static bool readStr(const char* p, const char* end, char* out, size_t size) {
	if (!p || !out || size == 0) return false;
	p = skipWs(p, end);
	if (p >= end || *p != '"') return false;
	p++;
	size_t n = 0;
	while (p < end && *p != '"') {
		char ch = *p++;
		if (ch == '\\' && p < end) ch = *p++;
		if (n + 1 < size) out[n++] = ch;
	}
	out[n] = 0;
	return p < end && *p == '"';
}

static bool readU32(const char* p, const char* end, u32* out) {
	if (!p || !out) return false;
	p = skipWs(p, end);
	if (p >= end || *p < '0' || *p > '9') return false;
	u64 v = 0;
	while (p < end && *p >= '0' && *p <= '9') {
		v = v * 10 + (u64)(*p - '0');
		if (v > 0xFFFFFFFFULL) v = 0xFFFFFFFFULL;
		p++;
	}
	*out = (u32)v;
	return true;
}

bool parseResponse(const char* json, BoardView* out) {
	if (!json || !out) return false;
	BoardView parsed = {};
	const char* end = json + strlen(json);
	const char* root = skipWs(json, end);
	if (root >= end || *root != '{') return false;
	const char* rootEnd = skipValue(root, end);
	if (!rootEnd) return false;

	const char* okVal = findKey(root, rootEnd, "ok");
	if (!okVal || strncmp(skipWs(okVal, rootEnd), "true", 4) != 0) return false;

	if (const char* modeVal = findKey(root, rootEnd, "mode")) {
		readStr(modeVal, rootEnd, parsed.mode, sizeof parsed.mode);
	}
	if (const char* totalVal = findKey(root, rootEnd, "total")) {
		u32 tot = 0;
		if (readU32(totalVal, rootEnd, &tot)) parsed.total = (int)tot;
	}
	if (const char* youVal = findKey(root, rootEnd, "you")) {
		const char* y = skipWs(youVal, rootEnd);
		if (y < rootEnd && *y == '{') {
			const char* yEnd = skipValue(y, rootEnd);
			if (yEnd) {
				u32 rank = 0;
				if (const char* rVal = findKey(y, yEnd, "rank")) {
					if (readU32(rVal, yEnd, &rank)) parsed.yourRank = (int)rank;
				}
				if (const char* sVal = findKey(y, yEnd, "score")) readU32(sVal, yEnd, &parsed.yourScore);
			}
		}
	}
	if (const char* topVal = findKey(root, rootEnd, "top")) {
		const char* p = skipWs(topVal, rootEnd);
		if (p < rootEnd && *p == '[') {
			const char* arrEnd = skipValue(p, rootEnd);
			if (!arrEnd) return false;
			p++;
			while (p < arrEnd && parsed.count < 10) {
				p = skipWs(p, arrEnd);
				if (p >= arrEnd || *p == ']') break;
				if (*p == ',') {
					p++;
					continue;
				}
				if (*p != '{') break;
				const char* itemEnd = skipValue(p, arrEnd);
				if (!itemEnd) break;
				Entry& e = parsed.entries[parsed.count];
				char rawName[16] = {};
				if (const char* nv = findKey(p, itemEnd, "name")) readStr(nv, itemEnd, rawName, sizeof rawName);
				cleanName(rawName, e.name, sizeof e.name);
				if (const char* pv = findKey(p, itemEnd, "platform")) readStr(pv, itemEnd, e.platform, sizeof e.platform);
				if (!e.platform[0]) snprintf(e.platform, sizeof e.platform, "web");
				if (const char* sv = findKey(p, itemEnd, "score")) readU32(sv, itemEnd, &e.score);
				if (const char* iv = findKey(p, itemEnd, "player_id")) readStr(iv, itemEnd, e.playerId, sizeof e.playerId);
				parsed.count++;
				p = itemEnd;
			}
		}
	}
	parsed.status = STATUS_READY;
	*out = parsed;
	return true;
}

static bool dispatchNow(const PendingReq& req) {
	const char* slug = modeSlug(req.mode, req.endless);
	if (!slug) {
		sView.status = STATUS_OFFLINE;
		return false;
	}
	char base[256];
	baseUrl(base, sizeof base);
	char url[512];
	char body[512];
	const char* bodyPtr = nullptr;
	if (req.submit) {
		snprintf(url, sizeof url, "%s/v1/scores", base);
		if (!buildSubmitJson(req.mode, req.endless, req.score, gSave.leaderboardName, gSave.leaderboardId, req.testFlag, body, sizeof body)) {
			sView.status = STATUS_OFFLINE;
			return false;
		}
		bodyPtr = body;
	} else {
		char pid[24];
		formatPlayerId(gSave.leaderboardId, pid, sizeof pid);
		snprintf(url, sizeof url, "%s/v1/scores?mode=%s&player_id=%s&limit=10%s",
		    base, slug, pid, req.testFlag ? "&test=1" : "");
	}
	if (!port::lbRequest(url, bodyPtr)) {
		sView.status = STATUS_OFFLINE;
		return false;
	}
	sView.status = STATUS_LOADING;
	sView.lastWasSubmit = req.submit;
	snprintf(sView.mode, sizeof sView.mode, "%s", slug);
	return true;
}

static void enqueue(const PendingReq& req) {
	if (sView.status == STATUS_LOADING && port::lbPoll() == 0) {
		sPending = req;
		sPending.active = true;
		return;
	}
	sPending.active = false;
	dispatchNow(req);
}

void fetch(int mode, bool endless, bool testFlag) {
	enqueue({true, mode, endless, false, 0, testFlag});
}

void submit(int mode, bool endless, u32 score, bool testFlag) {
	enqueue({true, mode, endless, true, score, testFlag});
}

void tick() {
	if (sView.status != STATUS_LOADING) return;
	const int state = port::lbPoll();
	if (state == 0) return;

	if (state > 0) {
		BoardView next = {};
		const bool lastSubmit = sView.lastWasSubmit;
		if (parseResponse(port::lbText(), &next)) {
			sView = next;
			sView.lastWasSubmit = lastSubmit;
		} else {
			sView.status = STATUS_OFFLINE;
		}
	} else {
		sView.status = STATUS_OFFLINE;
	}

	if (sPending.active) {
		PendingReq next = sPending;
		sPending.active = false;
		dispatchNow(next);
	}
}

const BoardView& view() { return sView; }

void clearView() {
	sView = {};
	sPending = {};
}

}  // namespace lb
