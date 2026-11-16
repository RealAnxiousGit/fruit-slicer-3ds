// Nintendo 3DS StreetPass port: CECD mailbox IPC (cecd:u) with SD card fallback for homebrew/testing.
#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "port_api.h"

namespace port {
namespace {

constexpr u32 kFallbackProgramId = 0x000F51CEu;
constexpr u32 kPathMboxList = 1;
constexpr u32 kPathMboxInfo = 2;
constexpr u32 kPathInboxInfo = 3;
constexpr u32 kPathOutboxInfo = 4;
constexpr u32 kPathInboxMsg = 6;
constexpr u32 kPathOutboxMsg = 7;

Handle sCecd = 0;
bool sEnabled = false;
bool sBoxReady = false;
u32 sProgramId = kFallbackProgramId;
u8 sOutboxCopy[128];
int sOutboxLen = 0;

struct CecMboxListHeader {
	u16 magic;     // 0x6868
	u16 pad;
	u32 version;   // 0x00000001
	u32 boxCount;
	char names[24][16];
};

struct CecMboxInfoHeader {
	u16 magic;     // 0x6363
	u16 pad;
	u32 programId;
	u32 privateId;
	u32 flag;
	u8 flag2;
	u8 flag3;
	u8 pad2[2];
	u8 hmacKey[32];
	u32 flag4;
	u64 updateTime;
	u64 receiveTime;
};

struct CecBoxInfoHeader {
	u16 magic;      // 0x6262
	u16 pad;
	u32 boxSize;    // max size
	u32 curSize;
	u32 maxMsgs;
	u32 curMsgs;
};

u32 currentProgramId() {
	u64 tid = 0;
	if (R_SUCCEEDED(APT_GetProgramID(&tid)) && tid != 0) {
		const u32 low = (u32)(tid & 0xFFFFFFFFu);
		if (low != 0) return low;
	}
	return kFallbackProgramId;
}

bool ensureService() {
	if (sCecd) return true;
	if (R_FAILED(srvGetServiceHandle(&sCecd, "cecd:u"))) {
		sCecd = 0;
		return false;
	}
	sProgramId = currentProgramId();
	return true;
}

Result cecdOpenAndRead(u32 pid, u32 pathType, u32 option, void* buf, u32 size, u32* readOut) {
	u32* cmd = getThreadCommandBuffer();
	cmd[0] = IPC_MakeHeader(0x12, 4, 4);
	cmd[1] = size;
	cmd[2] = pathType;
	cmd[3] = option;
	cmd[4] = pid;
	cmd[5] = IPC_Desc_CurProcessId();
	cmd[6] = 0;
	cmd[7] = IPC_Desc_Buffer(size, IPC_BUFFER_W);
	cmd[8] = (u32)buf;
	Result rc = svcSendSyncRequest(sCecd);
	if (R_FAILED(rc)) return rc;
	if (readOut) *readOut = cmd[2];
	return (Result)cmd[1];
}

Result cecdOpenAndWrite(u32 pid, u32 pathType, u32 option, const void* buf, u32 size) {
	u32* cmd = getThreadCommandBuffer();
	cmd[0] = IPC_MakeHeader(0x11, 4, 4);
	cmd[1] = size;
	cmd[2] = pathType;
	cmd[3] = option;
	cmd[4] = pid;
	cmd[5] = IPC_Desc_CurProcessId();
	cmd[6] = 0;
	cmd[7] = IPC_Desc_Buffer(size, IPC_BUFFER_R);
	cmd[8] = (u32)buf;
	Result rc = svcSendSyncRequest(sCecd);
	if (R_FAILED(rc)) return rc;
	return (Result)cmd[1];
}

Result cecdReadMessage(u32 pid, bool isOutbox, const u8 msgId[8], void* buf, u32 size, u32* readOut) {
	u32* cmd = getThreadCommandBuffer();
	cmd[0] = IPC_MakeHeader(0x03, 4, 4);
	cmd[1] = pid;
	cmd[2] = isOutbox ? 1u : 0u;
	cmd[3] = 8u;
	cmd[4] = size;
	cmd[5] = IPC_Desc_Buffer(8, IPC_BUFFER_R);
	cmd[6] = (u32)msgId;
	cmd[7] = IPC_Desc_Buffer(size, IPC_BUFFER_W);
	cmd[8] = (u32)buf;
	Result rc = svcSendSyncRequest(sCecd);
	if (R_FAILED(rc)) return rc;
	if (readOut) *readOut = cmd[2];
	return (Result)cmd[1];
}

Result cecdWriteMessage(u32 pid, bool isOutbox, const u8 msgId[8], const void* buf, u32 size) {
	u32* cmd = getThreadCommandBuffer();
	cmd[0] = IPC_MakeHeader(0x06, 4, 4);
	cmd[1] = pid;
	cmd[2] = isOutbox ? 1u : 0u;
	cmd[3] = 8u;
	cmd[4] = size;
	cmd[5] = IPC_Desc_Buffer(8, IPC_BUFFER_R);
	cmd[6] = (u32)msgId;
	cmd[7] = IPC_Desc_Buffer(size, IPC_BUFFER_R);
	cmd[8] = (u32)buf;
	Result rc = svcSendSyncRequest(sCecd);
	if (R_FAILED(rc)) return rc;
	return (Result)cmd[1];
}

Result cecdDelete(u32 pid, u32 pathType, bool isOutbox, const u8 msgId[8]) {
	u32* cmd = getThreadCommandBuffer();
	cmd[0] = IPC_MakeHeader(0x08, 3, 2);
	cmd[1] = pid;
	cmd[2] = pathType;
	cmd[3] = (msgId && isOutbox) ? 8u : (msgId ? 8u : 0u);
	cmd[4] = IPC_Desc_Buffer(msgId ? 8u : 0u, IPC_BUFFER_R);
	cmd[5] = (u32)msgId;
	Result rc = svcSendSyncRequest(sCecd);
	if (R_FAILED(rc)) return rc;
	return (Result)cmd[1];
}

void ensureMailbox() {
	if (sBoxReady || !ensureService()) return;
	CecMboxInfoHeader mbox = {};
	u32 readBytes = 0;
	if (R_FAILED(cecdOpenAndRead(sProgramId, kPathMboxInfo, 0, &mbox, sizeof mbox, &readBytes)) || mbox.magic != 0x6363) {
		memset(&mbox, 0, sizeof mbox);
		mbox.magic = 0x6363;
		mbox.programId = sProgramId;
		mbox.privateId = 0x46534C50u;
		mbox.flag = 1;
		mbox.flag2 = 1;
		cecdOpenAndWrite(sProgramId, kPathMboxInfo, 2, &mbox, sizeof mbox);
		CecBoxInfoHeader box = {};
		box.magic = 0x6262;
		box.boxSize = 64 * 1024;
		box.maxMsgs = 16;
		cecdOpenAndWrite(sProgramId, kPathInboxInfo, 2, &box, sizeof box);
		cecdOpenAndWrite(sProgramId, kPathOutboxInfo, 2, &box, sizeof box);
	}
	CecMboxListHeader list = {};
	char boxHex[16];
	snprintf(boxHex, sizeof boxHex, "%08lx", (unsigned long)sProgramId);
	if (R_SUCCEEDED(cecdOpenAndRead(0, kPathMboxList, 0, &list, sizeof list, &readBytes)) && list.magic == 0x6868) {
		bool found = false;
		for (u32 i = 0; i < list.boxCount && i < 24; i++)
			if (strncmp(list.names[i], boxHex, 8) == 0) found = true;
		if (!found && list.boxCount < 24) {
			memset(list.names[list.boxCount], 0, 16);
			memcpy(list.names[list.boxCount], boxHex, 8);
			list.boxCount++;
			cecdOpenAndWrite(0, kPathMboxList, 2, &list, sizeof list);
		}
	}
	sBoxReady = true;
}

}  // namespace

bool spSupported() { return true; }

void spUserName(char* out, size_t size) {
	if (!out || size == 0) return;
	u16 utf16[14] = {};
	if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(0x1C, 0x000A0000, utf16))) {
		size_t n = 0;
		for (int i = 0; i < 10 && utf16[i] && n + 1 < size; i++) {
			const u16 ch = utf16[i];
			if (ch >= 32 && ch < 127) out[n++] = (char)ch;
		}
		out[n] = 0;
		if (n > 0) return;
	}
	snprintf(out, size, "3DS Player");
}

