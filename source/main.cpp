// The shared game: application flow and menus. Each port's main loop drives it through app.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "blade.h"
#include "boom.h"
#include "common.h"
#include "fruit.h"
#include "game.h"
#include "jobs.h"
#include "model3d.h"
#include "render.h"
#include "progress.h"
#include "shop.h"
#include "update.h"
#include "sprites.h"

#if PORT_3DS
#include <sys/stat.h>
#include "bgtop0.h"
#endif
#include "logo.h"
#include "port_api.h"
#if PORT_CUSTOM_MUSIC
#include "custom_music.h"
#endif

#define VERSION_STRING "1.0.0"

#if PORT_TWO_SCREENS
static constexpr float kSlideTop = 400, kSlideBottom = 320;
#else
static constexpr float kSlideTop = 900, kSlideBottom = 900;
#endif

enum Screen {
	SCREEN_LOADING, SCREEN_TITLE, SCREEN_MODES, SCREEN_OPTIONS, SCREEN_SECTION, SCREEN_CREDITS, SCREEN_HOWTO, SCREEN_SHOP,
	SCREEN_JOURNAL, SCREEN_STATS, SCREEN_BEAT, SCREEN_BEATTOOL, SCREEN_UPDATE,
	SCREEN_GAME, SCREEN_PAUSE, SCREEN_RESULT, SCREEN_BENCH, SCREEN_PASS_TURN, SCREEN_PASS_BOARD, SCREEN_LATENCY, SCREEN_COUNT
};

static Screen sScreen = SCREEN_LOADING;
static Game sGame;  // static storage: all gameplay pools are allocated once at load time
static Blade sBlade;
static int sFocus[SCREEN_COUNT];
static int sPressed = -1;
static bool sShowDebug = false;
static bool sSuspended = false;
static bool sNoSound = false;
static float sTime = 0;
static float sFps = 60;
static u64 sLastTick = 0;
static bool sWasTouching = false;
static float sPointX = kFieldW * 0.5f, sPointY = 150;  // the stick's blade point, in field units
static float sPointShow = 0;  // seconds the point stays up after the stick was last used

// ---------------------------------------------------------------- transitions
// Menus slide sideways; anything entering or leaving a game fades through black.
static constexpr float kSlideTime = 0.22f;
static constexpr float kFadeTime = 0.18f;
static bool sSliding = false;
static float sSlideT = 0;
static Screen sSlideFrom = SCREEN_TITLE;
static float sSlideDir = 1;
static float sFade = 0;       // 0 clear, 1 black
static bool sFadingOut = false;
static void (*sFadeAction)() = nullptr;

static void pickSplash();
static void toModes();
static void toTitle();
static void startGame();

static void goTo(Screen to, float dir) {
	sPressed = -1;
	if (to == SCREEN_TITLE) pickSplash();
	// These menus open on their first real choice, not on the back arrow.
	if ((to == SCREEN_MODES || to == SCREEN_OPTIONS) && sFocus[to] == 0) sFocus[to] = 1;
	if (!gSave.transitions) {
		sScreen = to;
		return;
	}
	sSlideFrom = sScreen;
	sScreen = to;
	sSlideDir = dir;
	sSliding = true;
	sSlideT = 0;
}

static void fadeThen(void (*action)()) {
	sPressed = -1;
	if (!gSave.transitions) {
		action();
		return;
	}
	sFadeAction = action;
	sFadingOut = true;
}

static inline bool inputBlocked() { return sSliding || sFadingOut || sFade > 0.5f; }

// ---------------------------------------------------------------- signs
struct Button {
	float x, y, w, h;
	const char* label;
};

static const Button kBack = {6, 5, 46, 34, nullptr};

static const Button kTitleButtons[] = {
#if PORT_3DS
	{40, 14, 240, 80, "Play"},
	{10, 102, 96, 54, "Options"},  {112, 102, 96, 54, "Journal"}, {214, 102, 96, 54, "Stats"},
	{10, 166, 96, 46, "How to Play"}, {112, 166, 96, 46, "Shop"}, {214, 166, 96, 46, "Credits"},
#else
	// The fruit sit a little higher here, to leave the bottom-left corner to the link icons.
	{40, 8, 240, 76, "Play"},
	{10, 88, 96, 50, "Options"},  {112, 88, 96, 50, "Journal"}, {214, 88, 96, 50, "Stats"},
	{10, 142, 96, 46, "How to Play"}, {112, 142, 96, 46, "Shop"}, {214, 142, 96, 46, "Credits"},
	// Link icons: not fruit, so a swipe never opens a browser.
	{8, 196, 38, 38, "GitHub"}, {52, 196, 38, 38, "X"}, {96, 196, 38, 38, "YouTube"},
#endif
};
static constexpr int kTitleCount = sizeof kTitleButtons / sizeof kTitleButtons[0];
static constexpr int kTitleSigns = 7;  // the fruit; anything after is a link icon
#if !PORT_3DS
static const char* const kTitleLinks[] = {"https://github.com/RealAnxiousGit/fruit-slicer", "https://x.com/NickBinMusic", "https://www.youtube.com/@NickBStudios"};

// The link icons are drawn from shapes at the size they appear, so they stay sharp at any scale. A convex
// shape gets a thin rim fading to clear, which smooths its edges.
static void softShape(const float* xy, int n, float cx, float cy, float s, u32 c) {
	constexpr int kMax = 40;
	float px[kMax], py[kMax], mx = 0, my = 0;
	for (int i = 0; i < n; i++) {
		px[i] = cx + xy[i * 2] * s;
		py[i] = cy + xy[i * 2 + 1] * s;
		mx += px[i] / n;
		my += py[i] / n;
	}
	const u32 clear = withAlpha(c, 0);
	constexpr float kRim = 0.7f;
	for (int i = 0; i < n; i++) {
		const int j = (i + 1) % n;
		gfx::tri(mx, my, c, px[i], py[i], c, px[j], py[j], c);
		float nx = py[j] - py[i], ny = px[i] - px[j];
		const float len = sqrtf(nx * nx + ny * ny);
		if (len < 1e-4f) continue;
		nx /= len;
		ny /= len;
		if (nx * (px[i] - mx) + ny * (py[i] - my) < 0) {
			nx = -nx;
			ny = -ny;
		}
		gfx::tri(px[i], py[i], c, px[j], py[j], c, px[j] + nx * kRim, py[j] + ny * kRim, clear);
		gfx::tri(px[i], py[i], c, px[j] + nx * kRim, py[j] + ny * kRim, clear, px[i] + nx * kRim, py[i] + ny * kRim, clear);
	}
}

static void softEllipse(float cx, float cy, float rx, float ry, u32 c) {
	constexpr int kSegs = 32;
	float xy[kSegs * 2];
	for (int i = 0; i < kSegs; i++) {
		const float a = i * 6.2831853f / kSegs;
		xy[i * 2] = cosf(a) * rx;
		xy[i * 2 + 1] = sinf(a) * ry;
	}
	softShape(xy, kSegs, cx, cy, 1, c);
}

constexpr int kMaxPts = 40;  // points in one outline

// Where a ray from (0, 0) leaves an outline that every such ray crosses once (in the outline's own units).
static float rayOut(const float* xy, int n, float dx, float dy) {
	float best = 1e9f;
	for (int i = 0; i < n; i++) {
		const int j = (i + 1) % n;
		const float ax = xy[i * 2], ay = xy[i * 2 + 1], ex = xy[j * 2] - ax, ey = xy[j * 2 + 1] - ay;
		const float den = dx * ey - dy * ex;
		if (fabsf(den) < 1e-6f) continue;
		const float t = (ax * ey - ay * ex) / den, u = (ax * dy - ay * dx) / den;
		if (t > 0 && u >= -1e-4f && u <= 1 + 1e-4f && t < best) best = t;
	}
	return best < 1e8f ? best : 0;
}

// Ink between an outer outline and a hole, both seen whole from the point (ox, oy) of the outlines' units:
// the hole is left empty, so a see-through plate shows through it as it does around the mark. Both edges
// fade over a fraction of a pixel to stay smooth.
static void inkWithHole(const float* outer, int no, const float* hole, int nh, float ox, float oy, float cx, float cy, float s, u32 c) {
	constexpr int kRays = 120;
	float po[kMaxPts * 2], ph[kMaxPts * 2];
	for (int i = 0; i < no; i++) {
		po[i * 2] = outer[i * 2] - ox;
		po[i * 2 + 1] = outer[i * 2 + 1] - oy;
	}
	for (int i = 0; i < nh; i++) {
		ph[i * 2] = hole[i * 2] - ox;
		ph[i * 2 + 1] = hole[i * 2 + 1] - oy;
	}
	const u32 clear = withAlpha(c, 0);
	const float rim = 0.7f / s, mx = cx + ox * s, my = cy + oy * s;
	float px[4], py[4], qx[4], qy[4];  // this ray: hole edge, hole fade, outer edge, outer fade
	for (int i = 0; i <= kRays; i++) {
		const float a = i * 6.2831853f / kRays, dx = cosf(a), dy = sinf(a);
		const float tOut = rayOut(po, no, dx, dy), tIn = nh ? rayOut(ph, nh, dx, dy) : 0;
		const float ts[4] = {tIn, tIn > rim ? tIn - rim : 0, tOut, tOut + rim};
		for (int k = 0; k < 4; k++) {
			qx[k] = mx + dx * ts[k] * s;
			qy[k] = my + dy * ts[k] * s;
		}
		if (i > 0) {
			gfx::tri(px[0], py[0], c, px[2], py[2], c, qx[2], qy[2], c);
			gfx::tri(px[0], py[0], c, qx[2], qy[2], c, qx[0], qy[0], c);
			gfx::tri(px[2], py[2], c, px[3], py[3], clear, qx[3], qy[3], clear);
			gfx::tri(px[2], py[2], c, qx[3], qy[3], clear, qx[2], qy[2], c);
			if (nh) {
				gfx::tri(px[1], py[1], clear, px[0], py[0], c, qx[0], qy[0], c);
				gfx::tri(px[1], py[1], clear, qx[0], qy[0], c, qx[1], qy[1], clear);
			}
		}
		for (int k = 0; k < 4; k++) {
			px[k] = qx[k];
			py[k] = qy[k];
		}
	}
}

// GitHub's cat, X's letter and YouTube's play button in black, on a light round plate. Both are see-through
// until the icon is pointed at or selected.
static void drawLinkIcon(const Button& b, int which, bool focused) {
	const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f, r = b.w * 0.5f;
	const float a = focused ? 1.0f : 0.55f;
	const u32 ink = withAlpha(rgba(16, 16, 18), focused ? 1.0f : 0.8f), plate = withAlpha(COL_CREAM, a);
	if (focused) softEllipse(cx, cy, r + 2.5f, r + 2.5f, COL_ACCENT);
	softEllipse(cx, cy, r, r, plate);
	const float s = r * 0.7f;  // the mark's own radius
	if (which == 0) {
		// A black circle with the cat standing out of it: head, two ears, a body down to the circle's foot.
		static float cat[kMaxPts * 2], ring[kMaxPts * 2];
		static int nCat;
		if (!nCat) {
			const auto head = [&](float deg) {
				const float t = deg * 0.017453293f;
				cat[nCat * 2] = cosf(t) * 0.5f;
				cat[nCat * 2 + 1] = -0.1f + sinf(t) * 0.42f;
				nCat++;
			};
			const auto pt = [&](float x, float y) {
				cat[nCat * 2] = x;
				cat[nCat * 2 + 1] = y;
				nCat++;
			};
			pt(-0.25f, 0.86f);
			pt(-0.23f, 0.28f);
			for (int d = 120; d <= 210; d += 15) head((float)d);
			pt(-0.44f, -0.7f);
			head(245);
			for (int d = 260; d <= 280; d += 10) head((float)d);
			head(295);
			pt(0.44f, -0.7f);
			for (int d = 330; d <= 420; d += 15) head((float)d);
			pt(0.23f, 0.28f);
			pt(0.25f, 0.86f);
			for (int i = 0; i < 32; i++) {
				ring[i * 2] = cosf(i * 6.2831853f / 32);
				ring[i * 2 + 1] = sinf(i * 6.2831853f / 32);
			}
		}
		inkWithHole(ring, 32, cat, nCat, 0, -0.1f, cx, cy, s, ink);
	} else if (which == 1) {
		// The X: a broad stroke falling left to right, hollow down its middle, crossed by a thin one.
		static const float kBroad[] = {-0.78f, -0.78f, -0.32f, -0.78f, 0.78f, 0.78f, 0.32f, 0.78f};
		static const float kHollow[] = {-0.58f, -0.68f, -0.46f, -0.68f, 0.58f, 0.68f, 0.46f, 0.68f};
		static const float kThinTop[] = {0.6f, -0.78f, 0.76f, -0.78f, 0.29f, -0.24f, 0.13f, -0.24f};
		static const float kThinLow[] = {-0.6f, 0.78f, -0.76f, 0.78f, -0.29f, 0.24f, -0.13f, 0.24f};
		inkWithHole(kBroad, 4, kHollow, 4, 0, 0, cx, cy, s, ink);
		softShape(kThinTop, 4, cx, cy, s, ink);
		softShape(kThinLow, 4, cx, cy, s, ink);
	} else {
		// A wide rounded box with the play arrow cut out of it.
		static float box[kMaxPts * 2];
		static const float kPlay[] = {-0.26f, -0.34f, 0.38f, 0.0f, -0.26f, 0.34f};
		constexpr int kArc = 7;
		if (box[0] == 0) {
			const float hx = 1.0f - 0.36f, hy = 0.7f - 0.36f;
			const float ox[4] = {hx, -hx, -hx, hx}, oy[4] = {hy, hy, -hy, -hy};
			int k = 0;
			for (int q = 0; q < 4; q++)
				for (int i = 0; i < kArc; i++) {
					const float t = (q + i / (float)(kArc - 1)) * 1.5707963f;
					box[k++] = ox[q] + cosf(t) * 0.36f;
					box[k++] = oy[q] + sinf(t) * 0.36f;
				}
		}
		inkWithHole(box, kArc * 4, kPlay, 3, -0.04f, 0, cx, cy, s, ink);
	}
}
#endif
// Index 0 of every list below is its back arrow, so D-pad and touch treat it like any other sign.
// Pick a Game runs in pages of up to six, the main modes first. A new mode only needs a place in this list.
enum { SIGN_MIXER = MODE_COUNT + 1, SIGN_PASS };  // signs on Pick a Game that open a page, past the modes and Practice
static const u8 kModeOrder[] = {MODE_CLASSIC, MODE_ARCADE, MODE_FRENZY, MODE_ZEN, MODE_SUDDEN, MODE_DAILY, MODE_MINEFIELD, MODE_COMBO, MODE_BEAT, SIGN_MIXER, SIGN_PASS};
static_assert(sizeof kModeOrder == MODE_COUNT + 2, "a sign for every mode");
static constexpr int kModesPerPage = 6;
static constexpr int kModePages = ((int)sizeof kModeOrder + kModesPerPage - 1) / kModesPerPage;
static constexpr float kModeTop = 46, kModeStep = 58, kModeSignW = 124, kModeSignH = 52;
static constexpr float kPageArrowY = kModeTop + kModeStep + kModeSignH * 0.5f - 25;
static int sModePage = 0, sModeOldPage = 0;
static float sModeSlide = 0;         // how far the new page still has to travel to settle
static int sArrowPressed = -1, sArrowHover = -1;
// The arrows answer across the whole height of the signs, so they can be hit without aiming.
static const Button kPageArrows[2] = {{0, kModeTop, 31, kModeStep * 2 + kModeSignH, nullptr}, {289, kModeTop, 31, kModeStep * 2 + kModeSignH, nullptr}};

static int mixerRulesOn() {
	int n = 0;
	for (int i = 0; i < MIX_COUNT; i++) n += gSave.mixer[i] != 0;
	return n;
}

static const char* modeSignName(int id) {
	if (id == SIGN_PASS) return "Pass and Play";
	if (id != SIGN_MIXER) return kModes[id].name;
	if (!mixerRulesOn()) return "Mixer";
	static char label[16];
	snprintf(label, sizeof label, "Mixer (%d on)", mixerRulesOn());
	return label;
}

// Index 0 is the back arrow, as on every other menu. A last sign alone in its row is centred.
static int modeSigns(int page, Button* out) {
	const int left = (int)sizeof kModeOrder - page * kModesPerPage;
	const int n = left < kModesPerPage ? left : kModesPerPage;
	out[0] = kBack;
	for (int i = 0; i < n; i++) {
		const float x = i == n - 1 && (n & 1) ? 160 - kModeSignW * 0.5f : (i & 1 ? 163.0f : 33.0f);
		out[i + 1] = {x, kModeTop + kModeStep * (i / 2), kModeSignW, kModeSignH, modeSignName(kModeOrder[page * kModesPerPage + i])};
	}
	return n + 1;
}
static int modeOnSign(int page, int sign) { return kModeOrder[page * kModesPerPage + sign - 1]; }

static const Button kOnlyBack[] = {kBack};
static const Button kHowtoButtons[] = {kBack, {90, 188, 140, 48, "Practice"}};
static const Button kPracticeEndButtons[] = {kBack, {90, 170, 140, 52, "Let's Play"}};
// Beat Slice's track list: a row is a tap (or Up and Down), and Play is a fruit like any other.
static const Button kBeatButtons[] = {kBack, {216, 46, 96, 76, "Play"}};
static constexpr int kBeatRows = 5;
static const Button kBeatUp = {8, 190, 98, 26, nullptr}, kBeatDown = {110, 190, 98, 26, nullptr};
static constexpr float kBeatRowTop = 46, kBeatRowStep = 28;
static Button beatRow(int r) { return {8, kBeatRowTop + r * kBeatRowStep, 200, 26, nullptr}; }
static int sBeatScroll = 0;  // the first row shown
#if PORT_CUSTOM_MUSIC
// Under Play: the Beat Tool for a song of your own, and the folder your songs go in. Taps, like the rows.
static const Button kBeatTool = {216, 130, 96, 28, "Beat Tool"}, kBeatFolder = {216, 164, 96, 28, "Add Songs"};
// The Beat Tool: taps that change the beat at once, then Find Again and Play.
static const Button kToolButtons[] = {kBack, {180, 184, 130, 52, "Play"}};
static const Button kToolTaps[] = {
	{12, 112, 70, 30, "Slower"}, {88, 112, 70, 30, "Faster"}, {164, 112, 70, 30, "Half"}, {240, 112, 70, 30, "Double"},
	{12, 148, 70, 30, "Earlier"}, {88, 148, 70, 30, "Later"}, {164, 148, 70, 30, "Click On"}, {240, 148, 70, 30, "Bar Start"},
	{12, 192, 130, 38, "Find Again"},
};
static constexpr int kToolTapCount = sizeof kToolTaps / sizeof kToolTaps[0];
static float sFolderNote = 0;  // seconds the folder's path stays up, where it can't be opened
#endif
static const Button kResultButtons[] = {kBack, {20, 170, 134, 52, "Again"}, {166, 170, 134, 52, "Modes"}, {110, 104, 100, 52, "Share"}};
// Share saves a score card: Windows, Linux and Android. The 3DS can't keep or send a picture.
static constexpr int kResultCount = PORT_3DS ? 3 : 4;
static bool sCardFrame;          // this frame is the score card: the sign shows the logo, not the coins
static const char* sCardNote;    // what became of the last card, shown for a while
static float sCardNoteT;
#if PORT_PAUSE_BUTTON
static const Button kPauseSign = {2, kFieldTop + 2, 36, 36, nullptr};  // in field units: top-left, just under the score sign
#endif
static const Button kPauseButtons[] = {kBack, {50, 44, 220, 56, "Resume"}, {50, 108, 220, 56, "Options"}, {50, 172, 220, 56, "Main Menu"}};

static bool hit(const Button& b, int x, int y) {
#if PORT_PAUSE_BUTTON
	// The way out is small for a fingertip, so it answers a little beyond its edges.
	if (b.x == kBack.x && b.y == kBack.y && b.w == kBack.w && b.h == kBack.h) return x >= 0 && x < b.x + b.w + 12 && y >= 0 && y < b.y + b.h + 2;
#endif
	return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static u32 withDim(u32 c) {
	u8 r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
	return rgba(r * 4 / 5, g * 4 / 5, b * 4 / 5);
}

static void drawPlank(float x, float y, float w, float h, bool focused, bool pressed) {
	float dy = pressed ? 2 : 0;
	gfx::sign(sprites_plank_idx, x, y + dy, w, h, pressed ? withDim(COL_PLANK) : COL_PLANK);
	if (focused) gfx::sign(sprites_plank_focus_idx, x - 2, y - 2 + dy, w + 4, h + 4, COL_ACCENT);
}

// ---------------------------------------------------------------- menu fruit
// A sign that takes you somewhere carries a fruit, always the same one for the same choice, and a swipe
// across the sign cuts it to pick it. The back sign carries a small berry on every screen.
struct MenuFruit {
	int whole, left, right, mesh;
	u32 juice;
};
enum { MF_MELON, MF_ORANGE, MF_PLUM, MF_LEMON, MF_BERRY, MF_APPLE, MF_PEAR, MF_PEACH, MF_KIWI, MF_GOLD, MF_PINEAPPLE, MF_COCONUT };
static const MenuFruit kMenuFruit[] = {
	{fruit_melon_idx, fruit_melon_l_idx, fruit_melon_r_idx, m3d::MESH_MELON, 0xFF4A3CF0u},
	{fruit_orange_idx, fruit_orange_l_idx, fruit_orange_r_idx, m3d::MESH_ORANGE, 0xFF2E9CFFu},
	{fruit_plum_idx, fruit_plum_l_idx, fruit_plum_r_idx, m3d::MESH_PLUM, 0xFF9A4AC2u},
	{fruit_lemon_idx, fruit_lemon_l_idx, fruit_lemon_r_idx, m3d::MESH_LEMON, 0xFF52E6FFu},
	{fruit_berry_idx, fruit_berry_l_idx, fruit_berry_r_idx, m3d::MESH_BERRY, 0xFF5A32F0u},
	{fruit_apple_idx, fruit_apple_l_idx, fruit_apple_r_idx, m3d::MESH_APPLE, 0xFF6AD8F5u},
	{fruit_pear_idx, fruit_pear_l_idx, fruit_pear_r_idx, m3d::MESH_PEAR, 0xFF8AEBDDu},
	{fruit_peach_idx, fruit_peach_l_idx, fruit_peach_r_idx, m3d::MESH_PEACH, 0xFF80B0FFu},
	{fruit_kiwi_idx, fruit_kiwi_l_idx, fruit_kiwi_r_idx, m3d::MESH_KIWI, 0xFF52D69Au},
	{fruit_gold_idx, fruit_gold_l_idx, fruit_gold_r_idx, m3d::MESH_GOLD, 0xFF40D8FFu},
	{fruit_pineapple_idx, fruit_pineapple_l_idx, fruit_pineapple_r_idx, m3d::MESH_PINEAPPLE, 0xFF50E0FFu},
	{fruit_coconut_idx, fruit_coconut_l_idx, fruit_coconut_r_idx, m3d::MESH_COCONUT, 0xFFF0F5FFu},
};

static int fruitFor(const char* label) {
	static const struct {
		const char* label;
		int fruit;
	} kMap[] = {
		{"Play", MF_MELON}, {"Options", MF_ORANGE}, {"Credits", MF_PLUM}, {"How to Play", MF_LEMON}, {"Shop", MF_GOLD},
		{"Journal", MF_KIWI}, {"Stats", MF_PEACH}, {"Practice", MF_APPLE}, {"Let's Play", MF_MELON},
		{"Resume", MF_APPLE}, {"Main Menu", MF_PEAR}, {"Again", MF_PEACH}, {"Modes", MF_KIWI}, {"Keep", MF_LEMON}, {"Try Again", MF_PEACH}, {"Share", MF_PLUM},
		{"Audio", MF_APPLE}, {"Visuals", MF_PEACH}, {"Visuals and Display", MF_PEACH}, {"Gameplay and Controls", MF_PINEAPPLE}, {"Accessibility", MF_PEAR}, {"Scores and Data", MF_PLUM},
	};
	if (!label) return MF_BERRY;
	for (const auto& m : kMap)
		if (strcmp(m.label, label) == 0) return m.fruit;
	return MF_APPLE;
}

// part: 0 whole, 1 left half, 2 right half. size is the fruit's width on screen.
static void drawMenuFruit(int f, float x, float y, float size, float spin, float rot = 0, int part = 0) {
	const MenuFruit& m = kMenuFruit[f];
	float s = size / 48.0f;
	if (gSave.artStyle == ART_3D && m3d::ready()) {
		m3d::begin(320, gfx::stereoPerZ());
		m3d::Pose pose = {x, y, 0, 0.25f, spin, rot, s, 0, 0, 0};
		m3d::draw(m.mesh, (m3d::Part)part, pose);
		m3d::end();
	} else {
		gfx::sprite(SHEET_FRUIT, part == 0 ? m.whole : (part == 1 ? m.left : m.right), x, y, s, s, rot);
	}
}

// The cut a swipe (or the confirm button) made. The choice acts a moment later, once the fruit has split.
static constexpr float kCutDelay = 0.2f, kCutLife = 0.75f;
static struct {
	bool on, pending;
	float t;
	Button btn;  // a copy: menus may hand over a rebuilt list, so cut signs are matched by place, not address
	int index, fruit;
	float x, y, ang;
	Screen screen;
} sCut;
static Screen sDrawing = SCREEN_LOADING;  // the screen whose signs are being drawn, during a slide too
static bool isCut(const Button& b) {
	return sCut.on && sCut.screen == sDrawing && sCut.btn.x == b.x && sCut.btn.y == b.y && sCut.btn.w == b.w && sCut.btn.h == b.h;
}
static Blade sMenuBlade;                  // the trail a swipe leaves across the menus
static bool menuBladeShown() {
	return sScreen == SCREEN_TITLE || sScreen == SCREEN_MODES || sScreen == SCREEN_OPTIONS || sScreen == SCREEN_CREDITS || sScreen == SCREEN_HOWTO || sScreen == SCREEN_JOURNAL || sScreen == SCREEN_STATS || sScreen == SCREEN_BEAT || sScreen == SCREEN_BEATTOOL || sScreen == SCREEN_UPDATE || sScreen == SCREEN_PAUSE || sScreen == SCREEN_RESULT || sScreen == SCREEN_PASS_TURN || sScreen == SCREEN_PASS_BOARD || sScreen == SCREEN_LATENCY;
}

static bool fruit3D() { return gSave.artStyle == ART_3D && m3d::ready(); }

// A menu choice is a fruit standing where its sign's box is. With 3D Models its name circles it like a
// ring; with 2D Stickers the name sits underneath, so the fruit sits a little higher.
static void fruitSpot(const Button& b, float* x, float* y, float* d) {
	*x = b.x + b.w * 0.5f;
	if (fruit3D()) {
		*d = fminf(b.h * 0.62f, 42);
		*y = b.y + b.h * 0.46f;
	} else {
		*d = fminf(b.h - 18, 40);
		*y = b.y + 2 + *d * 0.5f;
	}
}
static float fruitSize(const Button& b) {
	float x, y, d;
	fruitSpot(b, &x, &y, &d);
	return d;
}

// The name on a tilted ring round the fruit, swaying gently to and fro so the whole word always faces
// the front and reads in one go. Each letter narrows as it turns away and fades before the side of the
// ring, so letters never crowd there, and nothing is drawn on the far side, so nothing reads backwards.
// All the outlines go down first and every fill on top, so the letters join up like printed text.
static void drawRingText(const char* label, float cx, float cy, float d, float maxW, float scale, u32 color) {
	int n = (int)strlen(label);
	if (n == 0 || n > 31) return;
	float w = gfx::textWidth(label, scale);
	// Wide enough that the name spans about a third of the ring, so it reads in one go at the front.
	float rx = fmaxf(d * 0.5f + 10, w / 1.7f);
	if (rx > maxW * 0.5f - 4) rx = maxW * 0.5f - 4;
	// A long name shrinks to fit the front of the ring with room to sway.
	if (w > rx * 1.5f) {
		scale *= rx * 1.5f / w;
		w = rx * 1.5f;
	}
	const float ry = rx * 0.2f;
	// Calm Effects stops the sway and holds the name facing front.
	const float spin = gSave.calmFx ? 0 : sinf(sTime * 0.9f) * 0.32f;
	struct Letter {
		char c;
		float x, y, sx, sy;
		u32 col;
	};
	Letter out[64];
	int count = 0;
	{
		// Letters run clockwise seen from above, so across the near side they read left to right.
		float a = 1.5707963f + spin + (w * 0.5f) / rx;
		for (int i = 0; i < n; i++) {
			const float adv = gfx::glyphWidth(label[i], scale);
			const float ang = a - (adv * 0.5f) / rx;
			a -= adv / rx;
			const float s = sinf(ang);  // 1 at the front
			const float fade = clampf((s - 0.1f) / 0.3f, 0, 1);
			if (fade <= 0 || label[i] == ' ' || count >= 64) continue;
			const float depth = 0.92f + 0.08f * s;
			out[count++] = {label[i], cx + cosf(ang) * rx, cy + d * 0.2f + s * ry - 20 * scale * depth, scale * depth * (0.25f + 0.75f * s), scale * depth,
			                withAlpha(color, fade)};
		}
	}
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < count; i++) gfx::glyph(out[i].c, out[i].x, out[i].y, out[i].sx, out[i].sy, out[i].col, pass == 1);
}

static void drawFruitChoice(const Button& b, const char* label, bool focused, bool pressed, float scale, int fruit) {
	float x, y, d;
	fruitSpot(b, &x, &y, &d);
	float dy = pressed ? 2 : 0;
	bool cut = isCut(b);
	if (focused) gfx::sprite(SHEET_SPRITES, sprites_ring_idx, x, y + dy, d / 34, d / 34, 0, withAlpha(COL_ACCENT, 0.9f));
	float bob = focused && !gSave.calmFx ? sinf(sTime * 4) * 1.5f : 0;
	float spin = sTime * 0.8f + b.x * 0.05f;
	if (fruit3D()) {
		float ts = scale * 0.9f;
		if (!cut) drawMenuFruit(fruit, x, y + dy + bob, d, spin);
		// A name too long for one ring wraps onto a second, so it stays the same size as its neighbours.
		const char* split = strlen(label) > 16 ? strstr(label, " and ") : nullptr;
		if (split) {
			char first[32];
			snprintf(first, sizeof first, "%.*s and", (int)(split - label), label);
			drawRingText(first, x, y + dy - 6, d, b.w, ts * 0.8f, focused ? COL_ACCENT : COL_CREAM);
			drawRingText(split + 5, x, y + dy + 9, d, b.w, ts * 0.8f, focused ? COL_ACCENT : COL_CREAM);
		} else {
			drawRingText(label, x, y + dy, d, b.w, ts, focused ? COL_ACCENT : COL_CREAM);
		}
	} else {
		if (!cut) drawMenuFruit(fruit, x, y + dy + bob, d, spin);
		float ts = 0.4f, w = gfx::textWidth(label, ts);
		if (w > b.w - 6) ts *= (b.w - 6) / w;
		gfx::text(label, x, y + d * 0.5f + 1 + dy, ts, focused ? COL_ACCENT : COL_CREAM, ALIGN_CENTER);
	}
}

static void drawSign(const Button& b, const char* label, bool focused, bool pressed, float scale = 0.56f, int icon = -1, int fruit = -1) {
	if (fruit >= 0) {
		drawFruitChoice(b, label, focused, pressed, scale, fruit);
		return;
	}
	drawPlank(b.x, b.y, b.w, b.h, focused, pressed);
	float dy = pressed ? 2 : 0;
	float lead = icon >= 0 ? 30 : 0;
	float w = gfx::textWidth(label, scale);
	float room = b.w - 22 - lead;
	if (w > room) scale *= room / w;
	float cx = b.x + b.w * 0.5f + (icon >= 0 ? 12 : 0);
	// The coin sprite is drawn bigger than the mode icons, so it is scaled down to match them.
	const float iconScale = icon == sprites_coin_idx ? 0.46f : 0.8f;
	if (icon >= 0) gfx::sprite(SHEET_SPRITES, icon, b.x + 21, b.y + b.h * 0.5f + dy, iconScale, iconScale);
	gfx::text(label, cx, b.y + b.h * 0.5f - 20 * scale - 1 + dy, scale, COL_CREAM, ALIGN_CENTER);
}

