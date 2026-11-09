// IMA ADPCM, four bits a sample: how the 3DS stores full-length songs (tools/convert_shop_music.py writes them).
// A packed song is 4 bytes of starting state and then two samples to the byte, low nibble first.
#pragma once
#include "common.h"

constexpr u32 kAdpcmRoom = 16;  // spare bytes past the samples, so the packed bytes can sit at the end of the same buffer
constexpr u32 kAdpcmHeader = 4;
inline u32 adpcmFileBytes(u32 frames) { return kAdpcmHeader + (frames + 1) / 2; }

struct AdpcmState {
	int predicted, index;
};

inline AdpcmState adpcmStart(const u8* file) {
	return {(s16)(file[0] | (file[1] << 8)), file[2] > 88 ? 88 : file[2]};
}

// Unpacks samples first..first+count-1. Unpacking forward is safe even when the packed bytes share the buffer.
inline void adpcmUnpack(AdpcmState& s, const u8* nibbles, u32 first, u32 count, s16* out) {
	static const short kSteps[89] = {7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,    31,    34,    37,
	                                 41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,
	                                 230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,   1060,  1166,
	                                 1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
	                                 7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
	static const signed char kMoves[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
	int predicted = s.predicted, index = s.index;
	for (u32 i = first; i < first + count; i++) {
		const int code = i & 1 ? nibbles[i >> 1] >> 4 : nibbles[i >> 1] & 15;
		const int step = kSteps[index];
		int delta = step >> 3;
		if (code & 4) delta += step;
		if (code & 2) delta += step >> 1;
		if (code & 1) delta += step >> 2;
		predicted += code & 8 ? -delta : delta;
		if (predicted > 32767) predicted = 32767;
		if (predicted < -32768) predicted = -32768;
		index += kMoves[code & 7];
		if (index < 0) index = 0;
		if (index > 88) index = 88;
		out[i] = (s16)predicted;
	}
	s.predicted = predicted;
	s.index = index;
}
