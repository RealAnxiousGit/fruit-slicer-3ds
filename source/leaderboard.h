#pragma once
#include "common.h"

namespace lb {

enum Status { STATUS_IDLE, STATUS_LOADING, STATUS_READY, STATUS_OFFLINE };

struct Entry {
	char name[8];
	char platform[12];
	u32 score;
	char playerId[24];
};

struct BoardView {
	Status status;
	char mode[24];
	int count;
	Entry entries[100];
	int yourRank;  // 1-based, 0 if unranked
	u32 yourScore;
	int total;
	bool lastWasSubmit;
};

// Update-proof string slug per mode (empty/nullptr for invalid or Practice).
const char* modeSlug(int mode, bool endless);
const char* platformTag();

// Sanitizes a leaderboard display name to <= 6 safe characters ([A-Za-z0-9 _-]).
// Returns true if at least one non-space character remains; cleanName() falls back to "PLAYER".
bool sanitizeName(const char* in, char* out, size_t size);
void cleanName(const char* in, char* out, size_t size);
void formatPlayerId(u64 id, char* out, size_t size);

// Resolves base URL: FRUITSLICER_LB_URL env -> dataPath("lb_url.txt") -> compiled default.
void baseUrl(char* out, size_t size);

bool buildSubmitJson(int mode, bool endless, u32 score, const char* name, u64 playerId, bool testFlag, char* out, size_t size);
bool parseResponse(const char* json, BoardView* out);

void fetch(int mode, bool endless, bool testFlag = false, int limit = 10);
void submit(int mode, bool endless, u32 score, bool testFlag = false);
void tick();
const BoardView& view();
void clearView();

}  // namespace lb