// A mode on Pick a Game: its icon over its name, so the whole width of the sign goes to the name.
static void drawModeTile(const Button& b, int icon, bool focused, bool pressed) {
	drawPlank(b.x, b.y, b.w, b.h, focused, pressed);
	const float dy = pressed ? 2 : 0, cx = b.x + b.w * 0.5f, iconScale = icon == sprites_coin_idx ? 0.42f : 0.72f;
	float scale = 0.44f;
	const float w = gfx::textWidth(b.label, scale);
	if (w > b.w - 14) scale *= (b.w - 14) / w;
	gfx::sprite(SHEET_SPRITES, icon, cx, b.y + 16 + dy, iconScale, iconScale, icon == sprites_arrow_idx ? 3.14159265f : 0);  // the arrow points on to the next player
	gfx::text(b.label, cx, b.y + 28 + dy, scale, COL_CREAM, ALIGN_CENTER);
}

static void drawPageArrow(int side) {
	const float x = side ? 291 : 3, dy = sArrowPressed == side ? 2 : 0;
	drawPlank(x, kPageArrowY, 26, 50, sArrowHover == side, sArrowPressed == side);
	gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, x + 13, kPageArrowY + 24 + dy, 0.62f, 0.62f, side ? 3.14159265f : 0, COL_CREAM);
}

static void drawBackArrow(bool focused, bool pressed) {
	drawPlank(kBack.x, kBack.y, kBack.w, kBack.h, focused, pressed);
	float dy = pressed ? 2 : 0;
	bool cut = sCut.on && sCut.index == 0 && sCut.screen == sDrawing;
	if (!cut) drawMenuFruit(MF_BERRY, kBack.x + 14, kBack.y + kBack.h * 0.5f + dy, 18, sTime * 0.8f);
	gfx::sprite(SHEET_SPRITES, sprites_arrow_back_idx, kBack.x + 32, kBack.y + kBack.h * 0.5f + dy, 0.55f, 0.55f, 0, COL_CREAM);
}

#ifdef DEV_TOOLS
#define DEV_SUFFIX " DEV BUILD"
#else
#define DEV_SUFFIX ""
#endif

static void drawHeader(const char* title) {
	// Centred on the screen, like the signs below it. A long title shrinks to stay clear of the back arrow.
	float scale = 0.62f, w = gfx::textWidth(title, scale), room = 320 - 2 * (kBack.x + kBack.w + 4);
	if (w > room) scale *= room / w;
	gfx::text(title, 160, 8 + (0.62f - scale) * 20, scale, COL_CREAM, ALIGN_CENTER);
}

// The coin purse: every coin saved so far, the coin in front of the number. Right-aligned ends at x;
// otherwise the coin starts at x.
static void drawPurse(float x, float y, float s, bool right = true) {
	char buf[16];
	snprintf(buf, sizeof buf, "%lu", (unsigned long)gSave.coins);
	const float k = s / 0.44f, w = gfx::textWidth(buf, s);
	if (!right) x += 27 * k + w;
	gfx::sprite(SHEET_SPRITES, sprites_coin_idx, x - w - 15 * k, y + 8 * k, 0.5f * k, 0.5f * k);
	gfx::text(buf, x, y, s, COL_CREAM, ALIGN_RIGHT);
}

// Menus sit on the board chosen in Visuals, a little dimmed so that the signs stand out on it.
#if PORT_TWO_SCREENS
static void drawMenuBack() {
	if (gSave.highContrast) gfx::rect(0, 0, 320, 240, rgba(16, 16, 20));
	else gfx::image(SHEET_BOARD, 0, 0, 0, 320, 240, rgba(205, 205, 205));
}
#else
static void drawMenuBack() {}  // one screen, one board: it's already behind everything
#endif

// Press highlights, release on the same sign activates: a swipe that ends on a sign does nothing.
// A sign clicks the moment a finger or stylus lands on it, since a tap takes a tenth of a second to
// lift, and acts on release, so sliding off still cancels. It plays the click itself.
// The back arrow makes no click of its own: it is cut like a menu fruit (backCut).
static bool isBackButton(const Button& b) { return !b.label && b.x == kBack.x && b.y == kBack.y; }

static int touchButtons(const Button* btns, int n, const Input& in, bool wasTouching, int& focus) {
	static bool clicked;
	int result = -1;
	if (in.touching) {
		if (!wasTouching) {
			sPressed = -1;
			for (int i = 0; i < n; i++)
				if (hit(btns[i], in.tx, in.ty)) sPressed = focus = i;
			clicked = sPressed >= 0;
			if (clicked && !isBackButton(btns[sPressed])) audio::click();
		} else if (sPressed >= 0 && !hit(btns[sPressed], in.tx, in.ty)) {
			sPressed = -1;
		}
	} else if (wasTouching) {
		if (sPressed >= 0) result = sPressed;
		if (result >= 0 && !clicked && !isBackButton(btns[result])) audio::click();  // pressed while the screen was still locked
		sPressed = -1;
		clicked = false;
	}
	return result;
}

// ---------------------------------------------------------------- scrolling lists
// Every list moves the same way: the mouse wheel, or a drag with a finger, cursor or stylus.
// A press only becomes a drag once it has travelled a little, so a plain tap still picks what it landed on.
static constexpr int kDragStart = 6;
struct ListDrag {
	bool held, moved;
	int startY;
	float startPos;
};

// Once a frame. True while a drag is moving the list, and on the frame it lets go.
static bool listDrag(ListDrag& d, const Input& in, bool wasTouching, float top, float bottom, float* pos) {
	if (in.touching && !wasTouching) {
		d.held = in.ty >= top && in.ty < bottom;
		d.moved = false;
		d.startY = in.ty;
		d.startPos = *pos;
	}
	if (!d.held) return false;
	if (!in.touching) {
		d.held = false;
		return d.moved;
	}
	const int dy = in.ty - d.startY;
	if (dy > kDragStart || dy < -kDragStart) d.moved = true;
	if (d.moved) *pos = d.startPos - dy;
	return d.moved;
}

// A list that always comes to rest on a whole row, so what is on show is never cut in half.
struct RowScroller {
	float pos, aim;
	ListDrag drag;
};

static bool rowScroll(RowScroller& s, const Input& in, bool wasTouching, float top, float bottom, float step, int hiddenRows) {
	const float most = hiddenRows > 0 ? hiddenRows * step : 0;
	s.aim -= in.wheel * step;
	const bool dragging = listDrag(s.drag, in, wasTouching, top, bottom, &s.pos);
	s.pos = clampf(s.pos, 0, most);
	if (dragging) {
		s.aim = s.drag.held ? s.pos : floorf(s.pos / step + 0.5f) * step;
	} else {
		s.aim = clampf(s.aim, 0, most);
		s.pos += (s.aim - s.pos) * 0.3f;
		if (fabsf(s.aim - s.pos) < 0.5f) s.pos = s.aim;
	}
	return dragging;
}

// Brings a row the keys moved to into view.
static void rowReveal(RowScroller& s, int row, int shownRows, float step) {
	if (row * step < s.aim) s.aim = row * step;
	if ((row - shownRows + 1) * step > s.aim) s.aim = (row - shownRows + 1) * step;
}

static void rowReset(RowScroller& s) {
	s.pos = s.aim = 0;
	s.drag = {};
}

// A quiet mark at the right edge, only when the list is longer than its window.
static void drawScrollMark(float top, float room, float pos, float most) {
	if (most <= 0) return;
	const float size = room * room / (room + most);
	gfx::rect(315, top + (room - size) * clampf(pos / most, 0, 1), 3, size, COL_DIM);
}

static bool sameRow(const Button& a, const Button& b) {
	float top = a.y > b.y ? a.y : b.y;
	float bottom = (a.y + a.h < b.y + b.h) ? a.y + a.h : b.y + b.h;
	return bottom - top > (a.h < b.h ? a.h : b.h) * 0.25f;
}

// Lower ranks are tried first: the nearest sign ahead, then past the edge to the far end behind.
static float wrapRank(float along) {
	return along > 0 ? along : 10000 + along;
}

// Left and right stay within the row and wrap around it; a sign alone in its row stays put.
// Up and down go to the next row, or wrap to the far row, and land on the closest sign in it.
static void dpadNav(const Button* btns, int n, u32 keys, int& focus) {
	int dx = (keys & KEY_RIGHT) ? 1 : ((keys & KEY_LEFT) ? -1 : 0);
	int dy = (keys & KEY_DOWN) ? 1 : ((keys & KEY_UP) ? -1 : 0);
	if (dx == 0 && dy == 0) return;
	const Button& c = btns[focus];
	float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f;
	int best = -1;
	float bestRank = 1e9f;
	for (int i = 0; i < n; i++) {
		if (i == focus || sameRow(btns[i], c) != (dy == 0)) continue;
		float along = dy != 0 ? (btns[i].y + btns[i].h * 0.5f - cy) * dy : (btns[i].x + btns[i].w * 0.5f - cx) * dx;
		float rank = wrapRank(along);
		if (rank < bestRank) {
			bestRank = rank;
			best = i;
		}
	}
	if (best >= 0 && dy != 0) {
		const Button row = btns[best];
		float bestAcross = 1e9f;
		for (int i = 0; i < n; i++) {
			if (i == focus || !sameRow(btns[i], row) || sameRow(btns[i], c)) continue;
			float across = fabsf(btns[i].x + btns[i].w * 0.5f - cx);
			if (across < bestAcross) {
				bestAcross = across;
				best = i;
			}
		}
	}
	if (best >= 0) {
		focus = best;
		audio::click(0.5f);
	}
}

static void startCut(const Button* btns, int i, Screen screen, float ang) {
	const Button& b = btns[i];
	int fruit = i == 0 && !b.label ? MF_BERRY : (b.label ? fruitFor(b.label) : MF_BERRY);
	bool hasFruit = i == 0 || screen != SCREEN_MODES;  // mode signs show their icon, not a fruit
	sCut = {};
	sCut.on = sCut.pending = true;
	sCut.btn = b;
	sCut.index = i;
	sCut.fruit = hasFruit ? fruit : -1;
	sCut.screen = screen;
	sCut.ang = ang;
	if (i == 0 && !b.label) {
		sCut.x = kBack.x + 15;
		sCut.y = kBack.y + kBack.h * 0.5f;
	} else if (hasFruit) {
		float d;
		fruitSpot(b, &sCut.x, &sCut.y, &d);
	} else {
		sCut.x = b.x + b.w * 0.5f;
		sCut.y = b.y + b.h * 0.5f;
	}
	float v = gSave.menuSfx / 100.0f;
	audio::play(SFX_SWISH1, 0.7f * v);
	audio::play(SFX_JUICY0, 0.8f * v, 0, 1.1f);
}

// A swipe across a sign picks it. A plain tap only counts with a mouse, with Slice Assist, on the back
// sign, or with the 3DS stylus, since on a phone it may be a stray touch. Taps keep working on settings rows.
static int sliceButtons(const Button* btns, int n, Screen screen, const Input& in, bool wasTouching, int& focus) {
	static float lastX, lastY, length;
	static int downOn = -1;
	static bool done, mouse;
	if (in.hovering) mouse = true;
	const float minSwipe = gSave.sliceAssist ? 12.0f : 30.0f;
	if (in.touching) {
		if (!wasTouching) {
			lastX = in.tx;
			lastY = in.ty;
			length = 0;
			done = false;
			downOn = sPressed = -1;
			for (int i = 0; i < n; i++)
				if (hit(btns[i], in.tx, in.ty)) downOn = sPressed = focus = i;
			return -1;
		}
		if (sPressed >= 0 && !hit(btns[sPressed], in.tx, in.ty)) sPressed = -1;
		float dx = in.tx - lastX, dy = in.ty - lastY, d = sqrtf(dx * dx + dy * dy);
		if (done || d < 0.5f) return -1;
		// Walked in small steps, so a fast swipe can't jump over a sign between two frames.
		int steps = (int)(d / 4) + 1;
		for (int s = 1; s <= steps; s++) {
			float px = lastX + dx * s / steps, py = lastY + dy * s / steps;
			length += d / steps;
			if (length < minSwipe) continue;
			for (int i = 0; i < n; i++) {
				if (!hit(btns[i], (int)px, (int)py)) continue;
				done = true;
				sPressed = -1;
				focus = i;
				startCut(btns, i, screen, atan2f(dy, dx));
				lastX = in.tx;
				lastY = in.ty;
				return -1;
			}
		}
		lastX = in.tx;
		lastY = in.ty;
	} else if (wasTouching) {
		// A stylus touch is always on purpose, so on the 3DS a tap picks as well.
		bool tapOk = mouse || gSave.sliceAssist || downOn == 0 || PORT_3DS;
		if (!done && downOn >= 0 && length < minSwipe && tapOk && hit(btns[downOn], (int)lastX, (int)lastY))
			startCut(btns, downOn, screen, -0.6f);
		sPressed = downOn = -1;
		done = false;
	}
	return -1;
}

// Buttons from index `slices` on are plain icons: picked by a click that starts and ends on them, or by the
// confirm key, and returned at once with no fruit cut.
static int menuInput(const Button* btns, int n, Screen screen, const Input& in, bool wasTouching, int slices = -1) {
	int& focus = sFocus[screen];
	if (slices < 0) slices = n;
	if (slices < n && !sCut.pending) {
		static int iconDown = -1;
		static int lastX, lastY;
		if (in.touching) {
			if (!wasTouching) {
				iconDown = -1;
				for (int i = slices; i < n; i++)
					if (hit(btns[i], in.tx, in.ty)) iconDown = focus = i;
			}
			lastX = in.tx;
			lastY = in.ty;
		} else if (wasTouching && iconDown >= 0) {
			const int i = iconDown;
			iconDown = -1;
			if (hit(btns[i], lastX, lastY)) return i;
		}
		if ((in.down & KEY_A) && focus >= slices && focus < n) return focus;
	}
	if (sCut.pending) {
		// The choice acts once the fruit has split. If the screen changed some other way, it's dropped.
		if (sCut.screen != screen) sCut.pending = false;
		else if (sCut.t >= kCutDelay) {
			sCut.pending = false;
			return sCut.index;
		}
		return -1;
	}
	dpadNav(btns, n, in.repeat, focus);
#if PORT_MOUSE
	if (in.hovering)
		for (int i = 0; i < n; i++)
			if (hit(btns[i], in.tx, in.ty)) focus = i;
#endif
	sliceButtons(btns, slices, screen, in, wasTouching, focus);
	if (!sCut.pending && (in.down & KEY_A) && focus >= 0 && focus < n) startCut(btns, focus, screen, -0.6f);
	return -1;
}

// The back arrow on screens that run their own controls (the Shop, the Journal, an Options section). Like
// every other menu, a swipe across it or a click, tap or confirm cuts its berry, and the screen goes back
// once the berry has split. Call backCut first each frame: while it returns true the screen does nothing
// else, and *leave turns true on the frame to go back. cutBack is for the screen's own picks of the arrow.
static void cutBack(Screen screen) {
	if (sCut.pending) return;
	startCut(kOnlyBack, 0, screen, -0.6f);
	sPressed = -1;
}
static bool backCut(Screen screen, const Input& in, bool wasTouching, bool* leave) {
	static float lastX, lastY, length;
	*leave = false;
	if (sCut.pending && sCut.screen == screen) {
		if (sCut.t >= kCutDelay) {
			sCut.pending = false;
			*leave = true;
		}
		return true;
	}
	if (!in.touching) return false;
	if (!wasTouching) length = 0;
	const float dx = in.tx - lastX, dy = in.ty - lastY, d = sqrtf(dx * dx + dy * dy);
	const int steps = wasTouching ? (int)(d / 4) + 1 : 0;
	for (int s = 1; s <= steps; s++) {
		length += d / steps;
		if (length < (gSave.sliceAssist ? 12.0f : 30.0f) || !hit(kBack, (int)(lastX + dx * s / steps), (int)(lastY + dy * s / steps))) continue;
		startCut(kOnlyBack, 0, screen, atan2f(dy, dx));
		sPressed = -1;
		return true;
	}
	lastX = in.tx;
	lastY = in.ty;
	return false;
}

// The halves and juice of a cut sign, drawn over the screen it was on.
static void drawCut(Screen screen) {
	if (!sCut.on || sCut.screen != screen) return;
	float t = sCut.t, a = 1 - clampf(t / kCutLife, 0, 1);
	float r0 = sCut.ang - 1.5707963f, ux = cosf(r0), uy = sinf(r0);
	u32 juice = sCut.fruit >= 0 ? kMenuFruit[sCut.fruit].juice : COL_ACCENT;
	// The stroke itself, for the first moment: a thin bright wedge along the cut.
	if (t < 0.16f) {
		float k = 1 - t / 0.16f, ca = cosf(sCut.ang), sa = sinf(sCut.ang), len = 46, w = 2.5f * k;
		u32 c = withAlpha(COL_CREAM, k);
		gfx::tri(sCut.x - ca * len, sCut.y - sa * len, withAlpha(COL_CREAM, 0), sCut.x + ux * w, sCut.y + uy * w, c, sCut.x - ux * w, sCut.y - uy * w, c);
		gfx::tri(sCut.x + ca * len, sCut.y + sa * len, withAlpha(COL_CREAM, 0), sCut.x + ux * w, sCut.y + uy * w, c, sCut.x - ux * w, sCut.y - uy * w, c);
	}
	int drops = gSave.calmFx ? 5 : 12;
	for (int k = 0; k < drops; k++) {
		float th = k * 2.39996f + sCut.ang, v = 50 + (k * 37 % 90);
		float px = sCut.x + cosf(th) * v * t, py = sCut.y + sinf(th) * v * t + 260 * t * t;
		float s = 2 + (k % 3);
		gfx::rect(px - s * 0.5f, py - s * 0.5f, s, s, withAlpha(juice, a));
	}
	if (sCut.fruit < 0) return;
	float size = sCut.index == 0 ? 18 : fruitSize(sCut.btn);
	float off = 4 + 70 * t, fall = 300 * t * t, spin = gSave.calmFx ? 0 : t * 3;
	drawMenuFruit(sCut.fruit, sCut.x - ux * off, sCut.y - uy * off + fall, size, 0, r0 - spin, 1);
	drawMenuFruit(sCut.fruit, sCut.x + ux * off, sCut.y + uy * off + fall, size, 0, r0 + spin, 2);
}

// ---------------------------------------------------------------- settings rows
enum RowKind { ROW_SLIDER, ROW_CHOICE, ROW_ACTION, ROW_PAGE };  // a page row opens the section numbered by its lo
enum SectionId { SID_AUDIO, SID_VISUALS, SID_VISUALS_MORE, SID_GAMEPLAY, SID_GAMEPLAY_MORE, SID_ACCESS, SID_ACCESS_MORE, SID_DATA, SID_DEV, SID_MIXER, SID_PASS };
enum RowAction { ACT_NONE, ACT_RESET_AUDIO, ACT_RESET_VISUALS, ACT_RESET_GAMEPLAY, ACT_RESET_SCORES, ACT_RESET_ALL, ACT_SPEED_TEST, ACT_APPLY_DISPLAY, ACT_RESET_DISPLAY, ACT_RESET_ACCESS, ACT_RESET_CONTROLS, ACT_DEV_ADD_COINS, ACT_DEV_CLEAR_COINS, ACT_DEV_NEXT_DAY, ACT_CHECK_UPDATES, ACT_PASS_NAME, ACT_PASS_START, ACT_LATENCY };

struct Row {
	RowKind kind;
	const char* label;
	u8* value;
	u8 lo, hi;                  // slider range, or for a choice: 0 and the last index
	const char* const* names;   // choice labels
	float showLo, showHi;       // what a slider's ends mean, as a percentage (or a plain count)
	RowAction action;
	const char* line1;
	const char* line2;
};

static const char* const kBladeNames[kBladeCount] = {"Ember", "Azure", "Rose", "Mint", "Gold", "Rainbow"};
static const char* const kTrailNames[kTrailCount] = {
	"None",
#define SHOP_TRAIL(name, ...) name,
#include "shop_items.inc"
};
static const char* const kBoardNames[kBoardCount] = {
	"Picnic Blanket", "Cutting Board", "Plate", "Marble", "Beach", "Starlight",
#define SHOP_THEME(name, ...) name,
#include "shop_items.inc"
};
#if PORT_TWO_SCREENS
static const char* const kSceneNames[kSceneCount] = {"Dusk Orchard", "Morning Meadow", "Moonlit Bay"};
#endif
static const char* const kArtNames[] = {"2D Stickers", "3D Models"};
static const char* const kEffectNames[] = {"None", "Low", "Medium", "Full"};
static const char* const kOffOn[] = {"Off", "On"};
static const char* const kOnOff[] = {"On", "Off"};  // for a setting stored as "turned off"
static const char* const kUpdateNames[] = {"Ask", "On", "Off"};  // by gSave.updConsent
#if PORT_TWO_SCREENS
static const char* const kBgNames[] = {"Simple", "Full"};
#endif
static const char* const kDetailNames[] = {"Low", "High"};
static const char* const kSliceNames[] = {"Quick", "Exact"};
static const char* const kDifficultyNames[DIFF_COUNT] = {"Easy", "Normal", "Hard", "Insane"};

static const char* const kTrackRowNames[kTrackCount + 1] = {
	"Auto", "Orchard Morning", "Picnic Stroll", "Sleepy Meadow", "Lantern Walk", "Still Water", "Moss Garden", "Bamboo Breeze", "Drifting Clouds",
#define SHOP_TRACK(name, ...) name,
#include "shop_items.inc"
};

// Pass and Play: 2 to 4 people take one run each on the same device, and the highest score wins.
static constexpr int kPassMax = 4, kPassNameLen = 10;
static const u8 kPassModes[] = {MODE_CLASSIC, MODE_ARCADE, MODE_FRENZY, MODE_SUDDEN, MODE_MINEFIELD, MODE_COMBO};
static const char* const kPassModeNames[] = {"Classic", "Arcade", "Frenzy", "Sudden Death", "Minefield", "Combo Rush"};
static const char* const kPassCounts[] = {"2", "3", "4"};
static u8 sPassPlayers = 0, sPassModeRow = 0;  // row values: players is 0 for two people
static char sPassNames[kPassMax][kPassNameLen + 1];
static u32 sPassScores[kPassMax];
static int sPassTurn = -1;    // whose run it is, or -1 when nobody is playing Pass and Play
static int sPassTyping = -1;  // the player whose name is being typed
static int sNameFocus = 0;
static Mode sStartMode = MODE_CLASSIC;  // the mode the next game starts in
static const Button kPassTurnButtons[] = {kBack, {90, 146, 140, 52, "Ready"}};
static Button kPassResultButtons[] = {kBack, {90, 170, 140, 52, "Next Player"}};
static void passBegin();

static int passCount() { return sPassPlayers + 2; }
static u32 passColor(int player) {
	static const u8 k[kPassMax][3] = {{255, 128, 116}, {132, 196, 255}, {136, 228, 136}, {255, 216, 104}};
	return rgba(k[player][0], k[player][1], k[player][2]);
}
static void passDefaultName(int player) { snprintf(sPassNames[player], sizeof sPassNames[player], "Player %d", player + 1); }
// A typed name keeps only what the font can draw. With nothing left, the player goes back to their number.
static void passSetName(int player, const char* typed) {
	char clean[kPassNameLen + 1];
	int n = 0;
	for (const char* c = typed; *c && n < kPassNameLen; c++) {
		const bool letter = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9');
		if (letter || (*c == ' ' && n > 0)) clean[n++] = *c;
	}
	while (n > 0 && clean[n - 1] == ' ') n--;
	clean[n] = 0;
	if (n) snprintf(sPassNames[player], sizeof sPassNames[player], "%s", clean);
	else passDefaultName(player);
}
// The winner, or -1 on a tie at the top.
static int passWinner() {
	int best = 0;
	bool tied = false;
	for (int i = 1; i < passCount(); i++) {
		if (sPassScores[i] > sPassScores[best]) {
			best = i;
			tied = false;
		} else if (sPassScores[i] == sPassScores[best]) {
			tied = true;
		}
	}
	return tied ? -1 : best;
}

static const Row kPassRows[] = {
	{ROW_CHOICE, "Players", &sPassPlayers, 0, 2, kPassCounts, 0, 0, ACT_NONE, "Everyone takes one turn", "on this device."},
	{ROW_CHOICE, "Game", &sPassModeRow, 0, 5, kPassModeNames, 0, 0, ACT_NONE, "Everyone plays this mode.", "Scores here don't change your bests."},
	{ROW_ACTION, sPassNames[0], nullptr, 0, 0, nullptr, 0, 0, ACT_PASS_NAME, "Choose it to type a name.", ""},
	{ROW_ACTION, sPassNames[1], nullptr, 0, 0, nullptr, 0, 0, ACT_PASS_NAME, "Choose it to type a name.", ""},
	{ROW_ACTION, sPassNames[2], nullptr, 0, 0, nullptr, 0, 0, ACT_PASS_NAME, "Choose it to type a name.", ""},
	{ROW_ACTION, sPassNames[3], nullptr, 0, 0, nullptr, 0, 0, ACT_PASS_NAME, "Choose it to type a name.", ""},
	{ROW_ACTION, "Start", nullptr, 0, 0, nullptr, 0, 0, ACT_PASS_START, "Player 1 goes first.", ""},
};
static int passRowPlayer(const Row& r) {
	for (int i = 0; i < kPassMax; i++)
		if (r.label == sPassNames[i]) return i;
	return -1;
}

static const Row kMixerRows[] = {
	{ROW_CHOICE, "Low Gravity", &gSave.mixer[MIX_LOW_GRAVITY], 0, 1, kOffOn, 0, 0, ACT_NONE, "Fruit floats and hangs in the air.", "Not used in Daily Fruit or Beat Slice."},
	{ROW_CHOICE, "Giant Fruit", &gSave.mixer[MIX_GIANT], 0, 1, kOffOn, 0, 0, ACT_NONE, "Every fruit is half as big again.", "Turns Tiny Fruit off."},
	{ROW_CHOICE, "Tiny Fruit", &gSave.mixer[MIX_TINY], 0, 1, kOffOn, 0, 0, ACT_NONE, "Small fruit, harder to hit.", "Turns Giant Fruit off."},
	{ROW_CHOICE, "Mirror", &gSave.mixer[MIX_MIRROR], 0, 1, kOffOn, 0, 0, ACT_NONE, "Your blade moves left when", "you move right."},
	{ROW_CHOICE, "Double Bombs", &gSave.mixer[MIX_DOUBLE_BOMBS], 0, 1, kOffOn, 0, 0, ACT_NONE, "Twice the bombs in every wave.", "Zen stays bomb-free."},
	{ROW_CHOICE, "Sides Only", &gSave.mixer[MIX_SIDES], 0, 1, kOffOn, 0, 0, ACT_NONE, "Fruit only comes in", "from the left and right."},
};
static const Row kAudioRows[] = {
	{ROW_SLIDER, "Sound Effects", &gSave.sfx, 0, 100, nullptr, 0, 100, ACT_NONE, "Slices, combos and bombs.", ""},
	{ROW_SLIDER, "Music", &gSave.music, 0, 100, nullptr, 0, 100, ACT_NONE, "Menu and game music.", ""},
	{ROW_SLIDER, "Menu Sounds", &gSave.menuSfx, 0, 100, nullptr, 0, 100, ACT_NONE, "The tap when you press a sign.", ""},
	{ROW_CHOICE, "Menu Music", &gSave.menuTrack, 0, kTrackCount, kTrackRowNames, 0, 0, ACT_NONE, "What plays in the menus. Auto", "shuffles every track you own."},
	{ROW_CHOICE, "Game Music", &gSave.gameTrack, 0, kTrackCount, kTrackRowNames, 0, 0, ACT_NONE, "What plays during a game. Auto", "shuffles them; Zen keeps it calm."},
	{ROW_ACTION, "Latency Test", nullptr, 0, 0, nullptr, 0, 0, ACT_LATENCY, "Slice in time with a metronome", "to set Beat Timing for you."},
	{ROW_SLIDER, "Beat Timing", &gSave.beatLatency, 0, 200, nullptr, 0, 0, ACT_NONE, "Moves the Beat Slice beat. Raise it", "if your slices land late."},
	{ROW_ACTION, "Reset Audio", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_AUDIO, "Puts these back to their defaults.", ""},
};
#if PORT_HAS_DISPLAY
static const char* const kWindowModeNames[] = {"Windowed", "Borderless", "Fullscreen"};
static const char* const kPresetNames[] = {"Performance", "Quality", "Beautiful", "Custom"};
static const char* const kQualityNames[] = {"Low", "Medium", "High"};
// Textures alone goes on to Ultra, the 4K art (Windows and Linux).
static const char* const kTextureNames[] = {"Low", "Medium", "High", "Ultra"};
static const char* const kShadowNames[] = {"Off", "Simple", "Soft"};
static const char* const kBloomNames[] = {"Off", "Low", "High"};
static const char* const kScaleNames[] = {"75%", "100%", "125%"};
#endif

