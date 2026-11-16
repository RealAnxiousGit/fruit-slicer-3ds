// 3DS StreetPass card serialization, persistent inbox history, and outbox publishing.
#pragma once
#include "common.h"

namespace streetpass {

constexpr u32 kMagic = 0x50535346u;  // "FSSP"
constexpr u16 kVersion = 1;
constexpr int kMaxInbox = 32;
constexpr int kMsgMax = 24;
constexpr int kNameMax = 11;

#pragma pack(push, 1)
struct Packet {
	u32 magic;
	u16 version;
	u16 bossesBeaten;
	u64 senderId;
	char name[12];
	char msg[28];
	u32 classicBest;
	u32 totalFruit;
	u16 orchardBestRoom;
	u16 endlessBestRoom;
	u32 crc32;
};
#pragma pack(pop)
static_assert(sizeof(Packet) == 72, "StreetPass Packet must stay 72 bytes");

struct Entry {
	Packet pkt;
	u32 bestAttempt;
	u8 challenged;
	u8 won;
	u8 isNew;
	u8 reserved;
};

void cleanMessage(const char* in, char* out, size_t size, const char* fallback = "Hello! :)");
void cleanName(const char* in, char* out, size_t size, const char* fallback = "Player");

int pack(const char* playerName, u8* out, int cap);
bool unpack(const u8* data, int len, Packet* out);

void init();
void shutdown();
void update(float dt);
void publishNow();

int count();
const Entry& entry(int index);
bool hasPending();
int pendingIndex();
void dismissPending();
void markAttempt(int index, u32 ourScore, bool won);
int ingestPacket(const u8* data, int len);
void resetInbox();

}  // namespace streetpass
