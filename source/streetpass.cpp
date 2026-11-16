#include "streetpass.h"
#include "common.h"
#include "font_metrics.h"
#include "port_api.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace streetpass {

namespace {
Entry sEntries[kMaxInbox];
int sCount = 0;
bool sLoaded = false;
float sPollTimer = 0.0f;

constexpr u32 kInboxMagic = 0x31495053u;  // "SPI1"

u32 calcCrc32(const void* data, size_t len) {
	const u8* p = (const u8*)data;
	u32 crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++) {
		crc ^= p[i];
		for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
	}
	return ~crc;
}

bool allowedGlyph(char c) {
	if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
	const int idx = (int)(unsigned char)c - 32;
	if (idx < 0 || idx >= 96) return false;
	return c == ' ' || kFontIndex[idx] >= 0;
}

void saveInbox() {
	port::ensureDataDir();
	char path[256];
	port::dataPath(path, sizeof path, "streetpass_inbox.bin");
	FILE* f = fopen(path, "wb");
	if (!f) return;
	const u32 hdr[2] = {kInboxMagic, (u32)sCount};
	fwrite(hdr, sizeof hdr, 1, f);
	if (sCount > 0) fwrite(sEntries, sizeof(Entry), (size_t)sCount, f);
	fclose(f);
}

void loadInbox() {
	if (sLoaded) return;
	sLoaded = true;
	sCount = 0;
	char path[256];
	port::dataPath(path, sizeof path, "streetpass_inbox.bin");
	FILE* f = fopen(path, "rb");
	if (!f) return;
	u32 hdr[2] = {};
	if (fread(hdr, sizeof hdr, 1, f) == 1 && hdr[0] == kInboxMagic && hdr[1] <= kMaxInbox) {
		if (hdr[1] > 0) sCount = (int)fread(sEntries, sizeof(Entry), hdr[1], f);
	}
	fclose(f);
	for (int i = 0; i < sCount; i++) {
		cleanName(sEntries[i].pkt.name, sEntries[i].pkt.name, sizeof sEntries[i].pkt.name);
		cleanMessage(sEntries[i].pkt.msg, sEntries[i].pkt.msg, sizeof sEntries[i].pkt.msg);
	}
}

}  // namespace

void cleanMessage(const char* in, char* out, size_t size, const char* fallback) {
	if (!out || size == 0) return;
	char tmp[64];
	size_t n = 0;
	if (in) {
		for (; *in && n < sizeof tmp - 1 && n < size - 1; in++) {
			if (allowedGlyph(*in)) tmp[n++] = *in;
		}
	}
	while (n > 0 && tmp[n - 1] == ' ') n--;
	size_t start = 0;
	while (start < n && tmp[start] == ' ') start++;
	if (n > start) {
		const size_t len = n - start < size - 1 ? n - start : size - 1;
		memcpy(out, tmp + start, len);
		out[len] = 0;
	} else {
		snprintf(out, size, "%s", fallback ? fallback : "");
	}
}

void cleanName(const char* in, char* out, size_t size, const char* fallback) {
	if (!out || size == 0) return;
	char tmp[32];
	size_t n = 0;
	if (in) {
		for (; *in && n < sizeof tmp - 1 && n < size - 1; in++) {
			const char c = *in;
			const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
			if (ok) tmp[n++] = c;
		}
	}
	while (n > 0 && tmp[n - 1] == ' ') n--;
	size_t start = 0;
	while (start < n && tmp[start] == ' ') start++;
	if (n > start) {
		const size_t len = n - start < size - 1 ? n - start : size - 1;
		memcpy(out, tmp + start, len);
		out[len] = 0;
	} else {
		snprintf(out, size, "%s", fallback ? fallback : "Player");
	}
}

int pack(const char* playerName, u8* out, int cap) {
	if (!out || cap < (int)sizeof(Packet)) return 0;
	Packet p = {};
	p.magic = kMagic;
	p.version = kVersion;
	u16 bosses = 0;
	for (int b = 0; b < 8; b++)
		if (gSave.bossesBeaten >> b & 1u) bosses++;
	p.bossesBeaten = bosses;
	p.senderId = gSave.leaderboardId ? gSave.leaderboardId : 1ull;
	cleanName(playerName, p.name, sizeof p.name, "Slicer");
	cleanMessage(gSave.streetPassMsg, p.msg, sizeof p.msg, "Hello! :)");
	p.classicBest = gSave.best[MODE_CLASSIC];
	p.totalFruit = gSave.totalFruit;
	p.orchardBestRoom = gSave.orchardBestRoom;
	p.endlessBestRoom = gSave.endlessBestRoom;
	p.crc32 = calcCrc32(&p, offsetof(Packet, crc32));
	memcpy(out, &p, sizeof p);
	return (int)sizeof p;
}