// Each section shows the rows people change most, and ends in an Advanced page for the fine tuning and resets.
static const Row kVisualRows[] = {
	{ROW_CHOICE, "Art Style", &gSave.artStyle, 0, 1, kArtNames, 0, 0, ACT_NONE, "3D models or flat stickers.", PORT_ART_HINT},
	{ROW_CHOICE, "Board", &gSave.board, 0, kBoardCount - 1, kBoardNames, 0, 0, ACT_NONE, "What you slice over.", ""},
#if PORT_TWO_SCREENS
	{ROW_CHOICE, "Scenery", &gSave.scene, 0, kSceneCount - 1, kSceneNames, 0, 0, ACT_NONE, "The view on the top screen.", ""},
#endif
	{ROW_CHOICE, "Blade", &gSave.blade, 0, kBladeCount - 1, kBladeNames, 0, 0, ACT_NONE, "The colour of the cutting edge,", "at the front of every slice."},
	{ROW_CHOICE, "Trail", &gSave.trail, 0, kTrailCount - 1, kTrailNames, 0, 0, ACT_NONE, "What streaks out behind the blade.", "More trails are in the Shop."},
#if PORT_3DS
	{ROW_SLIDER, "3D Depth", &gSave.depth, 0, 100, nullptr, 0, 100, ACT_NONE, "How far things stand out,", "with the console's 3D slider."},
#endif
#if PORT_HAS_DISPLAY && !PORT_ANDROID
	{ROW_CHOICE, "Display Mode", &gSave.windowMode, 0, 2, kWindowModeNames, 0, 0, ACT_NONE, "Borderless fills the screen.", "Fullscreen takes it over."},
	{ROW_ACTION, "Apply", nullptr, 0, 0, nullptr, 0, 0, ACT_APPLY_DISPLAY, "Tries the mode and resolution.", "Undone in 10 seconds unless kept."},
#endif
#if PORT_HAS_DISPLAY
	{ROW_CHOICE, "Graphics", &gSave.preset, 0, 2, kPresetNames, 0, 0, ACT_NONE, "Sets the Advanced quality rows.", "Custom: your own mix of them."},
	{ROW_PAGE, "Advanced", nullptr, SID_VISUALS_MORE, 0, nullptr, 0, 0, ACT_NONE, "Resolution, renderer, textures,", "shadows, bloom and more."},
#else
	{ROW_PAGE, "Advanced", nullptr, SID_VISUALS_MORE, 0, nullptr, 0, 0, ACT_NONE, "Effects, background, detail", "and slice style."},
#endif
};
// Not const: the Resolution row's last choice depends on the screen and the Display Mode.
static Row kVisualMoreRows[] = {
#if PORT_HAS_DISPLAY && !PORT_ANDROID
	{ROW_CHOICE, "Resolution", &port::gResChoice, 0, 0, port::gResNames, 0, 0, ACT_NONE, "Window size, or the screen mode", "in Fullscreen."},
	{ROW_ACTION, "Apply", nullptr, 0, 0, nullptr, 0, 0, ACT_APPLY_DISPLAY, "Tries the mode and resolution.", "Undone in 10 seconds unless kept."},
	{ROW_CHOICE, "V-Sync", &gSave.vsync, 0, 1, kOffOn, 0, 0, ACT_NONE, "Waits for the screen: no tearing.", "Works on 60 and 120 Hz screens."},
#endif
#if PORT_HAS_DISPLAY
	{ROW_CHOICE, "Renderer", &gSave.renderer, 0, (u8)(kRendererCount - 1), kRendererNames, 0, 0, ACT_NONE, "How the game draws. Takes effect", "the next time the game starts."},
	{ROW_CHOICE, "Textures", &gSave.texQuality, 0, PORT_ULTRA ? 3 : 2, kTextureNames, 0, 0, ACT_NONE, PORT_ULTRA ? "Sharpness of the art. Ultra is" : "Sharpness of the art.", PORT_ULTRA ? "for 4K screens and uses more memory." : "Lower uses less memory."},
	{ROW_CHOICE, "Shadows", &gSave.shadowQuality, 0, 2, kShadowNames, 0, 0, ACT_NONE, "Shadows under 3D fruit.", ""},
	{ROW_CHOICE, "Ambient Occlusion", &gSave.ambientOcc, 0, 1, kOffOn, 0, 0, ACT_NONE, "Shaded undersides on 3D fruit.", ""},
	{ROW_CHOICE, "Bloom", &gSave.bloom, 0, 2, kBloomNames, 0, 0, ACT_NONE, "Glow around golden fruit", "and the last big fruit."},
	{ROW_CHOICE, "Render Scale", &gSave.renderScale, 0, 2, kScaleNames, 0, 0, ACT_NONE, "Pixels drawn, against the screen's.", "125% smooths edges; 75% is faster."},
#endif
	{ROW_CHOICE, "Effects", &gSave.effects, 0, 3, kEffectNames, 0, 0, ACT_NONE, "Juice, sparks and rings.", "Lower saves the most speed."},
#if PORT_TWO_SCREENS
	{ROW_CHOICE, "Background", &gSave.bgDetail, 0, 1, kBgNames, 0, 0, ACT_NONE, "Simple draws fewer scenery layers", "and helps an Old 3DS."},
#endif
	{ROW_CHOICE, "3D Detail", &gSave.detail, 0, 1, kDetailNames, 0, 0, ACT_NONE, "Polygons per fruit, 3D style only.", PORT_DETAIL_HINT},
	{ROW_CHOICE, "Slice Style", &gSave.sliceStyle, 0, 1, kSliceNames, 0, 0, ACT_NONE, "Exact cuts along your swipe.", "Quick uses ready-made halves."},
	{ROW_CHOICE, "Transitions", &gSave.transitions, 0, 1, kOffOn, 0, 0, ACT_NONE, "Slides between menus.", ""},
	{ROW_CHOICE, "Seasonal Boards", &gSave.seasonalOff, 0, 1, kOnOff, 0, 0, ACT_NONE, "Pumpkins, snow and spring flowers", "on free boards, by the date."},
	{ROW_ACTION, "Reset Visuals", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_VISUALS, PORT_RESET_HINT, ""},
#if PORT_HAS_DISPLAY
	{ROW_ACTION, "Reset Display", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_DISPLAY, "Back to the default display.", ""},
#endif
};
static const char* const kStickStyleNames[] = {"Hold", "Flick"};
static const Row kGameplayRows[] = {
#if PORT_HAS_SENSITIVITY
	{ROW_SLIDER, PORT_SENSITIVITY_LABEL, &gSave.stylus, 0, 100, nullptr, 0, 100, ACT_NONE, PORT_SENSITIVITY_HINT, "Lower smooths out a shaky line."},
#endif
	{ROW_CHOICE, "Difficulty", &gSave.difficulty, 0, DIFF_COUNT - 1, kDifficultyNames, 0, 0, ACT_NONE, "Changes fruit, bombs and how", "fast the Frenzy meter moves."},
	{ROW_SLIDER, "Misses Allowed", &gSave.misses, 1, 5, nullptr, 1, 5, ACT_NONE, "Misses before Classic ends.", ""},
	{ROW_CHOICE, "Stick Slicing", &gSave.stickStyle, 0, 1, kStickStyleNames, 0, 0, ACT_NONE, PORT_STICK_HINT, "Flick: a fast move slices."},
	{ROW_SLIDER, "Point Speed", &gSave.stickSpeed, 0, 100, nullptr, 50, 150, ACT_NONE, "How fast the stick moves", "the blade point."},
#if !PORT_3DS
	{ROW_CHOICE, "Vibration", &gSave.vibration, 0, 1, kOffOn, 0, 0, ACT_NONE, PORT_VIBRATION_HINT, "on slices, misses and bombs."},
#endif
	{ROW_PAGE, "Advanced", nullptr, SID_GAMEPLAY_MORE, 0, nullptr, 0, 0, ACT_NONE, "Fruit speed, bombs, forgiveness,", "snap assist and resets."},
};
static const Row kGameplayMoreRows[] = {
	{ROW_SLIDER, "Fruit Speed", &gSave.fruitSpeed, 0, 100, nullptr, 70, 130, ACT_NONE, "How fast everything flies.", "100% is normal."},
	{ROW_SLIDER, "Bomb Amount", &gSave.bombs, 0, 100, nullptr, 0, 200, ACT_NONE, "More or fewer bombs than usual.", "100% is normal."},
	{ROW_SLIDER, "Slice Forgiveness", &gSave.forgiveness, 0, 100, nullptr, 70, 130, ACT_NONE, "How close a swipe must pass.", "100% is normal."},
	{ROW_SLIDER, "Frenzy Drain", &gSave.frenzyDrain, 0, 100, nullptr, 40, 160, ACT_NONE, "How fast the meter drops.", "100% is normal."},
	{ROW_CHOICE, "Snap Assist", &gSave.snapAssist, 0, 1, kOffOn, 0, 0, ACT_NONE, "Bends the point gently toward", "a fruit just ahead of it."},
	{ROW_ACTION, "Reset Gameplay", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_GAMEPLAY, "Puts these back to their defaults.", "Your scores stay."},
	{ROW_ACTION, "Reset Controls", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_CONTROLS, "Puts these back to their defaults.", ""},
};
static const Row kDataRows[] = {
#if PORT_3DS
	{ROW_ACTION, "Speed Test", nullptr, 0, 0, nullptr, 0, 0, ACT_SPEED_TEST, "A 90 second test of this console.", "Results go in 3ds/FruitSlicer/bench.txt"},
#endif
	{ROW_CHOICE, "Check for Updates", &gSave.updConsent, 1, 2, kUpdateNames, 0, 0, ACT_NONE, "Looks on GitHub once a day", "for a new version."},
	{ROW_ACTION, "Check Now", nullptr, 0, 0, nullptr, 0, 0, ACT_CHECK_UPDATES, "Looks on GitHub right away.", ""},
	{ROW_ACTION, "Reset Scores", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_SCORES, "Erases best scores and totals.", "Settings stay."},
	{ROW_ACTION, "Reset Everything", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_ALL, "Erases scores and all settings.", ""},
};

#ifdef DEV_TOOLS
// Developer builds only (DEV_TOOLS): coins on demand for testing the Shop. Release builds leave
// this whole section out, so it can't be reached there at all.
static u8 sDevAmount = 2;
static const char* const kDevAmountNames[] = {"100", "1,000", "10,000", "100,000", "1,000,000"};
static const u32 kDevAmounts[] = {100, 1000, 10000, 100000, 1000000};
static const Row kDevRows[] = {
	{ROW_CHOICE, "Amount", &sDevAmount, 0, 4, kDevAmountNames, 0, 0, ACT_NONE, "How many coins Add Coins gives.", "Developer builds only."},
	{ROW_ACTION, "Add Coins", nullptr, 0, 0, nullptr, 0, 0, ACT_DEV_ADD_COINS, "Adds the amount to your coins.", "Developer builds only."},
	{ROW_ACTION, "Clear Coins", nullptr, 0, 0, nullptr, 0, 0, ACT_DEV_CLEAR_COINS, "Sets your coins back to 0.", "Developer builds only."},
	{ROW_ACTION, "Next Day", nullptr, 0, 0, nullptr, 0, 0, ACT_DEV_NEXT_DAY, "Daily Fruit thinks it is a day later,", "until the game closes. Developer builds only."},
};
#endif

static const char* const kUiScaleNames[] = {"100%", "125%", "150%"};
static const char* const kColorBlindNames[] = {"Off", "Red-Blind", "Green-Blind", "Blue-Blind"};
static const char* const kGameSpeedNames[] = {"100%", "85%", "70%"};
static const Row kAccessRows[] = {
	{ROW_CHOICE, "UI Scale", &gSave.uiScale, 0, PORT_3DS ? 1 : 2, kUiScaleNames, 0, 0, ACT_NONE, "Makes the score bar and the", "words during a game bigger."},
	{ROW_CHOICE, "Larger Text", &gSave.largeText, 0, 1, kOffOn, 0, 0, ACT_NONE, "All text one step bigger.", ""},
	{ROW_CHOICE, "High Contrast", &gSave.highContrast, 0, 1, kOffOn, 0, 0, ACT_NONE, "Dark board, solid signs,", "outlined text and fruit."},
	{ROW_CHOICE, "Colour-blind Mode", &gSave.colorBlind, 0, 3, kColorBlindNames, 0, 0, ACT_NONE, "Bombs get a spiky dashed ring,", "and warning colours change."},
	{ROW_CHOICE, "Slice Assist", &gSave.sliceAssist, 0, 1, kOffOn, 0, 0, ACT_NONE, "Shorter swipes count and fruit is", "easier to hit. Bests kept apart."},
	{ROW_CHOICE, "Sound Captions", &gSave.captions, 0, 1, kOffOn, 0, 0, ACT_NONE, "Words for key sounds:", "bombs, misses, Frenzy and more."},
#if PORT_MOUSE
	{ROW_CHOICE, "Slice Without Holding", &gSave.sliceNoHold, 0, 1, kOffOn, 0, 0, ACT_NONE, "Moving the mouse slices,", "with no button held."},
#endif
	{ROW_PAGE, "Advanced", nullptr, SID_ACCESS_MORE, 0, nullptr, 0, 0, ACT_NONE, "Calm effects, screen shake,", "game speed and reset."},
};
static const Row kAccessMoreRows[] = {
	{ROW_CHOICE, "Calm Effects", &gSave.calmFx, 0, 1, kOffOn, 0, 0, ACT_NONE, "Softer flashes and no", "colour cycling."},
	{ROW_SLIDER, "Screen Shake", &gSave.shake, 0, 100, nullptr, 0, 100, ACT_NONE, "How hard big hits shake the field.", ""},
	{ROW_CHOICE, "Game Speed", &gSave.gameSpeed, 0, 2, kGameSpeedNames, 0, 0, ACT_NONE, "Slows the whole game down.", "Bests below 100% are kept apart."},
	{ROW_ACTION, "Reset Accessibility", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_ACCESS, "Puts these back to their defaults.", ""},
};

struct Section {
	const char* title;
	const Row* rows;
	int count;
	SectionId id;
	int parent;    // the section whose Advanced row opens this one, -1 for a sign on the Options screen, or kFromModes
	bool inPause;  // reachable from the pause menu
	const char* hint1;
	const char* hint2;
};
#define SECTION_ROWS(rows) rows, sizeof rows / sizeof(Row)
static constexpr int kFromModes = -2;  // opened from Pick a Game, and goes back there
static const Section kSections[] = {
	{"Audio", SECTION_ROWS(kAudioRows), SID_AUDIO, -1, true, "Sound, music and menu volume.", ""},
#if PORT_HAS_DISPLAY
	{"Visuals and Display", SECTION_ROWS(kVisualRows), SID_VISUALS, -1, true, PORT_VISUALS_HINT, "the screen and graphics quality."},
	{"Advanced Graphics", SECTION_ROWS(kVisualMoreRows), SID_VISUALS_MORE, SID_VISUALS, true, "", ""},
#else
	{"Visuals", SECTION_ROWS(kVisualRows), SID_VISUALS, -1, true, PORT_VISUALS_HINT, "depth and speed savers."},
	{"Advanced Visuals", SECTION_ROWS(kVisualMoreRows), SID_VISUALS_MORE, SID_VISUALS, true, "", ""},
#endif
	{"Gameplay and Controls", SECTION_ROWS(kGameplayRows), SID_GAMEPLAY, -1, true, PORT_GAMEPLAY_HINT, "game plays, plus stick slicing."},
	{"Advanced Gameplay", SECTION_ROWS(kGameplayMoreRows), SID_GAMEPLAY_MORE, SID_GAMEPLAY, true, "", ""},
	{"Accessibility", SECTION_ROWS(kAccessRows), SID_ACCESS, -1, true, "Bigger text, contrast, colour,", "assists and captions."},
	{"Advanced Accessibility", SECTION_ROWS(kAccessMoreRows), SID_ACCESS_MORE, SID_ACCESS, true, "", ""},
	// Mid-game, the data section stays out of reach: its speed test would replace the paused game.
	{"Scores and Data", SECTION_ROWS(kDataRows), SID_DATA, -1, false, PORT_DATA_HINT, ""},
#ifdef DEV_TOOLS
	{"Developer", SECTION_ROWS(kDevRows), SID_DEV, -1, false, "Testing tools. Not in", "release builds."},
#endif
	{"Mixer", SECTION_ROWS(kMixerRows), SID_MIXER, kFromModes, false, "", ""},
	{"Pass and Play", SECTION_ROWS(kPassRows), SID_PASS, kFromModes, false, "", ""},
};
#undef SECTION_ROWS
constexpr int kSectionCount = sizeof kSections / sizeof kSections[0];
static int sectionIndex(int id) {
	for (int i = 0; i < kSectionCount; i++)
		if (kSections[i].id == id) return i;
	return 0;
}

static bool sOptionsFromPause = false;  // Options opened from the pause menu, with a game waiting

// The Options signs: the back arrow, then each section that can be reached right now. Up to four
// stack in one column; more go in two columns, with an odd last one centred.
static Button sOptionButtons[kSectionCount + 1];
static int sOptionSection[kSectionCount + 1];
static int optionButtons() {
	int n = 0;
	int visible[kSectionCount];
	for (int i = 0; i < kSectionCount; i++)
		if (kSections[i].parent == -1 && (!sOptionsFromPause || kSections[i].inPause)) visible[n++] = i;
	sOptionButtons[0] = kBack;
	sOptionSection[0] = -1;
	for (int i = 0; i < n; i++) {
		Button b;
		if (n <= 4) b = {36, 46 + 48.0f * i, 248, 44, nullptr};
		else if (i == n - 1 && (n & 1)) b = {86, 46 + 48.0f * (i / 2), 148, 44, nullptr};
		else b = {i & 1 ? 164.0f : 8.0f, 46 + 48.0f * (i / 2), 148, 44, nullptr};
		b.label = kSections[visible[i]].title;
		sOptionButtons[i + 1] = b;
		sOptionSection[i + 1] = visible[i];
	}
	return n + 1;
}
#ifdef AUTOTEST
static Button modeSignOnPage(int mode) {
	Button signs[kModesPerPage + 1];
	const int n = modeSigns(sModePage, signs);
	for (int i = 1; i < n; i++)
		if (modeOnSign(sModePage, i) == mode) return signs[i];
	return kBack;
}
static int modePageOf(int mode) {
	for (int i = 0; i < (int)sizeof kModeOrder; i++)
		if (kModeOrder[i] == mode) return i / kModesPerPage;
	return 0;
}
static const Button& optionButtonFor(SectionId id) {
	int n = optionButtons();
	for (int i = 1; i < n; i++)
		if (kSections[sOptionSection[i]].id == id) return sOptionButtons[i];
	return kBack;
}
#endif

static constexpr float kListTop = 46;
static constexpr float kRowH = 32;  // six rows fit below the header without scrolling
static constexpr float kTrackX0 = 162, kTrackX1 = 258;

static int sSection = 0;
static float sScroll = 0;
// An Advanced page slides in like any other screen. These stand in for the page it is sliding away from,
// and for the row to come back to.
static int sOldSection = 0, sOldFocus = 0, sParentFocus = 0;
static float sOldScroll = 0, sParentScroll = 0;
static void swapOutgoingPage() {
	if (!sSliding || sSlideFrom != SCREEN_SECTION || sScreen != SCREEN_SECTION) return;
	int s = sSection, f = sFocus[SCREEN_SECTION];
	float y = sScroll;
	sSection = sOldSection;
	sFocus[SCREEN_SECTION] = sOldFocus;
	sScroll = sOldScroll;
	sOldSection = s;
	sOldFocus = f;
	sOldScroll = y;
}
static int sTouchStartX;
static u8 sSliderStart;
static int sArmed = -1;  // a reset row waiting for its second tap
static bool sDirty = false;
enum TouchMode { TOUCH_NONE, TOUCH_BACK, TOUCH_SLIDER, TOUCH_ROW };
static TouchMode sTouchMode = TOUCH_NONE;
static int sTouchRow = -1;
static int sTouchStartY = 0;
static float sScrollStart = 0;
static bool sTouchMoved = false;
static Blade sPreviewBlade;
static int sLastTouchX = 0;  // a released touch has no position, so remember where it last was

static float maxScroll() {
	float h = kSections[sSection].count * kRowH;
	float room = 240 - kListTop - 2;
	return h > room ? h - room : 0;
}

#if PORT_HAS_DISPLAY
// After Apply, the new mode stays only if it is kept: a mode the screen can't show must undo itself.
static float sRevertT = 0;
static int sRevertFocus = 0;
static const Button kRevertButtons[] = {{40, 150, 110, 44, "Keep"}, {170, 150, 110, 44, "Undo"}};
static void refreshResolutions() {
	for (Row& r : kVisualMoreRows)
		if (r.value == &port::gResChoice) r.hi = (u8)(port::resolutionCount() - 1);
}
#endif

static bool rowDisabled(const Row& r) {
	if (passRowPlayer(r) >= passCount()) return true;  // this name's player isn't in the game
#if PORT_HAS_DISPLAY
	if (r.value == &port::gResChoice) return gSave.windowMode == 1;  // borderless always matches the desktop
#endif
	return r.value == &gSave.depth && !gHas3dScreen;
}

static void rowValueText(const Row& r, char* buf, size_t size) {
	buf[0] = 0;
	if (r.kind == ROW_CHOICE) {
		snprintf(buf, size, "%s", r.names[*r.value]);
	} else if (r.kind == ROW_SLIDER) {
		if (rowDisabled(r)) {
			snprintf(buf, size, "-");
		} else if (r.value == &gSave.misses) {
			snprintf(buf, size, "%d", *r.value);
		} else if (r.value == &gSave.beatLatency) {
			const int ms = (*r.value - 100) * 2;
			snprintf(buf, size, ms > 0 ? "+%d ms" : "%d ms", ms);
		} else {
			float t = (*r.value - r.lo) / (float)(r.hi - r.lo);
			snprintf(buf, size, "%d%%", (int)(lerpf(r.showLo, r.showHi, t) + 0.5f));
		}
	}
}

void applyAccessibility() {
	gfx::gHighContrast = gSave.highContrast != 0;
	gfx::gTextBoost = gSave.largeText ? 1.12f : 1.0f;
}
float uiScale() {
	static const float k[] = {1, 1.25f, 1.5f};
	return k[gSave.uiScale < 3 ? gSave.uiScale : 0];
}
float gameSpeedScale() {
	static const float k[] = {1, 0.85f, 0.7f};
	return k[gSave.gameSpeed < 3 ? gSave.gameSpeed : 0];
}
bool assistedRun() { return gSave.sliceAssist || gSave.gameSpeed; }

static void changedSetting(const Row& r) {
	applyAccessibility();
	sDirty = true;
	if (r.value == &gSave.music) audio::refreshVolumes();
	if (r.value == &gSave.mixer[MIX_GIANT] && *r.value) gSave.mixer[MIX_TINY] = 0;
	if (r.value == &gSave.mixer[MIX_TINY] && *r.value) gSave.mixer[MIX_GIANT] = 0;
	// Hear a track as soon as it is picked. Leaving the section puts the right one back.
	if (r.value == &gSave.menuTrack) audio::preview(MUSIC_MENU);
	if (r.value == &gSave.gameTrack) audio::preview(MUSIC_PLAY);
#if PORT_HAS_DISPLAY
	if (r.value == &gSave.windowMode) refreshResolutions();
	if (r.value == &gSave.vsync) port::applyVsync();
	if (r.value == &gSave.preset) {
		port::applyPreset(gSave.preset);
	} else if (r.value == &gSave.texQuality || r.value == &gSave.shadowQuality || r.value == &gSave.ambientOcc || r.value == &gSave.bloom ||
	           r.value == &gSave.effects || r.value == &gSave.detail || r.value == &gSave.renderScale) {
		port::matchPreset();
		port::applyGraphics();
	}
#endif
}

static bool choiceLocked(const Row& r, int v) {
	if (r.value == &gSave.board) return !boardUnlocked(v);
	if (r.value == &gSave.trail) return !trailUnlocked(v);
	if (r.value == &gSave.menuTrack || r.value == &gSave.gameTrack) return v > 0 && !trackUnlocked(v - 1);
	return false;
}

static void adjustRow(const Row& r, int dir) {
	if (rowDisabled(r) || r.kind == ROW_ACTION || r.kind == ROW_PAGE) return;
	int v = *r.value;
	if (r.kind == ROW_SLIDER) {
		int step = r.hi - r.lo >= 50 ? 5 : 1;
		v = v + dir * step;
		v = v < r.lo ? r.lo : (v > r.hi ? r.hi : v);
	} else {
		// Shop items that haven't been bought are skipped.
		for (int tries = r.lo; tries <= r.hi; tries++) {
			v += dir;
			if (v < r.lo) v = r.hi;
			if (v > r.hi) v = r.lo;
			if (!choiceLocked(r, v)) break;
		}
	}
	if (v == *r.value) return;
	*r.value = (u8)v;
	changedSetting(r);
	if (r.value == &gSave.sfx) audio::play(SFX_CLICK);  // lets you hear the level you just picked
	else audio::click(0.6f);
}

#if PORT_3DS
static void startSpeedTest();
#endif
static void startLatencyTest();

static void rowAction(int index, const Row& r) {
	if (rowDisabled(r)) return;
	if (r.action == ACT_PASS_NAME) {
		audio::click();
		sPassTyping = passRowPlayer(r);
		sNameFocus = 0;
		port::textStart(sPassNames[sPassTyping], kPassNameLen);
		return;
	}
	if (r.action == ACT_PASS_START) {
		audio::click();
		passBegin();
		return;
	}
	if (r.action == ACT_LATENCY) {
		audio::click();
		if (sDirty) saveWrite();
		sDirty = false;
		fadeThen(startLatencyTest);
		return;
	}
#if PORT_3DS
	if (r.action == ACT_SPEED_TEST) {
		audio::click();
		if (sDirty) saveWrite();
		sDirty = false;
		fadeThen(startSpeedTest);
		return;
	}
#endif
#if PORT_HAS_DISPLAY
	if (r.action == ACT_APPLY_DISPLAY) {
		audio::click();
		if (port::displayApply()) sRevertT = 10;
		return;
	}
#endif
	if (r.action == ACT_CHECK_UPDATES) {
		audio::click();
		update::checkNow();
		return;
	}
#ifdef DEV_TOOLS
	if (r.action == ACT_DEV_NEXT_DAY) {
		audio::click();
		progress::devSkipDay();
		return;
	}
	if (r.action == ACT_DEV_ADD_COINS || r.action == ACT_DEV_CLEAR_COINS) {
		// No second press needed: these are for testing, and adding again is the point.
		audio::click();
		if (r.action == ACT_DEV_CLEAR_COINS) gSave.coins = 0;
		else {
			u32 add = kDevAmounts[sDevAmount];
			gSave.coins = gSave.coins + add < gSave.coins ? 0xFFFFFFFFu : gSave.coins + add;
		}
		saveWrite();
		return;
	}
#endif
	if (sArmed != index) {
		sArmed = index;
		audio::click();
		return;
	}
	sArmed = -1;
	switch (r.action) {
		case ACT_RESET_AUDIO: saveDefaults(true, SEC_AUDIO); break;
		case ACT_RESET_VISUALS: saveDefaults(true, SEC_VISUALS); break;
		case ACT_RESET_GAMEPLAY: saveDefaults(true, SEC_GAMEPLAY); break;
		case ACT_RESET_SCORES: saveResetScores(); break;
		case ACT_RESET_ALL: saveDefaults(false, SEC_ALL); break;
		case ACT_RESET_DISPLAY: saveDefaults(true, SEC_DISPLAY); break;
		case ACT_RESET_ACCESS: saveDefaults(true, SEC_ACCESS); break;
		case ACT_RESET_CONTROLS: saveDefaults(true, SEC_CONTROLS); break;
		default: break;
	}
#if PORT_HAS_DISPLAY
	if (r.action == ACT_RESET_DISPLAY || r.action == ACT_RESET_ALL) {
		refreshResolutions();
		port::displayApply();
		port::applyVsync();
		port::applyPreset(1);  // then the first-start check runs again, and may pick Performance
	}
#endif
	audio::refreshVolumes();
	audio::reapply();
	applyAccessibility();
	audio::play(SFX_GOLD, 0.7f);
	saveWrite();
	sDirty = false;
}

static void holdOutgoingPage() {
	sOldSection = sSection;
	sOldFocus = sFocus[SCREEN_SECTION];
	sOldScroll = sScroll;
}

static void openPage(const Row& r) {
	audio::click();
	holdOutgoingPage();
	sParentFocus = sFocus[SCREEN_SECTION];
	sParentScroll = sScroll;
	sSection = sectionIndex(r.lo);
	sScroll = 0;
	sFocus[SCREEN_SECTION] = 0;
	sArmed = -1;
	sTouchMode = TOUCH_NONE;
	goTo(SCREEN_SECTION, 1);
}

static void leaveSection(bool clicked = false) {
	if (!clicked) audio::click();
	if (sDirty) saveWrite();
	sDirty = false;
	sArmed = -1;
	sTouchMode = TOUCH_NONE;
	if (kSections[sSection].parent >= 0) {
		// Up one page, back on the row that opened this one.
		holdOutgoingPage();
		sSection = sectionIndex(kSections[sSection].parent);
		sFocus[SCREEN_SECTION] = sParentFocus;
		sScroll = sParentScroll;
		goTo(SCREEN_SECTION, -1);
		return;
	}
#if PORT_HAS_DISPLAY
	port::displayDiscard();  // a mode or resolution picked but never applied
#endif
	audio::reapply();
	goTo(kSections[sSection].parent == kFromModes ? SCREEN_MODES : SCREEN_OPTIONS, -1);
}

static void setSliderFromTouch(const Row& r, int x) {
	if (rowDisabled(r)) return;
	float t = clampf((x - kTrackX0) / (kTrackX1 - kTrackX0), 0, 1);
	u8 v = (u8)(lerpf(r.lo, r.hi, t) + 0.5f);
	if (v != *r.value) {
		*r.value = v;
		changedSetting(r);
	}
}

// ---------------------------------------------------------------- shop
// Sign 0 is the back arrow, then the tabs, the Buy / Use sign and every item tile on the tab.
// The grid keeps normal taps and the D-pad: slicing a small tile would be fiddly.
// Whatever is focused is tried on at once (board, trail or menu track) and put back on the way out.
// Six tiles are on show at a time; only they scroll, while the tabs and the Buy sign stay put.
static constexpr int kShopTab0 = 1, kShopAct = kShopTab0 + SHOP_TABS, kShopTile0 = kShopAct + 1;
static constexpr int kShopSigns = kShopTile0 + kMaxShopItems;
static constexpr int kShopCols = 3, kShopRowsShown = 2;
static constexpr float kShopTop = 78, kShopStep = 60;
static constexpr float kShopWindowTop = kShopTop - 2, kShopWindowH = kShopRowsShown * kShopStep + 2;
static int sShopTab, sShopItem;
static RowScroller sShopList;
static u8 sKeep[SHOP_TABS];  // the real choices, while the item in focus is being tried on
static float sShopArmT;    // a Buy press waits this long for the second press that confirms it
static float sShopNoteT;
static const char* sShopNote = "";

static Button shopButton(int i) {
	if (i == 0) return kBack;
	if (i < kShopAct) {
		const float w = (300 - (SHOP_TABS - 1) * 4.0f) / SHOP_TABS;
		return {10 + (i - kShopTab0) * (w + 4), 46, w, 26, shopTabName(i - kShopTab0)};
	}
	if (i == kShopAct) return {80, 202, 160, 34, nullptr};
	const int k = i - kShopTile0;
	return {(float)(10 + k % kShopCols * 102), kShopTop + k / kShopCols * kShopStep - sShopList.pos, 96, 54, nullptr};
}

static int shopHiddenRows(int tab) { return (shopCount(tab) + kShopCols - 1) / kShopCols - kShopRowsShown; }

static void shopRemember() {
	for (int t = 0; t < SHOP_TABS; t++) sKeep[t] = *shopSlot(t);
}

static void shopPutBack() {
	for (int t = 0; t < SHOP_TABS; t++) *shopSlot(t) = sKeep[t];
}

static bool shopInUse(int tab, int item) { return sKeep[tab] == shopValue(tab, item); }

static void shopTryOn() {
	const u8 track = gSave.menuTrack;
	shopPutBack();
	*shopSlot(sShopTab) = (u8)shopValue(sShopTab, sShopItem);
	if (gSave.menuTrack != track) audio::preview(MUSIC_MENU);
}

// Saves with the real choices in place, never the one being tried on.
static void shopSave() {
	const u8 onShow = *shopSlot(sShopTab);
	shopPutBack();
	saveWrite();
	*shopSlot(sShopTab) = onShow;
}

static void openShop() {
	shopRemember();
	sShopTab = SHOP_THEMES;
	sShopItem = 0;
	sShopArmT = sShopNoteT = 0;
	rowReset(sShopList);
	sFocus[SCREEN_SHOP] = kShopTile0;
	audio::play(SFX_SHOPBELL, 0.8f);  // the bell over the shop door
	shopTryOn();
	goTo(SCREEN_SHOP, 1);
}

static void leaveShop() {
	bool trackChanged = gSave.menuTrack != sKeep[SHOP_MUSIC];
	shopPutBack();
	if (trackChanged) audio::reapply();
	sMenuBlade.reset();
	goTo(SCREEN_TITLE, -1);
}

static void shopNote(const char* s) {
	sShopNote = s;
	sShopNoteT = 2.0f;
}

// Equipping works on the real choices, then the focused item goes back on show.
static void shopEquipNow() {
	shopPutBack();
	shopEquip(sShopTab, sShopItem);
	shopRemember();
	shopTryOn();
	shopSave();
}

static void shopAct() {
	if (shopOwned(sShopTab, sShopItem)) {
		shopEquipNow();
		audio::play(SFX_GOLD, 0.6f);
		shopNote(shopCanTakeOff(sShopTab) && sKeep[sShopTab] == 0 ? "Taken off" : "Now in use");
		return;
	}
	if (gSave.coins < shopItem(sShopTab, sShopItem).price) {
		audio::click(0.4f);
		shopNote("Not enough coins yet");
		return;
	}
	if (sShopArmT <= 0) {
		audio::click();
		sShopArmT = 2.5f;
		return;
	}
	sShopArmT = 0;
	if (shopBuy(sShopTab, sShopItem) == BUY_DONE) {
		audio::play(SFX_PLINK);
		audio::play(SFX_GOLD, 0.7f);
		shopEquipNow();
		shopNote("Bought and in use!");
	}
}

static void shopSetTab(int tab) {
	tab = (tab + SHOP_TABS) % SHOP_TABS;
	if (tab == sShopTab) return;
	sShopTab = tab;
	sShopItem = 0;
	sShopArmT = 0;
	rowReset(sShopList);
	shopTryOn();
}

