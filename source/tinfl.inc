// Compact RFC 1951 raw DEFLATE decompressor for ZIP mod archives (stored + deflate method 8).
// Public-domain style implementation with bounded bit-reader and table-free Huffman decoding.

namespace tinfl {

struct BitReader {
	const u8* p;
	size_t len;
	size_t pos;
	u32 bitBuf;
	int bitCnt;

	u32 peek(int n) {
		while (bitCnt < n && pos < len) {
			bitBuf |= ((u32)p[pos++]) << bitCnt;
			bitCnt += 8;
		}
		return bitBuf & ((1u << n) - 1u);
	}
	void drop(int n) {
		bitBuf >>= n;
		bitCnt -= n;
	}
	u32 read(int n) {
		u32 v = peek(n);
		drop(n);
		return v;
	}
	void align() {
		int skip = bitCnt & 7;
		if (skip) drop(skip);
	}
};

struct Huff {
	u16 counts[16];
	u16 symbols[320];
	int maxSym;
};

static bool buildHuff(Huff& h, const u8* lens, int num) {
	for (int i = 0; i < 16; i++) h.counts[i] = 0;
	for (int i = 0; i < num; i++) if (lens[i] < 16) h.counts[lens[i]]++;
	h.counts[0] = 0;
	u16 offs[16];
	offs[0] = 0;
	for (int i = 1; i < 16; i++) offs[i] = (u16)(offs[i - 1] + h.counts[i - 1]);
	for (int i = 0; i < num; i++) {
		if (lens[i] > 0 && lens[i] < 16) h.symbols[offs[lens[i]]++] = (u16)i;
	}
	h.maxSym = num;
	return true;
}

static int decodeSym(BitReader& br, const Huff& h) {
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= 15; len++) {
		code |= (int)br.read(1);
		int count = h.counts[len];
		if (code - count < first) return h.symbols[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

static bool inflateRaw(const u8* src, size_t srcLen, u8* dst, size_t dstLen) {
	static const u16 kLenBase[29] = {
		3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
	};
	static const u8 kLenExtra[29] = {
		0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
	};
	static const u16 kDistBase[30] = {
		1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
		1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
	};
	static const u8 kDistExtra[30] = {
		0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
	};
	static const u8 kClOrder[19] = {
		16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
	};

	BitReader br{src, srcLen, 0, 0, 0};
	size_t outPos = 0;
	bool bfinal = false;
	while (!bfinal) {
		if (br.pos >= br.len && br.bitCnt < 3) return false;
		bfinal = br.read(1) != 0;
		u32 btype = br.read(2);
		if (btype == 3) return false;
		if (btype == 0) {
			br.align();
			if (br.pos + 4 > br.len) return false;
			u16 len = (u16)(br.p[br.pos] | (br.p[br.pos + 1] << 8));
			u16 nlen = (u16)(br.p[br.pos + 2] | (br.p[br.pos + 3] << 8));
			br.pos += 4;
			if ((u16)~len != nlen) return false;
			if (br.pos + len > br.len || outPos + len > dstLen) return false;
			memcpy(dst + outPos, br.p + br.pos, len);
			br.pos += len;
			outPos += len;
			continue;
		}
		Huff litH{}, distH{};
		u8 litLens[288], distLens[32];
		if (btype == 1) {
			for (int i = 0; i <= 143; i++) litLens[i] = 8;
			for (int i = 144; i <= 255; i++) litLens[i] = 9;
			for (int i = 256; i <= 279; i++) litLens[i] = 7;
			for (int i = 280; i <= 287; i++) litLens[i] = 8;
			for (int i = 0; i < 32; i++) distLens[i] = 5;
			buildHuff(litH, litLens, 288);
			buildHuff(distH, distLens, 32);
		} else {
			int hlit = (int)br.read(5) + 257;
			int hdist = (int)br.read(5) + 1;
			int hclen = (int)br.read(4) + 4;
			if (hlit > 286 || hdist > 30) return false;
			u8 clLens[19] = {};
			for (int i = 0; i < hclen; i++) clLens[kClOrder[i]] = (u8)br.read(3);
			Huff clH{};
			buildHuff(clH, clLens, 19);
			u8 allLens[320] = {};
			int idx = 0, total = hlit + hdist;
			while (idx < total) {
				int s = decodeSym(br, clH);
				if (s < 0) return false;
				if (s < 16) {
					allLens[idx++] = (u8)s;
				} else {
					int rep = 0;
					u8 val = 0;
					if (s == 16) {
						if (idx == 0) return false;
						val = allLens[idx - 1];
						rep = 3 + (int)br.read(2);
					} else if (s == 17) {
						rep = 3 + (int)br.read(3);
					} else {
						rep = 11 + (int)br.read(7);
					}
					if (idx + rep > total) return false;
					for (int r = 0; r < rep; r++) allLens[idx++] = val;
				}
			}
			buildHuff(litH, allLens, hlit);
			buildHuff(distH, allLens + hlit, hdist);
		}
		for (;;) {
			int sym = decodeSym(br, litH);
			if (sym < 0) return false;
			if (sym < 256) {
				if (outPos >= dstLen) return false;
				dst[outPos++] = (u8)sym;
			} else if (sym == 256) {
				break;
			} else {
				int li = sym - 257;
				if (li < 0 || li >= 29) return false;
				int mlen = (int)kLenBase[li] + (kLenExtra[li] ? (int)br.read(kLenExtra[li]) : 0);
				int dsym = decodeSym(br, distH);
				if (dsym < 0 || dsym >= 30) return false;
				int dist = (int)kDistBase[dsym] + (kDistExtra[dsym] ? (int)br.read(kDistExtra[dsym]) : 0);
				if (dist <= 0 || (size_t)dist > outPos || outPos + (size_t)mlen > dstLen) return false;
				for (int i = 0; i < mlen; i++) {
					dst[outPos] = dst[outPos - (size_t)dist];
					outPos++;
				}
			}
		}
	}
	return outPos == dstLen;
}

}  // namespace tinfl
