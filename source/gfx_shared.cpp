// The parts of drawing that look the same everywhere: board colours and the sign-painted font.
#include "font_metrics.h"
#include "render.h"
#include "sprites.h"

namespace gfx {
struct BoardTheme {
	u8 plank[3], accent[3], bar[3];
};
// Picked from each board: its check, its wood, its plate rim, its veins, its sea, its night.
static const BoardTheme kThemes[kBoardCount] = {
	{{46, 84, 116}, {255, 204, 96}, {22, 42, 62}},
	{{112, 68, 36}, {255, 170, 60}, {62, 36, 18}},
	{{44, 84, 148}, {255, 214, 120}, {70, 50, 40}},
	{{78, 82, 100}, {130, 214, 240}, {52, 54, 68}},
	{{24, 100, 108}, {255, 214, 110}, {16, 70, 76}},
	{{66, 46, 118}, {255, 200, 120}, {24, 18, 54}},
	{{40, 48, 110}, {255, 214, 120}, {16, 20, 54}},
	{{30, 110, 120}, {255, 206, 80}, {120, 40, 36}},
	{{96, 92, 88}, {255, 170, 90}, {58, 56, 54}},
	{{70, 100, 150}, {200, 230, 255}, {36, 52, 86}},
	{{110, 60, 30}, {255, 170, 60}, {56, 30, 16}},
};
u32 themePlank() {
	const u8* c = kThemes[gSave.board % kBoardCount].plank;
	// The plank art is about 84% white, so this lands on the listed colour.
	auto up = [](u8 v) { int n = v * 6 / 5; return (u8)(n > 255 ? 255 : n); };
	return C2D_Color32(up(c[0]), up(c[1]), up(c[2]), 255);
}
u32 themeAccent() {
	const u8* c = kThemes[gSave.board % kBoardCount].accent;
	return C2D_Color32(c[0], c[1], c[2], 255);
}
u32 boardColor(int board, int part) {
	const BoardTheme& t = kThemes[board % kBoardCount];
	const u8* c = part == 0 ? t.plank : (part == 1 ? t.accent : t.bar);
	return C2D_Color32(c[0], c[1], c[2], 255);
}
u32 themeBar() {
	const u8* c = kThemes[gSave.board % kBoardCount].bar;
	return C2D_Color32(c[0], c[1], c[2], 255);
}

static inline int glyphSlot(char c) {
	if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
	if (c < 32 || c > 127) return 0;
	return c - 32;
}

bool gHighContrast = false;
float gTextBoost = 1;

float textWidth(const char* s, float scale) {
	float w = 0;
	for (; *s; s++) w += kFontAdvance[glyphSlot(*s)];
	return w * scale * gTextBoost;
}

static void textRun(const char* s, float x, float y, float scale, u32 color) {
	const float cell = FONT_CELL * scale;
	for (; *s; s++) {
		int slot = glyphSlot(*s);
		float adv = kFontAdvance[slot] * scale;
		int g = kFontIndex[slot];
		if (g >= 0) image(SHEET_FONT, g, x + (adv - cell) * 0.5f, y, cell, cell, color);
		x += adv;
	}
}

void text(const char* s, float x, float y, float scale, u32 color, Align align) {
	if (align != ALIGN_LEFT) {
		float w = textWidth(s, scale);
		x -= align == ALIGN_CENTER ? w * 0.5f : w;
	}
	scale *= gTextBoost;
	if (gHighContrast) {
		// A dark outline: the same words four times, nudged a little each way, under the real ones.
		const float o = 1.0f + scale * 1.2f;
		const u32 edge = C2D_Color32(0, 0, 0, (u8)(((color >> 24) & 255) * 0.9f));
		textRun(s, x - o, y, scale, edge);
		textRun(s, x + o, y, scale, edge);
		textRun(s, x, y - o, scale, edge);
		textRun(s, x, y + o, scale, edge);
	}
	textRun(s, x, y, scale, color);
}

// High Contrast signs: a solid dark panel with a light edge. The focus frame stays an edge only.
void contrastSign(int idx, float x, float y, float w, float h, u32 color) {
	const float t = 2;
	const bool frameOnly = idx == sprites_plank_focus_idx;
	const u32 edge = frameOnly ? color : C2D_Color32(235, 235, 235, (u8)(color >> 24));
	if (!frameOnly) rect(x, y, w, h, C2D_Color32(14, 14, 18, (u8)((color >> 24) * 0.94f)));
	const float k = frameOnly ? t * 1.5f : t;
	rect(x, y, w, k, edge);
	rect(x, y + h - k, w, k, edge);
	rect(x, y + k, k, h - k * 2, edge);
	rect(x + w - k, y + k, k, h - k * 2, edge);
}
}  // namespace gfx