static void updateShop(const Input& in, bool wasTouching) {
	int& f = sFocus[SCREEN_SHOP];
	if (sShopArmT > 0) sShopArmT -= kDt;
	if (sShopNoteT > 0) sShopNoteT -= kDt;
	const int n = shopCount(sShopTab);
	if (in.down & KEY_B) {
		audio::click();
		leaveShop();
		return;
	}
	bool leave;
	if (backCut(SCREEN_SHOP, in, wasTouching, &leave)) {
		if (leave) leaveShop();
		return;
	}
	if (in.down & KEY_L) { audio::click(0.5f); shopSetTab(sShopTab - 1); }
	if (in.down & KEY_R) { audio::click(0.5f); shopSetTab(sShopTab + 1); }

	// A drag or the wheel moves the tiles and nothing else, and a drag is never a tap on a tile.
	if (rowScroll(sShopList, in, wasTouching, kShopWindowTop, kShopWindowTop + kShopWindowH, kShopStep, shopHiddenRows(sShopTab))) sPressed = -1;

	Button btns[kShopSigns];
	for (int i = 0; i < kShopSigns; i++) btns[i] = shopButton(i);
	// Tiles past the end of the tab, or scrolled out of the window, can't be touched.
	const bool inWindow = in.ty >= kShopWindowTop && in.ty < kShopWindowTop + kShopWindowH;
	for (int i = kShopTile0; i < kShopSigns; i++)
		if (i - kShopTile0 >= n || !inWindow) btns[i] = {-100, -100, 0, 0, nullptr};
	int t = touchButtons(btns, kShopSigns, in, wasTouching, f);
#if PORT_MOUSE
	if (in.hovering && !in.touching)
		for (int i = 0; i < kShopSigns; i++)
			if (hit(btns[i], in.tx, in.ty)) f = i;
#endif

	// The D-pad: back arrow, the tab row, the rows of tiles, then the Buy sign.
	const bool onTab = f >= kShopTab0 && f < kShopAct, onTile = f >= kShopTile0;
	const int k = f - kShopTile0;
	const int rows = (n + kShopCols - 1) / kShopCols;
	if (in.repeat & KEY_DOWN) {
		if (f == 0) f = kShopTab0 + sShopTab;
		else if (onTab) f = kShopTile0 + (sShopItem < n ? sShopItem : 0);
		else if (onTile) {
			const int row = k / kShopCols;
			if (row + 1 >= rows) f = kShopAct;
			else f = kShopTile0 + (k + kShopCols < n ? k + kShopCols : n - 1);
		}
		audio::click(0.5f);
	}
	if (in.repeat & KEY_UP) {
		if (f == kShopAct) f = kShopTile0 + sShopItem;
		else if (onTile) f = k >= kShopCols ? f - kShopCols : kShopTab0 + sShopTab;
		else if (onTab) f = 0;
		audio::click(0.5f);
	}
	if (in.repeat & (KEY_LEFT | KEY_RIGHT)) {
		int dir = (in.repeat & KEY_RIGHT) ? 1 : -1;
		if (onTab) {
			shopSetTab(sShopTab + dir);
			f = kShopTab0 + sShopTab;
		} else if (onTile) {
			int row = k / kShopCols, col = (k % kShopCols + dir + kShopCols) % kShopCols;
			int last = row * kShopCols + kShopCols - 1 < n ? kShopCols - 1 : (n - 1) % kShopCols;
			if (col > last) col = dir > 0 ? 0 : last;
			f = kShopTile0 + row * kShopCols + col;
		}
		audio::click(0.5f);
	}
	if ((in.repeat & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) && f >= kShopTile0)
		rowReveal(sShopList, (f - kShopTile0) / kShopCols, kShopRowsShown, kShopStep);

	if (t < 0 && (in.down & KEY_A)) t = f;
	if (t == 0) {
		cutBack(SCREEN_SHOP);
	} else if (t >= kShopTab0 && t < kShopAct) {
		shopSetTab(t - kShopTab0);
	} else if (t >= kShopTile0 && t - kShopTile0 < n) {
		// Pointing at a tile only highlights it. A click, tap or A picks it and tries it on;
		// the same again on the picked tile does what the action sign says.
		if (t - kShopTile0 != sShopItem) {
			sShopItem = t - kShopTile0;
			sShopArmT = 0;
			shopTryOn();
		} else {
			shopAct();
		}
	} else if (t == kShopAct) {
		shopAct();
	}

	// The trail on show draws itself in a slow loop beside the grid.
	if (sShopTab == SHOP_TRAILS) {
		Input fake{};
		fake.touching = true;
		fake.tx = (int)(200 + sinf(sTime * 4.4f) * 110);
		fake.ty = (int)(140 + sinf(sTime * 8.8f) * 45);
		sPreviewBlade.update(fake);
	}
}

static void drawShopIcon(int tab, int item, float cx, float cy) {
	if (tab == SHOP_THEMES) {
		int b = kBaseBoards + item;
		gfx::rect(cx - 30, cy - 9, 60, 18, gfx::boardColor(b, 2));
		gfx::rect(cx - 27, cy - 6, 30, 12, gfx::boardColor(b, 0));
		gfx::rect(cx + 5, cy - 6, 22, 12, gfx::boardColor(b, 1));
	} else if (tab == SHOP_TRAILS) {
		// A short wavy stroke in the trail's colours, thick at the head.
		float px = cx - 30, py = cy;
		for (int s = 1; s <= 12; s++) {
			float u = s / 12.0f;
			float x = cx - 30 + u * 60, y = cy + sinf(u * 6.3f) * 5;
			float w = 0.6f + 2.8f * u;
			u32 c = trailSwatch(item + 1, u);
			gfx::tri(px, py - w, c, x, y - w, c, x, y + w, c);
			gfx::tri(px, py - w, c, x, y + w, c, px, py + w, c);
			px = x;
			py = y;
		}
	} else if (tab == SHOP_SKINS) {
		if (gSave.artStyle == ART_3D && m3d::ready()) {
			m3d::begin(gfx::onTop() ? 400 : 320, gfx::stereoPerZ());
			m3d::setSkin(&shopSkinLook(item));
			m3d::Pose pose = {cx, cy, 0, 0.25f, 0.5f, 0, 0.62f, 0, 0, 0};
			m3d::draw(m3d::MESH_APPLE, m3d::PART_WHOLE, pose);
			m3d::end();
		} else {
			gfx::sprite(SHEET_FRUIT, fruit_apple_idx, cx, cy, 0.62f, 0.62f, 0, shopSkinTint(item));
		}
	} else if (tab == SHOP_SPLATS) {
		// Three drops in the colour on sale, the way it lands on the board.
		const u32 c = shopSplatPreview(item);
		gfx::sprite(SHEET_SPRITES, sprites_drop_idx, cx - 12, cy + 2, 1.5f, 1.5f, 0.4f, c);
		gfx::sprite(SHEET_SPRITES, sprites_drop_idx, cx + 2, cy - 4, 2.2f, 2.2f, 0, c);
		gfx::sprite(SHEET_SPRITES, sprites_drop_idx, cx + 14, cy + 4, 1.1f, 1.1f, -0.5f, c);
	} else {
		// Two beamed notes.
		u32 c = COL_CREAM;
		gfx::rect(cx - 10, cy - 10, 3, 16, c);
		gfx::rect(cx + 8, cy - 12, 3, 16, c);
		gfx::tri(cx - 10, cy - 10, c, cx + 11, cy - 12, c, cx + 11, cy - 8, c);
		gfx::tri(cx - 10, cy - 10, c, cx + 11, cy - 8, c, cx - 10, cy - 6, c);
		gfx::rect(cx - 16, cy + 3, 8, 6, c);
		gfx::rect(cx + 2, cy + 1, 8, 6, c);
	}
}

static void drawShop() {
	char buf[40];
	int f = sFocus[SCREEN_SHOP];
	drawHeader("Shop");
	drawBackArrow(f == 0, sPressed == 0);
	drawPurse(306, 13, 0.44f);

	for (int i = 0; i < SHOP_TABS; i++) {
		Button b = shopButton(kShopTab0 + i);
		drawPlank(b.x, b.y, b.w, b.h, f == kShopTab0 + i, sPressed == kShopTab0 + i);
		gfx::text(b.label, b.x + b.w / 2, b.y + 6, 0.38f, i == sShopTab ? COL_ACCENT : COL_DIM, ALIGN_CENTER);
	}
	const int n = shopCount(sShopTab);
	gfx::setClip(0, kShopWindowTop, 320, kShopWindowH);
	for (int k = 0; k < n; k++) {
		Button b = shopButton(kShopTile0 + k);
		if (b.y + b.h + 2 < kShopWindowTop || b.y - 2 > kShopWindowTop + kShopWindowH) continue;
		bool sel = k == sShopItem;
		// The picked item keeps the gold frame; a tile only pointed at gets a faint one.
		const bool pressed = sPressed == kShopTile0 + k;
		drawPlank(b.x, b.y, b.w, b.h, sel, pressed);
		if (!sel && f == kShopTile0 + k) gfx::sign(sprites_plank_focus_idx, b.x - 2, b.y - 2 + (pressed ? 2 : 0), b.w + 4, b.h + 4, withAlpha(COL_CREAM, 0.5f));
		drawShopIcon(sShopTab, k, b.x + b.w / 2, b.y + 17);
		const ShopItem& it = shopItem(sShopTab, k);
		float s = 0.3f, tw = gfx::textWidth(it.name, s);
		if (tw > b.w - 10) s *= (b.w - 10) / tw;
		gfx::text(it.name, b.x + b.w / 2, b.y + 28, s, COL_CREAM, ALIGN_CENTER);
		if (shopInUse(sShopTab, k)) {
			gfx::text("IN USE", b.x + b.w / 2, b.y + 40, 0.26f, COL_ACCENT, ALIGN_CENTER);
		} else if (shopOwned(sShopTab, k)) {
			gfx::text("OWNED", b.x + b.w / 2, b.y + 40, 0.26f, COL_DIM, ALIGN_CENTER);
		} else {
			snprintf(buf, sizeof buf, "%u", (unsigned)it.price);
			float pw = gfx::textWidth(buf, 0.3f);
			gfx::sprite(SHEET_SPRITES, sprites_coin_idx, b.x + b.w / 2 - pw / 2 - 5, b.y + 45, 0.3f, 0.3f);
			gfx::text(buf, b.x + b.w / 2 + 4, b.y + 39, 0.3f, gSave.coins >= it.price ? COL_CREAM : COL_DIM, ALIGN_CENTER);
		}
	}
	gfx::clearClip();
	drawScrollMark(kShopTop, kShopRowsShown * kShopStep - 6, sShopList.pos, shopHiddenRows(sShopTab) * kShopStep);

	// The action sign says exactly what a press will do.
	const ShopItem& it = shopItem(sShopTab, sShopItem);
	bool owned = shopOwned(sShopTab, sShopItem);
	const bool inUse = shopInUse(sShopTab, sShopItem);
	if (owned) snprintf(buf, sizeof buf, "%s", inUse ? (shopCanTakeOff(sShopTab) ? "Take Off" : "In Use") : "Use");
	else if (sShopArmT > 0) snprintf(buf, sizeof buf, "Sure? %u", (unsigned)it.price);
	else snprintf(buf, sizeof buf, "Buy %u", (unsigned)it.price);
	Button a = shopButton(kShopAct);
	drawPlank(a.x, a.y, a.w, a.h, f == kShopAct, sPressed == kShopAct);
	gfx::text(buf, a.x + a.w / 2, a.y + 8, 0.44f, owned || gSave.coins >= it.price ? COL_CREAM : COL_DIM, ALIGN_CENTER);
}

// What the focused item is, with its live preview: the top screen on the 3DS, beside the menu elsewhere.
static void drawShopPreview() {
	const ShopItem& it = shopItem(sShopTab, sShopItem);
	gfx::setDepth(0.7f);
	gfx::text(it.name, 200, 22, 0.6f, COL_ACCENT, ALIGN_CENTER);
	gfx::text(it.line, 200, 52, 0.38f, COL_CREAM, ALIGN_CENTER);
	if (sShopTab == SHOP_THEMES) {
		gfx::setDepth(0.5f);
		gfx::image(SHEET_BOARD, 0, 122, 74, 156, 117);
	} else if (sShopTab == SHOP_TRAILS) {
		gfx::setDepth(0.5f);
		sPreviewBlade.draw();
	} else if (sShopTab == SHOP_SPLATS || sShopTab == SHOP_SKINS) {
		gfx::setDepth(0.5f);
		drawShopIcon(sShopTab, sShopItem, 200, 125);
	} else {
		gfx::text("Now playing as a preview", 200, 120, 0.36f, COL_DIM, ALIGN_CENTER);
		gfx::text("Use it to make it the menu music.", 200, 140, 0.36f, COL_DIM, ALIGN_CENTER);
	}
	// What the item costs against the purse, or that it is already yours.
	char buf[48];
	const bool owned = shopOwned(sShopTab, sShopItem);
	const bool inUse = shopInUse(sShopTab, sShopItem);
	if (owned) snprintf(buf, sizeof buf, "%s", inUse ? "Yours, and in use" : "Yours to use");
	else if (gSave.coins >= it.price) snprintf(buf, sizeof buf, "%u coins. You have %lu.", (unsigned)it.price, (unsigned long)gSave.coins);
	else snprintf(buf, sizeof buf, "%u coins. %lu more to go.", (unsigned)it.price, (unsigned long)(it.price - gSave.coins));
	gfx::sprite(SHEET_SPRITES, sprites_coin_idx, 200 - gfx::textWidth(buf, 0.4f) * 0.5f - 12, 205, 0.4f, 0.4f);
	gfx::text(buf, 200, 197, 0.4f, owned ? COL_ACCENT : COL_CREAM, ALIGN_CENTER);
	if (sShopNoteT > 0) gfx::text(sShopNote, 200, 214, 0.46f, withAlpha(COL_CREAM, clampf(sShopNoteT * 2, 0, 1)), ALIGN_CENTER);
}

// Moves dir pages along, wrapping past either end. The old page slides out as the new one comes in.
static void turnModePage(int dir) {
	sModeOldPage = sModePage;
	sModePage = (sModePage + dir + kModePages) % kModePages;
	sModeSlide = gSave.transitions ? dir * 320.0f : 0;
	sPressed = -1;
	audio::click(0.6f);
}

// The page arrows, and the keys that turn the page. Returns true while a press that landed on an arrow is
// held, so that it can't go on to slice the signs of the new page. Left and right walk along a row, and
// past the end of it they turn the page and land on the near side of the same row.
static bool updateModePages(Input& keys, bool wasTouching) {
	if (sArrowPressed >= 0) {
		if (!keys.touching) sArrowPressed = -1;
		return true;
	}
	if (sCut.pending) return false;
	if (keys.wheel) {
		turnModePage(keys.wheel < 0 ? 1 : -1);
		return false;
	}
	sArrowHover = -1;
#if PORT_MOUSE
	if (keys.hovering)
		for (int side = 0; side < 2; side++)
			if (hit(kPageArrows[side], keys.tx, keys.ty)) sArrowHover = side;
#endif
	if (keys.touching && !wasTouching)
		for (int side = 0; side < 2; side++)
			if (hit(kPageArrows[side], keys.tx, keys.ty)) {
				sArrowPressed = side;
				turnModePage(side ? 1 : -1);
				return true;
			}
	const int dir = (keys.repeat & KEY_RIGHT) ? 1 : ((keys.repeat & KEY_LEFT) ? -1 : 0);
	if (dir == 0) return false;
	Button signs[kModesPerPage + 1];
	int& focus = sFocus[SCREEN_MODES];
	const int n = modeSigns(sModePage, signs) - 1, i = focus - 1;
	const bool alone = i < 0 || (i == n - 1 && (n & 1));
	if (!alone && (i & 1) != (dir > 0 ? 1 : 0)) return false;
	turnModePage(dir);
	keys.repeat &= ~(u32)(KEY_LEFT | KEY_RIGHT);
	if (focus > 0) {
		const int count = modeSigns(sModePage, signs) - 1;
		const int want = (i / 2) * 2 + (dir > 0 ? 0 : 1);
		focus = (want < count ? want : count - 1) + 1;
	}
	return false;
}

static const Button kNameButtons[] = {{40, 150, 110, 44, "OK"}, {170, 150, 110, 44, "Cancel"}};

// Typing a Pass and Play name, over the setup list.
static void updateNameEntry(const Input& in, bool wasTouching) {
	char typed[32];
	int result = port::textPoll(typed, sizeof typed);
	if (result == 0) {
		int t = touchButtons(kNameButtons, 2, in, wasTouching, sNameFocus);
		if (in.repeat & (KEY_LEFT | KEY_RIGHT)) sNameFocus ^= 1;
		if (in.down & KEY_A) t = sNameFocus;
		if (in.down & KEY_B) t = 1;
		if (t < 0) return;
		audio::click();
		result = t == 0 ? 1 : -1;
	}
	if (result > 0) passSetName(sPassTyping, typed);
	port::textStop();
	sPassTyping = -1;
}

static void updateSection(const Input& in, bool wasTouching) {
	if (sPassTyping >= 0) {
		updateNameEntry(in, wasTouching);
		return;
	}
	const Section& sec = kSections[sSection];
	int& focus = sFocus[SCREEN_SECTION];  // -1 is the back arrow

#if PORT_HAS_DISPLAY
	if (sRevertT > 0) {
		int t = touchButtons(kRevertButtons, 2, in, wasTouching, sRevertFocus);
		if (in.repeat & (KEY_LEFT | KEY_RIGHT)) sRevertFocus ^= 1;
		if (in.down & KEY_A) t = sRevertFocus;
		if (in.down & KEY_B) t = 1;
		sRevertT -= 1.0f / 60;
		if (t < 0 && sRevertT <= 0) t = 1;
		if (t >= 0) {
			if (t == 1) port::displayRevert();
			else audio::play(SFX_GOLD, 0.7f);
			refreshResolutions();
			sRevertT = 0;
			sRevertFocus = 0;
			saveWrite();
			sDirty = false;
		}
		return;
	}
#endif
	bool leave;
	if (backCut(SCREEN_SECTION, in, wasTouching, &leave)) {
		if (leave) leaveSection(true);
		return;
	}
	if (in.down & KEY_B) {
		leaveSection();
		return;
	}
	if (in.repeat & KEY_DOWN) {
		focus = focus + 1 < sec.count ? focus + 1 : -1;
		audio::click(0.5f);
		sArmed = -1;
	}
	if (in.repeat & KEY_UP) {
		focus = focus >= 0 ? focus - 1 : sec.count - 1;
		audio::click(0.5f);
		sArmed = -1;
	}
	if (focus < 0 && (in.repeat & (KEY_UP | KEY_DOWN))) sScroll = 0;
#if PORT_MOUSE
	sScroll -= in.wheel * kRowH;
	if (in.hovering) {
		if (hit(kBack, in.tx, in.ty)) {
			focus = -1;
		} else if (in.ty >= kListTop) {
			int row = (int)((in.ty - kListTop + sScroll) / kRowH);
			if (row >= 0 && row < sec.count) {
				if (row != focus) sArmed = -1;
				focus = row;
			}
		}
	}
#endif
	if (focus >= 0) {
		const Row& r = sec.rows[focus];
		if (in.repeat & KEY_LEFT) adjustRow(r, -1);
		if (in.repeat & KEY_RIGHT) adjustRow(r, 1);
		if (in.down & KEY_A) {
			if (r.kind == ROW_PAGE) {
				openPage(r);
				return;
			}
			if (r.kind == ROW_ACTION) rowAction(focus, r);
			else if (r.kind == ROW_CHOICE) adjustRow(r, 1);
		}
		// Keep the focused row in view.
		float top = focus * kRowH, bottom = top + kRowH;
		float room = 240 - kListTop - 2;
		if (in.repeat & (KEY_UP | KEY_DOWN)) {
			if (top < sScroll) sScroll = top;
			if (bottom > sScroll + room) sScroll = bottom - room;
		}
	} else if (in.down & KEY_A) {
		cutBack(SCREEN_SECTION);
		return;
	}

	if (in.touching) {
		sLastTouchX = in.tx;
		if (!wasTouching) {
			sTouchMode = TOUCH_NONE;
			if (hit(kBack, in.tx, in.ty)) {
				sTouchMode = TOUCH_BACK;
				sPressed = 0;
				focus = -1;
			} else if (in.ty >= kListTop) {
				int row = (int)((in.ty - kListTop + sScroll) / kRowH);
				if (row >= 0 && row < sec.count) {
					if (row != sArmed) sArmed = -1;
					focus = row;
					sTouchRow = row;
					const Row& r = sec.rows[row];
					if (r.kind == ROW_SLIDER && in.tx >= kTrackX0 - 12 && in.tx <= kTrackX1 + 12) {
						sTouchMode = TOUCH_SLIDER;
						sTouchStartX = in.tx;
						sTouchStartY = in.ty;
						sSliderStart = *r.value;
						setSliderFromTouch(r, in.tx);
					} else {
						sTouchMode = TOUCH_ROW;
						sTouchStartY = in.ty;
						sScrollStart = sScroll;
						sTouchMoved = false;
					}
				}
			}
		} else if (sTouchMode == TOUCH_BACK) {
			if (!hit(kBack, in.tx, in.ty)) {
				sTouchMode = TOUCH_NONE;
				sPressed = -1;
			}
		} else if (sTouchMode == TOUCH_SLIDER) {
			// A mostly upward or downward drag means the list, so the slider goes back to where it was.
			int dx = in.tx - sTouchStartX, dy = in.ty - sTouchStartY;
			if (abs(dy) > 8 && abs(dy) > abs(dx) * 2) {
				*sec.rows[sTouchRow].value = sSliderStart;
				audio::refreshVolumes();
				sTouchMode = TOUCH_ROW;
				sScrollStart = sScroll;
				sTouchMoved = true;
				sScroll = sScrollStart - dy;
			} else {
				setSliderFromTouch(sec.rows[sTouchRow], in.tx);
			}
		} else if (sTouchMode == TOUCH_ROW) {
			int dy = in.ty - sTouchStartY;
			if (dy > 6 || dy < -6) sTouchMoved = true;
			if (sTouchMoved) sScroll = sScrollStart - dy;
		}
	} else if (wasTouching) {
		sPressed = -1;
		if (sTouchMode == TOUCH_BACK) {
			sTouchMode = TOUCH_NONE;
			cutBack(SCREEN_SECTION);
			return;
		}
		if (sTouchMode == TOUCH_SLIDER) {
			const Row& r = sec.rows[sTouchRow];
			if (r.value == &gSave.sfx) audio::play(SFX_CLICK);
			else audio::click(0.6f);
		} else if (sTouchMode == TOUCH_ROW && !sTouchMoved) {
			const Row& r = sec.rows[sTouchRow];
			if (r.kind == ROW_PAGE) {
				openPage(r);
				return;
			}
			if (r.kind == ROW_ACTION) rowAction(sTouchRow, r);
			else if (r.kind == ROW_CHOICE) adjustRow(r, sLastTouchX < 214 ? -1 : 1);
		}
		sTouchMode = TOUCH_NONE;
	}
	sScroll = clampf(sScroll, 0, maxScroll());
}

// ---------------------------------------------------------------- top-screen art
#if PORT_TWO_SCREENS
static void drawTopBackground() {
	const float d = sTime;
	// Layers sit behind the screen plane and slowly drift for life.
	gfx::setDepth(-2.0f);
	gfx::image(SHEET_BGTOP, bgtop0_sky_idx, -12, 0, 424, 240);
	if (gSave.bgDetail) {
		gfx::setDepth(-1.3f);
		gfx::image(SHEET_BGTOP, bgtop0_far_idx, -12 + sinf(d * 0.11f) * 2, 120, 424, 120);
	}
	gfx::setDepth(-0.7f);
	gfx::image(SHEET_BGTOP, bgtop0_mid_idx, -12 + sinf(d * 0.15f) * 3, 130, 424, 110);
	if (gSave.bgDetail) {
		gfx::setDepth(-0.15f);
		gfx::image(SHEET_BGTOP, bgtop0_near_idx, -12, 170, 424, 70);
	}
}
#endif

// A slowly turning fruit, drawn with whichever art style is chosen.
static void drawHero(float x, float y, float scale, float depth) {
	static const int kSprites[] = {fruit_orange_idx, fruit_apple_idx, fruit_melon_idx, fruit_pineapple_idx, fruit_peach_idx, fruit_berry_idx};
	static const int kMeshes[] = {m3d::MESH_ORANGE, m3d::MESH_APPLE, m3d::MESH_MELON, m3d::MESH_PINEAPPLE, m3d::MESH_PEACH, m3d::MESH_BERRY};
	int which = ((int)(sTime / 4.0f)) % 6;
	if (gSave.artStyle == ART_3D && m3d::ready()) {
		m3d::begin(400, gfx::stereoPerZ());
		m3d::Pose pose = {x, y, depth * 30, 0.25f, sTime * 0.9f, sinf(sTime * 0.7f) * 0.15f, scale, 0, 0, 0};
		m3d::draw(kMeshes[which], m3d::PART_WHOLE, pose);
		m3d::end();
	} else {
		gfx::setDepth(depth);
		gfx::sprite(SHEET_FRUIT, kSprites[which], x, y, scale, scale, sinf(sTime * 0.7f) * 0.15f);
	}
}

// ---------------------------------------------------------------- splash line
static const char* const kSplash[] = {
#include "splash_lines.inc"
};
static constexpr int kSplashCount = sizeof kSplash / sizeof kSplash[0];
static int sSplash = 0;

// A new line each time the title appears, never the same one twice running.
static void pickSplash() {
	static u32 seed = 0;
	seed = seed * 1664525u + 1013904223u + (u32)(sTime * 1000.0f);
	int next = (int)((seed >> 8) % kSplashCount);
	if (next == sSplash) next = (next + 1) % kSplashCount;
	sSplash = next;
}

// Tilted over the logo's lower right corner, gently pulsing. Text can't turn, so the letters step up
// along the slope instead. Called twice: every outline, then every fill, so neighbours never clip.
static void drawTilted(const char* s, int len, float x, float y, float scale, bool fill) {
	const float slope = -0.22f;
	char buf[64];
	if (len > 63) len = 63;
	memcpy(buf, s, len);
	buf[len] = 0;
	float cx = x - gfx::textWidth(buf, scale) * 0.5f;
	for (int i = 0; i < len; i++) {
		const float adv = gfx::glyphWidth(buf[i], scale);
		gfx::glyph(buf[i], cx + adv * 0.5f, y + (cx - x) * slope - 20 * scale, scale, scale, COL_ACCENT, fill);
		cx += adv;
	}
}

// A long line breaks in two at the space nearest its middle, so every line stays readable.
static void drawSplash(float x, float y) {
	const char* s = kSplash[sSplash];
	const float room = 140;
	int len = (int)strlen(s), cut = -1;
	float scale = 0.46f;
	if (gfx::textWidth(s, scale) > room) {
		for (int i = 0; i < len; i++)
			if (s[i] == ' ' && (cut < 0 || abs(i - len / 2) < abs(cut - len / 2))) cut = i;
	}
	float pulse = gSave.calmFx ? 1 : 1 + 0.06f * sinf(sTime * 5.5f);
	if (cut < 0) {
		float w = gfx::textWidth(s, scale);
		if (w > room) scale *= room / w;
		for (int pass = 0; pass < 2; pass++) drawTilted(s, len, x, y, scale * pulse, pass == 1);
		return;
	}
	char first[64];
	snprintf(first, sizeof first, "%.*s", cut, s);
	float w = fmaxf(gfx::textWidth(first, scale), gfx::textWidth(s + cut + 1, scale));
	if (w > room) scale *= room / w;
	float gap = 9 * scale / 0.42f;
	for (int pass = 0; pass < 2; pass++) {
		drawTilted(s, cut, x, y - gap, scale * pulse, pass == 1);
		drawTilted(s + cut + 1, len - cut - 1, x + 6, y + gap, scale * pulse, pass == 1);
	}
}

static void topPanel(float x, float y, float w, float h) {
	gfx::setDepth(0.3f);
	gfx::sign(sprites_plank_idx, x, y, w, h, COL_PLANK);
}

#if PORT_3DS
static void drawDebug() {
	char buf[64];
	gfx::setDepth(0);
	gfx::rect(4, 170, 310, 66, rgba(0, 0, 0, 170));
	snprintf(buf, sizeof buf, "FPS %.1f CPU %.0f%% GPU %.0f%%", sFps, C3D_GetProcessingTime() * 6.0f, C3D_GetDrawingTime() * 6.0f);
	gfx::text(buf, 8, 170, 0.36f, rgba(160, 255, 160));
	snprintf(buf, sizeof buf, "DRAWS %lu OBJ %d PART %d", (unsigned long)gfx::drawCalls, sGame.activeObjects(), sGame.activeParticles());
	gfx::text(buf, 8, 186, 0.36f, rgba(160, 255, 160));
	snprintf(buf, sizeof buf, "LINEAR FREE %luK CORE2 %s", (unsigned long)(linearSpaceFree() / 1024), jobs::threaded() ? "ON" : "OFF");
	gfx::text(buf, 8, 202, 0.36f, rgba(160, 255, 160));
	snprintf(buf, sizeof buf, "TOUCH %.0f,%.0f BLADE %.0f,%.0f", sBlade.rawX, sBlade.rawY, sBlade.x, sBlade.y);
	gfx::text(buf, 8, 218, 0.36f, rgba(160, 255, 160));
}
#endif

// ---------------------------------------------------------------- speed test
#if PORT_3DS
struct BenchProfile {
	const char* name;
	u8 art, effects, detail, bg, slice;
	bool fast, stereo;
};
static const BenchProfile kBench[] = {
	{"NEW 3D FULL", ART_3D, FX_FULL, 1, 1, 1, true, false},
	{"NEW 3D FULL STEREO", ART_3D, FX_FULL, 1, 1, 1, true, true},
	{"NEW 2D FULL STEREO", ART_2D, FX_FULL, 0, 1, 0, true, true},
	{"OLD 2D DEFAULT", ART_2D, FX_MEDIUM, 0, 0, 0, false, false},
	{"OLD 2D STEREO", ART_2D, FX_MEDIUM, 0, 0, 0, false, true},
	{"OLD 3D LOW STEREO", ART_3D, FX_LOW, 0, 0, 0, false, true},
};
static constexpr int kBenchCount = sizeof kBench / sizeof kBench[0];
static constexpr int kBenchSettle = 30;
static constexpr int kBenchFrames = 840;

struct BenchResult {
	float logicAvg, logicMax, cpuAvg, cpuMax, gpuAvg, gpuMax;
	int missed;
	u32 linearFree;
};
static BenchResult sBenchResults[kBenchCount];
static int sBenchIndex = 0;
static int sBenchFrame = 0;
static bool sBenchDone = false;
static bool sBenchSaved = false;
static Settings sBenchRestore;
static u64 sBenchLastTick = 0;
static float sBenchLogicMs = 0;

static void benchSetup() {
	const BenchProfile& p = kBench[sBenchIndex];
	gSave = sBenchRestore;
	gSave.artStyle = p.art;
	gSave.effects = p.effects;
	gSave.detail = p.detail;
	gSave.bgDetail = p.bg;
	gSave.sliceStyle = p.slice;
	gSave.depth = 50;
	gSave.difficulty = DIFF_INSANE;
	gSave.bombs = 0;
	gSave.sfx = gSave.music = 0;
	osSetSpeedupEnable(p.fast);
	jobs::setEnabled(p.fast);
	gfx::forceStereo(p.stereo);
	gRng.s = 0x51CE51CE;
	sGame.start(MODE_ARCADE);  // with a Fruit Frenzy held on below: Frenzy mode itself runs lighter here
	sBlade.reset();
	sBenchFrame = 0;
	sBenchResults[sBenchIndex] = {};
	sBenchResults[sBenchIndex].linearFree = 0xFFFFFFFFu;
}

static void startSpeedTest() {
	sBenchRestore = gSave;
	sBenchIndex = 0;
	sBenchDone = false;
	sBenchSaved = false;
	audio::music(MUSIC_NONE);
	audio::fuse(false);
	benchSetup();
	sScreen = SCREEN_BENCH;
}

static void benchWriteFile() {
	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/FruitSlicer", 0777);
	FILE* f = fopen("sdmc:/3ds/FruitSlicer/bench.txt", "w");
	sBenchSaved = f != nullptr;
	if (!f) return;
	u8 model = 255;
	CFGU_GetSystemModel(&model);
	fprintf(f, "Fruit Slicer 3DS %s speed test\nconsole model %d, new=%d, 3d screen=%d, core2=%d\n", VERSION_STRING, model, gIsNew3ds, gHas3dScreen, jobs::threaded());
	fprintf(f, "Each frame has 16.7 ms. OLD profiles run at 268 MHz, without L2 cache or the extra core.\n");
	fprintf(f, "profile, missed frames of %d, logic avg/max ms, draw avg/max ms, gpu avg/max ms, linear free KB\n", kBenchFrames);
	for (int i = 0; i < kBenchCount; i++) {
		const BenchResult& r = sBenchResults[i];
		fprintf(f, "%s, %d, %.1f/%.1f, %.1f/%.1f, %.1f/%.1f, %lu\n", kBench[i].name, r.missed, r.logicAvg / kBenchFrames, r.logicMax, r.cpuAvg / kBenchFrames, r.cpuMax,
		        r.gpuAvg / kBenchFrames, r.gpuMax, (unsigned long)(r.linearFree / 1024));
	}
	fclose(f);
}