void spSetEnabled(bool on) {
	sEnabled = on;
	if (on) ensureMailbox();
}

bool spPublish(const void* data, int size) {
	if (!sEnabled || !data || size <= 0 || size > (int)sizeof sOutboxCopy) return false;
	memcpy(sOutboxCopy, data, (size_t)size);
	sOutboxLen = size;

	// Mirror outbox packet onto SD so homebrew setups can inspect/exchange it as well.
	ensureDataDir();
	char path[256];
	dataPath(path, sizeof path, "sp_outbox.bin");
	if (FILE* f = fopen(path, "wb")) {
		fwrite(data, 1, (size_t)size, f);
		fclose(f);
	}

	if (!ensureService()) return false;
	ensureMailbox();
	const u8 msgId[8] = {'F', 'S', 'L', 'C', 0, 0, 0, 1};
	// Frame as CecMessageHeader (0x6060) + payload so system CECD boxes can exchange it.
	u8 msg[128 + 64] = {};
	const u32 hdrSize = 0x70;
	const u32 totalSize = hdrSize + (u32)size;
	const u16 magic = 0x6060;
	memcpy(msg + 0x00, &magic, 2);
	memcpy(msg + 0x04, &totalSize, 4);
	memcpy(msg + 0x08, &hdrSize, 4);
	memcpy(msg + 0x0C, &size, 4);
	memcpy(msg + 0x10, &sProgramId, 4);
	memcpy(msg + 0x20, msgId, 8);
	memcpy(msg + hdrSize, data, (size_t)size);
	return R_SUCCEEDED(cecdWriteMessage(sProgramId, true, msgId, msg, totalSize));
}

