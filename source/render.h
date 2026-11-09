#pragma once
#include "common.h"

#define WHITE 0xFFFFFFFFu
inline u32 rgba(u8 r, u8 g, u8 b, u8 a = 255) { return C2D_Color32(r, g, b, a); }
inline u32 withAlpha(u32 c, float a) {
	u8 v = (u8)(clampf(a, 0, 1) * ((c >> 24) & 0xFF));
	return (c & 0x00FFFFFFu) | ((u32)v << 24);
}

// Text stays cream everywhere. Sign colour and the one accent follow the board chosen in Visuals.
#define COL_CREAM 0xFFD2F0FFu   // 255, 240, 210
#define COL_DIM 0xFFB4CADCu     // 220, 202, 180
#define COL_ACCENT gfx::themeAccent()
#define COL_PLANK gfx::themePlank()

enum Align { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };
enum SheetId { SHEET_SPRITES, SHEET_FONT, SHEET_LOGO, SHEET_BGTOP, SHEET_BOARD, SHEET_FRUIT, SHEET_COUNT };

namespace gfx {
// Loads the little that the loading screen itself needs. The rest arrives through loadStep().
bool init();
int loadStepCount();
void loadStep(int i);
void shutdown();
u32 themePlank();   // sign colour for the chosen board
u32 themeAccent();  // highlight colour for the chosen board
u32 themeBar();     // dark strip colour for the chosen board
u32 boardColor(int board, int part);  // part 0 plank, 1 accent, 2 bar: a board's colours without choosing it
// The board to draw: the chosen one, or the season's while a free board is chosen and Seasonal Boards is on.
int shownBoard();
int shownBoardOn(int month, int day);  // month 1-12; shownBoard for a given date
void setBoardPicking(bool picking);    // while the Board row is chosen, show exactly what it says

// Keeps the chosen scenery and board loaded, and the 2D fruit sheet only while the 2D art style
// needs it. Replaced textures are freed a few frames later, once the GPU is done with them.
void sync();
// The 2D fruit sheet stays loaded with 3D art too while this is on: the Fruit Journal and High Contrast
// draw stickers whichever art style is chosen. A no-op where the sheet is always loaded.
void keepStickers(bool on);
bool stickersLoaded();  // for the Lab: is the 2D fruit sheet in right now

void beginFrame();
void endFrame();
// eye: 0 = left, 1 = right. Returns false if that eye does not need drawing.
bool beginTop(int eye);
void beginBottom();
// Windows, Linux and Android only (the Quit Game bomb): the whole window in sign-side units, which on a
// wide window reaches left of and above the signs. beginCorner draws in those units anywhere in the window.
void windowArea(float* x, float* y, float* w, float* h);
void beginCorner();
bool onTop();
// Stereo plane for following top-screen draws: >0 pops out, <0 sinks behind the screen.
void setDepth(float depth);
// Sideways shift per unit of model z for the eye being drawn, used by the 3D fruit.
float stereoPerZ();
void setShake(float dx, float dy);
// Moves everything drawn from now on; screen transitions slide with this.
void setBase(float dx, float dy);
void getOffset(float* x, float* y);  // base + shake, without the stereo shift
// Anything drawn from now on is cut off outside this rectangle, until clearClip. It slides with setBase.
void setClip(float x, float y, float w, float h);
void clearClip();
// For the Lab: letters in font_metrics.h, and images in the loaded font sheet (each letter has two).
int fontLetterCount();
int fontImageCount();

#if !PORT_TWO_SCREENS
// One screen: the board behind everything, the field in its own view, and a fade over all of it.
void drawBackdrop(u32 tint);
void beginField();
void fadeAll(float alpha);
#endif

// Test hook: draws both eyes as if the 3D slider were all the way up, even on a 2DS.
void forceStereo(bool on);

void sprite(SheetId sheet, int idx, float x, float y, float sx = 1, float sy = 1, float rot = 0, u32 color = WHITE);
void image(SheetId sheet, int idx, float x, float y, float w, float h, u32 color = WHITE);
// Part of a sprite, from (u0, v0) to (u1, v1) with 0 to 1 across it. A backwards range mirrors it.
void imagePart(SheetId sheet, int idx, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 color = WHITE);
void rect(float x, float y, float w, float h, u32 color);
void tri(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2, float y2, u32 c2);
float textWidth(const char* s, float scale);
void text(const char* s, float x, float y, float scale, u32 color = WHITE, Align align = ALIGN_LEFT);
// One letter centred on x, top at y, sx and sy its width and height scales. Text laid out by hand (the
// ring round a menu fruit) draws all its letters with fill false, then all again with fill true.
void glyph(char c, float x, float y, float sx, float sy, u32 color, bool fill);
float glyphWidth(char c, float scale);

// High Contrast draws signs as solid panels and outlines text; Larger Text makes all text a step bigger.
extern bool gHighContrast;
extern float gTextBoost;
void contrastSign(int idx, float x, float y, float w, float h, u32 color);

// A sign at any size: corners and screws keep their shape, the wood along the grain stretches, and
// a tall sign repeats its middle band, mirrored so that the grain runs on with no seam.
// Text that starts inside a panel is kept inside it (text() shrinks a line that would run over). Each screen's
// drawing starts with clearPanels(); every sign notes itself.
void clearPanels();
void notePanel(float x, float y, float w, float h);

inline void sign(int idx, float x, float y, float w, float h, u32 color = WHITE) {
	notePanel(x, y, w, h);
	if (gHighContrast) {
		contrastSign(idx, x, y, w, h, color);
		return;
	}
	constexpr float kArtW = 256, kArtH = 56, kEndW = 16, kEndH = 18, kBandH = kArtH - kEndH * 2;
	float k = 1;
	if (w < kEndW * 2 && w / (kEndW * 2) < k) k = w / (kEndW * 2);
	if (h < kEndH * 2 && h / (kEndH * 2) < k) k = h / (kEndH * 2);
	const float cw = kEndW * k, ch = kEndH * k, midW = w - cw * 2, midH = h - ch * 2;
	const float su = kEndW / kArtW, sv = kEndH / kArtH;
	const float xs[3] = {x, x + cw, x + w - cw}, ws[3] = {cw, midW, cw};
	const float us[4] = {0, su, 1 - su, 1};
	const auto row = [&](float y0, float rowH, float v0, float v1) {
		for (int i = 0; i < 3; i++)
			if (ws[i] > 0) imagePart(SHEET_SPRITES, idx, xs[i], y0, ws[i], rowH, us[i], v0, us[i + 1], v1, color);
	};
	row(y, ch, 0, sv);
	if (midH > 0) {
		int bands = (int)(midH / (kBandH * k) + 0.5f);
		bands |= 1;  // an odd count, so the last band ends where the bottom row begins
		const float bandH = midH / bands;
		for (int b = 0; b < bands; b++) {
			const bool flip = b & 1;
			row(y + ch + bandH * b, bandH, flip ? 1 - sv : sv, flip ? sv : 1 - sv);
		}
	}
	row(y + h - ch, ch, 1 - sv, 1);
}

extern u32 drawCalls;      // draws issued last frame
extern u32 drawCallsLive;  // draws issued so far this frame
}  // namespace gfx