static void benchFinish() {
	gSave = sBenchRestore;
	osSetSpeedupEnable(true);
	jobs::setEnabled(true);
	gfx::forceStereo(false);
	gfx::setShake(0, 0);
	audio::refreshVolumes();
	audio::music(MUSIC_MENU);
	benchWriteFile();
	sBenchDone = true;
}

static void benchMeasure() {
	u64 now = svcGetSystemTick();
	float frameMs = (now - sBenchLastTick) / (float)CPU_TICKS_PER_MSEC;
	sBenchLastTick = now;
	if (sBenchFrame < kBenchSettle) return;
	BenchResult& r = sBenchResults[sBenchIndex];
	float cpu = C3D_GetProcessingTime(), gpu = C3D_GetDrawingTime();
	r.logicAvg += sBenchLogicMs;
	r.cpuAvg += cpu;
	r.gpuAvg += gpu;
	if (sBenchLogicMs > r.logicMax) r.logicMax = sBenchLogicMs;
	if (cpu > r.cpuMax) r.cpuMax = cpu;
	if (gpu > r.gpuMax) r.gpuMax = gpu;
	if (frameMs > 17.7f) r.missed++;
	u32 freeNow = linearSpaceFree();
	if (freeNow < r.linearFree) r.linearFree = freeNow;
}

static void benchUpdate(const Input& hw) {
	if (sBenchDone) {
		if (hw.down & (KEY_A | KEY_B)) {
			audio::click();
			audio::music(MUSIC_MENU);
			sFocus[SCREEN_SECTION] = 0;
			sScreen = SCREEN_SECTION;
		}
		return;
	}
	if (hw.down & KEY_B) {  // leave without saving a partial report
		gSave = sBenchRestore;
		osSetSpeedupEnable(true);
		jobs::setEnabled(true);
		gfx::forceStereo(false);
		gfx::setShake(0, 0);
		audio::refreshVolumes();
		audio::music(MUSIC_MENU);
		sScreen = SCREEN_SECTION;
		return;
	}
	u64 start = svcGetSystemTick();
	Input in{};
	in.touching = true;
	in.tx = (int)(160 + sinf(sBenchFrame * 0.25f) * 150);
	in.ty = (int)(120 + sinf(sBenchFrame * 0.37f) * 100);
	sGame.timeLeft = 40;
	sGame.elapsed = 45;
	if (sGame.phase == PHASE_PLAY) sGame.feverT = 7;
	sBlade.update(in);
	sGame.update(sBlade);
	sGame.kickParticles();
	sBenchLogicMs = (svcGetSystemTick() - start) / (float)CPU_TICKS_PER_MSEC;
	if (++sBenchFrame >= kBenchFrames + kBenchSettle) {
		sBenchIndex++;
		if (sBenchIndex >= kBenchCount) benchFinish();
		else benchSetup();
	}
}

#endif  // PORT_3DS

// ---------------------------------------------------------------- screens: top
// ---------------------------------------------------------------- Daily Fruit, Fruit Journal, Stats
// Pick a Game, with Daily Fruit chosen: today's score, this week, and the streak.
static void drawDailyTop() {
	char buf[48];
	const u32 today = progress::today();
	gfx::text("Daily Fruit", 200, 100, 0.56f, COL_ACCENT, ALIGN_CENTER);
	if (progress::dailyPlayed(today)) snprintf(buf, sizeof buf, "TODAY'S BEST %lu", (unsigned long)progress::dailyScore(today));
	else snprintf(buf, sizeof buf, "First run today pays %lu coins", (unsigned long)progress::dailyBonusPreview(today));
	gfx::text(buf, 200, 126, 0.4f, COL_CREAM, ALIGN_CENTER);
	// A week of the player's own calendar: day 1 is the first day they played, and today's box moves along
	// the row. Day 8 starts the next row of seven. The day number on top, the score below once played.
	const int number = progress::dailyNumber(today);
	const int todayCell = (number - 1) % 7;
	static constexpr float kCell = 42, kGap = 4;
	const float x0 = 200 - (7 * kCell + 6 * kGap) / 2;
	for (int i = 0; i < 7; i++) {
		const u32 day = today - todayCell + i;
		const float x = x0 + i * (kCell + kGap), y = 150;
		const bool played = progress::dailyPlayed(day);
		const bool ahead = day > today;
		gfx::rect(x, y, kCell, 44, played ? withAlpha(COL_ACCENT, 0.3f) : rgba(0, 0, 0, ahead ? 30 : 60));
		if (day == today) {
			gfx::rect(x, y, kCell, 2, COL_ACCENT);
			gfx::rect(x, y + 42, kCell, 2, COL_ACCENT);
			gfx::rect(x, y, 2, 44, COL_ACCENT);
			gfx::rect(x + kCell - 2, y, 2, 44, COL_ACCENT);
		}
		snprintf(buf, sizeof buf, "%d", number - todayCell + i);
		gfx::text(buf, x + kCell / 2, y + 4, 0.3f, ahead ? withAlpha(COL_DIM, 0.5f) : COL_DIM, ALIGN_CENTER);
		if (played) {
			u32 sc = progress::dailyScore(day);
			if (sc >= 10000) snprintf(buf, sizeof buf, "%luK", (unsigned long)(sc / 1000));
			else snprintf(buf, sizeof buf, "%lu", (unsigned long)sc);
			gfx::text(buf, x + kCell / 2, y + 21, 0.36f, COL_CREAM, ALIGN_CENTER);
		}
	}
	snprintf(buf, sizeof buf, "DAY %d   STREAK %d   BEST %d", number, progress::dailyStreak(today), (int)gSave.dailyBestStreak);
	gfx::text(buf, 200, 204, 0.34f, COL_DIM, ALIGN_CENTER);
}

// The Fruit Journal: a sticker for every fruit, dark until the first one is sliced.
static constexpr int kJournalCols = 5, kJournalRowsShown = 4;
static constexpr float kJournalTop = 48, kJournalStep = 47;
static constexpr float kJournalWindowTop = kJournalTop - 2, kJournalWindowH = kJournalRowsShown * kJournalStep + 2;
static RowScroller sJournalList;
static Button journalButton(int i) {
	if (i == 0) return kBack;
	const int p = i - 1;
	return {10.0f + (p % kJournalCols) * 60, kJournalTop + (p / kJournalCols) * kJournalStep - sJournalList.pos, 58, 45, nullptr};
}
static int journalHiddenRows() { return (progress::journalPages() + kJournalCols - 1) / kJournalCols - kJournalRowsShown; }
static const u32 kUnfound = rgba(40, 28, 20, 225);

static void updateJournal(const Input& in, bool wasTouching) {
	const int n = progress::journalPages() + 1;
	if (rowScroll(sJournalList, in, wasTouching, kJournalWindowTop, kJournalWindowTop + kJournalWindowH, kJournalStep, journalHiddenRows())) sPressed = -1;
	Button btns[progress::kNamedFruit + 1];
	for (int i = 0; i < n; i++) btns[i] = journalButton(i);
	int& f = sFocus[SCREEN_JOURNAL];
	if (f < 0 || f >= n) f = 1;
	if (in.down & KEY_B) {
		audio::click();
		goTo(SCREEN_TITLE, -1);
		return;
	}
	bool leave;
	if (backCut(SCREEN_JOURNAL, in, wasTouching, &leave)) {
		if (leave) goTo(SCREEN_TITLE, -1);
		return;
	}
	dpadNav(btns, n, in.repeat, f);
	if ((in.repeat & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) && f > 0) rowReveal(sJournalList, (f - 1) / kJournalCols, kJournalRowsShown, kJournalStep);
	// Stickers scrolled out of the window can't be touched.
	if (in.ty < kJournalWindowTop)
		for (int i = 1; i < n; i++) btns[i] = {-100, -100, 0, 0, nullptr};
	int t = touchButtons(btns, n, in, wasTouching, f);
#if PORT_MOUSE
	if (in.hovering && !in.touching)
		for (int i = 0; i < n; i++)
			if (hit(btns[i], in.tx, in.ty)) f = i;
#endif
	if (t < 0 && (in.down & KEY_A) && f == 0) t = 0;
	if (t == 0) cutBack(SCREEN_JOURNAL);
}

static void drawJournal() {
	const int f = sFocus[SCREEN_JOURNAL];
	drawHeader("Fruit Journal");
	drawBackArrow(f == 0, sPressed == 0);
	char buf[12];
	gfx::setClip(0, kJournalWindowTop, 320, kJournalWindowH);
	for (int p = 0; p < progress::journalPages(); p++) {
		const Button b = journalButton(p + 1);
		if (b.y + b.h + 2 < kJournalWindowTop || b.y - 2 > kJournalWindowTop + kJournalWindowH) continue;
		const int fruit = progress::journalFruit(p);
		const u32 count = gSave.fruitSliced[fruit];
		const bool focused = f == p + 1;
		drawPlank(b.x, b.y, b.w, b.h, focused, sPressed == p + 1);
		const float cx = b.x + b.w * 0.5f;
		gfx::sprite(SHEET_FRUIT, fruitSprite(fruit), cx, b.y + 16, 0.56f, 0.56f, 0, count ? WHITE : kUnfound);
		if (count) snprintf(buf, sizeof buf, "%lu", (unsigned long)(count > 999999 ? 999999 : count));
		else snprintf(buf, sizeof buf, "?");
		gfx::text(buf, cx, b.y + 27, 0.28f, focused ? COL_ACCENT : COL_CREAM, ALIGN_CENTER);
		const int level = progress::journalLevel(count);
		for (int k = 0; k < progress::kJournalLevels; k++) gfx::rect(cx - 14 + k * 8, b.y + 39, 4, 3, k < level ? COL_ACCENT : rgba(0, 0, 0, 90));
	}
	gfx::clearClip();
	drawScrollMark(kJournalTop, kJournalRowsShown * kJournalStep - 2, sJournalList.pos, journalHiddenRows() * kJournalStep);
}

static void drawJournalTop() {
	char buf[48];
	const int f = sFocus[SCREEN_JOURNAL];
	topPanel(30, 8, 340, 224);
	gfx::setDepth(0.7f);
	if (f <= 0) {
		int found = 0, levels = 0;
		for (int p = 0; p < progress::journalPages(); p++) {
			u32 c = gSave.fruitSliced[progress::journalFruit(p)];
			found += c > 0;
			levels += progress::journalLevel(c);
		}
		gfx::text("Fruit Journal", 200, 40, 0.62f, COL_ACCENT, ALIGN_CENTER);
		snprintf(buf, sizeof buf, "FRUIT FOUND %d OF %d", found, progress::journalPages());
		gfx::text(buf, 200, 92, 0.44f, COL_CREAM, ALIGN_CENTER);
		snprintf(buf, sizeof buf, "MILESTONES %d OF %d", levels, progress::journalPages() * progress::kJournalLevels);
		gfx::text(buf, 200, 118, 0.44f, COL_CREAM, ALIGN_CENTER);
		gfx::text("Every milestone pays coins.", 200, 156, 0.38f, COL_DIM, ALIGN_CENTER);
		gfx::text("Back to the title", 200, 196, 0.42f, COL_CREAM, ALIGN_CENTER);
		return;
	}
	const int fruit = progress::journalFruit(f - 1);
	const u32 count = gSave.fruitSliced[fruit];
	gfx::setDepth(1.0f);
	gfx::sprite(SHEET_FRUIT, fruitSprite(fruit), 200, 58, 1.3f, 1.3f, gSave.calmFx ? 0 : sinf(sTime * 1.3f) * 0.12f, count ? WHITE : kUnfound);
	gfx::setDepth(0.7f);
	gfx::text(count ? progress::fruitName(fruit) : "Not found yet", 200, 94, 0.6f, COL_ACCENT, ALIGN_CENTER);
	snprintf(buf, sizeof buf, "SLICED %lu", (unsigned long)count);
	gfx::text(buf, 200, 124, 0.44f, COL_CREAM, ALIGN_CENTER);
	const int level = progress::journalLevel(count);
	for (int k = 1; k <= progress::kJournalLevels; k++) {
		const float x = 200 + (k - 2.5f) * 74;
		const bool reached = k <= level;
		gfx::sprite(SHEET_SPRITES, sprites_coin_idx, x, 162, 0.5f, 0.5f, 0, reached ? WHITE : rgba(90, 80, 70, 200));
		snprintf(buf, sizeof buf, "%lu", (unsigned long)progress::journalMilestone(k));
		gfx::text(buf, x, 174, 0.34f, reached ? COL_CREAM : COL_DIM, ALIGN_CENTER);
		snprintf(buf, sizeof buf, "+%lu", (unsigned long)progress::journalReward(k));
		gfx::text(buf, x, 188, 0.3f, reached ? COL_ACCENT : COL_DIM, ALIGN_CENTER);
	}
	if (level < progress::kJournalLevels)
		snprintf(buf, sizeof buf, "%lu more for %lu coins", (unsigned long)(progress::journalMilestone(level + 1) - count), (unsigned long)progress::journalReward(level + 1));
	else
		snprintf(buf, sizeof buf, "Every milestone reached!");
	gfx::text(buf, 200, 208, 0.36f, COL_CREAM, ALIGN_CENTER);
}

// Stats: the long view of everything played.
static void drawStats() {
	drawHeader("Stats");
	drawBackArrow(true, sPressed == 0);
	gfx::sign(sprites_plank_idx, 8, 48, 304, 186, COL_PLANK);
	char value[6][32];
	snprintf(value[0], 32, "%lu", (unsigned long)gSave.totalFruit);
	snprintf(value[1], 32, "%lu", (unsigned long)gSave.coinsLifetime);
	const unsigned long secs = gSave.playSeconds;
	if (secs >= 3600) snprintf(value[2], 32, "%luh %02lum", secs / 3600, secs / 60 % 60);
	else snprintf(value[2], 32, "%lum", secs / 60);
	const Mode fav = progress::favouriteMode();
	snprintf(value[3], 32, "%s", fav < MODE_COUNT ? modeName(fav) : "None yet");
	snprintf(value[4], 32, "%lu", (unsigned long)gSave.bestCombo);
	snprintf(value[5], 32, "%d (best %d)", progress::dailyStreak(progress::today()), (int)gSave.dailyBestStreak);
	static const char* const kNames[6] = {"FRUIT SLICED", "COINS EARNED", "TIME PLAYED", "FAVOURITE MODE", "LONGEST COMBO", "DAILY STREAK"};
	for (int i = 0; i < 6; i++) {
		const float y = 60 + i * 28;
		gfx::text(kNames[i], 26, y + 2, 0.32f, COL_DIM);
		float s = 0.42f, w = gfx::textWidth(value[i], s);
		if (w > 150) s *= 150 / w;
		gfx::text(value[i], 294, y, s, COL_CREAM, ALIGN_RIGHT);
	}
}

static void drawStatsTop() {
	char buf[40];
	gfx::setDepth(1.0f);
	gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 46, 0.56f, 0.56f);
	topPanel(30, 86, 340, 146);
	gfx::setDepth(0.7f);
	gfx::text("BEST SCORES", 200, 92, 0.4f, COL_ACCENT, ALIGN_CENTER);
	for (int m = 0; m < MODE_COUNT; m++) {
		const float x = m < 5 ? 46 : 214, y = 114 + (m % 5) * 22;
		gfx::text(kModes[m].name, x, y, 0.34f, COL_DIM);
		snprintf(buf, sizeof buf, "%lu", (unsigned long)gSave.best[m]);
		gfx::text(buf, x + 140, y - 1, 0.38f, COL_CREAM, ALIGN_RIGHT);
	}
}

// ---------------------------------------------------------------- Beat Slice track choice
// The game's tracks you have, then any songs of your own (Windows, Linux and Android).
static bool beatChoice(int t) { return t < kTrackCount ? trackUnlocked(t) && audio::trackHasBeat(t) : t < audio::trackCount(); }

static int beatSong() {
	int t = gSave.beatTrack ? gSave.beatTrack - 1 : 1;
	return beatChoice(t) ? t : 1;
}

// Every track in the list, in order. Returns how many.
static int beatList(int* ids) {
	int n = 0;
	for (int i = 0; i < audio::trackCount(); i++)
		if (beatChoice(i)) ids[n++] = i;
	return n;
}

// Keeps the chosen track's row in view.
static void beatScrollTo(int t) {
	static int ids[kTrackCount + 64];
	const int n = beatList(ids);
	int at = 0;
	for (int i = 0; i < n; i++)
		if (ids[i] == t) at = i;
	if (at < sBeatScroll) sBeatScroll = at;
	if (at >= sBeatScroll + kBeatRows) sBeatScroll = at - kBeatRows + 1;
	if (sBeatScroll > n - kBeatRows) sBeatScroll = n - kBeatRows;
	if (sBeatScroll < 0) sBeatScroll = 0;
}

#if PORT_CUSTOM_MUSIC
// A song of your own is listened to for its beat as soon as it is picked, if it has none yet.
static int sListenSong = -1;   // which of your songs is being listened to, or -1
static int sListenFailed = -1; // the last one that had no steady beat
static float sListenP = 0;

static void listenStop() {
	if (sListenSong >= 0) custom::analyzeCancel();
	sListenSong = -1;
}

static void listenFor(int t) {
	listenStop();
	if (t < kTrackCount || audio::trackHasBeat(t) || t - kTrackCount == sListenFailed) return;
	if (custom::analyzeStart(t - kTrackCount)) {
		sListenSong = t - kTrackCount;
		sListenP = 0;
	} else {
		sListenFailed = t - kTrackCount;
	}
}

static void listenTick() {
	if (sListenSong < 0) return;
	const float p = custom::analyzeStep();
	if (p >= 1) {
		sListenSong = -1;  // found, and kept in beats.txt
	} else if (p < 0) {
		sListenFailed = sListenSong;
		sListenSong = -1;
	} else {
		sListenP = p;
	}
}
#endif

// Picks a track: it plays from its start, and comes into view.
static void beatSelect(int t) {
	gSave.beatTrack = (u8)(t + 1);
	audio::click();
	audio::musicBeat(t);
	beatScrollTo(t);
#if PORT_CUSTOM_MUSIC
	listenFor(t);
#endif
}

// The next track you have, either way round.
static void beatPick(int dir) {
	int t = beatSong();
	const int count = audio::trackCount();
	for (int i = 0; i < count; i++) {
		t = (t + dir + count) % count;
		if (beatChoice(t)) break;
	}
	beatSelect(t);
}

#if PORT_CUSTOM_MUSIC
// A small plank that acts the moment it is tapped: its label fills it, as there is no fruit to fit around.
static void drawTap(const Button& b, const char* label) {
	drawPlank(b.x, b.y, b.w, b.h, false, false);
	float s = 0.36f, w = gfx::textWidth(label, s);
	if (w > b.w - 10) s *= (b.w - 10) / w;
	gfx::text(label, b.x + b.w * 0.5f, b.y + b.h * 0.5f - 20 * s - 1, s, COL_CREAM, ALIGN_CENTER);
}
#endif

static void drawBeatScreen() {
	char buf[32];
	const int f = sFocus[SCREEN_BEAT];
	drawHeader("Beat Slice");
	drawBackArrow(f == 0, sPressed == 0);
	const int t = beatSong();
	static int ids[kTrackCount + 64];
	const int n = beatList(ids);
	// The list: the chosen track lit, each row with its tempo, or how listening for it is going.
	for (int r = 0; r < kBeatRows && sBeatScroll + r < n; r++) {
		const int id = ids[sBeatScroll + r];
		const Button b = beatRow(r);
		const bool on = id == t;
		drawPlank(b.x, b.y, b.w, b.h, on, false);
		const bool own = id >= kTrackCount;
		const char* name = kTrackRowNames[own ? 0 : id + 1];
		buf[0] = 0;
		if (audio::trackHasBeat(id)) snprintf(buf, sizeof buf, "%d", (int)(60 / audio::trackBeat(id) + 0.5f));
#if PORT_CUSTOM_MUSIC
		if (own) {
			name = custom::name(id - kTrackCount);
			gfx::sprite(SHEET_SPRITES, sprites_ic_note_idx, b.x + 14, b.y + b.h * 0.5f, 0.5f, 0.5f, 0, on ? COL_ACCENT : COL_CREAM);
			if (sListenSong == id - kTrackCount) snprintf(buf, sizeof buf, "%d%%", (int)(sListenP * 100));
			else if (sListenFailed == id - kTrackCount && !audio::trackHasBeat(id)) snprintf(buf, sizeof buf, "NO BEAT");
			else if (!audio::trackHasBeat(id)) snprintf(buf, sizeof buf, "NEW");
		}
#endif
		const float bw = buf[0] ? gfx::textWidth(buf, 0.3f) + 8 : 0;
		float s = 0.34f, w = gfx::textWidth(name, s), room = b.w - 34 - bw;
		if (w > room) s *= room / w;
		gfx::text(name, b.x + 28, b.y + b.h * 0.5f - 20 * s - 1, s, on ? COL_ACCENT : COL_CREAM, ALIGN_LEFT);
		if (buf[0]) gfx::text(buf, b.x + b.w - 8, b.y + b.h * 0.5f - 7, 0.3f, COL_DIM, ALIGN_RIGHT);
	}
	if (n > kBeatRows) {
		for (int side = 0; side < 2; side++) {
			const Button& b = side ? kBeatDown : kBeatUp;
			const bool can = side ? sBeatScroll + kBeatRows < n : sBeatScroll > 0;
			drawPlank(b.x, b.y, b.w, b.h, false, false);
			gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, b.x + b.w * 0.5f, b.y + b.h * 0.5f, 0.45f, 0.45f, side ? -1.5707963f : 1.5707963f, can ? COL_ACCENT : withAlpha(COL_CREAM, 0.3f));
		}
	}
	const char* play = "Play";
#if PORT_CUSTOM_MUSIC
	if (sListenSong >= 0) play = "Listening";
	else if (!audio::trackHasBeat(t)) play = "Find Beat";
#endif
	drawSign(kBeatButtons[1], play, f == 1, sPressed == 1, 0.5f, -1, fruitFor("Play"));
	const char* hint = n - (audio::trackCount() - kTrackCount) < kTrackCount ? "Tracks from the Shop play here too." : "Every track is yours.";
#if PORT_CUSTOM_MUSIC
	if (t >= kTrackCount) drawTap(kBeatTool, kBeatTool.label);
	drawTap(kBeatFolder, kBeatFolder.label);
	if (custom::count() == 0) hint = "Add .mp3, .ogg, .flac or .wav songs to the Custom Music folder.";
	if (sFolderNote > 0) hint = custom::folder();
#endif
	float s = 0.28f, w = gfx::textWidth(hint, s);
	if (w > 304) s *= 304 / w;
	gfx::text(hint, 160, 224, s, COL_DIM, ALIGN_CENTER);
}

#if PORT_CUSTOM_MUSIC
// ---------------------------------------------------------------- Beat Tool
// Finds the beat of one of your songs, then lets you fix it by ear: a click plays on every beat it has, and
// the tempo and the first beat move until the clicks land with the music.
static struct {
	int song;          // which of your songs
	float bpm, first;  // the beat so far: tempo, and seconds to the first beat of a bar
	bool has;          // a beat has been found or set
	bool dirty;        // changed since it was last kept
	bool click;        // a click on every beat
	bool listening;    // the analysis is running
	bool failed;       // it ran and found no steady beat
	float progress;
	int lastBeat;
} sTool;

static void toolKeep() {
	if (sTool.listening) custom::analyzeCancel();
	sTool.listening = false;
	if (sTool.has && sTool.dirty) custom::setBeat(sTool.song, sTool.bpm, sTool.first);
	sTool.dirty = false;
}

static void toolListen() {
	sTool.listening = custom::analyzeStart(sTool.song);
	sTool.failed = !sTool.listening;
	sTool.progress = 0;
}

static void openBeatTool(int song) {
	static bool once;
	if (!once) sTool.click = once = true;  // the click is how the beat is checked, so it starts on
	sTool.song = song;
	sTool.has = custom::beat(song, &sTool.bpm, &sTool.first);
	sTool.dirty = false;
	sTool.failed = false;
	sTool.listening = false;
	sTool.lastBeat = -1;
	if (!sTool.has) toolListen();
	sFocus[SCREEN_BEATTOOL] = 1;
	goTo(SCREEN_BEATTOOL, 1);
}

// Keeps the first beat within one bar, so nudges wrap round instead of running off.
static void toolSetFirst(float first) {
	const float bar = 4 * 60 / sTool.bpm;
	sTool.first = first - floorf(first / bar) * bar;
}

static void toolChange(int what) {
	audio::click();
	if (what == 6) {
		sTool.click = !sTool.click;
		return;
	}
	if (what == 8) {
		toolListen();
		return;
	}
	if (sTool.listening) return;
	if (!sTool.has) {  // nothing found: start from a common tempo and set it by ear
		sTool.bpm = 120;
		sTool.first = 0;
		sTool.has = true;
	}
	switch (what) {
		case 0: sTool.bpm -= 0.5f; break;
		case 1: sTool.bpm += 0.5f; break;
		case 2: sTool.bpm *= 0.5f; break;
		case 3: sTool.bpm *= 2; break;
		case 4: sTool.first -= 0.01f; break;
		case 5: sTool.first += 0.01f; break;
		case 7: sTool.first += 60 / sTool.bpm; break;  // the bar starts on the next beat
	}
	sTool.bpm = clampf(sTool.bpm, 40, 240);
	toolSetFirst(sTool.first);
	sTool.failed = false;
	sTool.dirty = true;
}

// Runs every frame on the Beat Tool: the analysis a few milliseconds at a time, and the click.
static void toolTick() {
	if (sTool.listening) {
		const float p = custom::analyzeStep();
		if (p >= 1) {
			sTool.listening = false;
			sTool.has = custom::beat(sTool.song, &sTool.bpm, &sTool.first);
			sTool.dirty = false;
			sTool.lastBeat = -1;
		} else if (p < 0) {
			sTool.listening = false;
			sTool.failed = true;
		} else {
			sTool.progress = p;
		}
	}
	const float t = audio::musicTime();
	if (!sTool.has || t < 0) return;
	const int beat = (int)floorf((t - sTool.first) * sTool.bpm / 60);
	if (beat != sTool.lastBeat && sTool.lastBeat != -1 && sTool.click) audio::play(SFX_CLICK);
	sTool.lastBeat = beat;
}

static void drawBeatTool() {
	char buf[48];
	const int f = sFocus[SCREEN_BEATTOOL];
	drawHeader("Beat Tool");
	drawBackArrow(f == 0, sPressed == 0);
	gfx::sign(sprites_plank_idx, 30, 44, 260, 62, COL_PLANK);
	const char* name = custom::name(sTool.song);
	float s = 0.4f, w = gfx::textWidth(name, s);
	if (w > 236) s *= 236 / w;
	gfx::text(name, 160, 50, s, COL_ACCENT, ALIGN_CENTER);
	if (sTool.listening) {
		gfx::text("Listening for the beat", 160, 70, 0.34f, COL_CREAM, ALIGN_CENTER);
		gfx::rect(60, 90, 200, 6, withAlpha(COL_CREAM, 0.25f));
		gfx::rect(60, 90, 200 * sTool.progress, 6, COL_ACCENT);
	} else if (sTool.has) {
		snprintf(buf, sizeof buf, "%.1f BPM", sTool.bpm);
		gfx::text(buf, 112, 72, 0.44f, COL_CREAM, ALIGN_CENTER);
		// Four lights, as in the game: the bar the song is in, the first beat in the accent colour.
		const float t = audio::musicTime();
		const float beats = t >= 0 ? (t - sTool.first) * sTool.bpm / 60 : 0;
		const int now = ((int)floorf(beats) % 4 + 4) % 4;
		const float fresh = t >= 0 ? 1 - (beats - floorf(beats)) : 0;
		for (int i = 0; i < 4; i++) {
			const bool lit = t >= 0 && i == now;
			const float size = lit ? 9 + 4 * fresh * fresh : 8;
			const float cx = 196 + i * 18;
			gfx::rect(cx - size * 0.5f, 80 - size * 0.5f, size, size, lit ? (i == 0 ? COL_ACCENT : COL_CREAM) : withAlpha(COL_CREAM, 0.3f));
		}
		snprintf(buf, sizeof buf, "FIRST BEAT AT %.2f S%s", sTool.first, sTool.dirty ? ", CHANGED" : "");
		gfx::text(buf, 160, 92, 0.28f, COL_DIM, ALIGN_CENTER);
	} else {
		gfx::text(sTool.failed ? "No steady beat found." : "Can't read this song.", 160, 68, 0.34f, COL_CREAM, ALIGN_CENTER);
		gfx::text("Slower or Faster sets one by ear.", 160, 88, 0.3f, COL_DIM, ALIGN_CENTER);
	}
	for (int i = 0; i < kToolTapCount; i++) {
		const Button& b = kToolTaps[i];
		const char* label = i == 6 ? (sTool.click ? "Click On" : "Click Off") : b.label;
		drawTap(b, label);
	}
	drawSign(kToolButtons[1], "Play", f == 1, sPressed == 1, 0.56f, -1, fruitFor("Play"));
}
#endif

// ---------------------------------------------------------------- update sign
// What update.cpp has found or done, as a screen of its own: the changelog goes on the top screen (on one
// screen, the side panel), the choice below it. The answers are fruit like any other sign.
static Screen sUpdateBack = SCREEN_TITLE;  // where the sign goes back to
static bool sUpdateQuit = false;           // Restart was chosen: the game closes
static int sUpdateScroll = 0;              // first changelog line shown
static const Button kUpdateTwo[] = {{24, 150, 128, 64, nullptr}, {168, 150, 128, 64, nullptr}};
static const Button kUpdateOne[] = {{96, 150, 128, 64, nullptr}};
static constexpr int kNoteLines = 9, kNoteStep = 14;

// The changelog, wrapped to the panel: up to 200 lines, each a start and a length.
static int wrapNotes(const char** starts, int* lens, int maxLines) {
	const char* p = update::notes();
	int n = 0;
	while (*p && n < maxLines) {
		const char* end = strchr(p, '\n');
		if (!end) end = p + strlen(p);
		if (end == p) {  // a blank line stays, once
			if (n > 0 && lens[n - 1] > 0) {
				starts[n] = p;
				lens[n++] = 0;
			}
			p++;
			continue;
		}
		while (p < end && n < maxLines) {
			// As many words as fit in 316 units at the panel's text size.
			int len = (int)(end - p), fit = len;
			char buf[200];
			while (fit > 0) {
				snprintf(buf, sizeof buf, "%.*s", fit, p);
				if (gfx::textWidth(buf, 0.34f) <= 316) break;
				int back = fit - 1;
				while (back > 0 && p[back] != ' ') back--;
				fit = back > 0 ? back : fit - 1;
			}
			if (fit <= 0) fit = 1;
			starts[n] = p;
			lens[n++] = fit;
			p += fit;
			while (p < end && *p == ' ') p++;
		}
		p = *end ? end + 1 : end;
	}
	return n;
}

static int updateButtons(const Button** out, const char** a, const char** b) {
	*b = nullptr;
	switch (update::state()) {
		case update::ASK: *a = "Yes"; *b = "No"; break;
#if PORT_3DS
		case update::OFFER: *a = "Install Now"; *b = "Later"; break;
#else
		case update::OFFER: *a = "Update Now"; *b = "Later"; break;
#endif
		case update::DOWNLOADING: return 0;
#if PORT_3DS
		case update::READY: *a = "Close"; break;
#elif PORT_ANDROID
		case update::READY: *a = "OK"; break;
#else
		case update::READY: *a = "Restart"; break;
#endif
		default: *a = "OK"; break;
	}
	*out = *b ? kUpdateTwo : kUpdateOne;
	return *b ? 2 : 1;
}

