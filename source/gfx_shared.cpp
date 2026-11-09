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
#define SHOP_THEME(name, price, line, pr, pg, pb, ar, ag, ab, br, bg, bb) {{pr, pg, pb}, {ar, ag, ab}, {br, bg, bb}},
#include "shop_items.inc"
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

int fontLetterCount() { return FONT_FILL; }

float textWidth(const char* s, float scale) {
	float w = 0;
	for (; *s; s++) w += kFontAdvance[glyphSlot(*s)];
	return w * scale * gTextBoost;
}

// fill 0 draws whole letters, outline and all; FONT_FILL draws only their insides.
static void textRun(const char* s, float x, float y, float scale, u32 color, int fill = 0) {
	const float cell = FONT_CELL * scale;
	for (; *s; s++) {
		int slot = glyphSlot(*s);
		float adv = kFontAdvance[slot] * scale;
		int g = kFontIndex[slot];
		if (g >= 0) image(SHEET_FONT, g + fill, x + (adv - cell) * 0.5f, y, cell, cell, color);
		x += adv;
	}
}

// Panels drawn so far on this screen. A line of text that starts inside one but would run past its edges
// is made smaller until it fits (long translations of a line, Larger Text, a long version name).
namespace {
struct Panel {
	float x, y, w, h, ox, oy;
};
constexpr int kMaxPanels = 64;
Panel sPanels[kMaxPanels];
int sPanelCount;
}  // namespace

void clearPanels() { sPanelCount = 0; }

void notePanel(float x, float y, float w, float h) {
	if (sPanelCount >= kMaxPanels) return;
	Panel& p = sPanels[sPanelCount++];
	p.x = x;
	p.y = y;
	p.w = w;
	p.h = h;
	getOffset(&p.ox, &p.oy);  // a sliding screen's panels never squeeze the other screen's text
}

static float fitScale(const char* s, float x, float y, float scale, Align align) {
	const float w = textWidth(s, scale);
	const float midY = y + 16 * scale * gTextBoost;
	float ox, oy;
	getOffset(&ox, &oy);
	for (int i = sPanelCount - 1; i >= 0; i--) {
		const Panel& p = sPanels[i];
		if (p.ox != ox || p.oy != oy || x < p.x || x > p.x + p.w || midY < p.y || midY > p.y + p.h) continue;
		const float pad = p.w > 60 ? 7 : 3, lo = p.x + pad, hi = p.x + p.w - pad;
		float room = align == ALIGN_LEFT ? hi - x : (align == ALIGN_RIGHT ? x - lo : 2 * (x - lo < hi - x ? x - lo : hi - x));
		if (w > room && room > 8) return scale * room / w;
		return scale;
	}
	return scale;
}

void text(const char* s, float x, float y, float scale, u32 color, Align align) {
	if (sPanelCount) {
		const float fit = fitScale(s, x, y, scale, align);
		y += (scale - fit) * gTextBoost * 16;  // a shrunk line keeps its middle where it was
		scale = fit;
	}
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
	// Every outline first, then every fill over them: a letter's outline never cuts into the one beside it.
	textRun(s, x, y, scale, color);
	textRun(s, x, y, scale, color, FONT_FILL);
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

namespace gfx {
void glyph(char c, float x, float y, float sx, float sy, u32 color, bool fill) {
	const int g = kFontIndex[glyphSlot(c)];
	if (g < 0) return;
	sx *= gTextBoost;
	sy *= gTextBoost;
	image(SHEET_FONT, g + (fill ? FONT_FILL : 0), x - FONT_CELL * sx * 0.5f, y, FONT_CELL * sx, FONT_CELL * sy, color);
}

float glyphWidth(char c, float scale) { return kFontAdvance[glyphSlot(c)] * scale * gTextBoost; }
}  // namespace gfx