bool spPopInbox(void* outBuf, int cap, int* outLen) {
	if (!sEnabled || !outBuf || cap <= 0 || !outLen) return false;
	*outLen = 0;

	// 1. Check SD drop-in inbox file (if another tag or tool placed a packet here).
	char dropPath[256];
	dataPath(dropPath, sizeof dropPath, "sp_inbox_drop.bin");
	if (FILE* f = fopen(dropPath, "rb")) {
		const size_t n = fread(outBuf, 1, (size_t)cap, f);
		fclose(f);
		remove(dropPath);
		if (n >= 72) {
			*outLen = (int)n;
			return true;
		}
	}

	// 2. Poll real CECD inbox when available.
	if (!ensureService()) return false;
	ensureMailbox();
	u8 boxBuf[sizeof(CecBoxInfoHeader) + 16 * 112] = {};
	u32 readBytes = 0;
	if (R_FAILED(cecdOpenAndRead(sProgramId, kPathInboxInfo, 0, boxBuf, sizeof boxBuf, &readBytes))) return false;
	CecBoxInfoHeader hdr = {};
	memcpy(&hdr, boxBuf, sizeof hdr);
	if (hdr.magic != 0x6262 || hdr.curMsgs == 0) return false;

	// First entry header starts at offset sizeof(CecBoxInfoHeader) (0x14/0x70 depending on firmware; scan for msgId at +0x20).
	for (u32 m = 0; m < hdr.curMsgs && m < 16; m++) {
		const size_t off = 0x70 + (size_t)m * 0x70;
		if (off + 0x70 > readBytes) break;
		const u8* entryHdr = boxBuf + off;
		const u8* msgId = entryHdr + 0x20;
		u8 msg[256] = {};
		u32 msgRead = 0;
		if (R_SUCCEEDED(cecdReadMessage(sProgramId, false, msgId, msg, sizeof msg, &msgRead)) && msgRead > 0x70) {
			u32 hdrSz = 0x70;
			u32 bodySz = 0;
			memcpy(&hdrSz, msg + 0x08, 4);
			memcpy(&bodySz, msg + 0x0C, 4);
			if (hdrSz < msgRead && bodySz > 0 && hdrSz + bodySz <= msgRead && (int)bodySz <= cap) {
				memcpy(outBuf, msg + hdrSz, bodySz);
				*outLen = (int)bodySz;
				cecdDelete(sProgramId, kPathInboxMsg, false, msgId);
				return true;
			}
		}
		cecdDelete(sProgramId, kPathInboxMsg, false, msgId);
	}
	return false;
}

}  // namespace port