static void drawUpdateSign() {
	char buf[64];
	const int f = sFocus[SCREEN_UPDATE];
	const char* head = "Updates";
	const char* l1 = "";
	const char* l2 = "";
	switch (update::state()) {
		case update::ASK:
			head = "Check for Updates?";
			l1 = "The game can look on GitHub once a day";
			l2 = "for a new version. Options can change this.";
			break;
		case update::OFFER:
		case update::NOTE:
			snprintf(buf, sizeof buf, "Version %s", update::newVersion());
			head = buf;
#if PORT_3DS
			l1 = update::state() == update::OFFER ? "A new version is ready to install." : "A new version is on GitHub.";
			l2 = update::state() == update::OFFER ? "What's new is on the top screen." : "Get the .cia from the game's GitHub page.";
#else
			l1 = update::state() == update::OFFER ? "A new version is ready to install." : "A new version is on GitHub.";
			l2 = update::state() == update::OFFER ? "Your scores and settings stay." : "This copy can't update itself where it is.";
#endif
			break;
		case update::DOWNLOADING:
			head = "Updating";
			l1 = "Downloading the new version.";
			l2 = "Keep the game open until it's done.";
			break;
		case update::READY:
			head = "Update Ready";
#if PORT_3DS
			l1 = envIsHomebrew() ? "Downloaded. The game closes to the" : "Installed. The game closes to the";
			l2 = envIsHomebrew() ? "Homebrew Launcher; open it again to play." : "HOME Menu; open it again to play.";
#elif PORT_ANDROID
			l1 = "Android will ask to install it.";
			l2 = "Allow it, then open the game again.";
#else
			l1 = "Restart to play the new version.";
			l2 = "Your scores and settings stay.";
#endif
			break;
		case update::LATEST:
			head = "Up to Date";
			l1 = "This is the newest version.";
			break;
		case update::FAILED:
			head = "No Update";
			l1 = update::failedDownload() ? "The download didn't finish." : "GitHub didn't answer.";
			l2 = update::failedDownload() ? "Nothing was changed." : "Check the connection and try again.";
			break;
		default: break;
	}
	drawHeader(head);
	gfx::sign(sprites_plank_idx, 20, 52, 280, 78, COL_PLANK);
	gfx::text(l1, 160, 66, 0.36f, COL_CREAM, ALIGN_CENTER);
	gfx::text(l2, 160, 88, 0.36f, COL_DIM, ALIGN_CENTER);
	if (update::state() == update::DOWNLOADING) {
		gfx::rect(58, 108, 204, 12, gfx::themeBar());
		gfx::rect(60, 110, 200 * clampf(update::progress(), 0, 1), 8, COL_ACCENT);
		return;
	}
	const Button* btns;
	const char* a;
	const char* b;
	const int n = updateButtons(&btns, &a, &b);
	for (int i = 0; i < n; i++) drawSign(btns[i], i ? b : a, f == i, sPressed == i, 0.5f, -1, fruitFor(i ? b : a));
}

// The top screen: the changelog of the new version, scrolled with Up and Down.
static void drawUpdateTop() {
	gfx::setDepth(1.0f);
	gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 40, 0.5f, 0.5f);
	topPanel(30, 70, 340, 162);
	gfx::setDepth(0.7f);
	const update::State st = update::state();
	if (st != update::OFFER && st != update::NOTE) {
		gfx::text(st == update::ASK ? "Nothing about you is sent:" : "Fruit Slicer", 200, 120, 0.4f, COL_ACCENT, ALIGN_CENTER);
		gfx::text(st == update::ASK ? "only a request for the newest release." : "Version " VERSION_STRING, 200, 144, 0.36f, COL_CREAM, ALIGN_CENTER);
		return;
	}
	char buf[64];
	snprintf(buf, sizeof buf, "WHAT'S NEW IN %s", update::newVersion());
	gfx::text(buf, 200, 76, 0.38f, COL_ACCENT, ALIGN_CENTER);
	static const char* starts[200];
	static int lens[200];
	const int n = wrapNotes(starts, lens, 200);
	if (sUpdateScroll > n - kNoteLines) sUpdateScroll = n - kNoteLines;
	if (sUpdateScroll < 0) sUpdateScroll = 0;
	for (int i = 0; i < kNoteLines && sUpdateScroll + i < n; i++) {
		char line[200];
		snprintf(line, sizeof line, "%.*s", lens[sUpdateScroll + i], starts[sUpdateScroll + i]);
		gfx::text(line, 42, 96 + i * kNoteStep, 0.34f, COL_CREAM, ALIGN_LEFT);
	}
	if (n > kNoteLines) {
		snprintf(buf, sizeof buf, "%s  LINES %d-%d OF %d", PORT_3DS ? "UP/DOWN OR A DRAG SCROLLS" : "WHEEL OR DRAG TO SCROLL", sUpdateScroll + 1, sUpdateScroll + kNoteLines, n);
		gfx::text(buf, 200, 222, 0.28f, COL_DIM, ALIGN_CENTER);
	}
}

// Brings the sign up when there's something to say, on a screen where it fits: first-start questions and
// offers on the title screen, the answer to Check Now in Options. Never during a run.
static void updateWatch() {
	update::tick();
	if (sScreen == SCREEN_UPDATE || sSliding || sFade > 0 || !update::wantsSign()) return;
	const update::State st = update::state();
	const bool here = update::manual() ? sScreen == SCREEN_SECTION : sScreen == SCREEN_TITLE;
	if (!here || st == update::DOWNLOADING || st == update::READY) return;
	sUpdateBack = sScreen;
	sUpdateScroll = 0;
	sFocus[SCREEN_UPDATE] = 0;
	goTo(SCREEN_UPDATE, 1);
}

static void updateSignInput(const Input& in, bool wasTouching) {
	if (in.repeat & KEY_UP) sUpdateScroll--;
	if (in.repeat & KEY_DOWN) sUpdateScroll++;
	sUpdateScroll -= in.wheel;
	if (sUpdateScroll < 0) sUpdateScroll = 0;
	Input keys = in;
	keys.repeat &= ~(u32)(KEY_UP | KEY_DOWN);
	const Button* btns;
	const char* a;
	const char* b;
	const int n = updateButtons(&btns, &a, &b);
	// A drag that starts clear of the signs moves the notes; the top line of the list is clamped when drawn.
	static ListDrag drag;
	static float pos;
	bool onSign = false;
	for (int i = 0; i < n; i++) onSign |= hit(btns[i], in.tx, in.ty);
	if (in.touching && !wasTouching) pos = sUpdateScroll * (float)kNoteStep;
	if (listDrag(drag, in, wasTouching, onSign ? 1e6f : -1e6f, 1e6f, &pos)) {
		sUpdateScroll = pos > 0 ? (int)(pos / kNoteStep + 0.5f) : 0;
		keys.touching = false;
	}
	if (n == 0) return;  // downloading: nothing to press
	int t = menuInput(btns, n, SCREEN_UPDATE, keys, wasTouching);
	if (in.down & KEY_B) {
		audio::click();
		t = n - 1;  // the second answer, or the only one
	}
	if (t < 0) return;
	const update::State st = update::state();
	if (st == update::ASK) update::consent(t == 0);
	else if (st == update::OFFER && t == 0) {
		update::install();
		return;  // stays here while it downloads
	} else if (st == update::OFFER) update::later();
	else if (st == update::READY) {
		update::restart();
#if !PORT_ANDROID
		sUpdateQuit = true;  // the port starts the new version (or goes back to HOME) once the game has closed
#endif
		return;
	} else update::dismiss();
	goTo(sUpdateBack, -1);
}

static void skipPractice() {
	sGame.commitScore();
	toModes();
}

// ---------------------------------------------------------------- Latency Test
// The music stops and a metronome ticks at 120 BPM. One fruit sits in the middle and never breaks: each
// swipe across it (or each press of A) is one slice, timed against the tick nearest to it. The first few
// are for settling in; the middle value of the next 16 becomes Beat Timing, which moves Beat Slice's beat.

static constexpr float kLatBeat = 0.5f;   // seconds between ticks
static constexpr float kLatLeadIn = 1.0f; // quiet before the first tick
static constexpr int kLatSettle = 4;      // slices that don't count
static constexpr int kLatTake = 16;       // slices that do
static constexpr int kLatMarks = 8;       // recent slices shown on the ruler
static constexpr float kLatX = 160, kLatY = 112, kLatSize = 70, kLatReach = 44;
static const Button kLatButtons[] = {kBack, {20, 170, 134, 52, "Keep"}, {166, 170, 134, 52, "Try Again"}};

static float sLatT, sLatNext, sLatPlayed;  // the test's clock, the next tick, when the last one played
static int sLatTicks, sLatSlices;          // ticks played, slices made (settling in included)
static float sLatTaken[kLatTake];
static float sLatMarks[kLatMarks];         // newest first
static int sLatMarkCount;
static float sLatBump;                     // the fruit's jolt from the last slice
static u32 sLatStroke;
static bool sLatDone;
static float sLatResult;                   // seconds, once done

// The middle of the slices taken, which ignores a stray early or late one. Sorts v in place.
static float latencyMedian(float* v, int n) {
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && v[j] < v[j - 1]; j--) {
			const float t = v[j];
			v[j] = v[j - 1];
			v[j - 1] = t;
		}
	return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5f;
}

static void latencyReset() {
	sLatT = 0;
	sLatNext = kLatLeadIn;
	sLatPlayed = -1;
	sLatTicks = sLatSlices = sLatMarkCount = 0;
	sLatBump = 0;
	sLatStroke = sMenuBlade.strokeId;
	sLatDone = false;
	sLatResult = 0;
}

static void startLatencyTest() {
	latencyReset();
	audio::music(MUSIC_NONE);
	audio::fuse(false);
	sScreen = SCREEN_LATENCY;
	sFocus[SCREEN_LATENCY] = 1;
	sPressed = -1;
}

static void leaveLatencyTest() {
	audio::music(MUSIC_MENU);
	sScreen = SCREEN_SECTION;
	sPressed = -1;
}

// One slice at the test's current time: how far from the nearest tick, negative when early.
static void latencySlice() {
	if (sLatPlayed < 0 || sLatDone) return;
	const float before = sLatT - sLatPlayed, after = sLatT - (sLatPlayed + kLatBeat);
	const float off = fabsf(before) <= fabsf(after) ? before : after;
	for (int i = kLatMarks - 1; i > 0; i--) sLatMarks[i] = sLatMarks[i - 1];
	sLatMarks[0] = off;
	if (sLatMarkCount < kLatMarks) sLatMarkCount++;
	if (sLatSlices >= kLatSettle) sLatTaken[sLatSlices - kLatSettle] = off;
	sLatSlices++;
	sLatBump = 1;
	audio::play(SFX_CRISP0, 0.45f);
	if (sLatSlices >= kLatSettle + kLatTake) {
		float v[kLatTake];
		memcpy(v, sLatTaken, sizeof v);
		sLatResult = latencyMedian(v, kLatTake);
		sLatDone = true;
		sFocus[SCREEN_LATENCY] = 1;
	}
}

static float segmentReach(float ax, float ay, float bx, float by, float px, float py) {
	const float dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
	const float t = len > 0 ? clampf(((px - ax) * dx + (py - ay) * dy) / len, 0, 1) : 0;
	const float cx = ax + dx * t - px, cy = ay + dy * t - py;
	return sqrtf(cx * cx + cy * cy);
}

static void updateLatency(const Input& in, bool wasTouching, bool blocked) {
	sLatBump = fmaxf(0, sLatBump - kDt * 5);
	if (!sLatDone) {
		sLatT += kDt;
		if (sLatT >= sLatNext) {
			audio::play(SFX_KNOCK, 1.0f, 0, sLatTicks % 4 ? 1.5f : 1.85f);  // tick, with a higher one on each bar
			sLatPlayed = sLatT;
			sLatNext += kLatBeat;
			sLatTicks++;
		}
	}
	if (blocked) return;
	if (sLatDone) {
		const int t = menuInput(kLatButtons, 3, SCREEN_LATENCY, in, wasTouching);
		if (t == 0 || (t < 0 && (in.down & KEY_B))) {
			if (t < 0) audio::click();
			fadeThen(leaveLatencyTest);
		} else if (t == 1) {
			gSave.beatLatency = beatLatencyStore(sLatResult);
			sDirty = true;
			fadeThen(leaveLatencyTest);
		} else if (t == 2) {
			latencyReset();
		}
		return;
	}
	// While the test runs, A is a slice, so only B and the back arrow leave.
	if ((in.down & KEY_B) || (in.touching && !wasTouching && hit(kBack, in.tx, in.ty))) {
		audio::click();
		fadeThen(leaveLatencyTest);
		return;
	}
	if (in.down & KEY_A) latencySlice();
	const Blade& b = sMenuBlade;
	if (b.active && b.strokeId != sLatStroke && segmentReach(b.px, b.py, b.x, b.y, kLatX, kLatY) < kLatReach) {
		sLatStroke = b.strokeId;  // one slice per swipe
		latencySlice();
	}
}

static int latencyMs(float sec) { return (int)(sec * 1000 + (sec < 0 ? -0.5f : 0.5f)); }

static void drawLatencyTop() {
	char buf[48];
	topPanel(30, 96, 340, 134);
	gfx::setDepth(1.0f);
	gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
	gfx::setDepth(0.7f);
	gfx::text("Latency Test", 200, 104, 0.6f, COL_ACCENT, ALIGN_CENTER);
	if (sLatDone) {
		gfx::text("Keep it to use it in Beat Slice,", 200, 134, 0.38f, COL_CREAM, ALIGN_CENTER);
		gfx::text("or try again for a steadier one.", 200, 152, 0.38f, COL_CREAM, ALIGN_CENTER);
		const int now = (gSave.beatLatency - 100) * 2;
		snprintf(buf, sizeof buf, now > 0 ? "NOW +%d MS" : "NOW %d MS", now);
		gfx::text(buf, 200, 186, 0.44f, COL_DIM, ALIGN_CENTER);
		return;
	}
	gfx::text("Slice the fruit on every tick:", 200, 134, 0.38f, COL_CREAM, ALIGN_CENTER);
	gfx::text("swipe across it, or press A.", 200, 152, 0.38f, COL_CREAM, ALIGN_CENTER);
	if (sLatSlices < kLatSettle) snprintf(buf, sizeof buf, "SETTLING IN: %d MORE", kLatSettle - sLatSlices);
	else snprintf(buf, sizeof buf, "SLICE %d OF %d", sLatSlices - kLatSettle + 1, kLatTake);
	gfx::text(buf, 200, 182, 0.5f, COL_CREAM, ALIGN_CENTER);
	if (gSave.sfx < 20) gfx::text("Turn Sound Effects up to hear the ticks.", 200, 206, 0.34f, COL_ACCENT, ALIGN_CENTER);
	else gfx::text("Go by the sound. The music is off.", 200, 206, 0.34f, COL_DIM, ALIGN_CENTER);
}

static void drawLatencyBottom() {
	char buf[48];
	drawMenuBack();
	const int f = sFocus[SCREEN_LATENCY];
	drawHeader("Latency Test");
	drawBackArrow(f == 0 && sLatDone, sPressed == 0);
	if (sLatDone) {
		const int ms = latencyMs(sLatResult);
		gfx::sign(sprites_plank_idx, 20, 52, 280, 100, COL_PLANK);
		gfx::text("Your slices land", 160, 62, 0.4f, COL_CREAM, ALIGN_CENTER);
		snprintf(buf, sizeof buf, ms > 0 ? "+%d ms" : "%d ms", ms);
		gfx::text(buf, 160, 84, 0.8f, COL_ACCENT, ALIGN_CENTER);
		gfx::text(ms > 4 ? "after the beat" : (ms < -4 ? "before the beat" : "right on the beat"), 160, 122, 0.4f, COL_CREAM, ALIGN_CENTER);
		for (int i = 1; i < 3; i++) drawSign(kLatButtons[i], kLatButtons[i].label, f == i, sPressed == i, 0.56f, -1, fruitFor(kLatButtons[i].label));
		return;
	}
	const float jolt = sLatBump * sLatBump;
	drawMenuFruit(MF_ORANGE, kLatX, kLatY + jolt * 3, kLatSize * (1 + 0.1f * jolt), sTime * 0.8f, jolt * 0.3f);
	// The ruler: early on the left, late on the right, the newest slice brightest.
	const float rx = 160, ry = 184, half = 110, perSec = half / 0.2f;
	gfx::rect(rx - half, ry, half * 2, 2, withAlpha(COL_CREAM, 0.5f));
	gfx::rect(rx - 1, ry - 7, 3, 16, COL_CREAM);
	gfx::text("EARLY", rx - half, ry + 8, 0.3f, COL_DIM);
	gfx::text("LATE", rx + half, ry + 8, 0.3f, COL_DIM, ALIGN_RIGHT);
	for (int i = sLatMarkCount - 1; i >= 0; i--) {
		const float x = rx + clampf(sLatMarks[i] * perSec, -half, half);
		const float a = 1 - i / (float)kLatMarks;
		gfx::rect(x - 2, ry - 5, 4, 12, withAlpha(i == 0 ? COL_ACCENT : COL_CREAM, a));
	}
	if (sLatMarkCount > 0) {
		const int ms = latencyMs(sLatMarks[0]);
		if (ms > 4) snprintf(buf, sizeof buf, "%d ms late", ms);
		else if (ms < -4) snprintf(buf, sizeof buf, "%d ms early", -ms);
		else snprintf(buf, sizeof buf, "on the beat");
		gfx::text(buf, 160, 208, 0.42f, COL_CREAM, ALIGN_CENTER);
	}
}

static void drawTopContent(Screen screen) {
	char buf[48];
	gfx::clearPanels();
	switch (screen) {
		case SCREEN_LOADING:
			break;
		case SCREEN_TITLE:
			gfx::setDepth(1.2f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 66 + sinf(sTime * 1.6f) * 2);
			gfx::setDepth(1.4f);
			drawSplash(298, 114 + sinf(sTime * 1.6f) * 2);
			drawHero(200, 172, 1.25f, 0.8f);
#if PORT_TWO_SCREENS
			{
				// What the chosen sign below does, since the signs themselves sit on the other screen.
				static const char* const kWhat[kTitleCount] = {"Pick a game and start slicing.", "Sound, looks, controls and more.", "Every fruit you have sliced.", "Your numbers so far.", "The rules, and a practice run.", "Spend coins on themes, trails and music.", "Who made the game."};
				int f = sFocus[SCREEN_TITLE];
				gfx::setDepth(0.6f);
				if (f >= 0 && f < kTitleCount) gfx::text(kWhat[f], 200, 204, 0.36f, COL_CREAM, ALIGN_CENTER);
			}
#endif
			if (sNoSound) {
				gfx::setDepth(0.6f);
				gfx::text(PORT_NO_SOUND_TEXT, 200, 222, 0.34f, rgba(255, 190, 150), ALIGN_CENTER);
			}
			break;
		case SCREEN_MODES: {
			const int f = sFocus[SCREEN_MODES];
			const int mode = f >= 1 ? modeOnSign(sModePage, f) : -1;
			topPanel(30, 96, 340, 134);
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			gfx::setDepth(0.7f);
			if (mode == MODE_DAILY) {
				drawDailyTop();
			} else if (mode == SIGN_PASS) {
				gfx::text("Pass and Play", 200, 104, 0.6f, COL_ACCENT, ALIGN_CENTER);
				gfx::text("2 to 4 players take turns on one", 200, 134, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text("device. The highest score wins.", 200, 152, 0.38f, COL_CREAM, ALIGN_CENTER);
			} else if (mode == SIGN_MIXER) {
				gfx::text("Mixer", 200, 104, 0.6f, COL_ACCENT, ALIGN_CENTER);
				gfx::text("Rules that change how any mode", 200, 134, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text("plays, with bests of their own.", 200, 152, 0.38f, COL_CREAM, ALIGN_CENTER);
				snprintf(buf, sizeof buf, mixerRulesOn() ? "%d ON" : "ALL OFF", mixerRulesOn());
				gfx::text(buf, 200, 184, 0.6f, COL_CREAM, ALIGN_CENTER);
			} else if (mode >= 0) {
				const ModeInfo& m = kModes[mode];
				gfx::text(m.name, 200, 104, 0.6f, COL_ACCENT, ALIGN_CENTER);
				gfx::text(m.line1, 200, 134, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text(m.line2, 200, 152, 0.38f, COL_CREAM, ALIGN_CENTER);
				if (mixerRulesOn() && mode < MODE_DAILY) snprintf(buf, sizeof buf, "MIXER BEST %lu", (unsigned long)gSave.bestMixer[mode]);
				else if (assistedRun()) snprintf(buf, sizeof buf, "ASSISTED BEST %lu", (unsigned long)gSave.bestAssisted[mode]);
				else snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)gSave.best[mode]);
				gfx::text(buf, 200, 184, 0.6f, COL_CREAM, ALIGN_CENTER);
			} else {
				gfx::text("Back to the title", 200, 150, 0.46f, COL_CREAM, ALIGN_CENTER);
			}
			break;
		}
		case SCREEN_OPTIONS: {
			int n = optionButtons();
			int f = sFocus[SCREEN_OPTIONS];
			const Section* sec = f >= 1 && f < n ? &kSections[sOptionSection[f]] : nullptr;
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			topPanel(40, 112, 320, 84);
			gfx::setDepth(0.7f);
			gfx::text(sec ? sec->title : "Options", 200, 118, 0.5f, COL_ACCENT, ALIGN_CENTER);
			gfx::text(sec ? sec->hint1 : "", 200, 146, 0.38f, COL_CREAM, ALIGN_CENTER);
			gfx::text(sec ? sec->hint2 : (sOptionsFromPause ? "Back to the game" : "Back to the title"), 200, 164, 0.38f, COL_CREAM, ALIGN_CENTER);
			break;
		}
		case SCREEN_SECTION: {
			const Section& sec = kSections[sSection];
			int f = sFocus[SCREEN_SECTION];
			const Row* r = f >= 0 ? &sec.rows[f] : nullptr;
			if (r && (r->value == &gSave.artStyle || r->value == &gSave.detail || r->value == &gSave.sliceStyle || r->value == &gSave.depth)) {
				drawHero(200, 50, 1.2f, 1.0f);
			} else if (r && r->value == &gSave.board) {
				gfx::setDepth(0.6f);
				gfx::image(SHEET_BOARD, 0, 142, 8, 116, 87);
			} else if (r && (r->value == &gSave.blade || r->value == &gSave.trail)) {
				gfx::setDepth(0.6f);
				sPreviewBlade.draw();
			} else if (kSections[sSection].id == SID_DATA) {
				gfx::setDepth(0.6f);
				snprintf(buf, sizeof buf, "FRUIT SLICED %lu", (unsigned long)gSave.totalFruit);
				gfx::text(buf, 200, 20, 0.5f, COL_CREAM, ALIGN_CENTER);
				snprintf(buf, sizeof buf, "BEST COMBO %lu", (unsigned long)gSave.bestCombo);
				gfx::text(buf, 200, 50, 0.5f, COL_CREAM, ALIGN_CENTER);
			} else {
				// No preview for this row: the logo fills the space above the hint, as on the Options screen.
				gfx::setDepth(1.0f);
				gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			}
			topPanel(30, 100, 340, 130);
			gfx::setDepth(0.7f);
			gfx::text(r ? r->label : sec.title, 200, 106, 0.56f, COL_ACCENT, ALIGN_CENTER);
			if (r && rowDisabled(*r)) {
				gfx::text("This console has no 3D screen,", 200, 142, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text("so depth has no effect here.", 200, 160, 0.38f, COL_CREAM, ALIGN_CENTER);
			} else if (r) {
				gfx::text(r->line1, 200, 138, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text(r->line2, 200, 156, 0.38f, COL_CREAM, ALIGN_CENTER);
				#if PORT_3DS
				if (r->value == &gSave.stylus) gfx::text("Also calibrate in System Settings.", 200, 174, 0.34f, COL_DIM, ALIGN_CENTER);
#endif
				rowValueText(*r, buf, sizeof buf);
				gfx::text(buf, 200, 194, 0.5f, COL_CREAM, ALIGN_CENTER);
			} else {
				gfx::text("Back to Options", 200, 150, 0.42f, COL_CREAM, ALIGN_CENTER);
			}
			break;
		}
		case SCREEN_SHOP:
			topPanel(30, 8, 340, 224);
			drawShopPreview();
			break;
		case SCREEN_JOURNAL:
			drawJournalTop();
			break;
		case SCREEN_STATS:
			drawStatsTop();
			break;
#if PORT_CUSTOM_MUSIC
		case SCREEN_BEATTOOL:
			topPanel(30, 96, 340, 134);
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			gfx::setDepth(0.7f);
			gfx::text("Beat Tool", 200, 104, 0.6f, COL_ACCENT, ALIGN_CENTER);
			gfx::text("The game listens to your song for its beat.", 200, 134, 0.36f, COL_CREAM, ALIGN_CENTER);
			gfx::text("Turn the click on, then nudge the beat", 200, 152, 0.36f, COL_CREAM, ALIGN_CENTER);
			gfx::text("until every click lands with the music.", 200, 168, 0.36f, COL_CREAM, ALIGN_CENTER);
#if !PORT_ANDROID
			gfx::text("Left and Right: earlier or later.  Up and Down: tempo.", 200, 196, 0.3f, COL_DIM, ALIGN_CENTER);
			gfx::text("X: click on or off.  Y: the bar starts a beat later.", 200, 210, 0.3f, COL_DIM, ALIGN_CENTER);
#endif
			break;
#endif
		case SCREEN_UPDATE:
			drawUpdateTop();
			break;
		case SCREEN_BEAT: {
			topPanel(30, 96, 340, 134);
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			gfx::setDepth(0.7f);
			const ModeInfo& m = kModes[MODE_BEAT];
			gfx::text(m.name, 200, 104, 0.6f, COL_ACCENT, ALIGN_CENTER);
			gfx::text(m.line1, 200, 134, 0.38f, COL_CREAM, ALIGN_CENTER);
			gfx::text(m.line2, 200, 152, 0.38f, COL_CREAM, ALIGN_CENTER);
			if (assistedRun()) snprintf(buf, sizeof buf, "ASSISTED BEST %lu", (unsigned long)gSave.bestAssisted[MODE_BEAT]);
			else snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)gSave.best[MODE_BEAT]);
			gfx::text(buf, 200, 184, 0.6f, COL_CREAM, ALIGN_CENTER);
			break;
		}
		case SCREEN_CREDITS:
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 54, 0.72f, 0.72f);
			topPanel(30, 110, 340, 116);
			gfx::setDepth(0.7f);
			gfx::text("Free, non-commercial fan game.", 200, 118, 0.38f, COL_CREAM, ALIGN_CENTER);
			gfx::text("Not affiliated with any publisher.", 200, 136, 0.38f, COL_CREAM, ALIGN_CENTER);
			snprintf(buf, sizeof buf, "FRUIT SLICED %lu  BEST COMBO %lu", (unsigned long)gSave.totalFruit, (unsigned long)gSave.bestCombo);
			gfx::text(buf, 200, 164, 0.34f, COL_DIM, ALIGN_CENTER);
			gfx::text("V" VERSION_STRING DEV_SUFFIX, 200, 190, 0.42f, COL_ACCENT, ALIGN_CENTER);
			break;
		case SCREEN_HOWTO:
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			drawHero(200, 164, 1.2f, 0.8f);
			break;
#if PORT_TWO_SCREENS
		case SCREEN_GAME:
			sGame.drawFieldTop();
			sGame.drawHud();
			break;
#endif
		case SCREEN_PAUSE:
#if PORT_TWO_SCREENS
			sGame.drawFieldTop();
			sGame.drawHud();
#endif
			gfx::setDepth(1.8f);
			gfx::text("PAUSED", 200, 130, 1.1f, COL_CREAM, ALIGN_CENTER);
			{
				// Coins saved, with what this run has made so far waiting to be added.
				char cb[40];
				snprintf(cb, sizeof cb, "%lu", (unsigned long)gSave.coins);
				float w = gfx::textWidth(cb, 0.5f);
				gfx::sprite(SHEET_SPRITES, sprites_coin_idx, 200 - w / 2 - 6, 188, 0.56f, 0.56f);
				gfx::text(cb, 200 + 8, 179, 0.5f, COL_CREAM, ALIGN_CENTER);
				if (sGame.runCoins()) {
					snprintf(cb, sizeof cb, "+%lu FROM THIS RUN", (unsigned long)sGame.runCoins());
					gfx::text(cb, 200, 202, 0.34f, COL_ACCENT, ALIGN_CENTER);
				}
			}
			break;
		case SCREEN_PASS_TURN:
			topPanel(30, 96, 340, 134);
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			gfx::setDepth(0.7f);
			gfx::text(modeName(sStartMode), 200, 104, 0.6f, COL_ACCENT, ALIGN_CENTER);
			gfx::text(kModes[sStartMode].line1, 200, 134, 0.38f, COL_CREAM, ALIGN_CENTER);
			gfx::text(kModes[sStartMode].line2, 200, 152, 0.38f, COL_CREAM, ALIGN_CENTER);
			snprintf(buf, sizeof buf, "TURN %d OF %d", sPassTurn + 1, passCount());
			gfx::text(buf, 200, 184, 0.6f, COL_CREAM, ALIGN_CENTER);
			break;
		case SCREEN_LATENCY:
			drawLatencyTop();
			break;
		case SCREEN_PASS_BOARD: {
			const int winner = passWinner();
			topPanel(30, 96, 340, 134);
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			gfx::setDepth(0.7f);
			gfx::text(modeName(sStartMode), 200, 104, 0.5f, COL_CREAM, ALIGN_CENTER);
			if (winner >= 0) snprintf(buf, sizeof buf, "%s WINS!", sPassNames[winner]);
			else snprintf(buf, sizeof buf, "IT'S A TIE!");
			gfx::text(buf, 200, 136, 0.8f, winner >= 0 ? passColor(winner) : COL_ACCENT, ALIGN_CENTER);
			if (winner >= 0) {
				snprintf(buf, sizeof buf, "%lu", (unsigned long)sPassScores[winner]);
				gfx::text(buf, 200, 178, 0.7f, COL_CREAM, ALIGN_CENTER);
			}
			break;
		}
		case SCREEN_RESULT: {
			float in = clampf(sGame.phaseT * 4, 0, 1);
			gfx::setDepth(0.5f);
			gfx::sign(sprites_plank_idx, 30, 12, 340, 212, withAlpha(COL_PLANK, in));
			gfx::setDepth(1.0f);
			if (sGame.mode == MODE_PRACTICE) {
				gfx::setDepth(1.2f);
				gfx::text("PRACTICE DONE", 200, 44, 0.86f, COL_ACCENT, ALIGN_CENTER);
				gfx::setDepth(1.0f);
				gfx::text("You know the basics.", 200, 104, 0.46f, COL_CREAM, ALIGN_CENTER);
				gfx::text("Last lesson: slice the fruit", 200, 140, 0.42f, COL_CREAM, ALIGN_CENTER);
				gfx::text("below to pick a game.", 200, 160, 0.42f, COL_CREAM, ALIGN_CENTER);
				gfx::text("Practice again from How to Play.", 200, 196, 0.34f, COL_DIM, ALIGN_CENTER);
				break;
			}
			gfx::text(modeName(sGame.mode), 200, 26, 0.56f, COL_CREAM, ALIGN_CENTER);
			gfx::setDepth(1.7f);
			snprintf(buf, sizeof buf, "%lu", (unsigned long)sGame.score);
			gfx::text(buf, 200, 50, 1.7f, COL_ACCENT, ALIGN_CENTER);
			gfx::setDepth(1.0f);
			if (sPassTurn >= 0) {
				gfx::text(sPassNames[sPassTurn], 200, 116, 0.6f, passColor(sPassTurn), ALIGN_CENTER);
			} else if (sGame.newBest) {
				gfx::text("NEW BEST!", 200, 116, 0.7f + sinf(sTime * 6) * 0.04f, COL_CREAM, ALIGN_CENTER);
			} else {
				snprintf(buf, sizeof buf, sGame.mixed ? "MIXER BEST %lu" : (sGame.assisted ? "ASSISTED BEST %lu" : "BEST %lu"), (unsigned long)sGame.best);
				gfx::text(buf, 200, 116, 0.6f, COL_DIM, ALIGN_CENTER);
			}
			if (sGame.mode == MODE_BEAT) snprintf(buf, sizeof buf, "SLICED %d   ON THE BEAT %d", sGame.sliced, sGame.beatHits);
			else snprintf(buf, sizeof buf, "SLICED %d   BEST COMBO %d", sGame.sliced, sGame.bestCombo);
			gfx::text(buf, 200, 152, 0.42f, COL_CREAM, ALIGN_CENTER);
			if (sCardFrame) {
				// The score card: the game's name where the coins go, which are nobody else's business.
				gfx::setDepth(1.2f);
				gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 194, 0.3f, 0.3f);
			} else if (sGame.scoreCommitted && sPassTurn < 0) {
				snprintf(buf, sizeof buf, "+%lu COINS", (unsigned long)sGame.coinsEarned);
				float cw = gfx::textWidth(buf, 0.5f);
				gfx::sprite(SHEET_SPRITES, sprites_coin_idx, 200 - cw / 2 - 12, 189, 0.5f, 0.5f);
				gfx::text(buf, 200 + 6, 180, 0.5f, COL_ACCENT, ALIGN_CENTER);
				// Where the extra coins came from.
				buf[0] = 0;
				if (sGame.dailyCoins && sGame.journalCoins) snprintf(buf, sizeof buf, "DAILY +%lu  JOURNAL +%lu", (unsigned long)sGame.dailyCoins, (unsigned long)sGame.journalCoins);
				else if (sGame.dailyCoins) snprintf(buf, sizeof buf, "DAILY BONUS +%lu", (unsigned long)sGame.dailyCoins);
				else if (sGame.journalCoins) snprintf(buf, sizeof buf, "JOURNAL MILESTONE +%lu", (unsigned long)sGame.journalCoins);
				if (buf[0]) gfx::text(buf, 200, 204, 0.34f, COL_DIM, ALIGN_CENTER);
			}
			break;
		}
#if PORT_3DS
		case SCREEN_BENCH:
			if (!sBenchDone) {
				sGame.drawFieldTop();
				gfx::setDepth(0);
				gfx::rect(0, 0, 400, 44, rgba(30, 20, 12, 220));
				snprintf(buf, sizeof buf, "SPEED TEST %d OF %d", sBenchIndex + 1, kBenchCount);
				gfx::text(buf, 200, 2, 0.42f, COL_CREAM, ALIGN_CENTER);
				gfx::text(kBench[sBenchIndex].name, 200, 22, 0.42f, COL_ACCENT, ALIGN_CENTER);
			} else {
				gfx::setDepth(0);
				gfx::rect(0, 0, 400, 240, rgba(30, 20, 12, 235));
				gfx::text("MISSED FRAMES", 200, 4, 0.46f, COL_ACCENT, ALIGN_CENTER);
				for (int i = 0; i < kBenchCount; i++) {
					snprintf(buf, sizeof buf, "%s: %d", kBench[i].name, sBenchResults[i].missed);
					gfx::text(buf, 200, 34 + i * 24, 0.4f, COL_CREAM, ALIGN_CENTER);
				}
				gfx::text(sBenchSaved ? "Saved to 3ds/FruitSlicer/bench.txt" : "Could not save the results", 200, 190, 0.36f, COL_DIM, ALIGN_CENTER);
			}
			break;
#endif
		default:
			break;
	}
	// Every main menu shows the coins saved, top left: the logo leaves it free, and on one screen
	// it stays well away from the menu's back arrow.
	if (screen == SCREEN_TITLE || screen == SCREEN_MODES || screen == SCREEN_OPTIONS || screen == SCREEN_JOURNAL || screen == SCREEN_STATS || screen == SCREEN_HOWTO || screen == SCREEN_BEAT) {
		gfx::setDepth(0.8f);
		drawPurse(12, 10, 0.5f, false);
	}
}