bool unpack(const u8* data, int len, Packet* out) {
	if (!data || !out || len < (int)sizeof(Packet)) return false;
	Packet p;
	memcpy(&p, data, sizeof p);
	if (p.magic != kMagic || p.version != kVersion || p.senderId == 0) return false;
	if (calcCrc32(&p, offsetof(Packet, crc32)) != p.crc32) return false;
	cleanName(p.name, p.name, sizeof p.name, "Slicer");
	cleanMessage(p.msg, p.msg, sizeof p.msg, "Hello! :)");
	*out = p;
	return true;
}

int ingestPacket(const u8* data, int len) {
	loadInbox();
	Packet p;
	if (!unpack(data, len, &p)) return -1;
	// Ignore loopback of our own card unless needed? Wait: allow any distinct card or update existing senderId.
	if (p.senderId == gSave.leaderboardId && p.senderId != 0) return -1;
	for (int i = 0; i < sCount; i++) {
		if (sEntries[i].pkt.senderId == p.senderId) {
			const bool updated = sEntries[i].pkt.classicBest != p.classicBest || strcmp(sEntries[i].pkt.msg, p.msg) != 0;
			sEntries[i].pkt = p;
			if (updated) {
				sEntries[i].isNew = 1;
				sEntries[i].challenged = 0;
				sEntries[i].won = 0;
			}
			// Move to top so freshest tag appears first.
			if (i > 0) {
				const Entry e = sEntries[i];
				memmove(&sEntries[1], &sEntries[0], sizeof(Entry) * (size_t)i);
				sEntries[0] = e;
			}
			saveInbox();
			return 0;
		}
	}
	if (sCount >= kMaxInbox) sCount = kMaxInbox - 1;
	if (sCount > 0) memmove(&sEntries[1], &sEntries[0], sizeof(Entry) * (size_t)sCount);
	sEntries[0] = {};
	sEntries[0].pkt = p;
	sEntries[0].isNew = 1;
	sCount++;
	saveInbox();
	return 0;
}

void init() {
	loadInbox();
	port::spSetEnabled(gSave.streetPassOn != 0);
	if (gSave.streetPassOn) publishNow();
	sPollTimer = 0.0f;
	update(5.0f);
}

void shutdown() {
	if (sLoaded) saveInbox();
}

void publishNow() {
	if (!port::spSupported()) return;
	if (!gSave.streetPassOn) return;
	char who[32] = {};
	if (gSave.leaderboardName[0]) snprintf(who, sizeof who, "%s", gSave.leaderboardName);
	else port::spUserName(who, sizeof who);
	u8 pkt[sizeof(Packet)];
	const int n = pack(who, pkt, sizeof pkt);
	if (n > 0) port::spPublish(pkt, n);
}

void update(float dt) {
	if (!port::spSupported() || !gSave.streetPassOn) return;
	sPollTimer += dt;
	if (sPollTimer < 2.0f) return;
	sPollTimer = 0.0f;
	u8 buf[sizeof(Packet) + 16];
	int len = 0;
	int guard = 0;
	while (guard++ < 8 && port::spPopInbox(buf, sizeof buf, &len)) {
		ingestPacket(buf, len);
	}
}

int count() {
	loadInbox();
	return sCount;
}

const Entry& entry(int index) {
	loadInbox();
	return sEntries[index < 0 ? 0 : (index >= sCount ? (sCount > 0 ? sCount - 1 : 0) : index)];
}

int pendingIndex() {
	loadInbox();
	for (int i = 0; i < sCount; i++)
		if (sEntries[i].isNew) return i;
	return -1;
}

bool hasPending() { return pendingIndex() >= 0; }

void dismissPending() {
	const int i = pendingIndex();
	if (i >= 0) {
		sEntries[i].isNew = 0;
		saveInbox();
	}
}

void markAttempt(int index, u32 ourScore, bool won) {
	loadInbox();
	if (index < 0 || index >= sCount) return;
	sEntries[index].isNew = 0;
	sEntries[index].challenged = 1;
	if (ourScore > sEntries[index].bestAttempt) sEntries[index].bestAttempt = ourScore;
	if (won) sEntries[index].won = 1;
	saveInbox();
}

void resetInbox() {
	sLoaded = true;
	sCount = 0;
	saveInbox();
}

}  // namespace streetpass