// ---------------------------------------------------------------- quit bomb (Windows, Linux and Android)
#if !PORT_3DS
// A bomb in the window's top-left corner, labelled Quit Game. Picking it blows up the whole screen,
// then the game closes. It is the last of the title's buttons, an icon like the links.
static constexpr int kTitleQuit = kTitleCount;
static constexpr float kQuitBlast = 1.25f;  // seconds of explosion before the game closes
static constexpr float kQuitHold = 3;       // seconds the bomb (or Escape) must be held to light it
static float sQuitT = -1;                   // time since the bomb went off, or -1
static float sQuitHold = 0;                 // how long it has been held so far
static float sQuitTick = 0;                 // time since the last tick of the fuse
static int sQuitTicks = 0;
static Button sTitleBtns[kTitleCount + 1];

static Button quitButton() {
	float x, y, w, h;
	gfx::windowArea(&x, &y, &w, &h);
	return {x + 4, y + 4, 70, 58, "Quit Game"};
}

static const Button* titleButtons() {
	for (int i = 0; i < kTitleCount; i++) sTitleBtns[i] = kTitleButtons[i];
	sTitleBtns[kTitleQuit] = quitButton();
	return sTitleBtns;
}

static void startQuit() {
	const Button b = quitButton();
	float x, y, w, h;
	gfx::windowArea(&x, &y, &w, &h);
	sQuitT = 0;
	boom::clear();
	const float bx = b.x + b.w * 0.5f, by = b.y + 22;
	boom::start(bx, by, 1.6f);
	// Then a chain of blasts runs out from the corner across the whole window, the farthest last.
	const float reach = sqrtf(w * w + h * h);
	for (int i = 0; i < 12; i++) {
		const float px = x + w * ((i % 4) + 0.5f + 0.3f * sinf(i * 2.7f)) / 4, py = y + h * ((i / 4) + 0.5f + 0.3f * cosf(i * 1.9f)) / 3;
		const float d = sqrtf((px - bx) * (px - bx) + (py - by) * (py - by));
		boom::start(px, py, 2.2f + 0.8f * ((i * 7) % 5) / 4.0f, 0.06f + 0.5f * d / reach);
	}
	audio::play(SFX_BOOM, 1.0f, -0.6f);
	audio::play(SFX_FINALE_BURST, 0.8f);
	PORT_VIBRATE(1.0f, 400);
}

// Holding the bomb down (mouse or finger), Escape, or Enter or A while the bomb is selected lights its fuse.
// The fuse ticks faster and faster; let go before kQuitHold and nothing happens. True when it goes off.
static bool holdQuit(const Input& in, int focus) {
	const Button b = quitButton();
	// A key counts only if it went down on the title, so the Escape that backed out of Options can't light it.
	static bool keyArmed = false;
	const bool escape = (in.held & (KEY_B | KEY_START)) == (KEY_B | KEY_START);
	const bool enter = focus == kTitleQuit && (in.held & KEY_A);
	if (in.down & (KEY_B | KEY_A)) keyArmed = true;
	if (!escape && !enter) keyArmed = false;
	const bool held = (in.touching && hit(b, in.tx, in.ty)) || (keyArmed && (escape || enter));
	if (!held) {
		sQuitHold = sQuitTick = 0;
		sQuitTicks = 0;
		return false;
	}
	sQuitHold += kDt;
	sQuitTick += kDt;
	const float gap = 0.5f - 0.36f * clampf(sQuitHold / kQuitHold, 0, 1);  // half a second down to a seventh
	if (sQuitHold == kDt || sQuitTick >= gap) {
		sQuitTick = 0;
		audio::play(SFX_KNOCK, 0.7f, -0.6f, sQuitTicks++ % 2 ? 1.5f : 1.85f);  // tick, tock
		PORT_VIBRATE(0.25f, 20);
	}
	if (sQuitHold < kQuitHold) return false;
	sQuitHold = 0;
	return true;
}

// The bomb and its label, in the corner. alpha fades it with a slide.
static void drawQuitBomb(bool focused, float alpha) {
	if (alpha <= 0) return;
	const Button b = quitButton();
	const float lit = clampf(sQuitHold / kQuitHold, 0, 1);  // 0 until held, 1 just before it goes off
	const float shake = gSave.calmFx ? lit * 0.6f : lit * 2.5f;
	const float cx = b.x + b.w * 0.5f + sinf(sTime * 53) * shake, cy = b.y + 22 + cosf(sTime * 47) * shake;
	const float wob = gSave.calmFx ? 0 : sinf(sTime * 2.2f) * 0.12f;
	if (sQuitT < 0) {
		if (focused || lit > 0) gfx::sprite(SHEET_SPRITES, sprites_ring_idx, cx, cy, 0.62f + 0.12f * lit, 0.62f + 0.12f * lit, 0, withAlpha(lit > 0 ? rgba(255, (u8)(200 - 150 * lit), 40, 255) : COL_ACCENT, 0.9f * alpha));
		if (fruit3D()) {
			// A model can't fade, so during a slide the 3D bomb shrinks away instead of turning into the sticker.
			const float grow = alpha * alpha * (3 - 2 * alpha);
			m3d::begin(320, gfx::stereoPerZ());
			m3d::Pose pose = {cx, cy, 0, 0.25f, sTime * 0.7f, wob, 0.72f * grow, 0, 0, 0};
			m3d::draw(m3d::MESH_BOMB, m3d::PART_WHOLE, pose);
			m3d::end();
		} else {
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, cx, cy, 0.62f, 0.62f, wob, withAlpha(WHITE, alpha));
		}
		if (lit > 0) {
			// The lit fuse: a spark at the top of the bomb that flickers and grows as the time runs out.
			const float s = (0.35f + 0.45f * lit) * (0.85f + 0.15f * sinf(sTime * 40));
			gfx::sprite(SHEET_SPRITES, sprites_spark_idx, cx + 7, cy - 16, s, s, sTime * 9, rgba(255, 200, 60, 255));
			gfx::sprite(SHEET_SPRITES, sprites_spark_idx, cx + 7, cy - 16, s * 0.5f, s * 0.5f, -sTime * 7, WHITE);
		}
		char label[24];
		if (lit > 0) snprintf(label, sizeof label, "Quitting in %d", (int)ceilf(kQuitHold - sQuitHold));
		else snprintf(label, sizeof label, "%s", focused ? "Hold to Quit" : b.label);
		const u32 lc = withAlpha(focused || lit > 0 ? COL_ACCENT : COL_CREAM, alpha);
		// Centred under the bomb, but a long label is moved right so it never runs off the window's edge.
		const float lx = fmaxf(b.x + b.w * 0.5f, b.x + gfx::textWidth(label, 0.3f) * 0.5f);
		gfx::text(label, lx, b.y + 44, 0.3f, lc, ALIGN_CENTER);
	}
	if (sQuitT >= 0) {
		float x, y, w, h;
		gfx::windowArea(&x, &y, &w, &h);
		// A white flash, the blasts, then everything goes dark just before the game closes.
		const float flash = 1 - clampf(sQuitT / 0.25f, 0, 1);
		boom::draw();
		if (flash > 0) gfx::rect(x, y, w, h, withAlpha(WHITE, flash * (gSave.calmFx ? 0.4f : 0.9f)));
		const float dark = clampf((sQuitT - (kQuitBlast - 0.45f)) / 0.4f, 0, 1);
		if (dark > 0) gfx::rect(x, y, w, h, rgba(0, 0, 0, (u8)(255 * dark)));
	}
}
#else
static constexpr int kTitleQuit = -2;  // the 3DS has no Quit Game bomb: HOME closes the game there
static constexpr float kQuitBlast = 1;
static float sQuitT = -1;
static const Button* titleButtons() { return kTitleButtons; }
static void startQuit() {}
static bool holdQuit(const Input&, int) { return false; }
#endif
static constexpr int kTitleAll = PORT_3DS ? kTitleCount : kTitleCount + 1;  // with the Quit Game bomb

// ---------------------------------------------------------------- screens: bottom
static void drawRows() {
	const Section& sec = kSections[sSection];
	int focus = sFocus[SCREEN_SECTION];
	char buf[32];
	for (int i = 0; i < sec.count; i++) {
		const Row& r = sec.rows[i];
		float y = kListTop + i * kRowH - sScroll;
		if (y + kRowH < kListTop || y > 240) continue;
		bool pressed = sTouchMode == TOUCH_ROW && sTouchRow == i && !sTouchMoved && (r.kind == ROW_ACTION || r.kind == ROW_PAGE);
		drawPlank(8, y + 1, 304, kRowH - 3, focus == i, pressed);
		float ty = y + kRowH * 0.5f - 8;
		u32 labelColor = rowDisabled(r) ? COL_DIM : COL_CREAM;
		if (r.kind == ROW_ACTION) {
			const char* label = sArmed == i ? PORT_CONFIRM_TEXT : r.label;
			const int player = passRowPlayer(r);
			const u32 color = rowDisabled(r) ? COL_DIM : (player >= 0 ? passColor(player) : (sArmed == i ? COL_ACCENT : COL_CREAM));
			gfx::text(label, 160, ty - 1, 0.42f, color, ALIGN_CENTER);
			continue;
		}
		float ls = 0.36f;
		float lw = gfx::textWidth(r.label, ls);
		if (lw > 134) ls *= 134 / lw;
		gfx::text(r.label, 18, ty + (0.36f - ls) * 10, ls, labelColor);
		if (r.kind == ROW_PAGE) {
			// One arrow, not the pair a choice has: this row goes somewhere rather than changing a value.
			gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, 298, y + kRowH * 0.5f - 1, 0.5f, 0.5f, 3.14159265f, COL_ACCENT);
			continue;
		}
		rowValueText(r, buf, sizeof buf);
		if (r.kind == ROW_SLIDER) {
			float cy = y + kRowH * 0.5f - 1;
			float t = (*r.value - r.lo) / (float)(r.hi - r.lo);
			if (rowDisabled(r)) t = 0;
			gfx::rect(kTrackX0, cy - 3, kTrackX1 - kTrackX0, 6, gfx::themeBar());
			gfx::rect(kTrackX0, cy - 3, (kTrackX1 - kTrackX0) * t, 6, rowDisabled(r) ? COL_DIM : COL_ACCENT);
			gfx::sprite(SHEET_SPRITES, sprites_knob_idx, lerpf(kTrackX0, kTrackX1, t), cy, 0.72f, 0.72f);
			gfx::text(buf, 306, ty + 1, 0.3f, COL_CREAM, ALIGN_RIGHT);
		} else {
			float cy = y + kRowH * 0.5f - 1;
			gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, 164, cy, 0.5f, 0.5f, 0, COL_ACCENT);
			gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, 298, cy, 0.5f, 0.5f, 3.14159265f, COL_ACCENT);
			float vs = 0.36f;
			float w = gfx::textWidth(buf, vs);
			if (w > 108) vs *= 108 / w;
			gfx::text(buf, 231, ty + (0.36f - vs) * 10, vs, COL_CREAM, ALIGN_CENTER);
		}
	}
	drawScrollMark(kListTop, 240 - kListTop, sScroll, maxScroll());
}

static void drawBottomContent(Screen screen) {
	static const int kIcons[MODE_COUNT] = {sprites_pip_on_idx, sprites_ic_clock_idx, sprites_ic_leaf_idx, sprites_ic_fast_idx, sprites_ic_one_idx, sprites_ic_mine_idx, sprites_ic_chain_idx, sprites_coin_idx, sprites_ic_note_idx};
	sDrawing = screen;
	gfx::clearPanels();
	switch (screen) {
		case SCREEN_LOADING:
			break;
		case SCREEN_TITLE: {
			drawMenuBack();
			// The choices are fruit now, standing on the board: no signpost behind them.
			int f = sFocus[SCREEN_TITLE];
			for (int i = 0; i < kTitleSigns; i++) drawSign(kTitleButtons[i], kTitleButtons[i].label, f == i, sPressed == i, i == 0 ? 1.0f : 0.46f, -1, fruitFor(kTitleButtons[i].label));
#if !PORT_3DS
			for (int i = kTitleSigns; i < kTitleCount; i++) drawLinkIcon(kTitleButtons[i], i - kTitleSigns, f == i);
#endif
			break;
		}
		case SCREEN_MODES: {
			drawMenuBack();
			int f = sFocus[SCREEN_MODES];
			drawHeader("Pick a Game");
			drawBackArrow(f == 0, sPressed == 0);
			const auto drawPage = [&](int page, float dx, bool live) {
				Button signs[kModesPerPage + 1];
				const int n = modeSigns(page, signs);
				for (int i = 1; i < n; i++) {
					Button b = signs[i];
					b.x += dx;
					const int id = modeOnSign(page, i);
					drawModeTile(b, id == SIGN_MIXER ? sprites_knob_idx : (id == SIGN_PASS ? sprites_arrow_idx : kIcons[id]), live && f == i, live && sPressed == i);
				}
			};
			if (sModeSlide != 0) drawPage(sModeOldPage, sModeSlide - (sModeSlide > 0 ? 320 : -320), false);
			drawPage(sModePage, sModeSlide, true);
			for (int side = 0; side < 2; side++) drawPageArrow(side);
			for (int p = 0; p < kModePages; p++)
				gfx::rect(160 - kModePages * 10.0f + p * 20.0f + 3, 224, 14, 4, p == sModePage ? COL_ACCENT : COL_DIM);
			break;
		}
		case SCREEN_OPTIONS: {
			drawMenuBack();
			int f = sFocus[SCREEN_OPTIONS];
			drawHeader("Options");
			drawBackArrow(f == 0, sPressed == 0);
			int n = optionButtons();
			for (int i = 1; i < n; i++) drawSign(sOptionButtons[i], sOptionButtons[i].label, f == i, sPressed == i, 0.56f, -1, fruitFor(sOptionButtons[i].label));
			break;
		}
		case SCREEN_SECTION:
			drawMenuBack();
			drawRows();
			// The header covers rows as they scroll up beneath it.
			gfx::rect(0, 0, 320, 44, gfx::themeBar());
			drawHeader(kSections[sSection].title);
			drawBackArrow(sFocus[SCREEN_SECTION] < 0, sPressed == 0);
			if (sPassTyping >= 0 && screen == sScreen) {
				char typed[36];
				port::textPoll(typed, sizeof typed - 1);
				strcat(typed, "_");
				gfx::rect(0, 0, 320, 240, rgba(30, 18, 10, 170));
				gfx::sign(sprites_plank_idx, 20, 60, 280, 146, COL_PLANK);
				gfx::text("Type a name", 160, 72, 0.5f, COL_ACCENT, ALIGN_CENTER);
				gfx::text(typed, 160, 108, 0.6f, passColor(sPassTyping), ALIGN_CENTER);
				for (int i = 0; i < 2; i++) drawSign(kNameButtons[i], kNameButtons[i].label, sNameFocus == i, sPressed == i, 0.5f);
			}
#if PORT_HAS_DISPLAY
			if (sRevertT > 0 && screen == sScreen) {
				gfx::rect(0, 0, 320, 240, rgba(30, 18, 10, 170));
				gfx::sign(sprites_plank_idx, 20, 60, 280, 146, COL_PLANK);
				gfx::text("Keep this display?", 160, 72, 0.56f, COL_ACCENT, ALIGN_CENTER);
				char line[32];
				snprintf(line, sizeof line, "Going back in %d", (int)ceilf(sRevertT));
				gfx::text(line, 160, 108, 0.42f, COL_CREAM, ALIGN_CENTER);
				for (int i = 0; i < 2; i++) drawSign(kRevertButtons[i], kRevertButtons[i].label, sRevertFocus == i, sPressed == i, 0.5f);
			}
#endif
			break;
		case SCREEN_SHOP:
			drawMenuBack();
			drawShop();
			break;
		case SCREEN_JOURNAL:
			drawMenuBack();
			drawJournal();
			break;
		case SCREEN_BEAT:
			drawMenuBack();
			drawBeatScreen();
			break;
		case SCREEN_UPDATE:
			drawMenuBack();
			drawUpdateSign();
			break;
#if PORT_CUSTOM_MUSIC
		case SCREEN_BEATTOOL:
			drawMenuBack();
			drawBeatTool();
			break;
#endif
		case SCREEN_STATS:
			drawMenuBack();
			drawStats();
			break;
		case SCREEN_CREDITS:
			drawMenuBack();
			drawHeader("Credits");
			drawBackArrow(true, sPressed == 0);
			gfx::sign(sprites_plank_idx, 10, 50, 300, 176, COL_PLANK);
			gfx::text("PROGRAMMING", 160, 57, 0.3f, COL_ACCENT, ALIGN_CENTER);
			gfx::text("NickBin", 160, 69, 0.46f, COL_CREAM, ALIGN_CENTER);
			{
				const auto line = [](const char* s, float y, u32 color) {
					float w = gfx::textWidth(s, 0.3f);
					gfx::text(s, 160, y, w > 280 ? 0.3f * 280 / w : 0.3f, color, ALIGN_CENTER);
				};
				line("BUILT WITH", 96, COL_ACCENT);
				line(PORT_CREDITS_TOOLS_1, 109, COL_CREAM);
				line(PORT_CREDITS_TOOLS_2, 122, COL_CREAM);
				line("Python, Pillow, numpy", 135, COL_CREAM);
				line("TOOLKITS", 152, COL_ACCENT);
				line("Claude Code, Graphify", 165, COL_CREAM);
				line("Claude Code Game Studios by Donchitos", 178, COL_CREAM);
				line("Anti Slop by miqdadbadjuber", 191, COL_CREAM);
				line("Shop music: Of Far Different Nature (CC BY 4.0), NickBin, King Bakuyo Pro,", 203, COL_DIM);
				line("Mq_, Cabex Music and Aneolent", 214, COL_DIM);
			}
			break;
		case SCREEN_HOWTO: {
			static const char* const kLines[] = {
				"Swipe through fruit. Miss the bombs.",
				"Each slice fills the Fruit Frenzy meter.",
				"It never stops draining, so keep slicing.",
				"If it runs dry, your combo resets.",
				"Fill it for Frenzy: double points!",
				"One long swipe scores a big bonus.",
				PORT_HOWTO_PAUSE,
			};
			drawMenuBack();
			drawHeader("How to Play");
			drawBackArrow(sFocus[SCREEN_HOWTO] == 0, sPressed == 0);
			gfx::sign(sprites_plank_idx, 8, 46, 304, 140, COL_PLANK);
			drawSign(kHowtoButtons[1], kHowtoButtons[1].label, sFocus[SCREEN_HOWTO] == 1, sPressed == 1, 0.5f, -1, fruitFor("Practice"));
			// One size for every line, narrow enough to stay clear of the plank's screws.
			float s = 0.36f;
			for (int i = 0; i < 7; i++) {
				float w = gfx::textWidth(kLines[i], s);
				if (w > 262) s *= 262 / w;
			}
			for (int i = 0; i < 7; i++)
				gfx::text(kLines[i], 160, 54 + i * 18.5f, s, COL_CREAM, ALIGN_CENTER);
			break;
		}
#if PORT_TWO_SCREENS
		case SCREEN_GAME:
			sGame.drawField(sBlade);
			break;
#endif
		case SCREEN_PAUSE: {
#if PORT_TWO_SCREENS
			sGame.drawField(sBlade);
			gfx::rect(0, 0, 320, 240, rgba(30, 18, 10, 150));
#endif
			int f = sFocus[SCREEN_PAUSE];
			drawBackArrow(f == 0, sPressed == 0);
			for (int i = 1; i < 4; i++) {
				const char* label = i == 3 && sGame.mode == MODE_PRACTICE ? "Skip Lessons" : (i == 3 && sGame.mode == MODE_ZEN ? "Finish Run" : kPauseButtons[i].label);
				drawSign(kPauseButtons[i], label, f == i, sPressed == i, 0.56f, -1, fruitFor(kPauseButtons[i].label));
			}
			break;
		}
		case SCREEN_PASS_TURN: {
			drawMenuBack();
			const int f = sFocus[SCREEN_PASS_TURN];
			drawHeader("Pass and Play");
			drawBackArrow(f == 0, sPressed == 0);
			gfx::sign(sprites_plank_idx, 20, 52, 280, 78, COL_PLANK);
			gfx::text("Pass it to", 160, 62, 0.4f, COL_CREAM, ALIGN_CENTER);
			gfx::text(sPassNames[sPassTurn], 160, 84, 0.7f, passColor(sPassTurn), ALIGN_CENTER);
			drawSign(kPassTurnButtons[1], kPassTurnButtons[1].label, f == 1, sPressed == 1, 0.56f, -1, fruitFor(kPassTurnButtons[1].label));
			break;
		}
		case SCREEN_LATENCY:
			drawLatencyBottom();
			break;
		case SCREEN_PASS_BOARD: {
			drawMenuBack();
			const int f = sFocus[SCREEN_PASS_BOARD];
			drawHeader("Results");
			drawBackArrow(f == 0, sPressed == 0);
			int order[kPassMax] = {0, 1, 2, 3};
			for (int i = 1; i < passCount(); i++)  // highest first; ties keep turn order
				for (int j = i; j > 0 && sPassScores[order[j]] > sPassScores[order[j - 1]]; j--) {
					const int swap = order[j];
					order[j] = order[j - 1];
					order[j - 1] = swap;
				}
			char score[16];
			for (int i = 0; i < passCount(); i++) {
				const float y = 46 + i * 30.0f;
				drawPlank(20, y, 280, 27, false, false);
				gfx::text(sPassNames[order[i]], 34, y + 5, 0.42f, passColor(order[i]));
				snprintf(score, sizeof score, "%lu", (unsigned long)sPassScores[order[i]]);
				gfx::text(score, 286, y + 5, 0.42f, COL_CREAM, ALIGN_RIGHT);
			}
			for (int i = 1; i < 3; i++) drawSign(kResultButtons[i], kResultButtons[i].label, f == i, sPressed == i, 0.56f, -1, fruitFor(kResultButtons[i].label));
			break;
		}
		case SCREEN_RESULT: {
#if PORT_TWO_SCREENS
			sGame.drawField(sBlade);
#endif
			int f = sFocus[SCREEN_RESULT];
			drawBackArrow(f == 0, sPressed == 0);
			if (sPassTurn >= 0) {
				kPassResultButtons[1].label = sPassTurn + 1 < passCount() ? "Next Player" : "See Results";
				drawSign(kPassResultButtons[1], kPassResultButtons[1].label, f == 1, sPressed == 1, 0.56f, -1, fruitFor(kPassResultButtons[1].label));
			} else if (sGame.mode == MODE_PRACTICE) drawSign(kPracticeEndButtons[1], kPracticeEndButtons[1].label, f == 1, sPressed == 1, 0.56f, -1, fruitFor(kPracticeEndButtons[1].label));
			else {
				for (int i = 1; i < kResultCount; i++) drawSign(kResultButtons[i], kResultButtons[i].label, f == i, sPressed == i, 0.56f, -1, fruitFor(kResultButtons[i].label));
				if (sCardNoteT > 0 && sCardNote) gfx::text(sCardNote, 160, 159, 0.34f, withAlpha(COL_CREAM, clampf(sCardNoteT, 0, 1)), ALIGN_CENTER);
			}
			break;
		}
#if PORT_3DS
		case SCREEN_BENCH:
			if (!sBenchDone) {
				sGame.drawField(sBlade);
				gfx::text("B stops the test", 160, 222, 0.32f, COL_CREAM, ALIGN_CENTER);
			} else {
				drawMenuBack();
				drawHeader("Speed Test");
				gfx::text("Each line should read 0.", 160, 90, 0.42f, COL_CREAM, ALIGN_CENTER);
				gfx::text("On an emulator these numbers only", 160, 120, 0.36f, COL_DIM, ALIGN_CENTER);
				gfx::text("describe the PC, not a 3DS.", 160, 138, 0.36f, COL_DIM, ALIGN_CENTER);
				gfx::text("A or B: back", 160, 190, 0.42f, COL_ACCENT, ALIGN_CENTER);
			}
			break;
#endif
		default:
			break;
	}
	drawCut(screen);
}

// ---------------------------------------------------------------- flow

static void startGame() {
	sCardNoteT = 0;
	sGame.start(sStartMode);
	sBlade.reset();
	gfx::setShake(0, 0);
	sScreen = SCREEN_GAME;
	sPressed = -1;
}

static bool sLeaving = false;

static void toModes() {
	sPassTurn = -1;
	sScreen = SCREEN_MODES;
	sPressed = -1;
	gfx::setShake(0, 0);
	audio::fuse(false);
	audio::music(MUSIC_MENU);
}

static void passScreen(Screen s) {
	sScreen = s;
	sFocus[s] = 1;
	sPressed = -1;
	gfx::setShake(0, 0);
	audio::fuse(false);
	audio::music(MUSIC_MENU);
}
static void toPassTurn() { passScreen(SCREEN_PASS_TURN); }
static void toPassBoard() { passScreen(SCREEN_PASS_BOARD); }

static void passBegin() {
	if (sDirty) saveWrite();
	sDirty = false;
	sArmed = -1;
	sTouchMode = TOUCH_NONE;
	memset(sPassScores, 0, sizeof sPassScores);
	sPassTurn = 0;
	sStartMode = (Mode)kPassModes[sPassModeRow];
	fadeThen(toPassTurn);
}

static void startPassRun() {
	startGame();
	sGame.passName = sPassNames[sPassTurn];
}

static void passNext() {
	sPassScores[sPassTurn] = sGame.score;
	sPassTurn++;
	fadeThen(sPassTurn < passCount() ? toPassTurn : toPassBoard);
}

static void toTitle() {
	sPassTurn = -1;
	sOptionsFromPause = false;
	toModes();
	sScreen = SCREEN_TITLE;
	pickSplash();
}

static void quitRun() {
	sGame.commitScore();
	toTitle();
}

static void pauseGame() {
	// Not while a run is ending: its score is only saved once that finishes.
	if (sScreen != SCREEN_GAME || sGame.phase == PHASE_OVER || sGame.phase == PHASE_DYING) return;
	sScreen = SCREEN_PAUSE;
	gfx::setShake(0, 0);
	audio::fuse(false);
	sFocus[SCREEN_PAUSE] = 1;
	sPressed = -1;
	sBlade.reset();
}

static float sMenuPointX = 160, sMenuPointY = 140, sMenuPointShow;  // the menu blade point (stickBlade, below)
static void stickBlade(Input& in);

#ifdef AUTOTEST
#include "autotest.inc"
#endif

static void updateTransitions() {
	if (sSliding) {
		sSlideT += kDt / kSlideTime;
		if (sSlideT >= 1) sSliding = false;
	}
	if (sFadingOut) {
		sFade += kDt / kFadeTime;
		if (sFade >= 1) {
			sFade = 1;
			sFadingOut = false;
			if (sFadeAction) sFadeAction();
			sFadeAction = nullptr;
		}
	} else if (sFade > 0) {
		sFade -= kDt / kFadeTime;
		if (sFade < 0) sFade = 0;
	}
}

static void updateFrame(const Input& in, bool wasTouching) {
	sTime += kDt;
	if (in.down & KEY_SELECT) sShowDebug = !sShowDebug;
	if (sSuspended) {
		sSuspended = false;
		pauseGame();
	}
	updateTransitions();
	updateWatch();
	if (sCut.on) {
		sCut.t += kDt;
		if (sCut.t > kCutLife) sCut.on = false;
	}
	if (menuBladeShown()) sMenuBlade.update(in);
	else sMenuBlade.reset();

	// Trick fruit effects that reach outside the field: a stunned blade goes grey,
	// honey drags it, and a chili rush speeds the music up a touch.
	const bool inGame = sScreen == SCREEN_GAME;
	gBladeDull = inGame && sGame.stunT > 0;
	gBladeDrag = inGame ? clampf(sGame.stickyT, 0, 1) : 0;
	audio::musicSpeed(inGame && sGame.chiliT > 0 ? 1.08f : 1.0f);

	// The game keeps moving during a fade, but menus ignore input while anything is sliding.
	const bool blocked = inputBlocked();
	switch (sScreen) {
		case SCREEN_TITLE: {
			if (blocked) break;
			if (sQuitT >= 0) break;  // the game is closing
			if (holdQuit(in, sFocus[SCREEN_TITLE])) {
				startQuit();
				break;
			}
			int t = menuInput(titleButtons(), kTitleAll, SCREEN_TITLE, in, wasTouching, kTitleSigns);
			if (t == kTitleQuit) {
				// A click or Enter alone does nothing: the bomb has to be held (holdQuit).
			} else if (t == 0) {
				// The very first Play starts the Practice run; it can be skipped from the pause menu.
				if (!gSave.practiceDone) {
					sStartMode = MODE_PRACTICE;
					fadeThen(startGame);
				} else {
					goTo(SCREEN_MODES, 1);
				}
			} else if (t == 1) goTo(SCREEN_OPTIONS, 1);
			else if (t == 2) {
				sFocus[SCREEN_JOURNAL] = 1;
				goTo(SCREEN_JOURNAL, 1);
			} else if (t == 3) goTo(SCREEN_STATS, 1);
			else if (t == 4) {
				sFocus[SCREEN_HOWTO] = 1;
				goTo(SCREEN_HOWTO, 1);
			} else if (t == 5) openShop();
			else if (t == 6) goTo(SCREEN_CREDITS, 1);
#if !PORT_3DS
			else if (t >= kTitleSigns) {
				audio::click();
				port::openLink(kTitleLinks[t - kTitleSigns]);
			}
#endif
#if PORT_ANDROID
			if (in.down & KEY_B) sLeaving = true;  // Back on the first screen leaves, as on any phone app
#endif
			break;
		}
		case SCREEN_MODES: {
			sModeSlide -= sModeSlide * fminf(1, kDt * 18);
			if (fabsf(sModeSlide) < 1) sModeSlide = 0;
			if (blocked) break;
			Input keys = in;
			if (updateModePages(keys, wasTouching)) break;
			Button signs[kModesPerPage + 1];
			const int count = modeSigns(sModePage, signs);
			int t = menuInput(signs, count, SCREEN_MODES, keys, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			const int mode = t > 0 ? modeOnSign(sModePage, t) : -1;
			if (t == 0) goTo(SCREEN_TITLE, -1);
			else if (mode == SIGN_MIXER || mode == SIGN_PASS) {
				for (int i = 0; i < kPassMax; i++)
					if (!sPassNames[i][0]) passDefaultName(i);
				sSection = sectionIndex(mode == SIGN_MIXER ? SID_MIXER : SID_PASS);
				sScroll = 0;
				sArmed = -1;
				sFocus[SCREEN_SECTION] = 0;
				goTo(SCREEN_SECTION, 1);
			} else if (mode == MODE_BEAT) {
				// Beat Slice picks its track first, playing each one as it is chosen.
				sFocus[SCREEN_BEAT] = 1;
#if PORT_CUSTOM_MUSIC
				custom::scan();  // songs added since last time
				sListenFailed = -1;
				listenFor(beatSong());
#endif
				audio::musicBeat(beatSong());
				beatScrollTo(beatSong());
				goTo(SCREEN_BEAT, 1);
			} else if (t > 0) {
				sStartMode = (Mode)mode;
				fadeThen(startGame);
			}
			break;
		}
		case SCREEN_BEAT: {
#if PORT_CUSTOM_MUSIC
			listenTick();
#endif
			if (blocked) break;
			// The arrows move along the list here, so they don't move between the signs.
			Input keys = in;
			keys.repeat &= ~(u32)(KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN);
			if (in.repeat & (KEY_UP | KEY_LEFT)) beatPick(-1);
			else if (in.repeat & (KEY_DOWN | KEY_RIGHT)) beatPick(1);
			{
				static int ids[kTrackCount + 64];
				const int n = beatList(ids);
				sBeatScroll -= in.wheel;
				static ListDrag drag;
				static float dragPos;
				static int pressedRow = -1;
				if (in.touching && !wasTouching) dragPos = sBeatScroll * kBeatRowStep;
				if (listDrag(drag, in, wasTouching, kBeatRowTop, kBeatRowTop + kBeatRows * kBeatRowStep, &dragPos)) {
					sBeatScroll = dragPos > 0 ? (int)(dragPos / kBeatRowStep + 0.5f) : 0;
					pressedRow = -1;
				}
				if (sBeatScroll > n - kBeatRows) sBeatScroll = n - kBeatRows;
				if (sBeatScroll < 0) sBeatScroll = 0;
				// A row is chosen when the press lifts, so a drag that starts on one only scrolls.
				if (!in.touching && wasTouching && pressedRow >= 0) {
					if (sBeatScroll + pressedRow < n && ids[sBeatScroll + pressedRow] != beatSong()) beatSelect(ids[sBeatScroll + pressedRow]);
					pressedRow = -1;
				}
				if (in.touching && !wasTouching) {
					pressedRow = -1;
					for (int r = 0; r < kBeatRows && sBeatScroll + r < n; r++)
						if (hit(beatRow(r), in.tx, in.ty)) pressedRow = r;
					if (n > kBeatRows && hit(kBeatUp, in.tx, in.ty) && sBeatScroll > 0) {
						audio::click();
						sBeatScroll--;
					}
					if (n > kBeatRows && hit(kBeatDown, in.tx, in.ty) && sBeatScroll + kBeatRows < n) {
						audio::click();
						sBeatScroll++;
					}
				}
			}
#if PORT_CUSTOM_MUSIC
			if (sFolderNote > 0) sFolderNote -= kDt;
			{
				const bool own = beatSong() >= kTrackCount;
				const bool tap = in.touching && !wasTouching;
				if (own && ((tap && hit(kBeatTool, in.tx, in.ty)) || (in.down & KEY_X))) {
					audio::click();
					listenStop();
					openBeatTool(beatSong() - kTrackCount);
					break;
				}
				if ((tap && hit(kBeatFolder, in.tx, in.ty)) || (in.down & KEY_Y)) {
					audio::click();
					if (!custom::openFolder()) sFolderNote = 6;  // no file manager to show it in: the path instead
				}
			}
#endif
			int t = menuInput(kBeatButtons, 2, SCREEN_BEAT, keys, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 0) {
#if PORT_CUSTOM_MUSIC
				listenStop();
#endif
				audio::music(MUSIC_MENU);
				goTo(SCREEN_MODES, -1);
			} else if (t == 1 && !audio::trackHasBeat(beatSong())) {
#if PORT_CUSTOM_MUSIC
				// Still listening: Play waits. Nothing found: the Beat Tool, to set it by ear.
				if (sListenSong < 0) openBeatTool(beatSong() - kTrackCount);
#endif
			} else if (t == 1) {
				saveWrite();  // keeps the chosen track
				sStartMode = MODE_BEAT;
				fadeThen(startGame);
			}
			break;
		}
		case SCREEN_UPDATE:
			if (!blocked) updateSignInput(in, wasTouching);
			break;
#if PORT_CUSTOM_MUSIC
		case SCREEN_BEATTOOL: {
			toolTick();
			if (blocked) break;
			// The arrows change the beat here, so they don't move between the signs.
			Input keys = in;
			keys.repeat &= ~(u32)(KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN);
			if (in.repeat & KEY_DOWN) toolChange(0);
			else if (in.repeat & KEY_UP) toolChange(1);
			else if (in.repeat & KEY_LEFT) toolChange(4);
			else if (in.repeat & KEY_RIGHT) toolChange(5);
			if (in.down & KEY_X) toolChange(6);
			if (in.down & KEY_Y) toolChange(7);
			if (in.touching && !wasTouching)
				for (int i = 0; i < kToolTapCount; i++)
					if (hit(kToolTaps[i], in.tx, in.ty)) toolChange(i);
			int t = menuInput(kToolButtons, 2, SCREEN_BEATTOOL, keys, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 0) {
				toolKeep();
				goTo(SCREEN_BEAT, -1);
			} else if (t == 1 && sTool.has && !sTool.listening) {
				toolKeep();
				gSave.beatTrack = (u8)(kTrackCount + sTool.song + 1);
				saveWrite();
				sStartMode = MODE_BEAT;
				fadeThen(startGame);
			}
			break;
		}
#endif
		case SCREEN_OPTIONS: {
			if (blocked) break;
			int t = menuInput(sOptionButtons, optionButtons(), SCREEN_OPTIONS, in, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 0) {
				if (sOptionsFromPause) goTo(SCREEN_PAUSE, -1);
				else goTo(SCREEN_TITLE, -1);
			}
			else if (t > 0) {
				sSection = sOptionSection[t];
#if PORT_HAS_DISPLAY
				if (kSections[sSection].id == SID_VISUALS) refreshResolutions();
#endif
				sScroll = 0;
				sArmed = -1;
				sFocus[SCREEN_SECTION] = 0;
				goTo(SCREEN_SECTION, 1);
			}
			break;
		}
		case SCREEN_SECTION: {
			if (blocked) break;
			updateSection(in, wasTouching);
			// A little figure-eight, so the Blade row shows the real trail.
			const Row* rows = kSections[sSection].rows;
			int f = sFocus[SCREEN_SECTION];
			if (f >= 0 && (rows[f].value == &gSave.blade || rows[f].value == &gSave.trail)) {
				Input fake{};
				fake.touching = true;
				fake.tx = (int)(200 + sinf(sTime * 3.1f) * 60);
				fake.ty = (int)(50 + sinf(sTime * 6.2f) * 30);
				sPreviewBlade.update(fake);
			}
			break;
		}
		case SCREEN_SHOP:
			if (!blocked) updateShop(in, wasTouching);
			break;
		case SCREEN_JOURNAL:
			if (!blocked) updateJournal(in, wasTouching);
			break;
		case SCREEN_HOWTO: {
			if (blocked) break;
			int t = menuInput(kHowtoButtons, 2, SCREEN_HOWTO, in, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 0) goTo(SCREEN_TITLE, -1);
			else if (t == 1) {
				sStartMode = MODE_PRACTICE;
				fadeThen(startGame);
			}
			break;
		}
		case SCREEN_CREDITS:
		case SCREEN_STATS: {
			if (blocked) break;
			int& f = sFocus[sScreen];
			f = 0;
			int t = menuInput(kOnlyBack, 1, sScreen, in, wasTouching);
			if (t == 0 || (in.down & KEY_B)) {
				if (t != 0) audio::click();
				goTo(SCREEN_TITLE, -1);
			}
			break;
		}
		case SCREEN_GAME:
#if PORT_PAUSE_BUTTON
			if (in.touching && !wasTouching && hit(kPauseSign, in.tx, in.ty)) {
				audio::click();
				pauseGame();
				break;
			}
#endif
			sBlade.update(sGame.bladeInput(in));
			sGame.update(sBlade);
			sGame.kickParticles();
			if (in.down & KEY_START) {
				pauseGame();
				break;
			}
			if (sGame.phase == PHASE_OVER) {
				sScreen = SCREEN_RESULT;
				sFocus[SCREEN_RESULT] = 1;
				sPressed = -1;
				sBlade.reset();
			}
			break;
		case SCREEN_PAUSE: {
			if (blocked) break;
			int t = menuInput(kPauseButtons, 4, SCREEN_PAUSE, in, wasTouching);
			if (t < 0 && (in.down & (KEY_START | KEY_B))) {
				t = 1;
				audio::click();
			}
			if (t == 0 || t == 1) {
				sOptionsFromPause = false;
				sScreen = SCREEN_GAME;
				sBlade.reset();
			} else if (t == 2) {
				sOptionsFromPause = true;
				sFocus[SCREEN_OPTIONS] = 1;
				goTo(SCREEN_OPTIONS, 1);
			} else if (t == 3) {
				if (sGame.mode == MODE_ZEN) {
					sScreen = SCREEN_GAME;
					sBlade.reset();
					sGame.finishRun();
				} else {
					fadeThen(sGame.mode == MODE_PRACTICE ? skipPractice : quitRun);
				}
			}
			break;
		}
		case SCREEN_PASS_TURN: {
			if (blocked) break;
			int t = menuInput(kPassTurnButtons, 2, SCREEN_PASS_TURN, in, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 0) fadeThen(toModes);
			else if (t == 1) fadeThen(startPassRun);
			break;
		}
		case SCREEN_LATENCY:
			updateLatency(in, wasTouching, blocked);
			break;
		case SCREEN_PASS_BOARD: {
			if (blocked) break;
			int t = menuInput(kResultButtons, 3, SCREEN_PASS_BOARD, in, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 1) {
				memset(sPassScores, 0, sizeof sPassScores);
				sPassTurn = 0;
				fadeThen(toPassTurn);
			} else if (t == 0 || t == 2) {
				fadeThen(toModes);
			}
			break;
		}
		case SCREEN_RESULT: {
			sGame.update(sBlade);  // keeps debris falling behind the buttons
			sGame.kickParticles();
#if !PORT_3DS
			if (sCardFrame) {
				const char* note;
				if (port::shareCardPoll(&note)) {
					sCardFrame = false;
					sCardNote = note;
					sCardNoteT = 4;
				}
			}
#endif
			sCardNoteT -= kDt;
			if (blocked) break;
			// A short lock-out stops a frantic swipe from tapping a sign by accident. Keys can't do that.
			if (sGame.phaseT <= (in.touching || wasTouching ? 0.7f : 0.25f)) break;
			if (sGame.mode == MODE_PRACTICE) {
				// The last lesson: slicing a menu fruit, which goes on to Pick a Game.
				int t = menuInput(kPracticeEndButtons, 2, SCREEN_RESULT, in, wasTouching);
				if (in.down & KEY_B) {
					audio::click();
					t = 0;
				}
				if (t == 0) fadeThen(toTitle);
				else if (t == 1) fadeThen(toModes);
				break;
			}
			if (sPassTurn >= 0) {
				int t = menuInput(kPassResultButtons, 2, SCREEN_RESULT, in, wasTouching);
				if (in.down & KEY_B) {
					audio::click();
					t = 0;
				}
				if (t == 0) fadeThen(toModes);
				else if (t == 1) passNext();
				break;
			}
			int t = menuInput(kResultButtons, kResultCount, SCREEN_RESULT, in, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 1) {
				sCardFrame = false;
				sStartMode = sGame.mode;
				fadeThen(startGame);
			} else if (t == 0 || t == 2) {
				sCardFrame = false;
				fadeThen(toModes);
			}
#if !PORT_3DS
			else if (t == 3 && !sCardFrame) {
				sCardFrame = true;  // drawn next, and kept from that frame
				port::shareCard(30, 12, 340, 212);
			}
#endif
			break;
		}
#if PORT_3DS
		case SCREEN_BENCH:
			benchUpdate(in);
			break;
#endif
		default:
			break;
	}
}

static void drawStickPoint();
[[maybe_unused]] static void drawMenuStickPoint();
#if PORT_TWO_SCREENS
static void drawFrame() {
	float e = sSlideT * sSlideT * (3 - 2 * sSlideT);  // eases in and out
	gHudPaused = sScreen == SCREEN_PAUSE;  // no lesson callout over the PAUSED sign
	gfx::beginFrame();
	for (int eye = 0; eye < 2; eye++) {
		if (!gfx::beginTop(eye)) continue;
		gfx::setBase(0, 0);
		drawTopBackground();
		if (sSliding) {
			gfx::setBase(-sSlideDir * kSlideTop * e, 0);
			swapOutgoingPage();
			drawTopContent(sSlideFrom);
			swapOutgoingPage();
			gfx::setBase(sSlideDir * kSlideTop * (1 - e), 0);
			drawTopContent(sScreen);
			gfx::setBase(0, 0);
		} else {
			drawTopContent(sScreen);
		}
		if (sFade > 0) {
			gfx::setDepth(0);
			gfx::rect(0, 0, 400, 240, rgba(0, 0, 0, (u8)(clampf(sFade, 0, 1) * 255)));
		}
		if (sShowDebug) drawDebug();
	}
	gfx::beginBottom();
	if (sSliding) {
		gfx::setBase(-sSlideDir * kSlideBottom * e, 0);
		swapOutgoingPage();
		drawBottomContent(sSlideFrom);
		swapOutgoingPage();
		gfx::setBase(sSlideDir * kSlideBottom * (1 - e), 0);
		drawBottomContent(sScreen);
		gfx::setBase(0, 0);
	} else {
		drawBottomContent(sScreen);
	}
	if (!sSliding) drawStickPoint();
	if (menuBladeShown()) sMenuBlade.draw();
	if (sFade > 0) gfx::rect(0, 0, 320, 240, rgba(0, 0, 0, (u8)(clampf(sFade, 0, 1) * 255)));
	gfx::endFrame();
}
#else
static bool showsField(Screen s) { return s == SCREEN_GAME || s == SCREEN_PAUSE || s == SCREEN_RESULT; }

// One screen: the board, then the field and its score sign if a game is on, then the menu layout on top.
static void drawFrame() {
	float e = sSlideT * sSlideT * (3 - 2 * sSlideT);
	bool field = showsField(sScreen) || (sSliding && showsField(sSlideFrom));
	gfx::beginFrame();
	gfx::drawBackdrop(field ? WHITE : rgba(205, 205, 205));
	if (field) {
		gfx::beginField();
		sGame.drawField(sBlade);
		gHudPaused = sScreen != SCREEN_GAME;
		sGame.drawHud();
		drawStickPoint();
#if PORT_PAUSE_BUTTON
		if (sScreen == SCREEN_GAME) {
			// Half see-through, so fruit passing behind it stays easy to see.
			gfx::sign(sprites_plank_idx, kPauseSign.x + 3, kPauseSign.y + 3, 30, 30, withAlpha(COL_PLANK, 0.55f));
			gfx::rect(kPauseSign.x + 12, kPauseSign.y + 10, 4, 16, withAlpha(COL_CREAM, 0.55f));
			gfx::rect(kPauseSign.x + 20, kPauseSign.y + 10, 4, 16, withAlpha(COL_CREAM, 0.55f));
		}
#endif
		if (sScreen == SCREEN_PAUSE) gfx::rect(kFillX, 0, kFillW, 240, rgba(30, 18, 10, 150));
	}
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 0) gfx::beginTop(0);
		else gfx::beginBottom();
		auto content = pass == 0 ? drawTopContent : drawBottomContent;
		float slide = pass == 0 ? kSlideTop : kSlideBottom;
		if (sSliding) {
			gfx::setBase(-sSlideDir * slide * e, 0);
			swapOutgoingPage();
			content(sSlideFrom);
			swapOutgoingPage();
			gfx::setBase(sSlideDir * slide * (1 - e), 0);
			content(sScreen);
			gfx::setBase(0, 0);
		} else {
			content(sScreen);
		}
		if (pass == 1 && menuBladeShown()) {
			sMenuBlade.draw();
			if (!sSliding) drawMenuStickPoint();
		}
	}
	// The Quit Game bomb sits in the window's own corner, outside both sides, and fades with a slide.
	{
		const float quitIn = sScreen == SCREEN_TITLE ? (sSliding ? e : 1) : (sSliding && sSlideFrom == SCREEN_TITLE ? 1 - e : 0);
		if (quitIn > 0 || sQuitT >= 0) {
			gfx::beginCorner();
			drawQuitBomb(sFocus[SCREEN_TITLE] == kTitleQuit, sQuitT >= 0 ? 1 : quitIn);
		}
	}
	// A message from starting up, such as the chosen renderer not working, shows under the logo for a while.
	static int noticeFrames = 0;
	const char* note = port::notice();
	if (note && sScreen == SCREEN_TITLE && !sSliding && noticeFrames < 60 * 10) {
		noticeFrames++;
		gfx::beginTop(0);
		float w = gfx::textWidth(note, 0.36f);
		gfx::rect(200 - w / 2 - 8, 214, w + 16, 22, rgba(30, 18, 10, 190));
		gfx::text(note, 200, 216, 0.36f, COL_CREAM, ALIGN_CENTER);
	}
	if (sFade > 0) gfx::fadeAll(clampf(sFade, 0, 1));
	gfx::endFrame();
}
#endif

// ---------------------------------------------------------------- loading screen
static void drawLoading(float progress) {
	gfx::beginFrame();
#if !PORT_TWO_SCREENS
	gfx::drawBackdrop(rgba(205, 205, 205));
#endif
	for (int eye = 0; eye < 2; eye++) {
		if (!gfx::beginTop(eye)) continue;
#if PORT_TWO_SCREENS
		drawTopBackground();
#endif
		gfx::setDepth(1.0f);
		gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 90);
		gfx::setDepth(0.3f);
		gfx::text(PORT_LOADING_TEXT, 200, 190, 0.38f, COL_CREAM, ALIGN_CENTER);
	}
	gfx::beginBottom();
	drawMenuBack();
	drawPlank(40, 84, 240, 72, false, false);
	gfx::text("Loading", 160, 92, 0.56f, COL_CREAM, ALIGN_CENTER);
	gfx::rect(58, 128, 204, 12, gfx::themeBar());
	gfx::rect(60, 130, 200 * clampf(progress, 0, 1), 8, COL_ACCENT);
	gfx::endFrame();
}

void app::showLoading() {
	const int gfxSteps = gfx::loadStepCount();
	const int audioSteps = audio::available() ? audio::loadStepCount() : 0;
	const int total = gfxSteps + 1 + audioSteps;
	int step = 0, frames = 0;
	// Work happens one step per frame. It always lasts at least a second, so that the title's arrival reads
	// as a moment and not a flicker.
	while (port::running() && (step < total || frames < 60)) {
		if (step < gfxSteps) gfx::loadStep(step);
		else if (step == gfxSteps) m3d::sync();
		else if (step < total) audio::loadStep(step - gfxSteps - 1);
		if (step < total) step++;
		frames++;
		float byStep = step / (float)total, byTime = frames / 60.0f;
		drawLoading(byStep < byTime ? byStep : byTime);
	}
	audio::music(MUSIC_MENU);
	sFade = 1;  // the title fades up out of black
	pickSplash();
	sScreen = SCREEN_TITLE;
#if PORT_3DS
	sLastTick = osGetTime();
#endif
}

bool app::init() {
	saveLoad();
#ifdef UPDATE_TEST
	update::boot("0.9.0");  // a test build that believes it's old, so the real updater has a release to fetch
#else
	update::boot(VERSION_STRING);
#endif
	applyAccessibility();
#ifdef TEST_ART
	gSave.artStyle = TEST_ART;
#endif
#ifdef TEST_DIFF
	gSave.difficulty = TEST_DIFF;
#endif
#if defined(AUTOTEST) && !defined(TEST_PRACTICE) && !defined(LAB)  // the Lab plays the lessons too
	gSave.practiceDone = 1;
#endif
#ifdef TEST_FX
	gSave.effects = TEST_FX;
#endif
#ifdef TEST_NOSFX
	gSave.sfx = 0;
#endif
#ifdef TEST_BOARD
	gSave.board = TEST_BOARD;
#endif
	if (!gfx::init()) {
		gfx::shutdown();
		return false;
	}
	m3d::init();
	jobs::init();
	bool audioOk = audio::init();
	sNoSound = !audioOk;
	DLOG("[boot] Fruit Slicer %s audio=%d new3ds=%d 3dscreen=%d\n", VERSION_STRING, audioOk, gIsNew3ds, gHas3dScreen);
	return true;
}

void app::notifySuspended() {
	sSuspended = true;
	// A phone may close the app from here without another word, so changed settings can't wait.
	if (sGame.keepBestSoFar()) sDirty = true;
	if (sDirty) saveWrite();
	sDirty = false;
}

bool app::inFieldView() { return sScreen == SCREEN_GAME; }

// ---------------------------------------------------------------- stick slicing
// During a game a stick moves a glowing point around the field. It slices while the slice button is
// held, or, with Flick chosen in Controls, whenever it moves fast. Touch and the mouse always win.

// In menus (Windows, Linux and Android), holding the right trigger turns the stick into a blade point that
// works like a finger: a swipe across a sign cuts it, and a pull and release on one picks it. Let go and the
// stick moves the selector again. The 3DS keeps its buttons for the menus and has the touch screen for this.
// (sMenuPointX, sMenuPointY and sMenuPointShow are declared above the Lab, which checks them.)
static void menuStickBlade(Input& in) {
#if !PORT_3DS
	if (!menuBladeShown() || in.touching) {
		if (in.touching || !menuBladeShown()) sMenuPointShow = 0;
		return;
	}
	if (!in.stickSlice) {
		if (sMenuPointShow > 0) sMenuPointShow -= kDt;
		return;
	}
	if (sMenuPointShow <= 0) {  // a fresh pull starts in the middle of the signs
		sMenuPointX = 160;
		sMenuPointY = 140;
	}
	sMenuPointShow = 1.5f;
	const float dead = 0.2f, sx = in.stickX, sy = in.stickY, mag = sqrtf(sx * sx + sy * sy);
	if (mag > dead) {
		const float k = clampf((mag - dead) / (1 - dead), 0, 1);
		const float speed = (80 + 560 * k * k) * (0.5f + gSave.stickSpeed / 100.0f);
		sMenuPointX = clampf(sMenuPointX + sx / mag * speed * kDt, 2, 318);
		sMenuPointY = clampf(sMenuPointY + sy / mag * speed * kDt, 2, 238);
	}
	// While it's a blade, the stick and D-pad don't move the selector as well.
	const u32 dirs = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
	in.down &= ~dirs;
	in.held &= ~dirs;
	in.repeat &= ~dirs;
	in.touching = true;
	in.tx = (int)sMenuPointX;
	in.ty = (int)sMenuPointY;
#else
	(void)in;
#endif
}

[[maybe_unused]] static void drawMenuStickPoint() {
	if (sMenuPointShow <= 0 || !menuBladeShown()) return;
	const float a = clampf(sMenuPointShow, 0, 1);
	// Bigger than the game's point: the menus are busier, and it has to be found among the signs.
	gfx::sprite(SHEET_SPRITES, sprites_glow_idx, sMenuPointX, sMenuPointY, 1.0f, 1.0f, 0, withAlpha(COL_ACCENT, a));
	gfx::sprite(SHEET_SPRITES, sprites_ring_idx, sMenuPointX, sMenuPointY, 0.32f, 0.32f, 0, withAlpha(COL_CREAM, 0.9f * a));
	gfx::rect(sMenuPointX - 2.5f, sMenuPointY - 2.5f, 5, 5, withAlpha(COL_CREAM, a));
}

static void stickBlade(Input& in) {
	if (sScreen != SCREEN_GAME) {
		menuStickBlade(in);
		return;
	}
	if (in.touching) {
		sPointShow = 0;
		return;
	}
	const float dead = 0.2f;
	float sx = in.stickX, sy = in.stickY, mag = sqrtf(sx * sx + sy * sy);
	if (mag > dead || in.stickSlice) sPointShow = 3;
	else if (sPointShow > 0) sPointShow -= kDt;
	if (sPointShow <= 0) return;
	float speed = 0;
	if (mag > dead) {
		// Further out goes faster, on a curve, so small pushes stay precise.
		float k = clampf((mag - dead) / (1 - dead), 0, 1);
		speed = (80 + 560 * k * k) * (0.5f + gSave.stickSpeed / 100.0f);
		sx /= mag;
		sy /= mag;
	}
	float vx = sx * speed, vy = sy * speed;
	float fx, fy;
	if (gSave.snapAssist && !sGame.mix[MIX_MIRROR] && speed > 60 && sGame.fruitAhead(sPointX, sPointY, sx, sy, 70, &fx, &fy)) {
		// Bend a third of the way toward the fruit, keeping the speed.
		float tx = fx - sPointX, ty = fy - sPointY, tl = sqrtf(tx * tx + ty * ty);
		if (tl > 1) {
			vx = lerpf(vx, tx / tl * speed, 0.35f);
			vy = lerpf(vy, ty / tl * speed, 0.35f);
		}
	}
	sPointX = clampf(sPointX + vx * kDt, 4, kFieldW - 4);
	sPointY = clampf(sPointY + vy * kDt, kFieldTop + 4, 236);
	in.touching = in.stickSlice || (gSave.stickStyle == 1 && speed > 320);
	in.tx = (int)sPointX;
	in.ty = (int)sPointY;
}

static void drawStickPoint() {
	if (sScreen != SCREEN_GAME || sPointShow <= 0 || sWasTouching) return;
	float a = clampf(sPointShow, 0, 1);
	const float x = sGame.mix[MIX_MIRROR] ? kFieldW - sPointX : sPointX;  // shows where the blade really is
	gfx::sprite(SHEET_SPRITES, sprites_glow_idx, x, sPointY, 0.5f, 0.5f, 0, withAlpha(COL_ACCENT, 0.8f * a));
	gfx::rect(x - 2, sPointY - 2, 4, 4, withAlpha(COL_CREAM, a));
}

bool app::frame(Input& in, int steps) {
#if PORT_3DS && defined(TEST_PROF)
	const u64 profStart = svcGetSystemTick();
#endif
	for (int i = 0; i < steps; i++) {
#ifdef AUTOTEST
		autotestInput(in);
		if (autotestDone()) return false;
#elif PORT_3DS
		// START+SELECT from the title quits, for builds launched from the Homebrew Launcher.
		if (sScreen == SCREEN_TITLE && (in.held & KEY_START) && (in.held & KEY_SELECT)) return false;
#endif

		stickBlade(in);
		gStickInUse = sPointShow > 0;
		updateFrame(in, sWasTouching);
		sWasTouching = in.touching;
		if (sScreen != SCREEN_PAUSE) boom::update(kDt);
#if !PORT_3DS
		if (sScreen != SCREEN_TITLE) sQuitHold = 0;  // a fuse lit on the title goes out when you leave it
#endif
#ifdef TEST_SLOWBOOM
		if (sQuitT >= 0) sQuitT += kDt * 0.12f;
#else
		if (sQuitT >= 0) sQuitT += kDt;
#endif
		if (sLeaving || sUpdateQuit || sQuitT > kQuitBlast) return false;
		// A press belongs to the first step only.
		in.down = 0;
		in.repeat = 0;
		in.wheel = 0;
	}

	// New scenery, boards and models load here, between frames. The Journal (also while it slides away) and
	// High Contrast draw 2D stickers even with 3D art, so the sticker sheet stays in for them.
	gfx::keepStickers(sScreen == SCREEN_JOURNAL || (sSliding && sSlideFrom == SCREEN_JOURNAL) || gSave.highContrast);
#if PORT_3DS && defined(TEST_PROF)
	const u64 profLogic = svcGetSystemTick();
#endif
	gfx::setBoardPicking(sScreen == SCREEN_SECTION && sFocus[SCREEN_SECTION] >= 0 &&
		kSections[sSection].rows[sFocus[SCREEN_SECTION]].value == &gSave.board);
	gfx::sync();
#if PORT_3DS && defined(TEST_PROF)
	const u64 profGfx = svcGetSystemTick();
#endif
	m3d::sync();
#if PORT_3DS && defined(TEST_PROF)
	const u64 profModels = svcGetSystemTick();
#endif
	audio::update(steps * kDt);
#if PORT_3DS && defined(TEST_PROF)
	const u64 profSync = svcGetSystemTick();
#endif

	drawFrame();
#if PORT_3DS && defined(TEST_PROF)
	{
		// Profiling builds: each second, how long the game's own work took on this screen. In Azahar the clock
		// counts emulated CPU work, so it ranks what costs most even though it isn't the console's real speed.
		static u64 profPrev, profFrom;
		static float logicSum, logicMax, syncSum, syncMax, cpuSum, cpuMax, gapMax, gfxMax, modelMax, audioMax;
		static int frames, slow;
		const u64 end = svcGetSystemTick();
		const float tick = (float)CPU_TICKS_PER_MSEC;
		const float logic = (profLogic - profStart) / tick, sync = (profSync - profLogic) / tick, cpu = C3D_GetProcessingTime();
		const float gap = profPrev ? (end - profPrev) / tick : 16.7f;
		profPrev = end;
		if (!profFrom) profFrom = end;
		logicSum += logic;
		syncSum += sync;
		cpuSum += cpu;
		logicMax = fmaxf(logicMax, logic);
		syncMax = fmaxf(syncMax, sync);
		cpuMax = fmaxf(cpuMax, cpu);
		gapMax = fmaxf(gapMax, gap);
		gfxMax = fmaxf(gfxMax, (profGfx - profLogic) / tick);
		modelMax = fmaxf(modelMax, (profModels - profGfx) / tick);
		audioMax = fmaxf(audioMax, (profSync - profModels) / tick);
		frames++;
		slow += gap > 17.5f;
		if ((end - profFrom) / tick >= 1000) {
			fprintf(stderr, "[prof] scr %d %s %s | logic %.2f/%.2f sync %.2f/%.2f draw %.2f/%.2f | sheets %.1f models %.1f music %.1f | gap max %.1f slow %d/%d\n", (int)sScreen,
			        sScreen == SCREEN_GAME ? modeName(sGame.mode) : "-", gSave.artStyle == ART_2D ? "2D" : "3D", logicSum / frames, logicMax,
			        syncSum / frames, syncMax, cpuSum / frames, cpuMax, gfxMax, modelMax, audioMax, gapMax, slow, frames);
			logicSum = logicMax = syncSum = syncMax = cpuSum = cpuMax = gapMax = gfxMax = modelMax = audioMax = 0;
			frames = slow = 0;
			profFrom = end;
		}
	}
#endif
#if PORT_3DS
	if (sScreen == SCREEN_BENCH && !sBenchDone) benchMeasure();

	u64 now = osGetTime();
	if (now > sLastTick) sFps = lerpf(sFps, 1000.0f / (float)(now - sLastTick), 0.1f);
	sLastTick = now;
#endif
	return true;
}

void app::shutdown() {
	if (sScreen == SCREEN_GAME || sScreen == SCREEN_PAUSE || sOptionsFromPause) sGame.commitScore();
	if (sDirty) saveWrite();
	jobs::shutdown();
	m3d::shutdown();
	audio::shutdown();
	gfx::shutdown();
}
