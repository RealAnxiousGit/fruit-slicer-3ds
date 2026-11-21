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
#include "leaderboard.h"
#include "model3d.h"
#include "render.h"
#include "progress.h"
#include "shop.h"
#include "streetpass.h"
#include "update.h"
#include "sprites.h"

#if PORT_3DS
#include <sys/stat.h>
#include "bgtop0.h"
// Extra headroom above ctrulib's 32 KB default for nested C++/Lab call stacks.
extern "C" u32 __stacksize__ = 96 * 1024;
#endif
#include "logo.h"
#include "port_api.h"
#if PORT_CUSTOM_MUSIC
#include "custom_music.h"
#endif

#define VERSION_STRING "2.0.0"

#if PORT_TWO_SCREENS
static constexpr float kSlideTop = 400, kSlideBottom = 320;
#else
static constexpr float kSlideTop = 900, kSlideBottom = 900;
#endif

enum Screen {
	SCREEN_LOADING, SCREEN_TITLE, SCREEN_MODES, SCREEN_OPTIONS, SCREEN_SECTION, SCREEN_CREDITS, SCREEN_HOWTO, SCREEN_GUIDES, SCREEN_SHOP,
	SCREEN_JOURNAL, SCREEN_STATS, SCREEN_BEAT, SCREEN_BEATTOOL, SCREEN_UPDATE,
	SCREEN_GAME, SCREEN_PAUSE, SCREEN_RESULT, SCREEN_BENCH, SCREEN_PASS_TURN, SCREEN_PASS_BOARD, SCREEN_LATENCY, SCREEN_BOSS_TEST, SCREEN_COUNT
};

static Screen sScreen = SCREEN_LOADING;
static Screen sDrawing = SCREEN_LOADING;  // the screen whose signs are being drawn, during a slide too
static Game sGame;  // static storage: all gameplay pools are allocated once at load time
static Blade sBlade;
static int sFocus[SCREEN_COUNT];
static int sPressed = -1;
static bool sLeaving = false;
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
bool gBossTestDone = false;
static bool sBossTestUseRush = false;
static bool sBossTestRoyal = false;
static int sBossTestDiff = DIFF_NORMAL;
static int sBossTestPreset = 0;
static int sBossTestBoss = BOSS_MELON;
#endif
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

static u32 lerpColor(u32 a, u32 b, float t) {
	t = clampf(t, 0.0f, 1.0f);
	const int ar = (int)(a & 255), ag = (int)((a >> 8) & 255), ab = (int)((a >> 16) & 255), aa = (int)((a >> 24) & 255);
	const int br = (int)(b & 255), bg = (int)((b >> 8) & 255), bb = (int)((b >> 16) & 255), ba = (int)((b >> 24) & 255);
	return rgba((int)(ar + (br - ar) * t + 0.5f), (int)(ag + (bg - ag) * t + 0.5f), (int)(ab + (bb - ab) * t + 0.5f), (int)(aa + (ba - aa) * t + 0.5f));
}

struct HighlightSlot {
	int x, y, w, h;
	Screen scr;
	bool used;
	float v;
	u32 lastFrame;
};
static constexpr int kHiSlots = 64;
static HighlightSlot sHiSlots[kHiSlots];
static u32 sHiFrame = 1;

static void beginHighlightFrame() {
	sHiFrame++;
	if (!sHiFrame) {
		for (auto& s : sHiSlots) s.lastFrame = 0;
		sHiFrame = 1;
	}
}

static float highlightFade(float x, float y, float w, float h, bool focused) {
	const int ix = (int)roundf(x), iy = (int)roundf(y), iw = (int)roundf(w), ih = (int)roundf(h);
	HighlightSlot* pick = nullptr;
	HighlightSlot* oldest = &sHiSlots[0];
	for (auto& s : sHiSlots) {
		if (s.used && s.scr == sDrawing && s.x == ix && s.y == iy && s.w == iw && s.h == ih) {
			pick = &s;
			break;
		}
		if (!s.used || sHiFrame - s.lastFrame > 4) {
			if (!pick) pick = &s;
		} else if (s.lastFrame < oldest->lastFrame) {
			oldest = &s;
		}
	}
	if (!pick) pick = oldest;
	const bool fresh = !pick->used || pick->scr != sDrawing || pick->x != ix || pick->y != iy || pick->w != iw || pick->h != ih || (sHiFrame - pick->lastFrame > 4);
	if (fresh) {
		pick->x = ix;
		pick->y = iy;
		pick->w = iw;
		pick->h = ih;
		pick->scr = sDrawing;
		pick->used = true;
		pick->v = focused ? 1.0f : 0.0f;
		pick->lastFrame = sHiFrame;
		return pick->v;
	}
	if (pick->lastFrame != sHiFrame) {
		pick->lastFrame = sHiFrame;
		const float target = focused ? 1.0f : 0.0f;
		const float step = kDt * 9.5f;  // ~0.105s crisp highlight fade
		if (pick->v < target) pick->v = fminf(target, pick->v + step);
		else if (pick->v > target) pick->v = fmaxf(target, pick->v - step);
	}
	return pick->v;
}

struct HoldRepeat {
	int id = -1;
	float elapsed = 0;
	float nextAt = 0;
	void reset() { id = -1; elapsed = nextAt = 0; }
	bool step(int which, bool active, bool fireOnPress = true) {
		if (!active || which < 0) {
			reset();
			return false;
		}
		if (which != id) {
			id = which;
			elapsed = 0;
			nextAt = 0.34f;  // short hold delay before auto-repeat begins
			return fireOnPress;
		}
		elapsed += kDt;
		if (elapsed >= nextAt) {
			const float interval = clampf(0.16f * powf(0.45f, fmaxf(0.0f, elapsed - 0.34f)), 0.03f, 0.16f);
			nextAt += interval;
			return true;
		}
		return false;
	}
};

static bool cursorVisible() { return (((int)(sTime * 2.4f)) & 1) == 0; }

static void drawTypingWithCursor(const char* typed, float cx, float y, float scale, float room, u32 color, bool active = true) {
	float w = gfx::textWidth(typed, scale);
	const float avail = room > 8.0f ? room - 6.0f : room;
	if (w > avail && w > 0) scale *= avail / w;
	w = gfx::textWidth(typed, scale);
	gfx::text(typed, cx, y, scale, color, ALIGN_CENTER);
	if (active && cursorVisible()) {
		const float curX = cx + w * 0.5f + 1.5f;
		const float curW = fmaxf(1.5f, 3.0f * scale);
		const float curH = 34.0f * scale;
		gfx::rect(curX, y + 2.0f * scale, curW, curH, COL_ACCENT);
	}
}

static const Button kTitleButtons[] = {
#if PORT_3DS
	{40, 14, 240, 80, "Play"},
	{10, 102, 96, 54, "Options"},  {112, 102, 96, 54, "Journal"}, {214, 102, 96, 54, "Stats"},
	{10, 166, 96, 46, "How to Play"}, {112, 166, 96, 46, "Shop"}, {214, 166, 96, 46, "Credits"},
#else
	// The fruit sit a little higher here, to leave the bottom-left corner to the link icons.
	{40, 16, 240, 70, "Play"},
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
	const float hi = highlightFade(b.x, b.y, b.w, b.h, focused);
	const float a = lerpf(0.55f, 1.0f, hi);
	const u32 ink = withAlpha(rgba(16, 16, 18), lerpf(0.8f, 1.0f, hi)), plate = withAlpha(COL_CREAM, a);
	if (hi > 0.01f) softEllipse(cx, cy, r + 2.5f, r + 2.5f, withAlpha(COL_ACCENT, hi));
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
// The Mixer changes any mode, so where the info side can be touched it is a fruit of its own under the
// mode's info sign. The 3DS can't touch its top screen, so there it stays a sign among the modes.
static constexpr bool kMixerFruit = !PORT_3DS;
#if PORT_3DS
static const u8 kModeOrder[] = {MODE_CLASSIC, MODE_ARCADE, MODE_FRENZY, MODE_ZEN, MODE_SUDDEN, MODE_DAILY, MODE_MINEFIELD, MODE_COMBO, MODE_BEAT, MODE_ORCHARD, MODE_BOSSRUSH, MODE_RECIPE, MODE_SHARP, MODE_JUGGLE, SIGN_MIXER, SIGN_PASS};
#else
static const u8 kModeOrder[] = {MODE_CLASSIC, MODE_ARCADE, MODE_FRENZY, MODE_ZEN, MODE_SUDDEN, MODE_DAILY, MODE_MINEFIELD, MODE_COMBO, MODE_BEAT, MODE_ORCHARD, MODE_BOSSRUSH, MODE_RECIPE, MODE_SHARP, MODE_JUGGLE, SIGN_PASS};
#endif
static constexpr int kModesComing = 0;  // modes not built yet, so they have no sign
static_assert(sizeof kModeOrder == MODE_COUNT + 2 - (kMixerFruit ? 1 : 0) - kModesComing, "a sign for every mode");
static constexpr int kModesPerPage = 6;
static constexpr int kModePages = ((int)sizeof kModeOrder + kModesPerPage - 1) / kModesPerPage;
static constexpr float kModeTop = 46, kModeStep = 58, kModeSignW = 124, kModeSignH = 52;
static constexpr float kPageArrowY = kModeTop + kModeStep + kModeSignH * 0.5f - 25;
static int sModePage = 0, sModeOldPage = 0;
static float sModeSlide = 0;         // how far the new page still has to travel to settle
static int sArrowPressed = -1, sArrowHover = -1;
// The arrows hit cleanly around their visual plank without bleeding into adjacent signs.
static const Button kPageArrows[2] = {{0, kPageArrowY - 6, 32, 62, nullptr}, {288, kPageArrowY - 6, 32, 62, nullptr}};

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

// The Mixer fruit keeps one place in Pick a Game's list on every page, after the six sign places.
static constexpr int kMixFocus = kMixerFruit ? kModesPerPage + 1 : -5;
static constexpr float kMixPicX = 200, kMixPicY = 221;  // its middle, on the picture side

// What a place in Pick a Game's list opens: a mode, the Mixer or Pass and Play. -1 for an empty place.
static int modeAt(int page, int sign) {
	if (sign == kMixFocus) return SIGN_MIXER;
	const int i = page * kModesPerPage + sign - 1;
	return sign >= 1 && sign <= kModesPerPage && i < (int)sizeof kModeOrder ? kModeOrder[i] : -1;
}

#if !PORT_3DS
// Where the Mixer fruit is, in the units of Pick a Game's signs, which reach across the whole window.
static Button mixerButton() {
	float x0, y0, x1, y1;
	gfx::topToCorner(kMixPicX - 58, kMixPicY - 17, &x0, &y0);
	gfx::topToCorner(kMixPicX + 58, kMixPicY + 17, &x1, &y1);
	return {x0, y0, x1 - x0, y1 - y0, "Mixer"};
}

// The fruit sits at the left of its place, with the name beside it.
static void mixerFruitSpot(const Button& b, float* x, float* y, float* d) {
	*d = b.h * 0.82f;
	*x = b.x + b.h * 0.6f;
	*y = b.y + b.h * 0.5f;
}
#endif

// Pick a Game's whole list: the back arrow, this page's signs, empty places up to six, and the Mixer fruit.
static int modeList(int page, Button* out) {
	int n = modeSigns(page, out);
#if !PORT_3DS
	for (; n <= kModesPerPage; n++) out[n] = {-1000, -1000, 0, 0, nullptr};
	out[kMixFocus] = mixerButton();
	n = kMixFocus + 1;
#endif
	return n;
}

static const Button kOnlyBack[] = {kBack};
static const Button kHowtoButtons[] = {kBack, {20, 188, 134, 48, "Practice"}, {166, 188, 134, 48, "Guides"}};
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
static char sAddNote[64];      // what Add Songs did, shown under the list for a few seconds
static float sAddNoteT = 0;
#endif
#if PORT_3DS
static const Button kResultButtons[] = {kBack, {20, 170, 134, 52, "Again"}, {166, 170, 134, 52, "Modes"}, {84, 108, 152, 46, "Top 10"}};
static constexpr int kResultCount = 4;
static constexpr int kResultTopIdx = 3;
#else
static const Button kResultButtons[] = {kBack, {20, 168, 134, 52, "Again"}, {166, 168, 134, 52, "Modes"}, {166, 104, 134, 48, "Share"}, {20, 104, 134, 48, "Top 10"}};
static constexpr int kResultCount = 5;
static constexpr int kResultTopIdx = 4;
#endif
static bool sResultBoard = false;
static bool sLbTypingName = false;
static int sLbNameFocus = 0;
static bool sCardFrame;          // this frame is the score card: the sign shows the logo, not the coins
static const char* sCardNote;    // what became of the last card, shown for a while
static float sCardNoteT;
#if PORT_PAUSE_BUTTON
static const Button kPauseSign = {2, kFieldTop + 5, 36, 36, nullptr};  // in field units: top-left, cleanly below score bar & Frenzy meter
static const Button kInvSign = {40, kFieldTop + 5, 36, 36, nullptr};   // mobile: opens Inventory in Orchard Blast
#endif
static const Button kPauseButtons[] = {kBack, {50, 44, 220, 56, "Resume"}, {50, 108, 220, 56, "Options"}, {50, 172, 220, 56, "Main Menu"}};
static const Button kPauseOrchardButtons[] = {kBack, {50, 38, 220, 44, "Resume"}, {50, 88, 220, 44, "Options"}, {50, 138, 220, 44, "Start Over"}, {50, 188, 220, 44, "Main Menu"}};
static const Button kStartOverConfirmBtns[2] = {{54, 144, 96, 32, "Yes"}, {170, 144, 96, 32, "No"}};
static bool sPauseStartOverConfirm = false;
static int sPauseConfirmFocus = 1;

static bool hit(const Button& b, int x, int y) {
#if PORT_PAUSE_BUTTON
	// The way out is small for a fingertip, so it answers a little beyond its edges.
	if (touchUiActive() && b.x == kBack.x && b.y == kBack.y && b.w == kBack.w && b.h == kBack.h) return x >= 0 && x < b.x + b.w + 12 && y >= 0 && y < b.y + b.h + 2;
#endif
	return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static u32 withDim(u32 c) {
	u8 r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
	return rgba(r * 4 / 5, g * 4 / 5, b * 4 / 5);
}

static void drawPlank(float x, float y, float w, float h, bool focused, bool pressed) {
	float dy = pressed ? 2 : 0;
	const float hi = highlightFade(x, y, w, h, focused);
	gfx::sign(sprites_plank_idx, x, y + dy, w, h, pressed ? withDim(COL_PLANK) : COL_PLANK);
	if (hi > 0.01f) gfx::sign(sprites_plank_focus_idx, x - 2, y - 2 + dy, w + 4, h + 4, withAlpha(COL_ACCENT, hi));
}

// A line centred on x, made smaller only if it would still be wider than room.
static void fitText(const char* s, float x, float y, float scale, float room, u32 color, Align align = ALIGN_CENTER) {
	const float w = gfx::textWidth(s, scale);
	if (w > room) scale *= room / w;
	gfx::text(s, x, y, scale, color, align);
}

// A label too wide for room at this scale, split at the space that leaves the two halves most even. False
// (and nothing written) when it fits as it is, or has no space to split at.
static bool splitLine(const char* s, float scale, float room, char* first, char* second, size_t size) {
	if (gfx::textWidth(s, scale) <= room) return false;
	const int n = (int)strlen(s);
	int best = -1;
	float bestWide = 1e9f;
	char a[64];
	for (int i = 1; i < n - 1 && i < (int)sizeof a - 1; i++) {
		if (s[i] != ' ') continue;
		snprintf(a, sizeof a, "%.*s", i, s);
		const float wide = fmaxf(gfx::textWidth(a, scale), gfx::textWidth(s + i + 1, scale));
		if (wide < bestWide) {
			bestWide = wide;
			best = i;
		}
	}
	if (best < 0) return false;
	snprintf(first, size, "%.*s", best, s);
	snprintf(second, size, "%s", s + best + 1);
	return true;
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
		{"Journal", MF_KIWI}, {"Stats", MF_PEACH}, {"Practice", MF_APPLE}, {"Guides", MF_PEAR}, {"Let's Play", MF_MELON}, {"Mixer", MF_COCONUT},
		{"Resume", MF_APPLE}, {"Main Menu", MF_PEAR}, {"Again", MF_PEACH}, {"Modes", MF_KIWI}, {"Keep", MF_LEMON}, {"Try Again", MF_PEACH}, {"Share", MF_PLUM},
		{"Audio", MF_APPLE}, {"Visuals", MF_PEACH}, {"Visuals and Display", MF_PEACH}, {"Gameplay and Controls", MF_PINEAPPLE}, {"Accessibility", MF_PEAR}, {"Scores and Data", MF_PLUM},
		{"StreetPass", MF_MELON}, {"Enable", MF_MELON}, {"Not Now", MF_APPLE}, {"Challenge", MF_MELON},
		{"Top 10", MF_GOLD}, {"Summary", MF_PEAR},
		{"Yes", MF_MELON}, {"No", MF_APPLE}, {"Update Now", MF_GOLD}, {"Install Now", MF_GOLD},
		{"Later", MF_ORANGE}, {"Restart", MF_PINEAPPLE}, {"Start Over", MF_PINEAPPLE}, {"Close", MF_PEAR}, {"OK", MF_PEACH},
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
		if (m.mesh == m3d::MESH_GOLD) m3d::setSkin(&shopSkinLook(0));
		m3d::Pose pose = {x, y, 0, 0.25f, spin, rot, s, 0, 0, 0};
		m3d::draw(m.mesh, (m3d::Part)part, pose);
		m3d::setSkin(nullptr);
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
static bool isCut(const Button& b) {
	return sCut.on && sCut.screen == sDrawing && sCut.btn.x == b.x && sCut.btn.y == b.y && sCut.btn.w == b.w && sCut.btn.h == b.h;
}
static Blade sMenuBlade;                  // the trail a swipe leaves across the menus
static bool menuBladeShown() {
	// Not on Credits: a drag there scrolls the list, and a blade trail would follow it.
	return sScreen == SCREEN_TITLE || sScreen == SCREEN_MODES || sScreen == SCREEN_OPTIONS || sScreen == SCREEN_HOWTO || sScreen == SCREEN_GUIDES || sScreen == SCREEN_JOURNAL || sScreen == SCREEN_STATS || sScreen == SCREEN_BEAT || sScreen == SCREEN_BEATTOOL || sScreen == SCREEN_UPDATE || sScreen == SCREEN_PAUSE || sScreen == SCREEN_RESULT || sScreen == SCREEN_PASS_TURN || sScreen == SCREEN_PASS_BOARD || sScreen == SCREEN_LATENCY;
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
		*y = b.y + (b.h - 14) * 0.5f;
	}
}
static float fruitSize(const Button& b) {
	float x, y, d;
	fruitSpot(b, &x, &y, &d);
	return d;
}
// A menu fruit answers only around the fruit itself (and its name just under it), not across the
// whole sign box, so a hover or swipe in the gap between fruit picks nothing. Mode signs, the Mixer
// and the back berry keep their own boxes.
static bool menuHit(const Button& b, Screen screen, int x, int y) {
	if (!b.label || screen == SCREEN_MODES) return hit(b, x, y);
	float fx, fy, d;
	fruitSpot(b, &fx, &fy, &d);
	const float half = d * 0.5f + 8;
	const float top = fy - d * 0.5f - 6, bottom = fy + d * 0.5f + (fruit3D() ? 8 : 16);
	return x >= fx - half && x < fx + half && y >= top && y < bottom && hit(b, x, y);
}

// The name on a tilted ring round the fruit, swaying gently to and fro so the whole word always faces
// the front and reads in one go. Each letter narrows as it turns away and fades before the side of the
// ring, so letters never crowd there, and nothing is drawn on the far side, so nothing reads backwards.
// All the outlines go down first and every fill on top, so the letters join up like printed text.
// The ring a name circles on: wide enough that the name spans about a third of it, so it reads in one go at
// the front, but no wider than its sign. ringFit is how much the name must shrink to fit it (1: not at all).
static float ringRadius(float w, float d, float maxW) {
	const float rx = fmaxf(d * 0.5f + 10, w / 1.7f);
	return rx > maxW * 0.5f - 4 ? maxW * 0.5f - 4 : rx;
}
static float ringFit(const char* label, float d, float maxW, float scale) {
	const float w = gfx::textWidth(label, scale), rx = ringRadius(w, d, maxW);
	return w > rx * 1.5f ? rx * 1.5f / w : 1.0f;
}

static void drawRingText(const char* label, float cx, float cy, float d, float maxW, float scale, u32 color) {
	int n = (int)strlen(label);
	if (n == 0 || n > 31) return;
	float w = gfx::textWidth(label, scale);
	float rx = ringRadius(w, d, maxW);
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
	const float hi = highlightFade(b.x, b.y, b.w, b.h, focused);
	const u32 labelCol = lerpColor(COL_CREAM, COL_ACCENT, hi);
	if (hi > 0.01f) gfx::sprite(SHEET_SPRITES, sprites_ring_idx, x, y + dy, d / 34, d / 34, 0, withAlpha(COL_ACCENT, 0.9f * hi));
	float bob = hi > 0.25f && !gSave.calmFx ? sinf(sTime * 4) * 1.5f * hi : 0;
	float spin = sTime * 0.8f + b.x * 0.05f;
	if (fruit3D()) {
		float ts = scale * 0.9f;
		if (!cut) drawMenuFruit(fruit, x, y + dy + bob, d, spin);
		// A name too long for one ring wraps onto a second, so it stays the same size as its neighbours: after
		// "and" when it has one, or else at the space that leaves the two halves most even.
		const char* split = strlen(label) > 16 ? strstr(label, " and ") : nullptr;
		char first[32], second[32];
		if (split) {
			snprintf(first, sizeof first, "%.*s and", (int)(split - label), label);
			snprintf(second, sizeof second, "%s", split + 5);
		}
		if (split || (ringFit(label, d, b.w, ts) < 0.8f && splitLine(label, ts, 0, first, second, sizeof first))) {
			drawRingText(first, x, y + dy - 6, d, b.w, ts * 0.8f, labelCol);
			drawRingText(second, x, y + dy + 9, d, b.w, ts * 0.8f, labelCol);
		} else {
			drawRingText(label, x, y + dy, d, b.w, ts, labelCol);
		}
	} else {
		if (!cut) drawMenuFruit(fruit, x, y + dy + bob, d, spin);
		float ts = 0.4f, w = gfx::textWidth(label, ts);
		if (w > b.w - 6) ts *= (b.w - 6) / w;
		gfx::text(label, x, y + d * 0.5f + 1 + dy, ts, labelCol, ALIGN_CENTER);
	}
}

static void drawSign(const Button& b, const char* label, bool focused, bool pressed, float scale = 0.56f, int icon = -1, int fruit = -1) {
	if (fruit >= 0) {
		drawFruitChoice(b, label, focused, pressed, scale, fruit);
		return;
	}
	drawPlank(b.x, b.y, b.w, b.h, focused, pressed);
	const float hi = highlightFade(b.x, b.y, b.w, b.h, focused);
	float dy = pressed ? 2 : 0;
	float lead = icon >= 0 ? 30 : 0;
	float w = gfx::textWidth(label, scale);
	float room = b.w - 22 - lead;
	if (w > room) scale *= room / w;
	float cx = b.x + b.w * 0.5f + (icon >= 0 ? 12 : 0);
	// The coin sprite is drawn bigger than the mode icons, so it is scaled down to match them.
	const float iconScale = icon == sprites_coin_idx ? 0.46f : 0.8f;
	if (icon >= 0) gfx::sprite(SHEET_SPRITES, icon, b.x + 21, b.y + b.h * 0.5f + dy, iconScale, iconScale);
	gfx::text(label, cx, b.y + b.h * 0.5f - 20 * scale - 1 + dy, scale, lerpColor(COL_CREAM, COL_ACCENT, hi), ALIGN_CENTER);
}

// A mode on Pick a Game: its icon over its name, so the whole width of the sign goes to the name.
static void drawModeTile(const Button& b, int icon, bool focused, bool pressed) {
	drawPlank(b.x, b.y, b.w, b.h, focused, pressed);
	const float hi = highlightFade(b.x, b.y, b.w, b.h, focused);
	const float dy = pressed ? 2 : 0, cx = b.x + b.w * 0.5f, iconScale = icon == sprites_coin_idx ? 0.42f : 0.72f;
	float scale = 0.44f;
	const float w = gfx::textWidth(b.label, scale);
	if (w > b.w - 22) scale *= (b.w - 22) / w;
	gfx::sprite(SHEET_SPRITES, icon, cx, b.y + 16 + dy, iconScale, iconScale, icon == sprites_arrow_idx ? 3.14159265f : 0);  // the arrow points on to the next player
	gfx::text(b.label, cx, b.y + 28 + dy, scale, lerpColor(COL_CREAM, COL_ACCENT, hi), ALIGN_CENTER);
}

static void drawPageArrow(int side) {
	const float x = side ? 291 : 3, dy = sArrowPressed == side ? 2 : 0;
	drawPlank(x, kPageArrowY, 26, 50, sArrowHover == side, sArrowPressed == side);
	const float hi = highlightFade(x, kPageArrowY, 26, 50, sArrowHover == side);
	gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, x + 13, kPageArrowY + 24 + dy, 0.62f, 0.62f, side ? 3.14159265f : 0, lerpColor(COL_CREAM, COL_ACCENT, hi));
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
		if (i == focus || btns[i].w <= 0 || sameRow(btns[i], c) != (dy == 0)) continue;  // w 0: an empty place
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
			if (i == focus || btns[i].w <= 0 || !sameRow(btns[i], row) || sameRow(btns[i], c)) continue;
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
	const bool mixer = screen == SCREEN_MODES && i == kMixFocus;
	bool hasFruit = i == 0 || screen != SCREEN_MODES || mixer;  // mode signs show their icon, not a fruit
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
#if !PORT_3DS
	} else if (mixer) {
		float d;
		mixerFruitSpot(b, &sCut.x, &sCut.y, &d);
#endif
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
	static bool done;
	const bool mouse = !PORT_3DS && !touchUiActive();
	const float minSwipe = gSave.sliceAssist ? 12.0f : 30.0f;
	if (in.touching) {
		if (!wasTouching) {
			lastX = in.tx;
			lastY = in.ty;
			length = 0;
			done = false;
			downOn = sPressed = -1;
			for (int i = 0; i < n; i++)
				if (menuHit(btns[i], screen, in.tx, in.ty)) downOn = sPressed = focus = i;
			return -1;
		}
		if (sPressed >= 0 && !menuHit(btns[sPressed], screen, in.tx, in.ty)) sPressed = -1;
		float dx = in.tx - lastX, dy = in.ty - lastY, d = sqrtf(dx * dx + dy * dy);
		if (done || d < 0.5f) return -1;
		// Walked in small steps, so a fast swipe can't jump over a sign between two frames.
		int steps = (int)(d / 4) + 1;
		for (int s = 1; s <= steps; s++) {
			float px = lastX + dx * s / steps, py = lastY + dy * s / steps;
			length += d / steps;
			if (length < minSwipe) continue;
			for (int i = 0; i < n; i++) {
				if (!menuHit(btns[i], screen, (int)px, (int)py)) continue;
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
		if (!done && downOn >= 0 && length < minSwipe && tapOk && menuHit(btns[downOn], screen, (int)lastX, (int)lastY))
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
			if (i < slices ? menuHit(btns[i], screen, in.tx, in.ty) : hit(btns[i], in.tx, in.ty)) focus = i;
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
// corner: the Mixer fruit's cut, which is drawn with the fruit over the whole window, not on the signs.
static void drawCut(Screen screen, bool corner = false) {
	if (!sCut.on || sCut.screen != screen) return;
	if ((screen == SCREEN_MODES && sCut.index == kMixFocus) != corner) return;
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
	float size = sCut.index == 0 ? 18 : (sCut.screen == SCREEN_MODES ? sCut.btn.h * 0.82f : fruitSize(sCut.btn));  // the Mixer fruit
	float off = 4 + 70 * t, fall = 300 * t * t, spin = gSave.calmFx ? 0 : t * 3;
	drawMenuFruit(sCut.fruit, sCut.x - ux * off, sCut.y - uy * off + fall, size, 0, r0 - spin, 1);
	drawMenuFruit(sCut.fruit, sCut.x + ux * off, sCut.y + uy * off + fall, size, 0, r0 + spin, 2);
}

// ---------------------------------------------------------------- settings rows
enum RowKind { ROW_SLIDER, ROW_CHOICE, ROW_ACTION, ROW_PAGE };  // a page row opens the section numbered by its lo
enum SectionId { SID_AUDIO, SID_VISUALS, SID_VISUALS_MORE, SID_GAMEPLAY, SID_GAMEPLAY_MORE, SID_ACCESS, SID_ACCESS_MORE, SID_DATA, SID_STREETPASS, SID_DEV, SID_MIXER, SID_PASS };
enum RowAction { ACT_NONE, ACT_RESET_AUDIO, ACT_RESET_VISUALS, ACT_RESET_GAMEPLAY, ACT_RESET_SCORES, ACT_RESET_LB_NAME, ACT_RESET_ALL, ACT_SPEED_TEST, ACT_APPLY_DISPLAY, ACT_RESET_DISPLAY, ACT_RESET_ACCESS, ACT_RESET_CONTROLS, ACT_DEV_ADD_COINS, ACT_DEV_CLEAR_COINS, ACT_DEV_NEXT_DAY, ACT_CHECK_UPDATES, ACT_PASS_NAME, ACT_PASS_START, ACT_LATENCY, ACT_SP_MSG, ACT_SP_SYNC };

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
static const char* const kSceneNames[kSceneCount] = {"Dusk Orchard", "Morning Meadow", "Moonlit Bay", "Sakura Grove", "Autumn Harvest", "Starlit Mountain"};
#endif
static const char* const kArtNames[] = {"2D Stickers", "3D Models"};
#if PORT_3DS
static const char* const kEffectNames[] = {"None", "Low", "Medium", "Full"};
#else
static const char* const kEffectNames[] = {"None", "Low", "Simple", "Full"};
#endif
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
static const char* const kBossTrackRowNames[kTrackCount + 2] = {
	"Off", "Auto", "Orchard Morning", "Picnic Stroll", "Sleepy Meadow", "Lantern Walk", "Still Water", "Moss Garden", "Bamboo Breeze", "Drifting Clouds",
#define SHOP_TRACK(name, ...) name,
#include "shop_items.inc"
};

// Pass and Play: 2 to 4 people take one run each on the same device, and the highest score wins.
static constexpr int kPassMax = 4, kPassNameLen = 10;
static const u8 kPassModes[] = {MODE_CLASSIC, MODE_ARCADE, MODE_FRENZY, MODE_SUDDEN, MODE_MINEFIELD, MODE_COMBO, MODE_RECIPE, MODE_SHARP, MODE_JUGGLE};
static const char* const kPassModeNames[] = {"Classic", "Arcade", "Frenzy", "Sudden Death", "Minefield", "Combo Rush", "Recipe", "Sharpshooter", "Juggle"};
static const char* const kPassCounts[] = {"2", "3", "4"};
static u8 sPassPlayers = 0, sPassModeRow = 0;  // row values: players is 0 for two people
static char sPassNames[kPassMax][kPassNameLen + 1];
static u32 sPassScores[kPassMax];
static int sPassTurn = -1;    // whose run it is, or -1 when nobody is playing Pass and Play
static int sPassTyping = -1;  // the player whose name is being typed
static bool sSpTypingMsg = false;  // typing the StreetPass greeting message in Options
static int sStatsTab = 0;          // 0 Personal, 1 StreetPass (3DS) / Global (Univ), 2 Global (3DS)
static int sGlobalModeIdx = 0;     // selected gamemode tab in Stats -> Global
static int sGlobalScroll = 0;      // vertical scroll index in Stats -> Global Top 100
static int sGlobalTabScroll = 0;   // horizontal scroll index of clickable gamemode tabs
static bool sGlobalDragging = false;
static int sGlobalDragY = 0;
static bool sGlobalMoved = false;
static int sSpMetric = 0;          // 3DS StreetPass metric toggle via ZL/ZR (0 Classic, 1 Orchard Room)
static int sSpStatSel = 0;         // highlighted inbox player in Stats -> StreetPass
static int sSpStatScroll = 0;
static int sSpModal = 0;           // 0 none, 1 startup Enable StreetPass prompt, 2 incoming tag challenge
static int sSpModalFocus = 0;
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
static void passDefaultName(int player) { snprintf(sPassNames[player], sizeof sPassNames[player], "Player %d", player % kPassMax + 1); }
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
	{ROW_CHOICE, "Game", &sPassModeRow, 0, 8, kPassModeNames, 0, 0, ACT_NONE, "Everyone plays this mode.", "Scores here don't change your bests."},
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
	{ROW_CHOICE, "Low Gravity", &gSave.mixer[MIX_LOW_GRAVITY], 0, 1, kOffOn, 0, 0, ACT_NONE, "Fruit floats and hangs in the air.", "Not used in Daily, Beat Slice or Boss Rush."},
	{ROW_CHOICE, "Giant Fruit", &gSave.mixer[MIX_GIANT], 0, 1, kOffOn, 0, 0, ACT_NONE, "Every fruit is half as big again.", "Turns Tiny Fruit off."},
	{ROW_CHOICE, "Tiny Fruit", &gSave.mixer[MIX_TINY], 0, 1, kOffOn, 0, 0, ACT_NONE, "Small fruit, harder to hit.", "Turns Giant Fruit off."},
	{ROW_CHOICE, "Mirror", &gSave.mixer[MIX_MIRROR], 0, 1, kOffOn, 0, 0, ACT_NONE, "Your blade moves left when", "you move right."},
	{ROW_CHOICE, "Double Bombs", &gSave.mixer[MIX_DOUBLE_BOMBS], 0, 1, kOffOn, 0, 0, ACT_NONE, "Twice the bombs in every wave.", "Zen stays bomb-free."},
	{ROW_CHOICE, "Sides Only", &gSave.mixer[MIX_SIDES], 0, 1, kOffOn, 0, 0, ACT_NONE, "Fruit only comes in", "from the left and right."},
};
static Row kAudioRows[] = {  // not const: Menu and Game Music grow with the Custom Music folder
	{ROW_SLIDER, "Sound Effects", &gSave.sfx, 0, 100, nullptr, 0, 100, ACT_NONE, "Slices, combos and bombs.", ""},
	{ROW_SLIDER, "Music", &gSave.music, 0, 100, nullptr, 0, 100, ACT_NONE, "Menu and game music.", ""},
	{ROW_SLIDER, "Menu Sounds", &gSave.menuSfx, 0, 100, nullptr, 0, 100, ACT_NONE, "The tap when you press a sign.", ""},
	{ROW_CHOICE, "Menu Music", &gSave.menuTrack, 0, kTrackCount, kTrackRowNames, 0, 0, ACT_NONE, "What plays in the menus. Auto", "shuffles every regular or custom song."},
	{ROW_CHOICE, "Game Music", &gSave.gameTrack, 0, kTrackCount, kTrackRowNames, 0, 0, ACT_NONE, "What plays during a game. Auto", "shuffles regular/custom; Zen stays calm."},
	{ROW_CHOICE, "Boss Music", &gSave.bossMusic, 0, kTrackCount + 1, kBossTrackRowNames, 0, 0, ACT_NONE, "Auto unlocks and shuffles owned", "boss tracks, or pick a boss/custom song."},
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
#if PORT_HAS_DISPLAY && !PORT_ANDROID && !PORT_WEB  // a phone fills its screen; a web page sizes the game itself
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
#if PORT_HAS_DISPLAY && !PORT_ANDROID && !PORT_WEB
	{ROW_CHOICE, "Resolution", &port::gResChoice, 0, 0, port::gResNames, 0, 0, ACT_NONE, "Window size, or the screen mode", "in Fullscreen."},
	{ROW_ACTION, "Apply", nullptr, 0, 0, nullptr, 0, 0, ACT_APPLY_DISPLAY, "Tries the mode and resolution.", "Undone in 10 seconds unless kept."},
	{ROW_CHOICE, "V-Sync", &gSave.vsync, 0, 1, kOffOn, 0, 0, ACT_NONE, "Waits for the screen: no tearing.", "Works on 60 and 120 Hz screens."},
#endif
#if PORT_HAS_DISPLAY && !PORT_WEB  // a browser only has WebGL
	{ROW_CHOICE, "Renderer", &gSave.renderer, 0, (u8)(kRendererCount - 1), kRendererNames, 0, 0, ACT_NONE, "How the game draws. Takes effect", "the next time the game starts."},
#endif
#if PORT_HAS_DISPLAY
	{ROW_CHOICE, "Textures", &gSave.texQuality, 0, PORT_ULTRA ? 3 : 2, kTextureNames, 0, 0, ACT_NONE, PORT_ULTRA ? "Sharpness of the art. Ultra is" : "Sharpness of the art.", PORT_ULTRA ? "for 4K screens and uses more memory." : "Lower uses less memory."},
	{ROW_CHOICE, "Shadows", &gSave.shadowQuality, 0, 2, kShadowNames, 0, 0, ACT_NONE, "Shadows under 3D fruit.", ""},
	{ROW_CHOICE, "Ambient Occlusion", &gSave.ambientOcc, 0, 1, kOffOn, 0, 0, ACT_NONE, "Shaded undersides on 3D fruit.", ""},
	{ROW_CHOICE, "Bloom", &gSave.bloom, 0, 2, kBloomNames, 0, 0, ACT_NONE, "Glow around golden fruit", "and the last big fruit."},
	{ROW_CHOICE, "Render Scale", &gSave.renderScale, 0, 2, kScaleNames, 0, 0, ACT_NONE, "Pixels drawn, against the screen's.", "125% smooths edges; 75% is faster."},
#endif
#if PORT_3DS
	{ROW_CHOICE, "Effects", &gSave.effects, 0, 3, kEffectNames, 0, 0, ACT_NONE, "Juice, sparks and rings.", "Lower saves the most speed."},
#else
	{ROW_CHOICE, "Effects", &gSave.effects, 0, 3, kEffectNames, 0, 0, ACT_NONE, "Off, light, regular Simple, or", "Full GPU-accelerated glow & trails."},
#endif
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
#if PORT_3DS
static const Row kStreetPassRows[] = {
	{ROW_CHOICE, "StreetPass", &gSave.streetPassOn, 0, 1, kOffOn, 0, 0, ACT_NONE, "Exchange score cards and greetings", "with nearby Nintendo 3DS systems."},
	{ROW_ACTION, "Greeting Message", nullptr, 0, 0, nullptr, 0, 0, ACT_SP_MSG, "Pre-set message sent when you tag", "someone. Default: Hello! :)"},
	{ROW_ACTION, "Update Outbox", nullptr, 0, 0, nullptr, 0, 0, ACT_SP_SYNC, "Refresh your shared StreetPass card", "with your current bests and message."},
};
#endif

static const Row kDataRows[] = {
#if PORT_3DS
	{ROW_ACTION, "Speed Test", nullptr, 0, 0, nullptr, 0, 0, ACT_SPEED_TEST, "A 90 second test of this console.", "Results go in 3ds/FruitSlicer/bench.txt"},
#endif
#if !PORT_WEB  // the browser version is always the newest one
	{ROW_CHOICE, "Check for Updates", &gSave.updConsent, 1, 2, kUpdateNames, 0, 0, ACT_NONE, "Looks on GitHub once a day", "for a new version."},
	{ROW_ACTION, "Check Now", nullptr, 0, 0, nullptr, 0, 0, ACT_CHECK_UPDATES, "Looks on GitHub right away.", ""},
#endif
	{ROW_ACTION, "Reset Scores", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_SCORES, "Erases best scores and totals.", "Settings stay."},
	{ROW_ACTION, "Reset Leaderboard Name", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_LB_NAME, "Clears your online leaderboard name.", "Prompts again on your next score."},
	{ROW_ACTION, "Reset Everything", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_ALL, "Erases scores and all settings.", ""},
};

#ifdef DEV_TOOLS
// Developer builds only (DEV_TOOLS): coins on demand for testing the Shop. Release builds leave
// this whole section out, so it can't be reached there at all.
static u8 sDevAmount = 2;
static const char* const kDevAmountNames[] = {"100", "1,000", "10,000", "100,000", "1,000,000"};
static const u32 kDevAmounts[] = {100, 1000, 10000, 100000, 1000000};
static u8 sPauseOnUnfocus = 0;  // 0 = disabled (doesn't pause when unfocused)
static const Row kDevRows[] = {
	{ROW_CHOICE, "Pause when Unfocused", &sPauseOnUnfocus, 0, 1, kOffOn, 0, 0, ACT_NONE, "Pauses the game when the window", "loses focus. Disabled by default."},
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
#if PORT_3DS
	{"StreetPass", SECTION_ROWS(kStreetPassRows), SID_STREETPASS, -1, false, "Share your scores and greeting", "message with nearby 3DS players."},
#endif
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

// The Options signs: the back arrow, then each section that can be reached right now. Up to three
// stack in one column; four use a centred 2x2 grid; more go in two columns, with an odd last one centred.
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
		const int rows = (n + 1) / 2;
		const float y0 = n == 6 ? (46 + (194 - (rows * 48 - 4)) * 0.5f) : 46.0f;
		if (n <= 3) b = {36, 46 + (194 - (n * 48 - 4)) * 0.5f + 48.0f * i, 248, 44, nullptr};
		else if (n == 4) b = {i & 1 ? 164.0f : 8.0f, 70 + 74.0f * (i / 2), 148, 60, nullptr};
		else if (i == n - 1 && (n & 1)) b = {86, y0 + 48.0f * (i / 2), 148, 44, nullptr};
		else b = {i & 1 ? 164.0f : 8.0f, y0 + 48.0f * (i / 2), 148, 44, nullptr};
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

// Menu, Game and Boss Music grow dynamically with the Custom Music folder.
static void refreshMusicRows() {
#if PORT_CUSTOM_MUSIC
	custom::scan();  // songs added since last time
	for (Row& r : kAudioRows) {
		if (r.value == &gSave.menuTrack || r.value == &gSave.gameTrack) {
			r.hi = (u8)(kTrackCount + custom::count());
			if (*r.value > r.hi) r.hi = *r.value;  // a song that left the folder stays chosen until it's changed
		} else if (r.value == &gSave.bossMusic) {
			r.hi = (u8)(1 + kTrackCount + custom::count());
			if (*r.value > r.hi) r.hi = *r.value;
		}
	}
#endif
}

static bool rowDisabled(const Row& r) {
	if (passRowPlayer(r) >= passCount()) return true;  // this name's player isn't in the game
#if PORT_HAS_DISPLAY
	if (r.value == &port::gResChoice) return gSave.windowMode == 1;  // borderless always matches the desktop
#endif
	return r.value == &gSave.depth && !gHas3dScreen;
}

static void rowValueText(const Row& r, char* buf, size_t size) {
	buf[0] = 0;
	if (r.action == ACT_SP_MSG) {
		snprintf(buf, size, "%s", gSave.streetPassMsg[0] ? gSave.streetPassMsg : "Hello! :)");
		return;
	}
	if (r.kind == ROW_CHOICE) {
		if ((r.value == &gSave.menuTrack || r.value == &gSave.gameTrack) && *r.value > kTrackCount) {
#if PORT_CUSTOM_MUSIC
			const int song = *r.value - 1 - kTrackCount;
			snprintf(buf, size, "%s", song < custom::count() ? custom::name(song) : "Song not found");
#else
			snprintf(buf, size, "Auto");
#endif
			return;
		}
		if (r.value == &gSave.bossMusic && *r.value >= 2 + kTrackCount) {
#if PORT_CUSTOM_MUSIC
			const int song = *r.value - 2 - kTrackCount;
			snprintf(buf, size, "%s", song < custom::count() ? custom::name(song) : "Song not found");
#else
			snprintf(buf, size, "Auto");
#endif
			return;
		}
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
	if (r.value == &gSave.bossMusic && sOptionsFromPause && sGame.orchardLike() && sGame.orc.boss.on && !sGame.orc.boss.beaten) {
		const int tr = bossMusicResolve(sGame.orc.boss.id, gRng.next());
		sGame.orc.boss.track = (s8)(tr >= 0 ? tr : -1);
		if (gSave.bossMusic && tr >= 0 && tr < audio::trackCount()) audio::bossMusic(tr);
		else audio::bossMusicEnd();
	}
	if (r.value == &gSave.mixer[MIX_GIANT] && *r.value) gSave.mixer[MIX_TINY] = 0;
	if (r.value == &gSave.mixer[MIX_TINY] && *r.value) gSave.mixer[MIX_GIANT] = 0;
	if (r.value == &gSave.streetPassOn) {
		port::spSetEnabled(gSave.streetPassOn != 0);
		if (gSave.streetPassOn) streetpass::publishNow();
	}
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
	if (r.value == &gSave.menuTrack || r.value == &gSave.gameTrack) return v > 0 && v <= kTrackCount && (!trackUnlocked(v - 1) || trackBoss(v - 1) >= 0);
	if (r.value == &gSave.bossMusic) return v >= 2 && v <= kTrackCount + 1 && (trackBoss(v - 2) < 0 || !trackUnlocked(v - 2));
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
		sSpTypingMsg = false;
		sNameFocus = 0;
		port::textStart(sPassNames[sPassTyping], kPassNameLen);
		return;
	}
	if (r.action == ACT_SP_MSG) {
		audio::click();
		sPassTyping = -1;
		sSpTypingMsg = true;
		sNameFocus = 0;
		port::textStart(gSave.streetPassMsg, streetpass::kMsgMax);
		return;
	}
	if (r.action == ACT_SP_SYNC) {
		audio::click();
		streetpass::publishNow();
		audio::play(SFX_GOLD, 0.7f);
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
		case ACT_RESET_LB_NAME:
			gSave.leaderboardName[0] = 0;
			sDirty = true;
			saveWrite();
			break;
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
	if (sOptionsFromPause && sGame.orc.cutscene) audio::refreshVolumes();
	else audio::reapply();
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
static u8 sKeepBossMusic;
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
	sKeepBossMusic = gSave.bossMusic;
}

static void shopPutBack() {
	for (int t = 0; t < SHOP_TABS; t++) *shopSlot(t) = sKeep[t];
	gSave.bossMusic = sKeepBossMusic;
}

// A song is in use once it is both the Menu and the Game Music (or selected as Boss Music if it's a boss track).
static bool shopInUse(int tab, int item) {
	if (tab == SHOP_MUSIC && trackBoss(kBaseTracks + item) >= 0) return sKeepBossMusic == (u8)(2 + kBaseTracks + item);
	const int v = shopValue(tab, item);
	return sKeep[tab] == v && (tab != SHOP_MUSIC || gSave.gameTrack == v);
}

// A song tried on plays for a few seconds, fades out, and the menu music in use comes back.
static constexpr float kPreviewPlay = 8.0f, kPreviewFadeOut = 1.5f, kPreviewFadeIn = 1.0f;
static float sPreviewT = -1;  // seconds the song on show has played; below 0, none is playing as a preview
static float sReturnT = -1;   // seconds since the music in use came back, while it fades in
static bool sPreviewOver;     // the song on show had its turn, and the music in use is back

static void shopTryOn() {
	const u8 track = gSave.menuTrack;
	shopPutBack();
	if (!shopLocked(sShopTab, sShopItem)) *shopSlot(sShopTab) = (u8)shopValue(sShopTab, sShopItem);
	if (gSave.menuTrack != track) {
		audio::previewFade(1);
		audio::preview(MUSIC_MENU);
		sPreviewT = gSave.menuTrack != sKeep[SHOP_MUSIC] ? 0 : -1;
		sReturnT = -1;
		sPreviewOver = false;
	}
}

static void shopPreviewTick() {
	if (sPreviewT >= 0) {
		sPreviewT += kDt;
		if (sPreviewT < kPreviewPlay) return;
		if (sPreviewT < kPreviewPlay + kPreviewFadeOut) {
			audio::previewFade(1 - (sPreviewT - kPreviewPlay) / kPreviewFadeOut);
			return;
		}
		// Its turn is over: back to the real choice, which fades in from silence.
		gSave.menuTrack = sKeep[SHOP_MUSIC];
		audio::previewFade(0);
		audio::preview(MUSIC_MENU);
		sPreviewT = -1;
		sReturnT = 0;
		sPreviewOver = true;
	} else if (sReturnT >= 0) {
		sReturnT += kDt;
		audio::previewFade(sReturnT / kPreviewFadeIn);
		if (sReturnT >= kPreviewFadeIn) sReturnT = -1;
	}
}

// Leaving the Shop, or a song put in use, ends any preview at full volume.
static void shopPreviewStop() {
	sPreviewT = sReturnT = -1;
	sPreviewOver = false;
	audio::previewFade(1);
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
	shopPreviewStop();
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
	if (sShopTab == SHOP_MUSIC) {
		// Regular songs play on in full; Boss tracks set Boss Music and return Shop audio cleanly.
		if (trackBoss(kBaseTracks + sShopItem) < 0) {
			shopPreviewStop();
			audio::preview(MUSIC_MENU);
		}
	}
}

static const char* bossName(int boss) {
	switch (boss) {
		case 0: return "The Big Melon";
		case 1: return "The Bomb King";
		case 2: return "The Pineapple Guard";
		case 3: return "The Inferno Pepper";
		default: return "Milestone Bosses";
	}
}

// "Discover beating The Big Melon", for a locked boss track.
static const char* shopUnlockLine(int item) {
	static char line[48];
	snprintf(line, sizeof line, "Discover beating %s", bossName(trackBoss(kBaseTracks + item)));
	return line;
}

static void shopAct() {
	if (shopLocked(sShopTab, sShopItem)) {
		audio::click(0.4f);
		shopNote(shopUnlockLine(sShopItem));
		return;
	}
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
	if (tab == SHOP_MUSIC && shopHasNew()) {
		shopSeenNew();
		shopSave();
	}
}

static void updateShop(const Input& in, bool wasTouching) {
	int& f = sFocus[SCREEN_SHOP];
	if (sShopArmT > 0) sShopArmT -= kDt;
	if (sShopNoteT > 0) sShopNoteT -= kDt;
	shopPreviewTick();
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
		fake.ty = (int)(132 + sinf(sTime * 8.8f) * 40);  // between the item's line and its price
		sPreviewBlade.update(fake);
	}
}

// A padlock: a shackle over a body with a keyhole. s scales it from tile size.
static void drawPadlock(float cx, float cy, float s) {
	const u32 metal = rgba(200, 196, 180), body = rgba(214, 170, 70), hole = rgba(60, 40, 20);
	gfx::rect(cx - 7 * s, cy - 13 * s, 2.5f * s, 9 * s, metal);
	gfx::rect(cx + 4.5f * s, cy - 13 * s, 2.5f * s, 9 * s, metal);
	gfx::rect(cx - 7 * s, cy - 15 * s, 14 * s, 2.5f * s, metal);
	gfx::rect(cx - 10 * s, cy - 5 * s, 20 * s, 15 * s, body);
	gfx::rect(cx - 1.5f * s, cy - 1 * s, 3 * s, 6 * s, hole);
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
		// Two beamed notes, compact enough to clear two-line tile names cleanly.
		u32 c = COL_CREAM;
		gfx::rect(cx - 8, cy - 6.0f, 2.5f, 10.0f, c);
		gfx::rect(cx + 5, cy - 7.5f, 2.5f, 10.0f, c);
		gfx::tri(cx - 8, cy - 6.0f, c, cx + 7.5f, cy - 7.5f, c, cx + 7.5f, cy - 4.5f, c);
		gfx::tri(cx - 8, cy - 6.0f, c, cx + 7.5f, cy - 4.5f, c, cx - 8, cy - 3.0f, c);
		gfx::rect(cx - 12.5f, cy + 2.0f, 6.5f, 4.5f, c);
		gfx::rect(cx + 0.5f, cy + 0.5f, 6.5f, 4.5f, c);
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
		if (i == SHOP_MUSIC && shopHasNew()) gfx::text("NEW", b.x + b.w - 3, b.y - 5, 0.26f + sinf(sTime * 6) * 0.015f, rgba(255, 230, 120), ALIGN_RIGHT);
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
		const ShopItem& it = shopItem(sShopTab, k);
		const bool locked = shopLocked(sShopTab, k);
		const bool isBossSong = sShopTab == SHOP_MUSIC && trackBoss(kBaseTracks + k) >= 0;
		// A name too long for one line goes on two, so it stays as big as its neighbours. The icon moves up for it.
		char top[32], bottom[32];
		const bool two = splitLine(locked ? "Boss Track" : it.name, 0.32f, b.w - 8, top, bottom, sizeof top);
		if (locked) {
			drawPadlock(b.x + b.w / 2, b.y + (two ? 12 : 16), two ? 0.72f : 0.8f);
			if (isBossSong) gfx::sprite(SHEET_SPRITES, sprites_ic_skull_idx, b.x + b.w - 13, b.y + 11, 0.44f, 0.44f);
		} else {
			if (isBossSong) {
				drawShopIcon(sShopTab, k, b.x + b.w / 2 - 9, b.y + (two ? 11.0f : 14.0f));
				gfx::sprite(SHEET_SPRITES, sprites_ic_skull_idx, b.x + b.w / 2 + 14, b.y + (two ? 11.0f : 14.0f), 0.42f, 0.42f);
			} else {
				drawShopIcon(sShopTab, k, b.x + b.w / 2, b.y + (two ? 11.0f : 14.0f));
			}
		}
		if (two) {
			fitText(top, b.x + b.w / 2, b.y + 19.0f, 0.28f, b.w - 8, COL_CREAM);
			fitText(bottom, b.x + b.w / 2, b.y + 29.0f, 0.28f, b.w - 8, COL_CREAM);
		} else {
			fitText(locked ? "Boss Track" : it.name, b.x + b.w / 2, b.y + 25.0f, 0.33f, b.w - 8, COL_CREAM);
		}
		const float priceY = b.y + (two ? 40.5f : 39.5f);
		if (locked) {
			gfx::text("LOCKED", b.x + b.w / 2, priceY, 0.30f, COL_DIM, ALIGN_CENTER);
		} else if (shopInUse(sShopTab, k)) {
			gfx::text("IN USE", b.x + b.w / 2, priceY, 0.30f, COL_ACCENT, ALIGN_CENTER);
		} else if (shopOwned(sShopTab, k)) {
			gfx::text("OWNED", b.x + b.w / 2, priceY, 0.30f, COL_DIM, ALIGN_CENTER);
		} else {
			snprintf(buf, sizeof buf, "%u", (unsigned)it.price);
			float pw = gfx::textWidth(buf, 0.31f);
			gfx::sprite(SHEET_SPRITES, sprites_coin_idx, b.x + b.w / 2 - pw / 2 - 5, priceY + 5.0f, 0.30f, 0.30f);
			gfx::text(buf, b.x + b.w / 2 + 4, priceY, 0.31f, gSave.coins >= it.price ? COL_CREAM : COL_DIM, ALIGN_CENTER);
		}
	}
	gfx::clearClip();
	drawScrollMark(kShopTop, kShopRowsShown * kShopStep - 6, sShopList.pos, shopHiddenRows(sShopTab) * kShopStep);

	// The action sign says exactly what a press will do.
	const ShopItem& it = shopItem(sShopTab, sShopItem);
	bool owned = shopOwned(sShopTab, sShopItem);
	const bool inUse = shopInUse(sShopTab, sShopItem);
	const bool locked = shopLocked(sShopTab, sShopItem);
	if (locked) snprintf(buf, sizeof buf, "Locked");
	else 	if (owned) snprintf(buf, sizeof buf, "%s", inUse ? (shopCanTakeOff(sShopTab) ? "Take Off" : "In Use") : "Use");
	else if (sShopArmT > 0) snprintf(buf, sizeof buf, "Sure? %u", (unsigned)it.price);
	else snprintf(buf, sizeof buf, "Buy %u", (unsigned)it.price);
	Button a = shopButton(kShopAct);
	drawPlank(a.x, a.y, a.w, a.h, f == kShopAct, sPressed == kShopAct);
	gfx::text(buf, a.x + a.w / 2, a.y + 8, 0.44f, (!locked && (owned || gSave.coins >= it.price)) ? COL_CREAM : COL_DIM, ALIGN_CENTER);
}

// What the focused item is, with its live preview: the top screen on the 3DS, beside the menu elsewhere.
static void drawShopPreview() {
	const ShopItem& it = shopItem(sShopTab, sShopItem);
	gfx::setDepth(0.7f);
	if (shopLocked(sShopTab, sShopItem)) {
		gfx::sprite(SHEET_SPRITES, sprites_ic_skull_idx, 96, 28, 0.65f, 0.65f);
		gfx::text("Locked Boss Track", 200, 20, 0.58f, COL_ACCENT, ALIGN_CENTER);
		gfx::text(shopUnlockLine(sShopItem), 200, 46, 0.36f, COL_CREAM, ALIGN_CENTER);
		gfx::setDepth(0.5f);
		drawPadlock(200, 110, 2.0f);
		fitText("Beat its boss to unlock in Shop. Restricted to Boss Music.", 200, 150, 0.35f, 316, COL_DIM);
		if (sShopNoteT > 0) gfx::text(sShopNote, 200, 192, 0.40f, withAlpha(COL_CREAM, clampf(sShopNoteT * 2, 0, 1)), ALIGN_CENTER);
		return;
	}
	fitText(it.name, 200, 20, 0.56f, 320, COL_ACCENT);
	fitText(it.line, 200, 46, 0.39f, 330, COL_CREAM);
	if (sShopTab == SHOP_THEMES) {
		gfx::setDepth(0.5f);
		gfx::image(SHEET_BOARD, 0, 130, 66, 140, 96);
	} else if (sShopTab == SHOP_TRAILS) {
		gfx::setDepth(0.5f);
		sPreviewBlade.draw();
	} else if (sShopTab == SHOP_SPLATS || sShopTab == SHOP_SKINS) {
		gfx::setDepth(0.5f);
		drawShopIcon(sShopTab, sShopItem, 200, 114);
	} else {
		const bool isBossSong = trackBoss(kBaseTracks + sShopItem) >= 0;
		if (isBossSong) gfx::sprite(SHEET_SPRITES, sprites_ic_skull_idx, 200, 82, 0.72f, 0.72f);
		const char* now = isBossSong
		                  ? (shopInUse(sShopTab, sShopItem) ? "Selected as Boss Music"
		                     : sPreviewOver ? "Preview over, your music is back"
		                     : sPreviewT >= 0 ? "Previewing boss track" : "Restricted to Boss Music.")
		                  : (shopInUse(sShopTab, sShopItem) ? "Playing in the menus and in games"
		                     : sPreviewOver ? "Preview over, your music is back"
		                     : sPreviewT >= 0 ? "Now playing as a preview" : "Your Menu Music, not your Game Music.");
		fitText(now, 200, 108, 0.38f, 310, COL_DIM);
		if (!shopInUse(sShopTab, sShopItem))
			fitText(isBossSong ? "Boss fights only. Use to set Boss Music." : "Use it for the Menu and Game Music.", 200, 129, 0.38f, 310, COL_DIM);
	}
	// What the item costs against the purse, or that it is already yours.
	char buf[48];
	const bool owned = shopOwned(sShopTab, sShopItem);
	const bool inUse = shopInUse(sShopTab, sShopItem);
	if (owned) snprintf(buf, sizeof buf, "%s", inUse ? "Yours, and in use" : "Yours to use");
	else if (gSave.coins >= it.price) snprintf(buf, sizeof buf, "%u coins. You have %lu.", (unsigned)it.price, (unsigned long)gSave.coins);
	else snprintf(buf, sizeof buf, "%u coins. %lu more to go.", (unsigned)it.price, (unsigned long)(it.price - gSave.coins));
	// Generous breathing room above the sign's bottom edge.
	const float tw = gfx::textWidth(buf, 0.4f);
	if (!owned) gfx::sprite(SHEET_SPRITES, sprites_coin_idx, 200 - tw * 0.5f - 12, 175, 0.4f, 0.4f);
	gfx::text(buf, 200 + (!owned ? 6.0f : 0.0f), 166, 0.4f, owned ? COL_ACCENT : COL_CREAM, ALIGN_CENTER);
	if (sShopNoteT > 0) gfx::text(sShopNote, 200, 192, 0.40f, withAlpha(COL_CREAM, clampf(sShopNoteT * 2, 0, 1)), ALIGN_CENTER);
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
	if (keys.hovering) {
		for (int side = 0; side < 2; side++)
			if (hit(kPageArrows[side], keys.tx, keys.ty)) sArrowHover = side;
		if (sArrowHover >= 0) sFocus[SCREEN_MODES] = -1;
	}
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
	if (focus == kMixFocus) return false;  // the Mixer fruit isn't on a page
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

// Typing a Pass and Play name or StreetPass greeting over the list.
static void updateNameEntry(const Input& in, bool wasTouching) {
	char typed[48];
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
	if (result > 0) {
		if (sSpTypingMsg) {
			streetpass::cleanMessage(typed, gSave.streetPassMsg, sizeof gSave.streetPassMsg, "Hello! :)");
			sDirty = true;
			saveWrite();
			streetpass::publishNow();
		} else if (sPassTyping >= 0) {
			passSetName(sPassTyping, typed);
		}
	}
	port::textStop();
	sPassTyping = -1;
	sSpTypingMsg = false;
}

static HoldRepeat sRowHold;
static int sRowHoldDir = 0;
static bool sRowDidRepeat = false;
static int sRowArrowHover = 0;

static void updateSection(const Input& in, bool wasTouching) {
	if (sPassTyping >= 0 || sSpTypingMsg) {
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
	sRowArrowHover = 0;
	if (in.hovering) {
		if (hit(kBack, in.tx, in.ty)) {
			focus = -1;
		} else if (in.ty >= kListTop) {
			int row = (int)((in.ty - kListTop + sScroll) / kRowH);
			if (row >= 0 && row < sec.count) {
				if (row != focus) sArmed = -1;
				focus = row;
				if (sec.rows[row].kind == ROW_CHOICE) {
					if (in.tx >= 146 && in.tx < 186) sRowArrowHover = -1;
					else if (in.tx >= 278 && in.tx < 312) sRowArrowHover = 1;
				}
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
						sRowHold.reset();
						sRowDidRepeat = false;
						sRowHoldDir = (r.kind == ROW_CHOICE) ? (in.tx < 214 ? -1 : 1) : 0;
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
				sRowHold.reset();
				sRowHoldDir = 0;
				sScroll = sScrollStart - dy;
			} else {
				setSliderFromTouch(sec.rows[sTouchRow], in.tx);
			}
		} else if (sTouchMode == TOUCH_ROW) {
			int dy = in.ty - sTouchStartY;
			if (dy > 6 || dy < -6) {
				sTouchMoved = true;
				sRowHold.reset();
			}
			if (sTouchMoved) sScroll = sScrollStart - dy;
			else if (sRowHoldDir != 0) {
				const float ry = kListTop + sTouchRow * kRowH - sScroll;
				const bool insideRow = in.ty >= ry && in.ty < ry + kRowH;
				if (insideRow) {
					sRowHoldDir = in.tx < 214 ? -1 : 1;
					const int key = sTouchRow * 4 + (sRowHoldDir > 0 ? 1 : 2);
					if (sRowHold.step(key, true, false)) {
						sRowDidRepeat = true;
						adjustRow(sec.rows[sTouchRow], sRowHoldDir);
					}
				} else {
					sRowHold.reset();
				}
			}
		}
	} else if (wasTouching) {
		sPressed = -1;
		sRowHold.reset();
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
			else if (r.kind == ROW_CHOICE && !sRowDidRepeat) adjustRow(r, sLastTouchX < 214 ? -1 : 1);
		}
		sTouchMode = TOUCH_NONE;
		sRowHoldDir = 0;
		sRowDidRepeat = false;
	} else {
		sRowHold.reset();
		sRowHoldDir = 0;
		sRowDidRepeat = false;
	}
	sScroll = clampf(sScroll, 0, maxScroll());
}

// ---------------------------------------------------------------- top-screen art
#if PORT_TWO_SCREENS
static void drawTopBackground() {
	const float d = sTime;
	// Sky stays grounded; seamless 424px-wide foreground layers glide endlessly at different parallax speeds.
	auto drawWrapped = [&](int img, float depth, float speed, float y, float h) {
		gfx::setDepth(depth);
		const float off = gSave.calmFx ? 0.0f : fmodf(d * speed, 424.0f);
		for (float tx = -12.0f - off; tx < 412.0f; tx += 424.0f) {
			if (tx + 424.0f > -12.0f) gfx::image(SHEET_BGTOP, img, tx, y, 424, h);
		}
	};
	gfx::setDepth(-2.0f);
	gfx::image(SHEET_BGTOP, bgtop0_sky_idx, -12, 0, 424, 240);
	if (gSave.bgDetail) drawWrapped(bgtop0_far_idx, -1.3f, 5.0f, 120, 120);
	drawWrapped(bgtop0_mid_idx, -0.7f, 12.0f, 130, 110);
	if (gSave.bgDetail) drawWrapped(bgtop0_near_idx, -0.15f, 24.0f, 170, 70);
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

// ---------------------------------------------------------------- black screen readings (3DS test build)
// `make TESTDEFS=-DTEST_READINGS`. The 3DS can go black in busy moments with only the fruit still drawn, and
// HOME then crashes it. This keeps a line a second in sd:/3ds/FruitSlicer/readings.txt, and one straight away
// when the screen changes or more kinds of fruit are on show than ever before. The file is closed after every
// line, so holding the power button afterwards loses nothing. The last run's file is kept as readings_prev.txt.
#if PORT_3DS && defined(TEST_READINGS)
static const char* const kReadingsFile = "sdmc:/3ds/FruitSlicer/readings.txt";
static float sReadT;
static int sReadFrame, sReadKindsMax;
static Screen sReadScreen = SCREEN_COUNT;

static void readingsStart() {
	remove("sdmc:/3ds/FruitSlicer/readings_prev.txt");
	rename(kReadingsFile, "sdmc:/3ds/FruitSlicer/readings_prev.txt");
	if (FILE* f = fopen(kReadingsFile, "w")) {
		fprintf(f, "Fruit Slicer %s readings. new3ds=%d. One line a second, and one on each change.\n", VERSION_STRING, gIsNew3ds);
		fprintf(f, "frame why screen mode art fps cpu%% gpu%% draws objs halves parts kinds linearK vramK appK fade slide stickers fx\n");
		fclose(f);
	}
}

static void readingsTick() {
	sReadFrame++;
	sReadT += kDt;
	const int kinds = sScreen == SCREEN_GAME || sScreen == SCREEN_PAUSE ? sGame.activeKinds() : 0;
	const char* why = nullptr;
	if (sScreen != sReadScreen) why = "screen";
	else if (kinds > sReadKindsMax) why = "kinds";
	else if (sReadT >= 1) why = "tick";
	if (!why) return;
	sReadT = 0;
	sReadScreen = sScreen;
	if (kinds > sReadKindsMax) sReadKindsMax = kinds;
	FILE* f = fopen(kReadingsFile, "a");
	if (!f) return;
	fprintf(f, "%d %s %d %d %s %.1f %.0f %.0f %lu %d %d %d %d %lu %lu %lu %.2f %d %d %d\n", sReadFrame, why, (int)sScreen, (int)sGame.mode,
	        gSave.artStyle == ART_3D ? "3d" : "2d", sFps, C3D_GetProcessingTime() * 6.0f, C3D_GetDrawingTime() * 6.0f,
	        (unsigned long)gfx::drawCalls, sGame.activeObjects(), sGame.activeHalves(), sGame.activeParticles(), kinds,
	        (unsigned long)(linearSpaceFree() / 1024), (unsigned long)(vramSpaceFree() / 1024),
	        (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024), sFade, (int)sSliding, (int)gfx::stickersLoaded(), (int)gSave.effects);
	fclose(f);
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
	// Centered cleanly below the coin purse and above the Mixer fruit.
	const float lift[4] = {PORT_3DS ? 0.0f : 46.0f, PORT_3DS ? 0.0f : 48.0f, PORT_3DS ? 0.0f : 48.0f, PORT_3DS ? 0.0f : 48.0f};
	gfx::text("Daily Fruit", 200, 100 - lift[0], 0.56f, COL_ACCENT, ALIGN_CENTER);
	if (progress::dailyPlayed(today)) snprintf(buf, sizeof buf, "TODAY'S BEST %lu", (unsigned long)progress::dailyScore(today));
	else snprintf(buf, sizeof buf, "First run today pays %lu coins", (unsigned long)progress::dailyBonusPreview(today));
	gfx::text(buf, 200, 126 - lift[1], 0.4f, COL_CREAM, ALIGN_CENTER);
	// A week of the player's own calendar: day 1 is the first day they played, and today's box moves along
	// the row. Day 8 starts the next row of seven. The day number on top, the score below once played.
	const int number = progress::dailyNumber(today);
	const int todayCell = (number - 1) % 7;
	static constexpr float kCell = 42, kGap = 4;
	const float x0 = 200 - (7 * kCell + 6 * kGap) / 2;
	for (int i = 0; i < 7; i++) {
		const u32 day = today - todayCell + i;
		const float x = x0 + i * (kCell + kGap), y = 150 - lift[2];
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
	gfx::text(buf, 200, 204 - lift[3], 0.34f, COL_DIM, ALIGN_CENTER);
}

// ---------------------------------------------------------------- credits
// One long list that scrolls inside its sign: with the wheel, a drag, a flick, or Up and Down. A new name only
// needs a line here.
enum CreditKind : u8 { CR_HEAD, CR_NAME, CR_LINE, CR_NOTE, CR_GAP, CR_BUILT };
struct CreditLine {
	CreditKind kind;
	const char* text;
};
static const char* const kCreditsBuilt[] = {PORT_CREDITS_BUILT};
static const CreditLine kCredits[] = {
	{CR_HEAD, "Programming"},
	{CR_NAME, "NickBin"},
	{CR_NOTE, "with Claude Code"},
	{CR_HEAD, "Music"},
	{CR_LINE, "Menu, game and Zen music"},
	{CR_NOTE, "made in code for Fruit Slicer"},
	{CR_HEAD, "Shop Music"},
	{CR_NAME, "Of Far Different Nature"},
	{CR_NOTE, "Loop Box 1 and 4, CC BY 4.0"},  // the font has no #
	{CR_LINE, "Summer House"},
	{CR_LINE, "8-Bit Hero Quest"},
	{CR_LINE, "Friendly Trap"},
	{CR_LINE, "Unruly Beat"},
	{CR_LINE, "Vengeance Electro"},
	{CR_LINE, "Eternal Explorer"},
	{CR_GAP, nullptr},
	{CR_NAME, "NickBin and friends"},
	{CR_NOTE, "All rights reserved, used with permission"},
	{CR_LINE, "Ahead of the Curve"},
	{CR_NOTE, "NickBin, King Bakuyo Pro"},
	{CR_LINE, "SPARE FOR ME"},
	{CR_NOTE, "NickBin ft. Mq_"},
	{CR_LINE, "My Own Fright"},
	{CR_NOTE, "NickBin ft. Cabex Music"},
	{CR_LINE, "WORTHLESS ENEMY"},
	{CR_NOTE, "NickBin ft. Aneolent"},
	{CR_LINE, "Lies"},
	{CR_NOTE, "NickBin"},
	{CR_LINE, "Lies (VIP Remix)"},
	{CR_NOTE, "NickBin"},
	{CR_LINE, "JUST STAY WITH ME"},
	{CR_NOTE, "NickBin"},
	{CR_HEAD, "Boss Music"},
	{CR_LINE, "Seed Spitter, Rind and Run,"},
	{CR_LINE, "Long Live the Fuse, Powder Keg Court"},
	{CR_NOTE, "made in code for Fruit Slicer"},
	{CR_GAP, nullptr},
	{CR_LINE, "8bit Action Boss Battle"},
	{CR_NOTE, "MintoDog, CC0"},
	{CR_LINE, "Boss Fight 2"},
	{CR_NOTE, "ansimuz, CC0"},
	{CR_LINE, "Boss Fight Bounce"},
	{CR_NOTE, "Locomule, CC0"},
	{CR_LINE, "Epic Boss Battle"},
	{CR_NOTE, "Juhani Junkala, CC0"},
	{CR_LINE, "A Champion's Will"},
	{CR_NOTE, "ATMANAN, CC BY 4.0"},
	{CR_NOTE, "all from OpenGameArt.org"},
	{CR_HEAD, "Art and Sound"},
	{CR_LINE, "Sprites, 3D models, boards,"},
	{CR_LINE, "the font and every sound"},
	{CR_NOTE, "made in code with Python, Pillow and numpy"},
	{CR_HEAD, "Built With"},
	{CR_BUILT, nullptr},
	{CR_HEAD, "Toolkits"},
	{CR_LINE, "Claude Code"},
	{CR_LINE, "Claude Code Game Studios"},
	{CR_NOTE, "by Donchitos"},
	{CR_LINE, "Anti Slop"},
	{CR_NOTE, "by miqdadbadjuber"},
	{CR_LINE, "Graphify"},
	{CR_HEAD, "License"},
	{CR_LINE, "Fruit Slicer: CC BY-NC 4.0"},
	{CR_NOTE, "Free to share and adapt with credit,"},
	{CR_NOTE, "not for commercial use"},
	{CR_GAP, nullptr},
	{CR_NAME, "Thanks for playing!"},
};
static constexpr float kCreditX = 10, kCreditY = 48, kCreditW = 300, kCreditH = 186;
static constexpr float kCreditTop = kCreditY + 6, kCreditBottom = kCreditY + kCreditH - 6;  // where the list shows
static float sCreditScroll = 0, sCreditFling = 0;  // how far down, and a flick still moving it
static bool sCreditDrag = false;                   // the touch or mouse press now held began on the list

static float creditStep(CreditKind k) {
	switch (k) {
		case CR_HEAD: return 22;
		case CR_NAME: return 22;
		case CR_LINE: return 19;
		case CR_NOTE: return 17;
		default: return 10;
	}
}
// Calls each(kind, text, y) for every line, y measured from the top of the list. Returns the list's height.
template <class F>
static float forEachCredit(F&& each) {
	float y = 2;
	for (const CreditLine& c : kCredits) {
		if (c.kind == CR_BUILT) {
			for (const char* s : kCreditsBuilt) {
				each(CR_LINE, s, y);
				y += creditStep(CR_LINE);
			}
			continue;
		}
		if (c.kind == CR_HEAD && y > 2) y += 12;  // room above each section
		if (c.text) each(c.kind, c.text, y);
		y += creditStep(c.kind);
	}
	return y + 4;
}
static float creditMaxScroll() {
	const float h = forEachCredit([](CreditKind, const char*, float) {});
	return fmaxf(0, h - (kCreditBottom - kCreditTop));
}

static void updateCredits(const Input& in, bool wasTouching) {
	static int lastY;
	bool& dragging = sCreditDrag;
	if (in.touching) {
		if (!wasTouching) {
			dragging = hit({kCreditX, kCreditY, kCreditW, kCreditH, nullptr}, in.tx, in.ty);
			sCreditFling = 0;
		} else if (dragging) {
			const float dy = (float)(in.ty - lastY);
			sCreditScroll -= dy;
			sCreditFling = -dy;  // let go mid-move and the list carries on a little way
		}
		lastY = in.ty;
	} else {
		dragging = false;
		sCreditScroll += sCreditFling;
		sCreditFling *= gSave.calmFx ? 0.75f : 0.92f;
		if (fabsf(sCreditFling) < 0.3f) sCreditFling = 0;
	}
	if (in.wheel) {
		sCreditScroll -= in.wheel * 34.0f;
		sCreditFling = 0;
	}
	if (in.repeat & KEY_DOWN) sCreditScroll += 24;
	if (in.repeat & KEY_UP) sCreditScroll -= 24;
	const float most = creditMaxScroll();
	if (sCreditScroll < 0 || sCreditScroll > most) sCreditFling = 0;
	sCreditScroll = clampf(sCreditScroll, 0, most);
}

static void drawCredits() {
	gfx::sign(sprites_plank_idx, kCreditX, kCreditY, kCreditW, kCreditH, COL_PLANK);
	gfx::setClip(kCreditX + 4, kCreditTop, kCreditW - 8, kCreditBottom - kCreditTop);
	const float top = kCreditTop - sCreditScroll, mid = kCreditX + kCreditW * 0.5f - 4;
	forEachCredit([&](CreditKind k, const char* s, float y) {
		const float at = top + y;
		if (at > kCreditBottom || at < kCreditTop - 24) return;
		float scale = 0.4f;
		u32 color = COL_CREAM;
		if (k == CR_HEAD) {
			scale = 0.44f;
			color = COL_ACCENT;
		} else if (k == CR_NAME) {
			scale = 0.46f;
		} else if (k == CR_NOTE) {
			scale = 0.34f;
			color = COL_DIM;
		}
		const float w = gfx::textWidth(s, scale), room = kCreditW - 44;
		if (w > room) scale *= room / w;
		gfx::text(s, mid, at, scale, color, ALIGN_CENTER);
	});
	gfx::clearClip();
	// Where in the list you are, along the right edge.
	const float most = creditMaxScroll();
	if (most > 0) {
		const float view = kCreditBottom - kCreditTop, bar = fmaxf(18, view * view / (view + most));
		const float x = kCreditX + kCreditW - 10, y = kCreditTop + (view - bar) * (sCreditScroll / most);
		gfx::rect(x, kCreditTop, 3, view, withAlpha(COL_DIM, 0.25f));
		gfx::rect(x, y, 3, bar, COL_ACCENT);
	}
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
// The Bosses page follows the fruit: a sticker for each boss, dark until it is beaten.
static int journalTiles() { return progress::journalPages() + BOSS_COUNT; }
static bool journalIsBoss(int p) { return p >= progress::journalPages(); }
static int journalHiddenRows() { return (journalTiles() + kJournalCols - 1) / kJournalCols - kJournalRowsShown; }
static const u32 kUnfound = rgba(40, 28, 20, 225);

static void updateJournal(const Input& in, bool wasTouching) {
	const int n = journalTiles() + 1;
	if (rowScroll(sJournalList, in, wasTouching, kJournalWindowTop, kJournalWindowTop + kJournalWindowH, kJournalStep, journalHiddenRows())) sPressed = -1;
	Button btns[progress::kNamedFruit + BOSS_COUNT + 1];
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

static bool bossBeaten(int boss) { return gSave.bossesBeaten >> boss & 1; }
static int bossSprite(int boss) {
	static const int kBossSprites[BOSS_COUNT] = {
		sprites_boss_melon_guard_idx,
		sprites_boss_king_guard_idx,
		sprites_boss_pineapple_guard_idx,
		sprites_boss_chili_guard_idx,
		sprites_boss_durian_guard_idx,
		sprites_boss_lemon_guard_idx,
		sprites_boss_apple_guard_idx,
	};
	return boss >= 0 && boss < BOSS_COUNT ? kBossSprites[boss] : sprites_boss_melon_guard_idx;
}

static void drawJournalBossTile(const Button& b, int boss, bool focused, bool pressed) {
	drawPlank(b.x, b.y, b.w, b.h, focused, pressed);
	const float cx = b.x + b.w * 0.5f;
	const bool beaten = bossBeaten(boss);
	gfx::sprite(SHEET_SPRITES, bossSprite(boss), cx, b.y + 17, 0.42f, 0.42f, 0, beaten ? WHITE : kUnfound);
	// A small crown marks it as a boss.
	const u32 gold = beaten ? rgba(255, 210, 80) : rgba(90, 80, 70, 220);
	gfx::rect(cx - 7, b.y + 3, 14, 3, gold);
	gfx::tri(cx - 7, b.y + 3, gold, cx - 5, b.y - 2, gold, cx - 3, b.y + 3, gold);
	gfx::tri(cx - 2, b.y + 3, gold, cx, b.y - 3, gold, cx + 2, b.y + 3, gold);
	gfx::tri(cx + 3, b.y + 3, gold, cx + 5, b.y - 2, gold, cx + 7, b.y + 3, gold);
	const u32 wins = gSave.bossWins[boss];
	char buf[16];
	if (beaten) snprintf(buf, sizeof buf, "%lu WIN%s", (unsigned long)wins, wins == 1 ? "" : "S");
	else snprintf(buf, sizeof buf, "?");
	fitText(buf, cx, b.y + 28, 0.30f, b.w - 6, focused ? COL_ACCENT : COL_CREAM);
	const int level = progress::bossLevel(wins);
	for (int k = 0; k < progress::kBossLevels; k++) gfx::rect(cx - 14 + k * 8, b.y + 39, 4, 3, k < level ? rgba(255, 210, 80) : rgba(0, 0, 0, 90));
}

static void drawJournal() {
	const int f = sFocus[SCREEN_JOURNAL];
	drawHeader("Fruit Journal");
	drawBackArrow(f == 0, sPressed == 0);
	char buf[12];
	gfx::setClip(0, kJournalWindowTop, 320, kJournalWindowH);
	for (int p = 0; p < journalTiles(); p++) {
		const Button b = journalButton(p + 1);
		if (b.y + b.h + 2 < kJournalWindowTop || b.y - 2 > kJournalWindowTop + kJournalWindowH) continue;
		if (journalIsBoss(p)) {
			drawJournalBossTile(b, p - progress::journalPages(), f == p + 1, sPressed == p + 1);
			continue;
		}
		const int fruit = progress::journalFruit(p);
		const u32 count = gSave.fruitSliced[fruit];
		const bool focused = f == p + 1;
		drawPlank(b.x, b.y, b.w, b.h, focused, sPressed == p + 1);
		const float cx = b.x + b.w * 0.5f;
		gfx::sprite(SHEET_FRUIT, fruitSprite(fruit), cx, b.y + 15, 0.56f, 0.56f, 0, count ? WHITE : kUnfound);
		if (count) snprintf(buf, sizeof buf, "%lu", (unsigned long)(count > 999999 ? 999999 : count));
		else snprintf(buf, sizeof buf, "?");
		fitText(buf, cx, b.y + 26, 0.32f, b.w - 6, focused ? COL_ACCENT : COL_CREAM);
		const int level = progress::journalLevel(count);
		for (int k = 0; k < progress::kJournalLevels; k++) gfx::rect(cx - 14 + k * 8, b.y + 39, 4, 3, k < level ? COL_ACCENT : rgba(0, 0, 0, 90));
	}
	gfx::clearClip();
	drawScrollMark(kJournalTop, kJournalRowsShown * kJournalStep - 2, sJournalList.pos, journalHiddenRows() * kJournalStep);
}

// A boss's page: who it is, where it waits, and what beating it gave.
static void drawJournalBossTop(int boss) {
	char buf[64];
	const bool beaten = bossBeaten(boss);
	gfx::setDepth(1.0f);
	gfx::sprite(SHEET_SPRITES, bossSprite(boss), 200, 42, 0.76f, 0.76f, gSave.calmFx ? 0 : sinf(sTime * 1.3f) * 0.1f, beaten ? WHITE : kUnfound);
	gfx::setDepth(0.7f);
	gfx::text(beaten ? kBosses[boss].name : "Not beaten yet", 200, 72, 0.54f, COL_ACCENT, ALIGN_CENTER);
	static const char* const kBossWhere[BOSS_COUNT] = {
		"ORCHARD BLAST & BOSS RUSH",
		"ORCHARD BLAST & BOSS RUSH",
		"ORCHARD BLAST & BOSS RUSH",
		"ORCHARD BLAST & BOSS RUSH",
		"ORCHARD ROOM 25+ & BOSS RUSH",
		"ORCHARD ROOM 25+ & BOSS RUSH",
		"ORCHARD ROOM 25+ & BOSS RUSH",
	};
	gfx::text(boss >= 0 && boss < BOSS_COUNT ? kBossWhere[boss] : "ORCHARD BLAST", 200, 94, 0.38f, COL_CREAM, ALIGN_CENTER);
	const u32 wins = gSave.bossWins[boss];
	snprintf(buf, sizeof buf, "%d SLICES TO BEAT   BEATEN %lu", kBosses[boss].slices, (unsigned long)wins);
	gfx::text(buf, 200, 110, 0.35f, COL_CREAM, ALIGN_CENTER);
	static const char* const kBossBlurb[BOSS_COUNT] = {
		"Spits seeds and flops. Slice it when its guard drops.",
		"Fires cannonballs and lights its fuse. Snuff the fuse in three swipes.",
		"Keeps its spiny guard up. Knock lobbed bombs back into it!",
		"Rains embers and triggers Heat Surges that speed up combat.",
		"Launches armored thorn volleys and dust quakes every 25 rooms.",
		"Fires crisscrossing sour zest barrages at blistering speed.",
		"Pokes out hungry worms. Slice or tap them before they lunge!",
	};
	if (beaten) fitText(boss >= 0 && boss < BOSS_COUNT ? kBossBlurb[boss] : kBossBlurb[0], 200, 126, 0.34f, 326, COL_DIM);
	else fitText("Reach it in Orchard Blast or Boss Rush and beat it to fill this page.", 200, 126, 0.34f, 326, COL_DIM);
	// Win milestones, worth far more than a fruit's.
	const int level = progress::bossLevel(wins);
	for (int k = 1; k <= progress::kBossLevels; k++) {
		const float x = 200 + (k - 2.5f) * 76;
		const bool reached = k <= level;
		gfx::sprite(SHEET_SPRITES, sprites_coin_idx, x, 146, 0.38f, 0.38f, 0, reached ? WHITE : rgba(90, 80, 70, 200));
		snprintf(buf, sizeof buf, "%lu WIN%s  +%lu", (unsigned long)progress::bossMilestone(k), k == 1 ? "" : "S", (unsigned long)progress::bossReward(k));
		fitText(buf, x, 156, 0.31f, 70, reached ? COL_ACCENT : COL_DIM);
	}
	const int tracks = bossTrackCount(boss >= BOSS_DURIAN ? 4 : boss);
	if (level < progress::kBossLevels)
		snprintf(buf, sizeof buf, "%lu more win%s for %lu coins", (unsigned long)(progress::bossMilestone(level + 1) - wins), progress::bossMilestone(level + 1) - wins == 1 ? "" : "s", (unsigned long)progress::bossReward(level + 1));
	else
		snprintf(buf, sizeof buf, "Every boss milestone reached!");
	fitText(buf, 200, 173, 0.36f, 324, COL_CREAM);
	if (tracks > 0) {
		if (beaten) snprintf(buf, sizeof buf, "Its %d tracks are in the Shop", tracks);
		else snprintf(buf, sizeof buf, "Unlocks %d tracks in the Shop", tracks);
		gfx::text(buf, 200, 190, 0.35f, beaten ? COL_ACCENT : COL_DIM, ALIGN_CENTER);
	}
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
		int beaten = 0;
		for (int k = 0; k < BOSS_COUNT; k++) beaten += bossBeaten(k);
		snprintf(buf, sizeof buf, "BOSSES BEATEN %d OF %d", beaten, (int)BOSS_COUNT);
		gfx::text(buf, 200, 138, 0.44f, COL_CREAM, ALIGN_CENTER);
		gfx::text("Every milestone pays coins, a boss's the most.", 200, 166, 0.39f, COL_DIM, ALIGN_CENTER);
		gfx::text("Back to the title", 200, 196, 0.42f, COL_CREAM, ALIGN_CENTER);
		return;
	}
	if (journalIsBoss(f - 1)) {
		drawJournalBossTop(f - 1 - progress::journalPages());
		return;
	}
	const int fruit = progress::journalFruit(f - 1);
	const u32 count = gSave.fruitSliced[fruit];
	gfx::setDepth(1.0f);
	gfx::sprite(SHEET_FRUIT, fruitSprite(fruit), 200, 50, 1.15f, 1.15f, gSave.calmFx ? 0 : sinf(sTime * 1.3f) * 0.12f, count ? WHITE : kUnfound);
	gfx::setDepth(0.7f);
	gfx::text(count ? progress::fruitName(fruit) : "Not found yet", 200, 84, 0.54f, COL_ACCENT, ALIGN_CENTER);
	snprintf(buf, sizeof buf, "SLICED %lu", (unsigned long)count);
	gfx::text(buf, 200, 110, 0.42f, COL_CREAM, ALIGN_CENTER);
	const int level = progress::journalLevel(count);
	for (int k = 1; k <= progress::kJournalLevels; k++) {
		const float x = 200 + (k - 2.5f) * 76;
		const bool reached = k <= level;
		gfx::sprite(SHEET_SPRITES, sprites_coin_idx, x, 146, 0.44f, 0.44f, 0, reached ? WHITE : rgba(90, 80, 70, 200));
		snprintf(buf, sizeof buf, "%lu", (unsigned long)progress::journalMilestone(k));
		fitText(buf, x, 158, 0.35f, 68, reached ? COL_CREAM : COL_DIM);
		snprintf(buf, sizeof buf, "+%lu", (unsigned long)progress::journalReward(k));
		fitText(buf, x, 173, 0.33f, 68, reached ? COL_ACCENT : COL_DIM);
	}
	if (level < progress::kJournalLevels)
		snprintf(buf, sizeof buf, "%lu more for %lu coins", (unsigned long)(progress::journalMilestone(level + 1) - count), (unsigned long)progress::journalReward(level + 1));
	else
		snprintf(buf, sizeof buf, "Every milestone reached!");
	fitText(buf, 200, 194, 0.38f, 324, COL_CREAM);
}

// Best room reached in Orchard Blast (keeps compatibility with legacy pre-endless saves).
static int statsRoomsConquered() {
	const int leg = (int)gSave.orchardBestRoom - 1;
	const int legClamped = leg < 0 ? 0 : (leg > kOrchardRooms ? kOrchardRooms : leg);
	return (int)gSave.endlessBestRoom > legClamped ? (int)gSave.endlessBestRoom : legClamped;
}

#if PORT_3DS
static const Button kStatsTabs[] = {{12, 42, 94, 18, "Personal"}, {113, 42, 94, 18, "StreetPass"}, {214, 42, 94, 18, "Global"}};
static constexpr int kStatsTabCount = 3, kStatsGlobalTab = 2;
#else
static const Button kStatsTabs[] = {{16, 42, 138, 18, "Personal"}, {166, 42, 138, 18, "Global"}};
static constexpr int kStatsTabCount = 2, kStatsGlobalTab = 1;
#endif
static const Button kSpChallengeBtn = {90, 200, 140, 34, "Challenge"};
static constexpr Mode kGlobalModes[] = {
	MODE_CLASSIC, MODE_ARCADE, MODE_FRENZY, MODE_ZEN, MODE_SUDDEN, MODE_MINEFIELD,
	MODE_COMBO, MODE_DAILY, MODE_BEAT, MODE_ORCHARD, MODE_BOSSRUSH, MODE_RECIPE, MODE_SHARP, MODE_JUGGLE
};
static constexpr int kGlobalModeCount = sizeof kGlobalModes / sizeof kGlobalModes[0];
static constexpr int kGlobalTabsVis = 4, kGlobalRowsVis = 8;
static const Button kGlobalModeArrows[2] = {{8, 62, 16, 17, "<"}, {296, 62, 16, 17, ">"}};

static const char* lbShortPlatform(const char* p);

static Button globalModeTabRect(int col) {
	return {26.0f + col * 67.0f, 62.0f, 65.0f, 17.0f, nullptr};
}

static void statsSelectGlobalMode(int idx) {
	sGlobalModeIdx = (idx % kGlobalModeCount + kGlobalModeCount) % kGlobalModeCount;
	if (sGlobalModeIdx < sGlobalTabScroll) sGlobalTabScroll = sGlobalModeIdx;
	if (sGlobalModeIdx >= sGlobalTabScroll + kGlobalTabsVis) sGlobalTabScroll = sGlobalModeIdx - kGlobalTabsVis + 1;
	if (sGlobalTabScroll > kGlobalModeCount - kGlobalTabsVis) sGlobalTabScroll = kGlobalModeCount - kGlobalTabsVis;
	if (sGlobalTabScroll < 0) sGlobalTabScroll = 0;
	sGlobalScroll = 0;
	lb::clearView();
	lb::fetch(kGlobalModes[sGlobalModeIdx], false, false, 100);
}

static void statsSwitchTab(int next) {
	sStatsTab = (next % kStatsTabCount + kStatsTabCount) % kStatsTabCount;
	sFocus[SCREEN_STATS] = 0;
	if (sStatsTab == kStatsGlobalTab) statsSelectGlobalMode(sGlobalModeIdx);
}

static void launchSpChallenge(int idx) {
	if (idx < 0 || idx >= streetpass::count()) return;
	const streetpass::Entry& e = streetpass::entry(idx);
	sSpModal = 0;
	sPassTurn = -1;
	sOptionsFromPause = false;
	const int tagIdx = idx;
	const u32 target = e.pkt.classicBest;
	char rival[16];
	snprintf(rival, sizeof rival, "%s", e.pkt.name);
	sStartMode = MODE_CLASSIC;
	sScreen = SCREEN_GAME;
	sGame.startStreetPassChallenge(tagIdx, rival, target);
}

// Stats: Personal overview, 3DS StreetPass rivals, and Global Top 100 leaderboards.
static void drawStats() {
	drawHeader("Stats");
	drawBackArrow(sFocus[SCREEN_STATS] == 0, sPressed == 0);
	for (int t = 0; t < kStatsTabCount; t++) {
		drawPlank(kStatsTabs[t].x, kStatsTabs[t].y, kStatsTabs[t].w, kStatsTabs[t].h, sStatsTab == t, false);
		fitText(kStatsTabs[t].label, kStatsTabs[t].x + kStatsTabs[t].w * 0.5f, kStatsTabs[t].y + 3, 0.31f, kStatsTabs[t].w - 8, sStatsTab == t ? COL_ACCENT : COL_CREAM);
	}
#if PORT_3DS
	if (sStatsTab == 1) {
		const int cnt = streetpass::count();
		if (cnt == 0) {
			gfx::sign(sprites_plank_idx, 8, 96, 304, 102, COL_PLANK);
			gfx::text("No StreetPass tags yet.", 160, 128, 0.44f, COL_CREAM, ALIGN_CENTER);
			fitText(gSave.streetPassOn ? "Keep 3DS in Sleep Mode or use NetPass." : "Turn StreetPass On in Options to exchange.", 160, 154, 0.32f, 280, COL_DIM);
			return;
		}
		if (sSpStatSel >= cnt) sSpStatSel = cnt - 1;
		if (sSpStatSel < 0) sSpStatSel = 0;
		if (sSpStatScroll > sSpStatSel) sSpStatScroll = sSpStatSel;
		if (sSpStatSel >= sSpStatScroll + 4) sSpStatScroll = sSpStatSel - 3;
		char line[48];
		for (int row = 0; row < 4 && sSpStatScroll + row < cnt; row++) {
			const int idx = sSpStatScroll + row;
			const streetpass::Entry& e = streetpass::entry(idx);
			const float y = 66.0f + row * 32.0f;
			drawPlank(8, y, 304, 29, idx == sSpStatSel, false);
			snprintf(line, sizeof line, "%s%s", e.pkt.name, e.won ? "  *WON" : "");
			fitText(line, 18, y + 4, 0.34f, 175, e.won ? rgba(160, 240, 170) : COL_CREAM, ALIGN_LEFT);
			if (sSpMetric == 0) snprintf(line, sizeof line, "CLASSIC %lu", (unsigned long)e.pkt.classicBest);
			else snprintf(line, sizeof line, "ORCHARD RM %u", (unsigned)e.pkt.endlessBestRoom);
			gfx::text(line, 302, y + 4, 0.30f, COL_ACCENT, ALIGN_RIGHT);
			fitText(e.pkt.msg, 18, y + 17, 0.27f, 280, COL_DIM, ALIGN_LEFT);
		}
		drawSign(kSpChallengeBtn, "Challenge", sFocus[SCREEN_STATS] == 1, sPressed == 1, 0.44f, -1, MF_MELON);
		return;
	}
#endif
	if (sStatsTab == kStatsGlobalTab) {
		for (int a = 0; a < 2; a++) {
			drawPlank(kGlobalModeArrows[a].x, kGlobalModeArrows[a].y, kGlobalModeArrows[a].w, kGlobalModeArrows[a].h, false, false);
			gfx::text(kGlobalModeArrows[a].label, kGlobalModeArrows[a].x + kGlobalModeArrows[a].w * 0.5f, kGlobalModeArrows[a].y + 2, 0.30f, COL_CREAM, ALIGN_CENTER);
		}
		for (int col = 0; col < kGlobalTabsVis && sGlobalTabScroll + col < kGlobalModeCount; col++) {
			const int mi = sGlobalTabScroll + col;
			const Mode m = kGlobalModes[mi];
			const Button b = globalModeTabRect(col);
			const bool sel = mi == sGlobalModeIdx;
			drawPlank(b.x, b.y, b.w, b.h, sel, false);
			fitText(kModes[m].name, b.x + b.w * 0.5f, b.y + 3.5f, 0.26f, b.w - 6, sel ? COL_ACCENT : COL_CREAM);
		}
		gfx::sign(sprites_plank_idx, 8, 82, 304, 152, COL_PLANK);
		const lb::BoardView& v = lb::view();
		const Mode curMode = kGlobalModes[sGlobalModeIdx];
		char buf[72];
		if (v.status == lb::STATUS_LOADING || v.status == lb::STATUS_IDLE) {
			gfx::text("Loading Top 100...", 160, 138, 0.38f, COL_CREAM, ALIGN_CENTER);
		} else if (v.status != lb::STATUS_READY) {
			gfx::text("Leaderboard offline or unreachable.", 160, 130, 0.35f, COL_CREAM, ALIGN_CENTER);
			gfx::text("Check internet connection and try again.", 160, 150, 0.29f, COL_DIM, ALIGN_CENTER);
		} else if (v.count == 0) {
			gfx::text("No scores uploaded yet!", 160, 138, 0.38f, COL_CREAM, ALIGN_CENTER);
		} else {
			const int maxScroll = v.count > kGlobalRowsVis ? v.count - kGlobalRowsVis : 0;
			if (sGlobalScroll > maxScroll) sGlobalScroll = maxScroll;
			if (sGlobalScroll < 0) sGlobalScroll = 0;
			char myPid[24];
			lb::formatPlayerId(gSave.leaderboardId, myPid, sizeof myPid);
			gfx::setClip(14, 86, 292, 114);
			for (int row = 0; row < kGlobalRowsVis && sGlobalScroll + row < v.count; row++) {
				const int i = sGlobalScroll + row;
				const lb::Entry& e = v.entries[i];
				const float ry = 88.0f + row * 14.0f;
				const bool isYou = e.playerId[0] && strcmp(e.playerId, myPid) == 0;
				const u32 col = isYou ? COL_ACCENT : COL_CREAM;
				if (isYou) gfx::rect(16, ry - 1, 282, 13, withAlpha(COL_ACCENT, 0.14f));
				snprintf(buf, sizeof buf, "%3d. %s", i + 1, e.name);
				gfx::text(buf, 18, ry, 0.29f, col);
				snprintf(buf, sizeof buf, "(%s)", lbShortPlatform(e.platform));
				gfx::text(buf, 188, ry + 0.5f, 0.26f, COL_DIM);
				snprintf(buf, sizeof buf, "%lu", (unsigned long)e.score);
				gfx::text(buf, 292, ry, 0.29f, col, ALIGN_RIGHT);
			}
			gfx::clearClip();
			if (v.count > kGlobalRowsVis) {
				const float trackH = 110.0f;
				const float barH = fmaxf(14.0f, trackH * kGlobalRowsVis / v.count);
				const float barY = 88.0f + (trackH - barH) * (maxScroll > 0 ? (float)sGlobalScroll / maxScroll : 0.0f);
				gfx::rect(300, 88, 3, trackH, rgba(0, 0, 0, 80));
				gfx::rect(300, barY, 3, barH, COL_ACCENT);
			}
		}
		gfx::rect(16, 203, 288, 1, withAlpha(COL_DIM, 0.35f));
		const u32 myBest = gSave.best[curMode];
		if (v.status == lb::STATUS_READY && v.yourRank > 0) {
			snprintf(buf, sizeof buf, "YOU: RANK %d OF %d   BEST %lu", v.yourRank, v.total > 0 ? v.total : v.yourRank, (unsigned long)v.yourScore);
			fitText(buf, 160, 211, 0.30f, 284, COL_ACCENT);
		} else if (!gSave.leaderboardName[0]) {
			snprintf(buf, sizeof buf, "YOUR BEST: %lu   (SET NAME ON NEXT BEST)", (unsigned long)myBest);
			fitText(buf, 160, 211, 0.28f, 284, COL_DIM);
		} else {
			snprintf(buf, sizeof buf, "YOUR BEST: %lu   (BEAT YOUR BEST TO RANK!)", (unsigned long)myBest);
			fitText(buf, 160, 211, 0.28f, 284, COL_DIM);
		}
		return;
	}
	const float boxY = 66.0f, boxH = 168.0f;
	gfx::sign(sprites_plank_idx, 8, boxY, 304, boxH, COL_PLANK);
	constexpr int kRows = 11;
	char value[kRows][32];
	snprintf(value[0], 32, "%lu", (unsigned long)gSave.totalFruit);
	snprintf(value[1], 32, "%lu", (unsigned long)gSave.coinsLifetime);
	const unsigned long secs = gSave.playSeconds;
	if (secs >= 3600) snprintf(value[2], 32, "%luh %02lum", secs / 3600, secs / 60 % 60);
	else snprintf(value[2], 32, "%lum", secs / 60);
	const Mode fav = progress::favouriteMode();
	snprintf(value[3], 32, "%s", fav < MODE_COUNT ? modeName(fav) : "None yet");
	snprintf(value[4], 32, "%lu", (unsigned long)gSave.bestCombo);
	snprintf(value[5], 32, "%d (best %d)", progress::dailyStreak(progress::today()), (int)gSave.dailyBestStreak);
	const int bestRoom = statsRoomsConquered();
	if (bestRoom > 0) snprintf(value[6], 32, "Room %d", bestRoom);
	else snprintf(value[6], 32, "None yet");
	const unsigned long runs = (unsigned long)gSave.endlessRuns + gSave.orchardRuns;
	snprintf(value[7], 32, "%lu", runs);
	const unsigned bestLv = gSave.endlessBestLevel > gSave.orchardBestLevel ? gSave.endlessBestLevel : gSave.orchardBestLevel;
	snprintf(value[8], 32, "%u", bestLv);
	unsigned long bossWins = 0;
	for (u16 w : gSave.bossWins) bossWins += w;
	snprintf(value[9], 32, "%lu", bossWins);
	if (gSave.rushBestSecs) snprintf(value[10], 32, "%u:%02u", gSave.rushBestSecs / 60u, gSave.rushBestSecs % 60u);
	else snprintf(value[10], 32, "Not cleared yet");
	static const char* const kNames[kRows] = {"FRUIT SLICED", "COINS EARNED", "TIME PLAYED", "FAVOURITE MODE", "LONGEST COMBO", "DAILY STREAK",
	                                          "BEST ROOM", "RUNS", "BEST LEVEL", "BOSS WINS", "FASTEST BOSS RUSH"};
	const float rowStep = 13.0f;
	const float headGap = 10.0f;
	const float yBase = 69.0f;
	for (int i = 0; i < kRows; i++) {
		// The Orchard Blast rows sit under their own heading.
		const float y = yBase + i * rowStep + (i >= 6 ? headGap : 0);
		if (i == 6) gfx::text("ORCHARD BLAST", 160, y - headGap - 1, 0.33f, COL_ACCENT, ALIGN_CENTER);
		float sv = 0.34f, w = gfx::textWidth(value[i], sv);
		if (w > 130) sv *= 130 / w;
		const float valW = gfx::textWidth(value[i], sv);
		const float nameMax = 296 - valW - 24 - 8;
		float nl = 0.32f, nw = gfx::textWidth(kNames[i], nl);
		if (nw > nameMax) nl *= nameMax / nw;
		gfx::text(kNames[i], 24, y + 1, nl, COL_DIM);
		gfx::text(value[i], 296, y, sv, COL_CREAM, ALIGN_RIGHT);
	}
}

static void drawStatsTop() {
	char buf[72];
	gfx::setDepth(1.0f);
	gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 44, 0.52f, 0.52f);
	topPanel(30, 80, 340, 150);
	gfx::setDepth(0.7f);
#if PORT_3DS
	if (sStatsTab == 1) {
		if (streetpass::count() == 0) {
			gfx::text("STREETPASS CARD", 200, 88, 0.44f, COL_ACCENT, ALIGN_CENTER);
			fitText(gSave.streetPassMsg[0] ? gSave.streetPassMsg : "Hello! :)", 200, 118, 0.42f, 300, COL_CREAM);
			snprintf(buf, sizeof buf, "CLASSIC BEST %lu   WINS %u", (unsigned long)gSave.best[MODE_CLASSIC], (unsigned)gSave.spChallengesWon);
			gfx::text(buf, 200, 152, 0.35f, COL_DIM, ALIGN_CENTER);
			gfx::text("L/R: TABS   ZL/ZR: METRIC", 200, 206, 0.30f, COL_DIM, ALIGN_CENTER);
			return;
		}
		const streetpass::Entry& e = streetpass::entry(sSpStatSel);
		gfx::text(e.pkt.name, 200, 88, 0.48f, COL_ACCENT, ALIGN_CENTER);
		fitText(e.pkt.msg, 200, 114, 0.40f, 310, COL_CREAM);
		snprintf(buf, sizeof buf, "CLASSIC %lu   ORCHARD ROOM %u", (unsigned long)e.pkt.classicBest, (unsigned)e.pkt.endlessBestRoom);
		gfx::text(buf, 200, 144, 0.34f, COL_CREAM, ALIGN_CENTER);
		snprintf(buf, sizeof buf, "FRUIT SLICED %lu   BOSSES %u", (unsigned long)e.pkt.totalFruit, (unsigned)e.pkt.bossesBeaten);
		gfx::text(buf, 200, 164, 0.34f, COL_DIM, ALIGN_CENTER);
		if (e.won) snprintf(buf, sizeof buf, "CHALLENGE WON! BEST %lu", (unsigned long)e.bestAttempt);
		else if (e.challenged) snprintf(buf, sizeof buf, "YOUR BEST ATTEMPT: %lu", (unsigned long)e.bestAttempt);
		else snprintf(buf, sizeof buf, "PRESS A OR CHALLENGE TO PLAY");
		gfx::text(buf, 200, 188, 0.34f, e.won ? rgba(160, 240, 170) : COL_ACCENT, ALIGN_CENTER);
		return;
	}
#endif
	if (sStatsTab == kStatsGlobalTab) {
		const Mode curMode = kGlobalModes[sGlobalModeIdx];
		snprintf(buf, sizeof buf, "GLOBAL TOP 100 - %s", modeName(curMode));
		fitText(buf, 200, 86, 0.40f, 320, COL_ACCENT);
		const lb::BoardView& v = lb::view();
		snprintf(buf, sizeof buf, "YOUR BEST: %lu", (unsigned long)gSave.best[curMode]);
		gfx::text(buf, 48, 108, 0.34f, COL_CREAM);
		if (v.status == lb::STATUS_READY && v.yourRank > 0) snprintf(buf, sizeof buf, "RANK %d OF %d", v.yourRank, v.total > 0 ? v.total : v.yourRank);
		else if (v.status == lb::STATUS_READY) snprintf(buf, sizeof buf, "UNRANKED");
		else snprintf(buf, sizeof buf, v.status == lb::STATUS_OFFLINE ? "OFFLINE" : "LOADING...");
		gfx::text(buf, 352, 108, 0.34f, v.yourRank > 0 ? COL_ACCENT : COL_DIM, ALIGN_RIGHT);
		gfx::rect(44, 124, 312, 1, withAlpha(COL_DIM, 0.35f));
		if (v.status == lb::STATUS_READY && v.count > 0) {
			for (int i = 0; i < 3 && i < v.count; i++) {
				const lb::Entry& e = v.entries[i];
				const float y = 130.0f + i * 18.0f;
				snprintf(buf, sizeof buf, "%d. %s (%s)", i + 1, e.name, lbShortPlatform(e.platform));
				gfx::text(buf, 48, y, 0.34f, i == 0 ? COL_ACCENT : COL_CREAM);
				snprintf(buf, sizeof buf, "%lu", (unsigned long)e.score);
				gfx::text(buf, 352, y, 0.34f, i == 0 ? COL_ACCENT : COL_CREAM, ALIGN_RIGHT);
			}
		} else {
			fitText(v.status == lb::STATUS_OFFLINE ? "Cannot reach leaderboard right now." : "Fetching Top 100 rankings...", 200, 150, 0.34f, 300, COL_DIM);
		}
		gfx::text("LB/RB: TAB   ZL/ZR: MODE   UP/DN: SCROLL", 200, 206, 0.28f, COL_DIM, ALIGN_CENTER);
		return;
	}
	gfx::text("BEST SCORES", 200, 86, 0.42f, COL_ACCENT, ALIGN_CENTER);
	// The modes on Pick a Game, in two columns: all 14 fit at seven to a column.
	constexpr int kPerColumn = (MODE_COUNT + 1) / 2;
	static_assert(kPerColumn <= 7, "the best scores need a second page");
	int shown = 0;
	for (int i = 0; i < (int)sizeof kModeOrder; i++) {
		const int m = kModeOrder[i];
		if (m >= MODE_COUNT) continue;
		const float x = shown < kPerColumn ? 44 : 214, y = 107 + (shown % kPerColumn) * 16.0f;
		shown++;
		snprintf(buf, sizeof buf, "%lu", (unsigned long)gSave.best[m]);
		const float vw = gfx::textWidth(buf, 0.35f);
		float ns = 0.34f, nw = gfx::textWidth(kModes[m].name, ns);
		const float maxNW = 140.0f - vw - 6.0f;
		if (nw > maxNW) ns *= maxNW / nw;
		gfx::text(kModes[m].name, x, y, ns, COL_DIM);
		gfx::text(buf, x + 140, y - 1, 0.35f, COL_CREAM, ALIGN_RIGHT);
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

#if PORT_CUSTOM_MUSIC
// Songs picked with Add Songs are in the folder: the list reads it again and the first new song is picked,
// which starts finding its beat. Songs sort by name, so the one already chosen is found again by its path.
static void beatImportTick() {
	if (sAddNoteT > 0) sAddNoteT -= kDt;
	char first[128];
	const int added = custom::importDone(first, sizeof first);
	if (added == -2) {
		// No file picker on this system: the folder to copy songs into by hand, or its path.
		if (!custom::openFolder()) sFolderNote = 6;
		return;
	}
	if (added < 0) return;
	char chosen[600] = "";
	if (beatSong() >= kTrackCount) snprintf(chosen, sizeof chosen, "%s", custom::path(beatSong() - kTrackCount));
	listenStop();
	sListenFailed = -1;
	custom::scan();
	int pick = -1, keep = -1;
	const size_t folderLen = strlen(custom::folder());
	for (int i = 0; i < custom::count(); i++) {
		const char* p = custom::path(i);
		if (added > 0 && first[0] && strcmp(p + folderLen, first) == 0) pick = i;
		if (chosen[0] && strcmp(p, chosen) == 0) keep = i;
	}
	if (pick >= 0) {
		beatSelect(kTrackCount + pick);
	} else if (chosen[0]) {
		// Still there: the same song under its new place in the list. Gone: back to the first track.
		const int t = keep >= 0 ? kTrackCount + keep : 1;
		gSave.beatTrack = (u8)(t + 1);
		audio::musicBeat(t);
		beatScrollTo(t);
		listenFor(t);
	}
	if (added == 0) snprintf(sAddNote, sizeof sAddNote, "No new songs added.");
	else snprintf(sAddNote, sizeof sAddNote, added == 1 ? "Added 1 song." : "Added %d songs.", added);
	sAddNoteT = 4;
}
#endif

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

static int sBeatHover = -1, sBeatArrowPress = -1;
static HoldRepeat sBeatArrowHold;

#if PORT_CUSTOM_MUSIC
static void drawTap(const Button& b, const char* label, bool focused = false, bool pressed = false) {
	drawPlank(b.x, b.y, b.w, b.h, focused, pressed);
	const float hi = highlightFade(b.x, b.y, b.w, b.h, focused);
	float s = 0.36f, w = gfx::textWidth(label, s);
	if (w > b.w - 10) s *= (b.w - 10) / w;
	gfx::text(label, b.x + b.w * 0.5f, b.y + b.h * 0.5f - 20 * s - 1 + (pressed ? 2 : 0), s, lerpColor(COL_CREAM, COL_ACCENT, hi), ALIGN_CENTER);
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
			const bool foc = can && (sBeatHover == side || sBeatArrowPress == side);
			const bool prs = can && sBeatArrowPress == side;
			drawPlank(b.x, b.y, b.w, b.h, foc, prs);
			gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, b.x + b.w * 0.5f, b.y + b.h * 0.5f + (prs ? 2 : 0), 0.45f, 0.45f, side ? -1.5707963f : 1.5707963f, can ? COL_ACCENT : withAlpha(COL_CREAM, 0.3f));
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
	if (t >= kTrackCount) drawTap(kBeatTool, kBeatTool.label, sBeatHover == 2, sBeatArrowPress == 2);
	drawTap(kBeatFolder, kBeatFolder.label, sBeatHover == 3, sBeatArrowPress == 3);
	if (custom::count() == 0) hint = "Add Songs takes .mp3, .ogg, .flac and .wav files.";
	if (custom::importOpen()) hint = "Pick your songs in the window that opened.";
	if (sAddNoteT > 0) hint = sAddNote;
	if (sFolderNote > 0) hint = custom::folder();
#endif
	float s = 0.32f, w = gfx::textWidth(hint, s);
	if (w > 304) s *= 304 / w;
	gfx::text(hint, 160, 223, s, COL_DIM, ALIGN_CENTER);
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

static int sToolHover = -1, sToolPress = -1;
static HoldRepeat sToolHold;

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
		drawTap(b, label, sToolHover == i || sToolPress == i, sToolPress == i);
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

static Button sUpdateButtons[2];
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
	if (*b) {
		sUpdateButtons[0] = kUpdateTwo[0];
		sUpdateButtons[0].label = *a;
		sUpdateButtons[1] = kUpdateTwo[1];
		sUpdateButtons[1].label = *b;
		*out = sUpdateButtons;
		return 2;
	}
	sUpdateButtons[0] = kUpdateOne[0];
	sUpdateButtons[0].label = *a;
	*out = sUpdateButtons;
	return 1;
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
	gfx::sign(sprites_plank_idx, 20, 52, 280, update::state() == update::DOWNLOADING ? 78.0f : 68.0f, COL_PLANK);
	fitText(l1, 160, 64, 0.36f, 246, COL_CREAM);
	fitText(l2, 160, 86, 0.36f, 246, COL_DIM);
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
	const update::State st = update::state();
	if (st != update::OFFER && st != update::NOTE) {
		gfx::setDepth(1.0f);
		gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
		topPanel(30, 96, 340, 74);
		gfx::setDepth(0.7f);
		gfx::text(st == update::ASK ? "Nothing about you is sent:" : "Fruit Slicer", 200, 112, 0.4f, COL_ACCENT, ALIGN_CENTER);
		gfx::text(st == update::ASK ? "only a request for the newest release." : "Version " VERSION_STRING, 200, 134, 0.36f, COL_CREAM, ALIGN_CENTER);
		return;
	}
	gfx::setDepth(1.0f);
	gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 40, 0.5f, 0.5f);
	topPanel(30, 70, 340, 162);
	gfx::setDepth(0.7f);
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
	static bool sUpdateWasTouch = false;
	const update::State noteState = update::state();
	const bool hasNotes = noteState == update::OFFER || noteState == update::NOTE;
	bool onSign = false;
	for (int i = 0; i < n; i++) onSign |= menuHit(btns[i], SCREEN_UPDATE, in.tx, in.ty);
	if (!wasTouching && !in.touching) sUpdateWasTouch = false;
	if (in.touching && !wasTouching) pos = sUpdateScroll * (float)kNoteStep;
	if (hasNotes && !onSign && in.ty < 140 && listDrag(drag, in, wasTouching, 0.0f, 140.0f, &pos)) {
		sUpdateScroll = pos > 0 ? (int)(pos / kNoteStep + 0.5f) : 0;
		keys.touching = false;
	} else if (!hasNotes) {
		drag = {};
	}
	const bool prevUpdateTouch = sUpdateWasTouch;
	sUpdateWasTouch = keys.touching;
	if (n == 0) return;  // downloading: nothing to press
	int t = menuInput(btns, n, SCREEN_UPDATE, keys, prevUpdateTouch);
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

// ---------------------------------------------------------------- Mode Guides
// A visual guide for every mode and way to play, reached from How to Play -> Guides. Adding a new mode only
// needs an entry in kGuides[]; the page count, navigation, and fallback screenshot preview adapt automatically.
struct GuideEntry {
	int id;
	const char* title;  // nullptr uses the mode's own name from kModes[]
	int icon;
	const char* goal;
	const char* line1;
	const char* line2;
	const char* line3;
	const char* line4;
};

static const GuideEntry kGuides[] = {
	{MODE_CLASSIC, nullptr, sprites_pip_on_idx, "SURVIVE WITHOUT DROPPING 3 FRUIT",
	 "Swipe fruit to score. Chain 3+ in one swipe for a combo bonus.",
	 "Dropping a whole fruit costs 1 life (up to 3 misses allowed).",
	 "Watch for rare Trick Fruit like Pomegranates and Dragon Fruit!",
	 "Slicing a single bomb ends the run immediately!"},
	{MODE_ARCADE, nullptr, sprites_ic_clock_idx, "60 SECONDS + POWER-UP BANANAS",
	 "Score as much as you can before the 60-second clock runs out.",
	 "Slice special bananas for Freeze, Frenzy, Double Points, and more.",
	 "Bombs don't end the run: they deduct 10 points and break your combo.",
	 "At 0:00, slice the giant Sunburst fruit rapidly for a finale bonus!"},
	{MODE_FRENZY, nullptr, sprites_ic_fast_idx, "30 SECONDS OF NON-STOP FRUIT SHOWERS",
	 "Fruit pours in from both sides for 30 seconds straight.",
	 "No miss penalties for dropped fruit: pure high-speed slicing.",
	 "Slicing a bomb deducts 5 seconds right off the clock!",
	 "Ends with the giant Sunburst fruit at 0:00."},
	{MODE_ZEN, nullptr, sprites_ic_leaf_idx, "ENDLESS RELAXING SLICING",
	 "No timer, no bombs, and no strikes for dropped fruit.",
	 "Practice big combos and trick fruit at your own pace.",
	 "Pause at any time and choose Finish Run to keep your score & coins.",
	 "Calm music plays throughout your run."},
	{MODE_SUDDEN, nullptr, sprites_ic_one_idx, "ONE MISS OR ONE BOMB ENDS IT ALL",
	 "High-stakes survival: you only have a single life.",
	 "Dropping even one whole fruit ends the run on the spot.",
	 "Slicing any bomb also ends the run immediately.",
	 "Stay sharp and prioritize falling fruit near the bottom edge."},
	{MODE_DAILY, nullptr, sprites_coin_idx, "ONE SEED EACH DAY FOR EVERYONE",
	 "Every player gets the exact same 60-second sequence of throws today.",
	 "Your first run each day awards a Daily Coin Bonus.",
	 "Play on consecutive days to build your Daily Streak.",
	 "Replay as many times as you like to raise Today's Best."},
	{MODE_MINEFIELD, nullptr, sprites_ic_mine_idx, "THREAD YOUR BLADE BETWEEN BOMBS",
	 "Every wave throws tight clusters of bombs alongside the fruit.",
	 "One bomb hit ends the run immediately. Misses are free!",
	 "Wait for fruit and bombs to separate at the peak of their arc.",
	 "Short, surgical cuts beat wide sweeps here."},
	{MODE_COMBO, nullptr, sprites_ic_chain_idx, "ONLY 3+ FRUIT COMBOS SCORE",
	 "Single slices and 2-fruit cuts score 0 points.",
	 "Slice 3 or more fruit in a single swipe to score and fill the meter.",
	 "Once you make your first cut, the meter never stops draining.",
	 "If the meter runs dry or you hit a bomb, the run is over!"},
	{MODE_BEAT, nullptr, sprites_ic_note_idx, "SLICE IN TIME WITH THE MUSIC",
	 "Pick any unlocked track (or import your own songs) before playing.",
	 "Watch the 4 beat lights at the top and slice right on the beat.",
	 "On-beat slices build your rhythm chain; off-beat cuts break your combo.",
	 "Use Audio -> Latency Test to calibrate Beat Timing to your setup."},
	{MODE_ORCHARD, nullptr, sprites_ic_tree_idx, "ENDLESS ROOMS & SEEDED BOSSES",
	 "Each room cleared scores +1. Normal-room fruit gives XP for upgrades.",
	 "Every 5 rooms a seeded boss arrives. Every 25th brings a Milestone Boss!",
	 "Boss-room fruit and shots give 0 XP unless you hold Boss Harvest.",
	 "Runs save at each room start. Pause to Start Over anytime!"},
	{MODE_BOSSRUSH, nullptr, sprites_ic_crown_idx, "5 BOSSES BACK TO BACK, NO FRUIT WAVES",
	 "Pick a starting Build, then defeat 4 seeded bosses plus a Milestone finale.",
	 "Each boss defeated scores +1 (up to 5) and heals half your Max HP.",
	 "Blue Ring = Guard Up (don't slice!). Gold Ring = Guard Down (slice!).",
	 "Slice boss shots, defuse Bomb King bombs, and snuff his lit fuse in 3 cuts!"},
	{MODE_RECIPE, nullptr, sprites_ic_cup_idx, "FILL SMOOTHIE ORDERS IN 60 SECONDS",
	 "The top bar shows 2-3 fruit kinds needed for the current Smoothie Cup.",
	 "Slice the listed fruits in any order to win bonus points and +8 seconds.",
	 "Slicing a fruit not in the recipe spoils the cup and starts a new order.",
	 "Slicing a bomb takes 10 seconds right off the clock!"},
	{MODE_SHARP, nullptr, sprites_ic_target_idx, "SLICE ONLY THE CALLED RING & SHAPE",
	 "Every fruit wears a coloured shape ring: Circle, Square, Triangle, or Star.",
	 "Slice fruits matching the SLICE: <SHAPE> call at the top in 60 seconds.",
	 "Slicing the called shape ring scores double points and builds your combo.",
	 "Slicing the wrong ring deducts 3 points and breaks your combo!"},
	{MODE_JUGGLE, nullptr, sprites_ic_juggle_idx, "KEEP WHOLE FRUIT BOUNCING IN THE AIR",
	 "Swiping a fruit knocks it back up into the air instead of cutting it.",
	 "Each bounce scores +1 for every fruit in the air (3x for Gold!).",
	 "Keep multiple fruits airborne at once to stack big points fast.",
	 "Avoid bombs: swiping one explodes it and costs a life! 3 misses end it."},
	{SIGN_MIXER, "Mixer", sprites_knob_idx, "CUSTOM RULES FOR ANY GAME MODE",
	 "Open the Mixer on Pick a Game to toggle modifiers like Low Gravity,",
	 "Giant Fruit, Tiny Fruit, Mirror Blade, Double Bombs, and Sides Only.",
	 "Combine as many rules as you like to invent your own challenge.",
	 "Mixer runs save to their own Mixer Best table so normal bests stay pure."},
	{SIGN_PASS, "Pass and Play", sprites_arrow_idx, "2 TO 4 PLAYERS ON ONE DEVICE",
	 "Pick 2, 3, or 4 players, type names, and choose any competitive mode.",
	 "Each player takes one full turn on the same device.",
	 "After the final turn, the scoreboard crowns the winner!",
	 "Pass and Play runs leave your solo best scores untouched."},
};
static constexpr int kGuideCount = sizeof kGuides / sizeof kGuides[0];
static int sGuidePage = 0;
static const Button kGuideArrows[2] = {{8, 44, 34, 32, nullptr}, {278, 44, 34, 32, nullptr}};

static const char* guideTitle(int idx) {
	const GuideEntry& g = kGuides[idx];
	return g.title ? g.title : kModes[g.id].name;
}

static void turnGuide(int dir) {
	sGuidePage = (sGuidePage + dir + kGuideCount) % kGuideCount;
	audio::click(0.6f);
}

static int sGuideHover = -1, sGuidePress = -1;
static HoldRepeat sGuideHold;

static void updateGuides(const Input& in, bool wasTouching) {
	if (in.down & KEY_B) {
		sGuidePress = -1;
		sGuideHold.reset();
		audio::click();
		goTo(SCREEN_HOWTO, -1);
		return;
	}
	bool leave;
	if (backCut(SCREEN_GUIDES, in, wasTouching, &leave)) {
		if (leave) {
			sGuidePress = -1;
			sGuideHold.reset();
			goTo(SCREEN_HOWTO, -1);
		}
		return;
	}
#if PORT_MOUSE
	if (in.hovering) {
		sGuideHover = -1;
		for (int side = 0; side < 2; side++)
			if (hit(kGuideArrows[side], in.tx, in.ty)) sGuideHover = side;
	}
#endif
	if ((in.repeat & (KEY_LEFT | KEY_UP)) || (in.down & KEY_L) || in.wheel > 0) turnGuide(-1);
	if ((in.repeat & (KEY_RIGHT | KEY_DOWN)) || (in.down & KEY_R) || in.wheel < 0) turnGuide(1);
	if (in.down & KEY_A) {
		cutBack(SCREEN_GUIDES);
		return;
	}
	if (in.touching) {
		if (!wasTouching) {
			if (hit(kBack, in.tx, in.ty)) {
				cutBack(SCREEN_GUIDES);
				return;
			}
			sGuidePress = -1;
			for (int side = 0; side < 2; side++) {
				if (hit(kGuideArrows[side], in.tx, in.ty)) {
					sGuidePress = side;
					break;
				}
			}
		}
		if (sGuidePress >= 0) {
			const bool over = hit(kGuideArrows[sGuidePress], in.tx, in.ty);
			if (sGuideHold.step(over ? sGuidePress : -1, over)) turnGuide(sGuidePress ? 1 : -1);
		}
	} else {
		sGuidePress = -1;
		sGuideHold.reset();
	}
}

// Draws a labelled callout tag inside the top-screen screenshot preview, with an optional pointer line.
static void drawGuideCallout(float x, float y, const char* text, u32 col = COL_ACCENT, float tx = -1, float ty = -1) {
	float s = PORT_3DS ? 0.33f : 0.31f, w = gfx::textWidth(text, s) + 12;
	if (w > 348) {
		s = 336.0f / gfx::textWidth(text, 1.0f);
		w = gfx::textWidth(text, s) + 12;
	}
	const float h = 18;
	const float bx = clampf(x, 24 + w * 0.5f, 376 - w * 0.5f);
	if (tx >= 0 && ty >= 0) {
		const float dx = tx - bx, dy = ty - y, len = sqrtf(dx * dx + dy * dy);
		if (len > 2) {
			const float nx = -dy / len * 1.1f, ny = dx / len * 1.1f;
			const u32 lc = withAlpha(col, 0.85f);
			gfx::tri(bx + nx, y + ny, lc, bx - nx, y - ny, lc, tx, ty, lc);
			gfx::rect(tx - 2.5f, ty - 2.5f, 5, 5, col);
		}
	}
	gfx::rect(bx - w * 0.5f - 1, y - h * 0.5f - 1, w + 2, h + 2, withAlpha(col, 0.9f));
	gfx::rect(bx - w * 0.5f, y - h * 0.5f, w, h, rgba(22, 16, 14, 235));
	gfx::text(text, bx, y - 20.0f * s, s, col, ALIGN_CENTER);
}

// Annotated in-engine screenshot diagram on the top screen showing what's what in the selected mode.
static void drawGuidesTop() {
	const GuideEntry& g = kGuides[sGuidePage];
	topPanel(18, 6, 364, 228);
	gfx::setDepth(0.5f);
	// Inner screenshot viewport with the board background and HUD bar.
	const float vx = 30, vy = 18, vw = 340, vh = 204;
	gfx::rect(vx - 2, vy - 2, vw + 4, vh + 4, rgba(18, 12, 8, 220));
	if (gSave.highContrast) gfx::rect(vx, vy, vw, vh, rgba(20, 20, 26));
	else gfx::image(SHEET_BOARD, 0, vx, vy, vw, vh, rgba(195, 195, 195));
	gfx::rect(vx, vy, vw, 24, rgba(28, 18, 12, 225));
	gfx::setDepth(0.8f);

	const float wob = gSave.calmFx ? 0 : sinf(sTime * 2.5f) * 0.08f;
	switch (g.id) {
		case MODE_CLASSIC:
		case MODE_SUDDEN: {
			const bool one = g.id == MODE_SUDDEN;
			gfx::text("SCORE 145", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			if (!one) gfx::text("BEST 380", 200, vy + 5, 0.30f, COL_CREAM, ALIGN_CENTER);
			for (int i = 0; i < (one ? 1 : 3); i++)
				gfx::sprite(SHEET_SPRITES, i == 0 ? sprites_pip_on_idx : sprites_pip_off_idx, vx + vw - 14 - i * 16, vy + 12, 0.45f, 0.45f);
			gfx::sprite(SHEET_FRUIT, fruit_melon_l_idx, 96, 102, 0.68f, 0.68f, -0.35f + wob);
			gfx::sprite(SHEET_FRUIT, fruit_melon_r_idx, 134, 108, 0.68f, 0.68f, 0.35f - wob);
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, 276, 104, 0.72f, 0.72f, wob);
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, 276, 104, 0.85f, 0.85f, 0, rgba(255, 80, 70, 200));
			gfx::sprite(SHEET_FRUIT, fruit_apple_idx, 194, 174, 0.6f, 0.6f);
			drawGuideCallout(194, 54, one ? "SUDDEN DEATH: 1 LIFE ONLY" : "3 LIVES (REMAINING MISSES)", COL_ACCENT, vx + vw - 30, vy + 12);
			drawGuideCallout(114, 144, "SWIPE FRUIT (+1)", rgba(150, 245, 160), 114, 106);
			drawGuideCallout(276, 152, "BOMB = GAME OVER!", rgba(255, 120, 110), 276, 112);
			drawGuideCallout(194, 204, one ? "1 DROP = GAME OVER" : "3 DROPS = GAME OVER", COL_CREAM, 194, 180);
			break;
		}
		case MODE_ARCADE: {
			gfx::text("SCORE 320", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::text("0:45", 200, vy + 5, 0.34f, COL_CREAM, ALIGN_CENTER);
			gfx::text("2X ON", vx + vw - 8, vy + 5, 0.32f, rgba(255, 215, 90), ALIGN_RIGHT);
			gfx::sprite(SHEET_SPRITES, sprites_pw_freeze_idx, 82, 92, 0.75f, 0.75f, -0.2f + wob);
			gfx::sprite(SHEET_SPRITES, sprites_pw_frenzy_idx, 132, 82, 0.75f, 0.75f, wob);
			gfx::sprite(SHEET_SPRITES, sprites_pw_double_idx, 182, 92, 0.75f, 0.75f, 0.2f - wob);
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, 286, 96, 0.68f, 0.68f);
			gfx::sprite(SHEET_FRUIT, fruit_gold_idx, 200, 168, 0.9f, 0.9f, wob);
			drawGuideCallout(132, 132, "POWER-UP BANANAS", COL_ACCENT, 132, 92);
			drawGuideCallout(286, 142, "BOMB (-10 PTS)", rgba(255, 135, 115), 286, 104);
			drawGuideCallout(200, 204, "0:00 SUNBURST FINALE", rgba(255, 225, 120), 200, 174);
			break;
		}
		case MODE_FRENZY: {
			gfx::text("SCORE 280", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::text("0:30", 200, vy + 5, 0.34f, COL_CREAM, ALIGN_CENTER);
			gfx::text("FRENZY!", vx + vw - 8, vy + 5, 0.32f, rgba(255, 210, 90), ALIGN_RIGHT);
			static const int kF[4] = {fruit_orange_idx, fruit_apple_idx, fruit_peach_idx, fruit_pineapple_idx};
			for (int i = 0; i < 4; i++)
				gfx::sprite(SHEET_FRUIT, kF[i], 74.0f + i * 56, 112.0f - sinf(i * 0.78f) * 26, 0.65f, 0.65f, wob * (i & 1 ? 1 : -1));
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, 298, 106, 0.68f, 0.68f, wob);
			gfx::rect(62, 102, 192, 3, COL_ACCENT);
			drawGuideCallout(130, 56, "NON-STOP SIDE SHOWERS", COL_ACCENT, 130, 86);
			drawGuideCallout(298, 56, "BOMB = -5 SECONDS!", rgba(255, 120, 110), 298, 92);
			drawGuideCallout(140, 164, "SWEEP WAVES FOR BIG COMBOS", rgba(150, 245, 160), 140, 108);
			drawGuideCallout(200, 200, "ENDS WITH GIANT SUNBURST AT 0:00", COL_CREAM);
			break;
		}
		case MODE_ZEN: {
			gfx::text("SCORE 95", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::text("ZEN MODE", 200, vy + 5, 0.32f, COL_CREAM, ALIGN_CENTER);
			gfx::text("NO BOMBS", vx + vw - 8, vy + 5, 0.3f, rgba(160, 235, 170), ALIGN_RIGHT);
			gfx::sprite(SHEET_FRUIT, fruit_peach_idx, 120, 108, 0.78f, 0.78f, wob);
			gfx::sprite(SHEET_FRUIT, fruit_kiwi_idx, 200, 92, 0.78f, 0.78f, -wob);
			gfx::sprite(SHEET_FRUIT, fruit_plum_idx, 280, 108, 0.78f, 0.78f, wob);
			drawGuideCallout(200, 148, "NO CLOCK, NO BOMBS, NO STRIKES", rgba(160, 240, 175), 200, 102);
			drawGuideCallout(200, 192, "PAUSE -> FINISH RUN TO BANK COINS", COL_ACCENT);
			break;
		}
		case MODE_DAILY: {
			gfx::text("DAILY RUN", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::text("0:60", 200, vy + 5, 0.34f, COL_CREAM, ALIGN_CENTER);
			gfx::text("STREAK 4", vx + vw - 8, vy + 5, 0.3f, COL_ACCENT, ALIGN_RIGHT);
			for (int i = 0; i < 7; i++) {
				const float cx = 66.0f + i * 40;
				gfx::rect(cx, 56, 34, 28, i < 4 ? withAlpha(COL_ACCENT, 0.35f) : rgba(0, 0, 0, 110));
				if (i == 3) gfx::rect(cx, 56, 34, 2, COL_ACCENT);
			}
			gfx::sprite(SHEET_FRUIT, fruit_melon_idx, 136, 138, 0.7f, 0.7f, wob);
			gfx::sprite(SHEET_SPRITES, sprites_coin_idx, 264, 138, 0.65f, 0.65f);
			drawGuideCallout(200, 102, "7-DAY STREAK CALENDAR", COL_ACCENT, 186, 84);
			drawGuideCallout(136, 186, "IDENTICAL THROWS TODAY", COL_CREAM, 136, 148);
			drawGuideCallout(274, 186, "1ST RUN BONUS COINS", COL_ACCENT, 264, 148);
			break;
		}
		case MODE_MINEFIELD: {
			gfx::text("SCORE 110", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::text("MINEFIELD", 200, vy + 5, 0.32f, rgba(255, 140, 120), ALIGN_CENTER);
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, 126, 112, 0.72f, 0.72f);
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, 274, 112, 0.72f, 0.72f);
			gfx::sprite(SHEET_FRUIT, fruit_orange_idx, 200, 108, 0.72f, 0.72f, wob);
			gfx::rect(198, 62, 4, 92, COL_ACCENT);
			drawGuideCallout(200, 56, "SURGICAL CUT THROUGH THE GAP", rgba(150, 245, 160), 200, 86);
			drawGuideCallout(112, 166, "AVOID BOMBS", rgba(255, 120, 110), 126, 122);
			drawGuideCallout(288, 166, "WAIT FOR SEPARATION", COL_ACCENT, 274, 122);
			break;
		}
		case MODE_COMBO: {
			gfx::text("SCORE 410", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::rect(144, vy + 8, 112, 8, rgba(0, 0, 0, 140));
			gfx::rect(144, vy + 8, 74, 8, rgba(255, 180, 70));
			for (int i = 0; i < 3; i++)
				gfx::sprite(SHEET_FRUIT, fruit_apple_idx + i * 3, 88.0f + i * 46, 112, 0.62f, 0.62f, wob);
			gfx::rect(68, 112, 132, 3, COL_ACCENT);
			gfx::sprite(SHEET_FRUIT, fruit_lemon_idx, 296, 116, 0.62f, 0.62f);
			drawGuideCallout(200, 54, "EMPTY METER = GAME OVER!", rgba(255, 130, 110), 200, vy + 14);
			drawGuideCallout(134, 162, "3+ IN ONE SWIPE SCORES", rgba(150, 245, 160), 134, 116);
			drawGuideCallout(292, 162, "1-2 ALONE = 0 PTS", COL_DIM, 296, 122);
			break;
		}
		case MODE_BEAT: {
			gfx::text("SCORE 260", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			for (int i = 0; i < 4; i++)
				gfx::rect(168.0f + i * 18, vy + 7, 10, 10, i == 0 ? COL_ACCENT : withAlpha(COL_CREAM, 0.35f));
			gfx::text("120 BPM", vx + vw - 8, vy + 5, 0.3f, COL_CREAM, ALIGN_RIGHT);
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, 200, 122, 1.15f, 1.15f, 0, COL_ACCENT);
			gfx::sprite(SHEET_FRUIT, fruit_pineapple_idx, 200, 122, 0.78f, 0.78f, wob);
			drawGuideCallout(196, 56, "4-BEAT BAR PULSE LIGHTS", COL_ACCENT, 196, vy + 14);
			drawGuideCallout(200, 182, "SLICE ON THE BEAT FOR BONUS CHAIN", rgba(150, 245, 160), 200, 136);
			break;
		}
		case MODE_ORCHARD: {
			gfx::text("4", vx + 10, vy + 4, 0.38f, COL_ACCENT);
			gfx::text("LV 3", 96, vy + 5, 0.34f, COL_CREAM);
			gfx::rect(134, vy + 6, 96, 12, rgba(40, 22, 18, 220));
			gfx::rect(134, vy + 6, 96, 12, rgba(110, 215, 115));
			gfx::text("HP 20/20", 182, vy + 5, 0.30f, COL_CREAM, ALIGN_CENTER);
			gfx::text("ROOM 5", vx + vw - 8, vy + 5, 0.34f, COL_ACCENT, ALIGN_RIGHT);
			gfx::sprite(SHEET_FRUIT, fruit_apple_idx, 96, 112, 0.68f, 0.68f, wob);
			gfx::sprite(SHEET_SPRITES, sprites_cd_health_idx, 200, 112, 0.85f, 0.85f);
			gfx::sprite(SHEET_SPRITES, sprites_boss_melon_guard_idx, 304, 112, 0.56f, 0.56f);
			drawGuideCallout(96, 60, "+1 SCORE PER ROOM CLEARED", COL_ACCENT, vx + 16, vy + 16);
			drawGuideCallout(96, 154, "FRUIT = XP ONLY", rgba(150, 245, 160), 96, 120);
			drawGuideCallout(200, 192, "LEVEL-UP CARDS", COL_ACCENT, 200, 124);
			drawGuideCallout(298, 154, "BOSSES EVERY 5 ROOMS", rgba(255, 140, 120), 304, 126);
			break;
		}
		case MODE_BOSSRUSH: {
			gfx::text("2", vx + 10, vy + 4, 0.38f, COL_ACCENT);
			gfx::rect(118, vy + 6, 104, 12, rgba(110, 215, 115));
			gfx::text("HP 20/20", 170, vy + 5, 0.30f, COL_CREAM, ALIGN_CENTER);
			gfx::text("BOSS 3/5", vx + vw - 8, vy + 5, 0.34f, COL_ACCENT, ALIGN_RIGHT);
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, 124, 110, 1.15f, 1.15f, 0, rgba(120, 200, 255));
			gfx::sprite(SHEET_SPRITES, sprites_boss_melon_guard_idx, 124, 110, 0.68f, 0.68f);
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, 276, 110, 1.15f, 1.15f, 0, rgba(255, 220, 90));
			gfx::sprite(SHEET_SPRITES, sprites_boss_king_open_idx, 276, 110, 0.68f, 0.68f);
			drawGuideCallout(118, 58, "BLUE RING: GUARD UP", rgba(140, 210, 255), 124, 84);
			drawGuideCallout(278, 58, "GOLD RING: SLICE IT!", COL_ACCENT, 276, 84);
			drawGuideCallout(124, 168, "+1 SCORE PER BOSS", rgba(150, 245, 160));
			drawGuideCallout(276, 168, "SLICE SHOTS & BOMBS", COL_CREAM, 276, 134);
			break;
		}
		case MODE_RECIPE: {
			gfx::text("SCORE 185", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::text("0:45", vx + vw - 8, vy + 5, 0.32f, COL_CREAM, ALIGN_RIGHT);
			gfx::rect(112, 48, 176, 26, rgba(24, 16, 12, 220));
			gfx::sprite(SHEET_FRUIT, fruit_apple_idx, 142, 61, 0.42f, 0.42f);
			gfx::sprite(SHEET_FRUIT, fruit_orange_idx, 186, 61, 0.42f, 0.42f);
			gfx::sprite(SHEET_FRUIT, fruit_berry_idx, 230, 61, 0.42f, 0.42f);
			gfx::sprite(SHEET_FRUIT, fruit_orange_idx, 100, 126, 0.68f, 0.68f, wob);
			gfx::sprite(SHEET_FRUIT, fruit_plum_idx, 200, 126, 0.65f, 0.65f);
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, 300, 126, 0.68f, 0.68f);
			drawGuideCallout(200, 90, "CUP ORDER (ANY SEQUENCE)", COL_ACCENT, 200, 74);
			drawGuideCallout(100, 166, "IN CUP (+BONUS)", rgba(150, 245, 160), 100, 136);
			drawGuideCallout(200, 198, "WRONG = SPOILS CUP", rgba(255, 160, 110), 200, 138);
			drawGuideCallout(300, 166, "BOMB = -10 SEC!", rgba(255, 120, 110), 300, 136);
			break;
		}
		case MODE_SHARP: {
			gfx::text("SCORE 90", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::text("SLICE: CIRCLE", 200, vy + 5, 0.32f, rgba(255, 120, 110), ALIGN_CENTER);
			gfx::text("0:45", vx + vw - 8, vy + 5, 0.32f, COL_CREAM, ALIGN_RIGHT);
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, 128, 114, 1.0f, 1.0f, 0, rgba(255, 110, 100));
			gfx::sprite(SHEET_FRUIT, fruit_apple_idx, 128, 114, 0.68f, 0.68f, wob);
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, 272, 114, 1.0f, 1.0f, 0, rgba(110, 225, 140));
			gfx::sprite(SHEET_FRUIT, fruit_pear_idx, 272, 114, 0.68f, 0.68f, -wob);
			drawGuideCallout(200, 56, "CALLED TARGET SHAPE", COL_ACCENT, 200, vy + 18);
			drawGuideCallout(128, 172, "MATCHING RING (2X PTS)", rgba(150, 245, 160), 128, 128);
			drawGuideCallout(272, 172, "WRONG RING (-3 PTS)", rgba(255, 120, 110), 272, 128);
			break;
		}
		case MODE_JUGGLE: {
			gfx::text("SCORE 150", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::text("3 IN AIR", 200, vy + 5, 0.32f, COL_CREAM, ALIGN_CENTER);
			gfx::sprite(SHEET_FRUIT, fruit_melon_idx, 116, 104, 0.72f, 0.72f, wob);
			gfx::sprite(SHEET_FRUIT, fruit_orange_idx, 200, 88, 0.68f, 0.68f, -wob);
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, 286, 112, 0.68f, 0.68f);
			drawGuideCallout(156, 148, "BOUNCE = +1 PER FRUIT IN AIR", rgba(150, 245, 160), 116, 114);
			drawGuideCallout(286, 168, "BOMB = BLOWS UP (-1 LIFE)!", rgba(255, 120, 110), 286, 122);
			drawGuideCallout(200, 200, "3 MISSES OR BOMB STRIKES = OVER", COL_CREAM);
			break;
		}
		case SIGN_MIXER: {
			gfx::text("MIXER RULES", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::text("MIXER BEST", vx + vw - 8, vy + 5, 0.3f, COL_CREAM, ALIGN_RIGHT);
			gfx::sprite(SHEET_FRUIT, fruit_melon_idx, 114, 112, 0.98f, 0.98f, wob);
			gfx::sprite(SHEET_FRUIT, fruit_apple_idx, 204, 112, 0.42f, 0.42f, -wob);
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, 276, 104, 0.62f, 0.62f);
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, 304, 120, 0.62f, 0.62f);
			drawGuideCallout(114, 166, "GIANT FRUIT", COL_ACCENT, 114, 128);
			drawGuideCallout(204, 150, "TINY FRUIT", COL_CREAM, 204, 120);
			drawGuideCallout(290, 166, "DOUBLE BOMBS", rgba(255, 130, 110), 290, 126);
			drawGuideCallout(200, 200, "STACK MODIFIERS ON ANY MODE", rgba(150, 245, 160));
			break;
		}
		case SIGN_PASS: {
			gfx::text("PASS AND PLAY", vx + 8, vy + 5, 0.32f, COL_ACCENT);
			gfx::text("TURN 1 OF 4", vx + vw - 8, vy + 5, 0.3f, COL_CREAM, ALIGN_RIGHT);
			static const char* const kP[3] = {"1. PLAYER 1   512", "2. PLAYER 2   440", "3. PLAYER 3   385"};
			for (int i = 0; i < 3; i++) {
				gfx::rect(86, 56.0f + i * 28, 228, 22, rgba(24, 16, 12, 210));
				gfx::text(kP[i], 200, 59.0f + i * 28, 0.32f, passColor(i), ALIGN_CENTER);
			}
			drawGuideCallout(200, 160, "2-4 PLAYERS TAKE TURNS ON 1 DEVICE", COL_ACCENT, 200, 136);
			drawGuideCallout(200, 196, "HIGHEST SCORE WINS THE CROWN", rgba(150, 245, 160));
			break;
		}
		default: {
			// Modular fallback for any future mode added to kGuides[].
			gfx::text(guideTitle(sGuidePage), 200, vy + 4, 0.34f, COL_ACCENT, ALIGN_CENTER);
			gfx::sprite(SHEET_SPRITES, g.icon, 200, 96, 1.2f, 1.2f);
			drawGuideCallout(200, 154, g.goal, COL_ACCENT, 200, 116);
			if (g.id >= 0 && g.id < MODE_COUNT)
				drawGuideCallout(200, 190, kModes[g.id].line1, COL_CREAM);
			break;
		}
	}
}

static void drawGuidesBottom() {
	char buf[32];
	const GuideEntry& g = kGuides[sGuidePage];
	drawMenuBack();
	drawHeader("Guides");
	drawBackArrow(true, sPressed == 0);
	snprintf(buf, sizeof buf, "%d / %d", sGuidePage + 1, kGuideCount);
	gfx::text(buf, 308, 14, 0.36f, COL_DIM, ALIGN_RIGHT);

	for (int side = 0; side < 2; side++) {
		const Button& b = kGuideArrows[side];
		const bool foc = sGuideHover == side || sGuidePress == side;
		const bool prs = sGuidePress == side;
		drawPlank(b.x, b.y, b.w, b.h, foc, prs);
		gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, b.x + b.w * 0.5f, b.y + b.h * 0.5f + (prs ? 2 : 0), 0.52f, 0.52f, side ? 3.14159265f : 0, COL_ACCENT);
	}
	drawPlank(46, 44, 228, 32, true, false);
	const char* title = guideTitle(sGuidePage);
	const float iconScale = g.icon == sprites_coin_idx ? 0.38f : 0.62f;
	float ts = 0.44f;
	float tw = gfx::textWidth(title, ts);
	const float maxTitleW = 180.0f;
	if (tw > maxTitleW && tw > 0) {
		ts *= maxTitleW / tw;
		tw = maxTitleW;
	}
	const float groupGap = 22.0f;  // icon + gap to the left of title text
	const float cx = 160.0f + groupGap * 0.5f;
	gfx::sprite(SHEET_SPRITES, g.icon, cx - tw * 0.5f - 14.0f, 60.0f, iconScale, iconScale, g.icon == sprites_arrow_idx ? 3.14159265f : 0);
	gfx::text(title, cx, 60.0f - 20.0f * ts - 1.0f, ts, COL_ACCENT, ALIGN_CENTER);

	const char* const lines[4] = {g.line1, g.line2, g.line3, g.line4};
	struct WrapLine { char text[160]; float y; };
	WrapLine out[8];
	int outN = 0;
	const float sc = PORT_3DS ? 0.33f : 0.31f, maxW = 264.0f, step = PORT_3DS ? 11.2f : 10.8f;
	float ly = 104.0f;
	for (int i = 0; i < 4; i++) {
		if (!lines[i] || !lines[i][0]) continue;
		char wbuf[160];
		snprintf(wbuf, sizeof wbuf, "%s", lines[i]);
		char* p = wbuf;
		int wrapped = 0;
		while (*p && wrapped < 2 && ly <= 212.0f && outN < 8) {
			while (*p == ' ') p++;
			if (!*p) break;
			if (gfx::textWidth(p, sc) <= maxW || wrapped == 1) {
				snprintf(out[outN].text, sizeof out[outN].text, "%s", p);
				out[outN++].y = ly;
				ly += step;
				break;
			}
			int split = -1;
			for (int k = 1; p[k]; k++) {
				if (p[k] == ' ') {
					p[k] = 0;
					const float twp = gfx::textWidth(p, sc);
					p[k] = ' ';
					if (twp <= maxW) split = k;
					else break;
				}
			}
			if (split < 0) {
				snprintf(out[outN].text, sizeof out[outN].text, "%s", p);
				out[outN++].y = ly;
				ly += step;
				break;
			}
			p[split] = 0;
			snprintf(out[outN].text, sizeof out[outN].text, "%s", p);
			out[outN++].y = ly;
			ly += step;
			p += split + 1;
			wrapped++;
		}
		ly += 2.0f;
	}
	const float boxH = clampf((outN > 0 ? out[outN - 1].y + step : 104.0f) - 80.0f + 22.0f, 88.0f, 152.0f);
	gfx::sign(sprites_plank_idx, 8, 80, 304, boxH, COL_PLANK);
	fitText(g.goal, 160, 87, 0.36f, 260, COL_ACCENT);
	for (int i = 0; i < outN; i++) fitText(out[i].text, 160, out[i].y, sc, maxW, COL_CREAM);
	// Page pips along the bottom of the plank.
	const float dotStep = 12.0f, dotX0 = 160 - (kGuideCount - 1) * dotStep * 0.5f;
	for (int i = 0; i < kGuideCount; i++)
		gfx::rect(dotX0 + i * dotStep - 3, 80.0f + boxH - 13.0f, 6, 4, i == sGuidePage ? COL_ACCENT : COL_DIM);
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
static bool sLatIn;                        // the blade is over the fruit; it must leave before it can slice again
static constexpr float kLatLeave = 10;     // how far past the fruit's reach the blade must go to have left it
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
	sLatIn = sMenuBlade.active;  // a finger still down from Try Again has to cross the fruit fresh
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
	// Every pass across the fruit is a slice, so one long held swipe can slice back and forth on each tick.
	const Blade& b = sMenuBlade;
	if (b.strokeId != sLatStroke || !b.active) {
		sLatStroke = b.strokeId;
		sLatIn = false;
	}
	if (b.active) {
		const float reach = segmentReach(b.px, b.py, b.x, b.y, kLatX, kLatY);
		if (!sLatIn && reach < kLatReach) {
			sLatIn = true;
			latencySlice();
		} else if (sLatIn && reach > kLatReach + kLatLeave) {
			sLatIn = false;
		}
	}
}

static int latencyMs(float sec) { return (int)(sec * 1000 + (sec < 0 ? -0.5f : 0.5f)); }

static void drawLatencyTop() {
	char buf[48];
	topPanel(30, 96, 340, sLatDone ? 116.0f : 134.0f);
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
	gfx::text("swipe back and forth, or press A.", 200, 152, 0.38f, COL_CREAM, ALIGN_CENTER);
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
		gfx::sign(sprites_plank_idx, 20, 52, 280, 94, COL_PLANK);
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

static bool lbEligible() {
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
	return false;
#endif
	return sGame.mode < MODE_COUNT && sPassTurn < 0 && !sGame.mixed && !sGame.assisted && !sGame.spChallenge;
}

static void onResultEntered() {
	sResultBoard = false;
	sLbTypingName = false;
	if (!lbEligible()) {
		lb::clearView();
		return;
	}
	const bool endless = false;
	if (!gSave.leaderboardName[0]) {
#ifndef AUTOTEST
		if (sGame.score > 0 || sGame.best > 0) {
			sLbTypingName = true;
			sLbNameFocus = 0;
			port::textStart("", 6);
			return;
		}
#endif
		lb::fetch(sGame.mode, endless);
		return;
	}
	if (sGame.newBest) lb::submit(sGame.mode, endless, sGame.score);
	else lb::fetch(sGame.mode, endless);
}

static void updateLbNameEntry(const Input& in, bool wasTouching) {
	char typed[32];
	int result = port::textPoll(typed, sizeof typed);
	if (result == 0) {
		int t = touchButtons(kNameButtons, 2, in, wasTouching, sLbNameFocus);
		if (in.repeat & (KEY_LEFT | KEY_RIGHT)) sLbNameFocus ^= 1;
		if (in.down & KEY_A) t = sLbNameFocus;
		if (in.down & KEY_B) t = 1;
		if (t < 0) return;
		audio::click();
		result = t == 0 ? 1 : -1;
	}
	const bool endless = false;
	if (result > 0) {
		lb::cleanName(typed, gSave.leaderboardName, sizeof gSave.leaderboardName);
		saveWrite();
		const u32 sendScore = sGame.score > sGame.best ? sGame.score : sGame.best;
		if (sendScore > 0) lb::submit(sGame.mode, endless, sendScore);
		else lb::fetch(sGame.mode, endless);
	} else {
		lb::fetch(sGame.mode, endless);
	}
	port::textStop();
	sLbTypingName = false;
}

static const char* lbShortPlatform(const char* p) {
	if (!p || !p[0]) return "WEB";
	if (strcmp(p, "3ds") == 0) return "3DS";
	if (strcmp(p, "win") == 0) return "WIN";
	if (strcmp(p, "linux") == 0) return "LIN";
	if (strcmp(p, "android") == 0) return "AND";
	return "WEB";
}

#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
static void drawBossTestTop();
#endif

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
			const int mode = f >= 1 ? modeAt(sModePage, f) : -1;
			// Where the Mixer fruit sits under it, the sign is shorter and its lines closer together.
			const float yName = kMixerFruit ? 96 : 104, yLine = kMixerFruit ? 124 : 134, yBest = kMixerFruit ? 166 : 184;
			if (mode < 0) {
				topPanel(44, 116, 312, 66);
			} else if (kMixerFruit) {
				if (mode == MODE_DAILY) topPanel(30, 46, 340, 138);
				else if (mode == SIGN_PASS) topPanel(30, 90, 340, 88);
				else topPanel(30, 90, 340, 108);
			} else {
				topPanel(30, 96, 340, mode == SIGN_PASS ? 92.0f : 122.0f);
			}
			gfx::setDepth(1.0f);
			if (mode != MODE_DAILY) gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			gfx::setDepth(0.7f);
			if (mode == MODE_DAILY) {
				drawDailyTop();
			} else if (mode == SIGN_PASS) {
				gfx::text("Pass and Play", 200, yName, 0.6f, COL_ACCENT, ALIGN_CENTER);
				gfx::text("2 to 4 players take turns on one", 200, yLine, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text("device. The highest score wins.", 200, yLine + 18, 0.38f, COL_CREAM, ALIGN_CENTER);
			} else if (mode == SIGN_MIXER) {
				gfx::text("Mixer", 200, yName, 0.6f, COL_ACCENT, ALIGN_CENTER);
				gfx::text("Rules that change how any mode", 200, yLine, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text("plays, with bests of their own.", 200, yLine + 18, 0.38f, COL_CREAM, ALIGN_CENTER);
				snprintf(buf, sizeof buf, mixerRulesOn() ? "%d ON" : "ALL OFF", mixerRulesOn());
				gfx::text(buf, 200, yBest, 0.6f, COL_CREAM, ALIGN_CENTER);
			} else if (mode == MODE_BOSSRUSH && !bossBeaten(BOSS_MELON)) {
				gfx::text(kModes[mode].name, 200, yName, 0.6f, COL_ACCENT, ALIGN_CENTER);
				gfx::text("Locked. Beat The Big Melon in", 200, yLine, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text("Orchard Blast to open it.", 200, yLine + 18, 0.38f, COL_CREAM, ALIGN_CENTER);
				drawPadlock(200, yBest + 10, 1.0f);
			} else if (mode >= 0) {
				const ModeInfo& m = kModes[mode];
				gfx::text(m.name, 200, yName, 0.6f, COL_ACCENT, ALIGN_CENTER);
				gfx::text(m.line1, 200, yLine, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text(m.line2, 200, yLine + 18, 0.38f, COL_CREAM, ALIGN_CENTER);
				if (mixerRulesOn() && mixerMode(mode)) snprintf(buf, sizeof buf, "MIXER BEST %lu", (unsigned long)gSave.bestMixer[mode]);
				else if (assistedRun()) snprintf(buf, sizeof buf, "ASSISTED BEST %lu", (unsigned long)gSave.bestAssisted[mode]);
				else snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)gSave.best[mode]);
				gfx::text(buf, 200, yBest, 0.6f, COL_CREAM, ALIGN_CENTER);
			} else {
				gfx::text("Back to the title", 200, 141, 0.46f, COL_CREAM, ALIGN_CENTER);
			}
			break;
		}
		case SCREEN_OPTIONS: {
			int n = optionButtons();
			int f = sFocus[SCREEN_OPTIONS];
			const Section* sec = f >= 1 && f < n ? &kSections[sOptionSection[f]] : nullptr;
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			if (!sec) {
				topPanel(48, 112, 304, 62);
				gfx::setDepth(0.7f);
				gfx::text("Options", 200, 120, 0.5f, COL_ACCENT, ALIGN_CENTER);
				gfx::text(sOptionsFromPause ? "Back to the game" : "Back to the title", 200, 146, 0.38f, COL_CREAM, ALIGN_CENTER);
			} else {
				const bool twoLines = sec->hint2[0] != 0;
				topPanel(40, 112, 320, twoLines ? 80.0f : 64.0f);
				gfx::setDepth(0.7f);
				gfx::text(sec->title, 200, 118, 0.5f, COL_ACCENT, ALIGN_CENTER);
				gfx::text(sec->hint1, 200, 146, 0.38f, COL_CREAM, ALIGN_CENTER);
				if (twoLines) gfx::text(sec->hint2, 200, 164, 0.38f, COL_CREAM, ALIGN_CENTER);
			}
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
			} else if (kSections[sSection].id == SID_STREETPASS) {
				topPanel(48, 10, 304, 78);
				gfx::setDepth(0.7f);
				snprintf(buf, sizeof buf, "STREETPASS: %s", gSave.streetPassOn ? "ON" : "OFF");
				gfx::text(buf, 200, 18, 0.42f, gSave.streetPassOn ? rgba(150, 240, 170) : COL_DIM, ALIGN_CENTER);
				fitText(gSave.streetPassMsg[0] ? gSave.streetPassMsg : "Hello! :)", 200, 41, 0.42f, 286, COL_ACCENT);
				snprintf(buf, sizeof buf, "CLASSIC %lu   TAGS %d   WINS %u", (unsigned long)gSave.best[MODE_CLASSIC], streetpass::count(), (unsigned)gSave.spChallengesWon);
				gfx::text(buf, 200, 64, 0.32f, COL_CREAM, ALIGN_CENTER);
			} else {
				// No preview for this row: the logo fills the space above the hint, as on the Options screen.
				gfx::setDepth(1.0f);
				gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			}
			if (!r) {
				topPanel(48, 118, 304, 62);
				gfx::setDepth(0.7f);
				gfx::text(sec.title, 200, 125, 0.52f, COL_ACCENT, ALIGN_CENTER);
				gfx::text("Back to Options", 200, 151, 0.40f, COL_CREAM, ALIGN_CENTER);
			} else if (rowDisabled(*r)) {
				topPanel(30, 100, 340, 94);
				gfx::setDepth(0.7f);
				gfx::text(r->label, 200, 106, 0.56f, COL_ACCENT, ALIGN_CENTER);
				gfx::text("This console has no 3D screen,", 200, 142, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text("so depth has no effect here.", 200, 160, 0.38f, COL_CREAM, ALIGN_CENTER);
			} else if (r->value == &gSave.bossMusic) {
				topPanel(30, 100, 340, 122);
				gfx::setDepth(0.7f);
				gfx::text(r->label, 200, 106, 0.56f, COL_ACCENT, ALIGN_CENTER);
				if (*r->value == 0) {
					gfx::text("Off: Boss fights keep playing", 200, 138, 0.38f, COL_CREAM, ALIGN_CENTER);
					gfx::text("your regular Game Music.", 200, 156, 0.38f, COL_CREAM, ALIGN_CENTER);
				} else if (*r->value == 1) {
					gfx::text("Auto: Plays undiscovered boss", 200, 138, 0.38f, COL_CREAM, ALIGN_CENTER);
					gfx::text("themes & shuffles owned tracks.", 200, 156, 0.38f, COL_CREAM, ALIGN_CENTER);
				} else {
					const int t = *r->value - 2;
					const bool custom = t >= kTrackCount;
					gfx::text(custom ? "Custom Song (All Modes)" : "Boss Track (Shop)", 200, 138, 0.38f, COL_ACCENT, ALIGN_CENTER);
					gfx::text("Plays during every boss fight.", 200, 156, 0.38f, COL_CREAM, ALIGN_CENTER);
				}
				rowValueText(*r, buf, sizeof buf);
				gfx::text(buf, 200, 190, 0.5f, COL_CREAM, ALIGN_CENTER);
			} else if (r->value == &gSave.menuTrack || r->value == &gSave.gameTrack) {
				topPanel(30, 100, 340, 122);
				gfx::setDepth(0.7f);
				gfx::text(r->label, 200, 106, 0.56f, COL_ACCENT, ALIGN_CENTER);
				if (*r->value == 0) {
					gfx::text("Auto: Loopable tracks loop 3 times,", 200, 138, 0.38f, COL_CREAM, ALIGN_CENTER);
					gfx::text("full songs play once before next.", 200, 156, 0.38f, COL_CREAM, ALIGN_CENTER);
				} else {
					const int t = *r->value - 1;
					const bool isSong = audio::isSongTrack(t);
					gfx::text(isSong ? "Category: Song (Full track)" : "Category: Loopable (Short loop)", 200, 138, 0.38f, COL_ACCENT, ALIGN_CENTER);
					gfx::text(isSong ? "Plays once before next in Auto." : "Loops 3 times before next in Auto.", 200, 156, 0.38f, COL_CREAM, ALIGN_CENTER);
				}
				rowValueText(*r, buf, sizeof buf);
				gfx::text(buf, 200, 190, 0.5f, COL_CREAM, ALIGN_CENTER);
			} else {
				rowValueText(*r, buf, sizeof buf);
				const bool hasL2 = r->line2[0] != 0;
				const bool extraStylus = PORT_3DS && r->value == &gSave.stylus;
				const bool hasVal = buf[0] != 0;
				const float valY = extraStylus ? 196.0f : (hasL2 ? 188.0f : 168.0f);
				const float bottomY = hasVal ? (valY + 30.0f) : (extraStylus ? 198.0f : (hasL2 ? 182.0f : 164.0f));
				topPanel(30, 100, 340, bottomY - 100.0f);
				gfx::setDepth(0.7f);
				gfx::text(r->label, 200, 106, 0.56f, COL_ACCENT, ALIGN_CENTER);
				gfx::text(r->line1, 200, 138, 0.38f, COL_CREAM, ALIGN_CENTER);
				if (hasL2) gfx::text(r->line2, 200, 156, 0.38f, COL_CREAM, ALIGN_CENTER);
#if PORT_3DS
				if (extraStylus) gfx::text("Also calibrate in System Settings.", 200, 174, 0.34f, COL_DIM, ALIGN_CENTER);
#endif
				if (hasVal) gfx::text(buf, 200, valY, 0.5f, COL_CREAM, ALIGN_CENTER);
			}
			break;
		}
		case SCREEN_SHOP:
			topPanel(30, 8, 340, sShopNoteT > 0 ? 222.0f : 204.0f);
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
			topPanel(30, 96, 340, PORT_ANDROID ? 96.0f : 132.0f);
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
			topPanel(30, 96, 340, 122);
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
		case SCREEN_GUIDES:
			drawGuidesTop();
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
			gfx::setDepth(0.9f);
			gfx::rect(0, 0, 400, 240, rgba(0, 0, 0, 140));
			gfx::setDepth(1.8f);
			gfx::text("PAUSED", 200, 120, 1.1f, COL_CREAM, ALIGN_CENTER);
			if (sGame.runCoins()) {
				char cb[40];
				snprintf(cb, sizeof cb, "+%lu FROM THIS RUN", (unsigned long)sGame.runCoins());
				gfx::text(cb, 200, 156, 0.38f, COL_ACCENT, ALIGN_CENTER);
			}
#else
			gfx::setDepth(1.8f);
			gfx::text("PAUSED", 200, 78, 1.1f, COL_CREAM, ALIGN_CENTER);
			{
				// Coins saved, with what this run has made so far waiting to be added.
				char cb[40];
				snprintf(cb, sizeof cb, "%lu", (unsigned long)gSave.coins);
				float w = gfx::textWidth(cb, 0.5f);
				gfx::sprite(SHEET_SPRITES, sprites_coin_idx, 200 - w / 2 - 6, 151, 0.56f, 0.56f);
				gfx::text(cb, 200 + 8, 142, 0.5f, COL_CREAM, ALIGN_CENTER);
				if (sGame.runCoins()) {
					snprintf(cb, sizeof cb, "+%lu FROM THIS RUN", (unsigned long)sGame.runCoins());
					gfx::text(cb, 200, 168, 0.34f, COL_ACCENT, ALIGN_CENTER);
				}
			}
#endif
			break;
		case SCREEN_PASS_TURN:
			topPanel(30, 96, 340, 122);
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
			topPanel(30, 96, 340, winner >= 0 ? 114.0f : 82.0f);
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			gfx::setDepth(0.7f);
			gfx::text(modeName(sStartMode), 200, 104, 0.5f, COL_CREAM, ALIGN_CENTER);
			if (winner >= 0) snprintf(buf, sizeof buf, "%s WINS!", sPassNames[winner]);
			else snprintf(buf, sizeof buf, "IT'S A TIE!");
			gfx::text(buf, 200, 136, 0.8f, winner >= 0 ? passColor(winner) : COL_ACCENT, ALIGN_CENTER);
			if (winner >= 0) {
				snprintf(buf, sizeof buf, "%lu", (unsigned long)sPassScores[winner]);
				gfx::text(buf, 200, 174, 0.7f, COL_CREAM, ALIGN_CENTER);
			}
			break;
		}
		case SCREEN_RESULT: {
			float in = clampf(sGame.phaseT * 4, 0, 1);
			const int resTracks = (sGame.mode == MODE_BOSSRUSH || sGame.mode == MODE_ORCHARD) ? sGame.orc.newTracks : 0;
			const float resCoinY = (sGame.mode == MODE_BOSSRUSH || sGame.mode == MODE_ORCHARD) ? (resTracks > 0 ? 168.0f : 152.0f) : 148.0f;
			const float resExtraY = (sGame.mode == MODE_BOSSRUSH || sGame.mode == MODE_ORCHARD) ? (resTracks > 0 ? 190.0f : 178.0f) : 174.0f;
			const bool resHasExtra = !sCardFrame && sGame.scoreCommitted && sPassTurn < 0 && (sGame.dailyCoins || sGame.journalCoins);
			float resH = 224.0f;
			if (sResultBoard && !sCardFrame && lbEligible()) {
				resH = 224.0f;
			} else if (sGame.mode == MODE_PRACTICE) {
				resH = 196.0f;
			} else if (sCardFrame) {
				resH = 208.0f;
			} else if (resHasExtra) {
				resH = resExtraY + 18.0f;
			} else if (sGame.scoreCommitted && sPassTurn < 0) {
				resH = resCoinY + 26.0f;
			} else {
				resH = 136.0f;
			}
			resH = clampf(resH, 136.0f, 224.0f);
			const float resY = sCardFrame ? 8.0f : (240.0f - resH) * 0.5f;
			const float dy = resY - 8.0f;
			gfx::setDepth(0.5f);
			gfx::sign(sprites_plank_idx, 28, resY, 344, resH, withAlpha(COL_PLANK, in));
			gfx::setDepth(1.0f);
			if (sResultBoard && !sCardFrame && lbEligible()) {
				gfx::text("ONLINE TOP 10", 200, 14, 0.48f, COL_ACCENT, ALIGN_CENTER);
				gfx::text(modeName(sGame.mode), 48, 34, 0.33f, COL_CREAM);
				if (gSave.leaderboardName[0]) {
					snprintf(buf, sizeof buf, "YOU: %s", gSave.leaderboardName);
					gfx::text(buf, 352, 34, 0.32f, COL_DIM, ALIGN_RIGHT);
				}
				gfx::rect(44, 48, 312, 1, withAlpha(COL_DIM, 0.35f));
				const lb::BoardView& v = lb::view();
				if (v.status == lb::STATUS_LOADING) {
					gfx::text("Connecting to leaderboard...", 200, 112, 0.40f, COL_CREAM, ALIGN_CENTER);
				} else if (v.status != lb::STATUS_READY) {
					gfx::text("Leaderboard offline or unreachable.", 200, 102, 0.39f, COL_CREAM, ALIGN_CENTER);
					gfx::text("Check internet connection and try again.", 200, 124, 0.33f, COL_DIM, ALIGN_CENTER);
				} else if (v.count == 0) {
					gfx::text("No scores uploaded yet!", 200, 112, 0.40f, COL_CREAM, ALIGN_CENTER);
				} else {
					char myPid[24];
					lb::formatPlayerId(gSave.leaderboardId, myPid, sizeof myPid);
					for (int i = 0; i < v.count && i < 10; i++) {
						const lb::Entry& e = v.entries[i];
						const float ry = 53.0f + i * 14.5f;
						const bool isYou = e.playerId[0] && strcmp(e.playerId, myPid) == 0;
						const u32 col = isYou ? COL_ACCENT : COL_CREAM;
						snprintf(buf, sizeof buf, "%2d. %s", i + 1, e.name);
						gfx::text(buf, 48, ry, 0.33f, col);
						snprintf(buf, sizeof buf, "(%s)", lbShortPlatform(e.platform));
						gfx::text(buf, 224, ry + 1, 0.29f, COL_DIM);
						snprintf(buf, sizeof buf, "%lu", (unsigned long)e.score);
						gfx::text(buf, 352, ry, 0.33f, col, ALIGN_RIGHT);
					}
					if (v.yourRank > 0) {
						char lbBuf[72];
						snprintf(lbBuf, sizeof lbBuf, "YOUR RANK %d OF %d   BEST %lu", v.yourRank, v.total > 0 ? v.total : v.yourRank, (unsigned long)v.yourScore);
						gfx::text(lbBuf, 200, 204, 0.33f, COL_ACCENT, ALIGN_CENTER);
					} else if (!gSave.leaderboardName[0]) {
						gfx::text("SET NAME ON NEXT BEST SCORE", 200, 204, 0.32f, COL_DIM, ALIGN_CENTER);
					} else {
						gfx::text("BEAT YOUR BEST TO POST TO TOP 10", 200, 204, 0.32f, COL_DIM, ALIGN_CENTER);
					}
				}
				break;
			}
			if (sGame.mode == MODE_PRACTICE) {
				gfx::setDepth(1.2f);
				gfx::text("PRACTICE DONE", 200, 14 + resY, 0.80f, COL_ACCENT, ALIGN_CENTER);
				gfx::setDepth(1.0f);
				gfx::text("You know the basics.", 200, 70 + resY, 0.44f, COL_CREAM, ALIGN_CENTER);
				gfx::text("Last lesson: slice the fruit", 200, 104 + resY, 0.40f, COL_CREAM, ALIGN_CENTER);
				gfx::text("below to pick a game.", 200, 124 + resY, 0.40f, COL_CREAM, ALIGN_CENTER);
				gfx::text("Practice again from How to Play.", 200, 162 + resY, 0.34f, COL_DIM, ALIGN_CENTER);
				break;
			}
			gfx::text(modeName(sGame.mode), 200, 16 + dy, 0.44f, COL_CREAM, ALIGN_CENTER);
			gfx::setDepth(1.7f);
			snprintf(buf, sizeof buf, "%lu", (unsigned long)sGame.score);
			gfx::text(buf, 200, 34 + dy, 0.92f, COL_ACCENT, ALIGN_CENTER);
			gfx::setDepth(1.0f);
			if (sPassTurn >= 0) {
				gfx::text(sPassNames[sPassTurn], 200, 78 + dy, 0.42f, passColor(sPassTurn), ALIGN_CENTER);
			} else if (sGame.spChallenge) {
				snprintf(buf, sizeof buf, sGame.spWon ? "BEAT %s! +50 COINS" : "VS %s (TARGET %lu)", sGame.spRival, (unsigned long)sGame.spTarget);
				gfx::text(buf, 200, 78 + dy, 0.38f, sGame.spWon ? rgba(160, 240, 170) : COL_CREAM, ALIGN_CENTER);
			} else if (sGame.newBest) {
				gfx::text("NEW BEST!", 200, 78 + dy, 0.40f + sinf(sTime * 6) * 0.02f, COL_CREAM, ALIGN_CENTER);
			} else {
				snprintf(buf, sizeof buf, sGame.mixed ? "MIXER BEST %lu" : (sGame.assisted ? "ASSISTED BEST %lu" : "BEST %lu"), (unsigned long)sGame.best);
				gfx::text(buf, 200, 78 + dy, 0.38f, COL_DIM, ALIGN_CENTER);
			}
			int tracks = 0;
			float coinY = 152, extraY = 178;
			if (sGame.mode == MODE_BOSSRUSH) {
				const Orchard& o = sGame.orc;
				const Modes12& m = sGame.m12;
				tracks = o.newTracks;
				if (o.won) snprintf(buf, sizeof buf, "RUSH CLEARED IN %d:%02d", (int)sGame.elapsed / 60, (int)sGame.elapsed % 60);
				else snprintf(buf, sizeof buf, "BEAT %d OF %d BOSSES", m.rushStage, kRushStages);
				gfx::text(buf, 200, 104 + dy, 0.39f, o.won ? rgba(255, 230, 140) : COL_CREAM, ALIGN_CENTER);
				snprintf(buf, sizeof buf, "BUILD: %s", m.rushBuild < BUILD_COUNT ? kRushBuilds[m.rushBuild].name : "NONE");
				gfx::text(buf, 200, 124 + dy, 0.35f, COL_DIM, ALIGN_CENTER);
				if (tracks > 0) {
					snprintf(buf, sizeof buf, "NEW IN THE SHOP: %d BOSS TRACK%s", tracks, tracks == 1 ? "" : "S");
					gfx::text(buf, 200, 144 + dy, 0.35f, COL_ACCENT, ALIGN_CENTER);
					coinY = 168; extraY = 190;
				}
			} else if (sGame.mode == MODE_ORCHARD) {
				const Orchard& o = sGame.orc;
				tracks = o.newTracks;
				snprintf(buf, sizeof buf, "REACHED ROOM %d", o.room);
				gfx::text(buf, 200, 104 + dy, 0.39f, rgba(170, 230, 255), ALIGN_CENTER);
				snprintf(buf, sizeof buf, "LEVEL %d   BOSSES %d   SEED %08X", o.level, o.bossesBeaten, (unsigned)o.runSeed);
				gfx::text(buf, 200, 124 + dy, 0.33f, COL_DIM, ALIGN_CENTER);
				if (tracks > 0) {
					snprintf(buf, sizeof buf, "NEW IN THE SHOP: %d BOSS TRACK%s", tracks, tracks == 1 ? "" : "S");
					gfx::text(buf, 200, 144 + dy, 0.35f, COL_ACCENT, ALIGN_CENTER);
					coinY = 168; extraY = 190;
				}
			} else {
				if (sGame.mode == MODE_BEAT) snprintf(buf, sizeof buf, "SLICED %d   ON THE BEAT %d", sGame.sliced, sGame.beatHits);
				else if (sGame.mode == MODE_RECIPE) snprintf(buf, sizeof buf, "SMOOTHIES %d   SPOILED %d", sGame.m12.cups, sGame.m12.spoiled);
				else if (sGame.mode == MODE_SHARP) snprintf(buf, sizeof buf, "ON CALL %d   WRONG %d", sGame.m12.calledHits, sGame.m12.wrongHits);
				else if (sGame.mode == MODE_JUGGLE) snprintf(buf, sizeof buf, "KNOCKS %d   MOST IN THE AIR %d", sGame.m12.knocks, sGame.m12.bestAir);
				else snprintf(buf, sizeof buf, "SLICED %d   BEST COMBO %d", sGame.sliced, sGame.bestCombo);
				gfx::text(buf, 200, 112 + dy, 0.39f, COL_CREAM, ALIGN_CENTER);
				coinY = 148; extraY = 174;
			}
			if (sCardFrame) {
				// The score card: the game's name where the coins go, which are nobody else's business.
				gfx::setDepth(1.2f);
				gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 182 + dy, 0.28f, 0.28f);
			} else if (sGame.scoreCommitted && sPassTurn < 0) {
				snprintf(buf, sizeof buf, "+%lu COINS", (unsigned long)sGame.coinsEarned);
				float cw = gfx::textWidth(buf, 0.40f);
				gfx::sprite(SHEET_SPRITES, sprites_coin_idx, 200 - cw / 2 - 10, coinY + dy + 4, 0.40f, 0.40f);
				gfx::text(buf, 200 + 5, coinY + dy, 0.40f, COL_ACCENT, ALIGN_CENTER);
				// Where the extra coins came from.
				buf[0] = 0;
				if (sGame.dailyCoins && sGame.journalCoins) snprintf(buf, sizeof buf, "DAILY +%lu  JOURNAL +%lu", (unsigned long)sGame.dailyCoins, (unsigned long)sGame.journalCoins);
				else if (sGame.dailyCoins) snprintf(buf, sizeof buf, "DAILY BONUS +%lu", (unsigned long)sGame.dailyCoins);
				else if (sGame.journalCoins) snprintf(buf, sizeof buf, "JOURNAL MILESTONE +%lu", (unsigned long)sGame.journalCoins);
				if (buf[0]) fitText(buf, 200, extraY + dy, 0.35f, 326, COL_DIM);
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
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
		case SCREEN_BOSS_TEST:
			drawBossTestTop();
			break;
#endif
		default:
			break;
	}
	// Every main menu shows the coins saved, top left: the logo leaves it free, and on one screen
	// it stays well away from the menu's back arrow. On Title with Quit Bomb, keep top-right symmetrical.
	if (screen == SCREEN_TITLE && PORT_QUIT_BOMB) {
		gfx::setDepth(0.8f);
		drawPurse(388, 10, 0.5f, true);
	} else if (screen == SCREEN_TITLE || screen == SCREEN_MODES || screen == SCREEN_OPTIONS || screen == SCREEN_JOURNAL || screen == SCREEN_STATS || screen == SCREEN_HOWTO || screen == SCREEN_BEAT) {
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
	boom::start(bx, by, 2.2f);
	// Then a chain of blasts runs out from the corner across the whole window, the farthest last.
	const float reach = sqrtf(w * w + h * h);
	for (int i = 0; i < 18; i++) {
		const float px = x + w * ((i % 6) + 0.5f + 0.3f * sinf(i * 2.7f)) / 6, py = y + h * ((i / 6) + 0.5f + 0.3f * cosf(i * 1.9f)) / 3;
		const float d = sqrtf((px - bx) * (px - bx) + (py - by) * (py - by));
		boom::start(px, py, 2.4f + 1.0f * ((i * 7) % 5) / 4.0f, 0.05f + 0.5f * d / reach);
	}
	audio::play(SFX_BOOM, 1.0f, -0.6f);
	audio::play(SFX_FINALE_BURST, 0.8f);
	PORT_VIBRATE(1.0f, 400);
}

// How hard the quit blast shakes the window now, in menu units: the Screen Shake slider sets it, Calm
// Effects softens it, and it settles to a rumble while the chain of blasts runs, then dies with the dark.
static float quitQuake() {
	if (sQuitT < 0) return 0;
	const float strength = gSave.shake / 100.0f * (gSave.calmFx ? 0.35f : 1.0f);
	const float dark = clampf((sQuitT - (kQuitBlast - 0.45f)) / 0.4f, 0, 1);
	return 14 * strength * (0.3f + 0.7f * expf(-sQuitT / 0.3f)) * (1 - dark);
}

// Each frame of the blast: the shake, and more booms rolling out across the window with the chain.
static void quitTick() {
	static int rolled;
	if (sQuitT < 0) return;
	if (sQuitT <= kDt) rolled = 0;
	static const float kRoll[] = {0.2f, 0.42f};
	if (rolled < 2 && sQuitT >= kRoll[rolled]) {
		audio::play(SFX_BOOM, 0.85f - 0.2f * rolled, 0.1f + 0.5f * rolled, 0.85f - 0.1f * rolled);
		PORT_VIBRATE(0.7f, 150);
		rolled++;
	}
	const float a = quitQuake();
	gfx::setShake(a * sinf(sQuitT * 97), a * cosf(sQuitT * 83 + 1));
	gfx::shakeMenus(a > 0);
}

// Holding the bomb down (mouse or finger), Escape, or Enter or A while the bomb is selected lights its fuse.
// The fuse ticks faster and faster; let go before kQuitHold and nothing happens. True when it goes off.
static bool holdQuit(const Input& in, int focus) {
	if (!PORT_QUIT_BOMB) return false;
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
// The Mixer fruit under Pick a Game's info sign: its name, and how many of its rules are on.
static void drawMixerFruit(float alpha) {
	if (alpha <= 0) return;
	const Button b = mixerButton();
	const bool focused = sFocus[SCREEN_MODES] == kMixFocus, pressed = sPressed == kMixFocus;
	const bool cut = sCut.on && sCut.screen == SCREEN_MODES && sCut.index == kMixFocus;
	float x, y, d;
	mixerFruitSpot(b, &x, &y, &d);
	const float dy = pressed ? 2 : 0, k = b.h / 34;
	if (focused) gfx::sprite(SHEET_SPRITES, sprites_ring_idx, x, y + dy, d / 34, d / 34, 0, withAlpha(COL_ACCENT, 0.9f * alpha));
	if (!cut) {
		// Like the Quit bomb, the fruit grows in and out with a slide, as a model can't fade.
		const float grow = alpha * alpha * (3 - 2 * alpha);
		const float bob = focused && !gSave.calmFx ? sinf(sTime * 4) * 1.5f * k : 0;
		drawMenuFruit(MF_COCONUT, x, y + dy + bob, d * grow, sTime * 0.8f);
	}
	const float tx = b.x + b.h * 1.2f;
	gfx::text("Mixer", tx, b.y + dy - 1 * k, 0.5f * k, withAlpha(focused ? COL_ACCENT : COL_CREAM, alpha));
	char state[16];
	if (mixerRulesOn()) snprintf(state, sizeof state, "%d ON", mixerRulesOn());
	else snprintf(state, sizeof state, "ALL OFF");
	gfx::text(state, tx, b.y + b.h * 0.56f + dy, 0.34f * k, withAlpha(COL_DIM, alpha));
}

static void drawQuitBomb(bool focused, float alpha) {
	if (alpha <= 0) return;
	const Button b = quitButton();
	const float lit = clampf(sQuitHold / kQuitHold, 0, 1);  // 0 until held, 1 just before it goes off
	const float hi = highlightFade(b.x, b.y, b.w, b.h, focused || lit > 0);
	const float shake = gSave.calmFx ? lit * 0.6f : lit * 2.5f;
	const float cx = b.x + b.w * 0.5f + sinf(sTime * 53) * shake, cy = b.y + 22 + cosf(sTime * 47) * shake;
	const float wob = gSave.calmFx ? 0 : sinf(sTime * 2.2f) * 0.12f;
	if (sQuitT < 0) {
		if (hi > 0.01f) gfx::sprite(SHEET_SPRITES, sprites_ring_idx, cx, cy, 0.62f + 0.12f * lit, 0.62f + 0.12f * lit, 0, withAlpha(lit > 0 ? rgba(255, (u8)(200 - 150 * lit), 40, 255) : COL_ACCENT, 0.9f * alpha * hi));
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
		const u32 lc = withAlpha(lerpColor(COL_CREAM, COL_ACCENT, hi), alpha);
		// Centred under the bomb, but a long label is moved right so it never runs off the window's edge.
		const float lx = fmaxf(b.x + b.w * 0.5f, b.x + gfx::textWidth(label, 0.3f) * 0.5f);
		gfx::text(label, lx, b.y + 44, 0.3f, lc, ALIGN_CENTER);
	}
	if (sQuitT >= 0) {
		float x, y, w, h;
		gfx::windowArea(&x, &y, &w, &h);
		// The flat colours reach past the window's edges, so the shake never shows a gap.
		x -= 30;
		y -= 30;
		w += 60;
		h += 60;
		// A white flash, a shock ring racing out from the bomb, the blasts, then everything goes dark
		// just before the game closes.
		const float flash = 1 - clampf(sQuitT / 0.25f, 0, 1);
		boom::draw();
		const float ring = clampf(sQuitT / 0.5f, 0, 1);
		if (ring < 1) {
			const Button b = quitButton();
			const float grow = 1 - (1 - ring) * (1 - ring);
			const float s = 0.8f + 18 * grow;
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, b.x + b.w * 0.5f, b.y + 22, s, s, 0, withAlpha(rgba(255, 220, 140, 255), (1 - ring) * (gSave.calmFx ? 0.35f : 0.8f)));
		}
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
static void quitTick() {}
#endif
static constexpr int kTitleAll = PORT_QUIT_BOMB ? kTitleCount + 1 : kTitleCount;  // with the Quit Game bomb

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
		if (r.kind == ROW_ACTION && r.action != ACT_SP_MSG) {
			const char* label = sArmed == i ? inputConfirmText() : r.label;
			const int player = passRowPlayer(r);
			const u32 color = rowDisabled(r) ? COL_DIM : (player >= 0 ? passColor(player) : (sArmed == i ? COL_ACCENT : COL_CREAM));
			gfx::text(label, 160, ty - 1, 0.42f, color, ALIGN_CENTER);
			continue;
		}
		// The label stops well short of the value's left arrow (at 158), so the two never touch.
		float ls = 0.36f;
		float lw = gfx::textWidth(r.label, ls);
		if (lw > 126) ls *= 126 / lw;
		gfx::text(r.label, 18, ty + (0.36f - ls) * 10, ls, labelColor);
		if (r.kind == ROW_PAGE) {
			// One arrow, not the pair a choice has: this row goes somewhere rather than changing a value.
			gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, 298, y + kRowH * 0.5f - 1, 0.5f, 0.5f, 3.14159265f, COL_ACCENT);
			continue;
		}
		rowValueText(r, buf, sizeof buf);
		if (r.kind == ROW_ACTION) {
			float vs = 0.34f;
			float w = gfx::textWidth(buf, vs);
			if (w > 142) vs *= 142 / w;
			gfx::text(buf, 302, ty + (0.34f - vs) * 10, vs, COL_ACCENT, ALIGN_RIGHT);
			continue;
		}
		if (r.kind == ROW_SLIDER) {
			float cy = y + kRowH * 0.5f - 1;
			float t = (*r.value - r.lo) / (float)(r.hi - r.lo);
			if (rowDisabled(r)) t = 0;
			gfx::rect(kTrackX0, cy - 3, kTrackX1 - kTrackX0, 6, gfx::themeBar());
			gfx::rect(kTrackX0, cy - 3, (kTrackX1 - kTrackX0) * t, 6, rowDisabled(r) ? COL_DIM : COL_ACCENT);
			gfx::sprite(SHEET_SPRITES, sprites_knob_idx, lerpf(kTrackX0, kTrackX1, t), cy, 0.72f, 0.72f);
			gfx::text(buf, 306, ty, 0.35f, COL_CREAM, ALIGN_RIGHT);
		} else {
			float cy = y + kRowH * 0.5f - 1;
			const bool dis = rowDisabled(r);
			const int pressDir = (focus == i && sTouchMode == TOUCH_ROW && !sTouchMoved && sTouchRow == i) ? sRowHoldDir : (focus == i ? sRowArrowHover : 0);
			const float lsLeft = pressDir < 0 ? 0.58f : 0.50f, lsRight = pressDir > 0 ? 0.58f : 0.50f;
			const u32 colLeft = dis ? COL_DIM : (pressDir < 0 ? WHITE : COL_ACCENT);
			const u32 colRight = dis ? COL_DIM : (pressDir > 0 ? WHITE : COL_ACCENT);
			gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, 164, cy, lsLeft, lsLeft, 0, colLeft);
			gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, 298, cy, lsRight, lsRight, 3.14159265f, colRight);
			float vs = 0.36f;
			float w = gfx::textWidth(buf, vs);
			if (w > 108) vs *= 108 / w;
			gfx::text(buf, 231, ty + (0.36f - vs) * 10, vs, dis ? COL_DIM : COL_CREAM, ALIGN_CENTER);
		}
	}
	drawScrollMark(kListTop, 240 - kListTop, sScroll, maxScroll());
}

#include "orchard_ui.inc"

static void drawBottomContent(Screen screen) {
	static const int kIcons[MODE_COUNT] = {sprites_pip_on_idx, sprites_ic_clock_idx, sprites_ic_leaf_idx, sprites_ic_fast_idx, sprites_ic_one_idx, sprites_ic_mine_idx, sprites_ic_chain_idx, sprites_coin_idx, sprites_ic_note_idx,
	                                          sprites_ic_tree_idx, sprites_ic_crown_idx, sprites_ic_cup_idx, sprites_ic_target_idx, sprites_ic_juggle_idx};
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
			if (sSpModal > 0 && screen == sScreen) {
				static const Button kSpModalBtns[] = {{36, 134, 108, 78, nullptr}, {176, 134, 108, 78, nullptr}};
				gfx::rect(0, 0, 320, 240, rgba(14, 10, 8, 215));
				gfx::sign(sprites_plank_idx, 18, 22, 284, 98, COL_PLANK);
				if (sSpModal == 1) {
					gfx::text("Enable StreetPass?", 160, 32, 0.48f, COL_ACCENT, ALIGN_CENTER);
					fitText("Swap score cards & greetings", 160, 62, 0.34f, 260, COL_CREAM);
					fitText("with nearby Nintendo 3DS players.", 160, 82, 0.33f, 260, COL_DIM);
					drawSign(kSpModalBtns[0], "Enable", sSpModalFocus == 0, sPressed == 0, 0.46f, -1, MF_MELON);
					drawSign(kSpModalBtns[1], "Not Now", sSpModalFocus == 1, sPressed == 1, 0.46f, -1, MF_APPLE);
				} else {
					const int pi = streetpass::pendingIndex();
					const streetpass::Entry& pe = streetpass::entry(pi >= 0 ? pi : 0);
					gfx::text("New StreetPass!", 160, 30, 0.46f, COL_ACCENT, ALIGN_CENTER);
					char hdr[48];
					snprintf(hdr, sizeof hdr, "%s  Classic %lu", pe.pkt.name, (unsigned long)pe.pkt.classicBest);
					fitText(hdr, 160, 56, 0.35f, 264, COL_CREAM);
					fitText(pe.pkt.msg, 160, 74, 0.32f, 264, rgba(170, 230, 255));
					fitText("Challenge their score?", 160, 94, 0.32f, 264, COL_DIM);
					drawSign(kSpModalBtns[0], "Challenge", sSpModalFocus == 0, sPressed == 0, 0.44f, -1, MF_MELON);
					drawSign(kSpModalBtns[1], "Not Now", sSpModalFocus == 1, sPressed == 1, 0.44f, -1, MF_APPLE);
				}
			}
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
			if ((sPassTyping >= 0 || sSpTypingMsg) && screen == sScreen) {
				char typed[48];
				port::textPoll(typed, sizeof typed - 1);
				gfx::rect(0, 0, 320, 240, rgba(30, 18, 10, 170));
				gfx::sign(sprites_plank_idx, 20, 60, 280, 146, COL_PLANK);
				gfx::text(sSpTypingMsg ? "StreetPass greeting" : "Type a name", 160, 72, 0.48f, COL_ACCENT, ALIGN_CENTER);
				drawTypingWithCursor(typed, 160, 108, sSpTypingMsg ? 0.44f : 0.6f, 256, sSpTypingMsg ? COL_CREAM : passColor(sPassTyping));
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
			drawCredits();
			break;
		case SCREEN_HOWTO: {
			const char* const kLines[] = {
				"Swipe through fruit. Miss the bombs.",
				"Each slice fills the Fruit Frenzy meter.",
				"It never stops draining, so keep slicing.",
				"If it runs dry, your combo resets.",
				"Fill it for Frenzy: double points!",
				"One long swipe scores a big bonus.",
				inputHowToPause(),
			};
			drawMenuBack();
			drawHeader("How to Play");
			drawBackArrow(sFocus[SCREEN_HOWTO] == 0, sPressed == 0);
			gfx::sign(sprites_plank_idx, 8, 46, 304, 138, COL_PLANK);
			drawSign(kHowtoButtons[1], kHowtoButtons[1].label, sFocus[SCREEN_HOWTO] == 1, sPressed == 1, 0.5f, -1, fruitFor("Practice"));
			drawSign(kHowtoButtons[2], kHowtoButtons[2].label, sFocus[SCREEN_HOWTO] == 2, sPressed == 2, 0.5f, -1, fruitFor("Guides"));
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
		case SCREEN_GUIDES:
			drawGuidesBottom();
			break;
#if PORT_TWO_SCREENS
		case SCREEN_GAME:
			sGame.drawField(sBlade);
			drawOrchardMenus();
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
			gfx::text(gSave.artStyle == ART_3D ? "SELECT: 3D MODELS" : "SELECT: 2D STICKERS",
			          kFieldW * 0.5f, kFieldH - 11.0f, 0.28f, withAlpha(COL_CREAM, 0.72f), ALIGN_CENTER);
#endif
			break;
#endif
		case SCREEN_PAUSE: {
#if PORT_TWO_SCREENS
			sGame.drawField(sBlade);
			gfx::rect(0, 0, 320, 240, rgba(30, 18, 10, 150));
			drawBackArrow(sFocus[SCREEN_PAUSE] == 0, sPressed == 0);
#endif
			int f = sFocus[SCREEN_PAUSE];
			if (sGame.mode == MODE_ORCHARD) {
				for (int i = 1; i < 5; i++) {
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
					const char* lbl = i == 4 ? "Boss Picker" : (i == 3 ? "Restart Boss" : kPauseOrchardButtons[i].label);
#else
					const char* lbl = kPauseOrchardButtons[i].label;
#endif
					drawSign(kPauseOrchardButtons[i], lbl, !sPauseStartOverConfirm && f == i, !sPauseStartOverConfirm && sPressed == i, 0.50f, -1, fruitFor(kPauseOrchardButtons[i].label));
				}
				if (sPauseStartOverConfirm) {
					gfx::rect(0, 0, 320, 240, rgba(20, 12, 8, 170));
					gfx::sign(sprites_plank_idx, 36, 54, 248, 132, COL_PLANK);
					orcWrap("Are you sure you wanna start over? You will lose all progress and go back to Room 1.", 160, 68, 0.34f, 224, 13.0f, COL_CREAM, 5);
					for (int i = 0; i < 2; i++) {
						const Button& b = kStartOverConfirmBtns[i];
						drawPlank(b.x, b.y, b.w, b.h, sPauseConfirmFocus == i, false);
						gfx::text(b.label, b.x + b.w * 0.5f, b.y + 7, 0.42f, sPauseConfirmFocus == i ? COL_ACCENT : COL_CREAM, ALIGN_CENTER);
					}
				}
			} else {
				for (int i = 1; i < 4; i++) {
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
					const char* base = kPauseButtons[i].label;
					const char* label = i == 3 ? "Boss Picker" : base;
#else
					const char* label = i == 3 && sGame.mode == MODE_PRACTICE ? "Skip Lessons" : (i == 3 && sGame.mode == MODE_ZEN ? "Finish Run" : kPauseButtons[i].label);
#endif
					drawSign(kPauseButtons[i], label, f == i, sPressed == i, 0.56f, -1, fruitFor(kPauseButtons[i].label));
				}
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
			const bool lbPrompt = sLbTypingName && screen == sScreen && sPassTurn < 0 && sGame.mode != MODE_PRACTICE;
			if (!lbPrompt && (PORT_TWO_SCREENS || sPassTurn >= 0 || sGame.mode == MODE_PRACTICE)) drawBackArrow(f == 0, sPressed == 0);
			if (sPassTurn >= 0) {
				kPassResultButtons[1].label = sPassTurn + 1 < passCount() ? "Next Player" : "See Results";
				drawSign(kPassResultButtons[1], kPassResultButtons[1].label, f == 1, sPressed == 1, 0.56f, -1, fruitFor(kPassResultButtons[1].label));
			} else if (sGame.mode == MODE_PRACTICE) drawSign(kPracticeEndButtons[1], kPracticeEndButtons[1].label, f == 1, sPressed == 1, 0.56f, -1, fruitFor(kPracticeEndButtons[1].label));
			else if (lbPrompt) {
				char typed[32];
				port::textPoll(typed, sizeof typed - 1);
#if PORT_TWO_SCREENS
				gfx::rect(0, 0, 320, 240, rgba(30, 18, 10, 175));
#endif
				gfx::sign(sprites_plank_idx, 20, 56, 280, 152, COL_PLANK);
				gfx::text("Leaderboard Name", 160, 68, 0.48f, COL_ACCENT, ALIGN_CENTER);
				gfx::text("Max 6 characters", 160, 88, 0.34f, COL_DIM, ALIGN_CENTER);
				drawTypingWithCursor(typed, 160, 110, 0.56f, 248, COL_CREAM);
				for (int i = 0; i < 2; i++) drawSign(kNameButtons[i], kNameButtons[i].label, sLbNameFocus == i, sPressed == i, 0.5f);
			} else {
				const bool elig = lbEligible();
				const int shown = elig ? kResultCount : kResultCount - 1;
				for (int i = 1; i < shown; i++) {
					const char* lbl = (i == kResultTopIdx && sResultBoard) ? "Summary" : kResultButtons[i].label;
					const float sc = (i == kResultTopIdx || (!PORT_3DS && i == 3)) ? 0.50f : 0.56f;
					drawSign(kResultButtons[i], lbl, f == i, sPressed == i, sc, -1, fruitFor(lbl));
				}
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
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
		case SCREEN_BOSS_TEST:
			drawBossTestBottom();
			break;
#endif
		default:
			break;
	}
	drawCut(screen);
}

// ---------------------------------------------------------------- flow

static void startGame() {
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
	startBossTestFight();
	return;
#endif
	sCardNoteT = 0;
	sResultBoard = false;
	sLbTypingName = false;
	sGame.start(sStartMode);
	sBlade.reset();
	gfx::setShake(0, 0);
	sScreen = SCREEN_GAME;
	sPressed = -1;
}

static void toModes() {
	sPassTurn = -1;
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
	sScreen = SCREEN_BOSS_TEST;
#else
	sScreen = SCREEN_MODES;
#endif
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
#if !defined(DEV_TOOLS) || !defined(BOSS_TEST_APP)
	sScreen = SCREEN_TITLE;
	pickSplash();
#endif
}

static void quitRun() {
	if (sGame.mode == MODE_ORCHARD && !sGame.passName) {
		// Orchard Blast auto-saves cleanly at the start of every room, so quitting mid-room preserves that room-start snapshot.
	} else {
		sGame.commitScore();
	}
	toTitle();
}

static void pauseGame() {
	// Not while a run is ending: its score is only saved once that finishes.
	if (sScreen != SCREEN_GAME || sGame.phase == PHASE_OVER || sGame.phase == PHASE_DYING) return;
	sScreen = SCREEN_PAUSE;
	sPauseStartOverConfirm = false;
	sPauseConfirmFocus = 1;
	gfx::setShake(0, 0);
	audio::fuse(false);
	sFocus[SCREEN_PAUSE] = 1;
	sPressed = -1;
	sBlade.reset();
}

static float sMenuPointX = 160, sMenuPointY = 140, sMenuPointShow;  // the menu blade point (stickBlade, below)
static void stickBlade(Input& in);

static bool hudShouldHide() {
	return sScreen == SCREEN_PAUSE || sScreen == SCREEN_RESULT || (sScreen == SCREEN_GAME && sOrcInv);
}

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
	shopTick(kDt);
#if PORT_3DS && defined(TEST_READINGS)
	readingsTick();
#endif
	// On Pick a Game, Select opens the Mixer instead.
	// In Orchard Blast and Boss Rush, Select opens the Inventory.
	if ((in.down & KEY_SELECT) && sScreen != SCREEN_MODES && !((sScreen == SCREEN_GAME || sScreen == SCREEN_PAUSE) && sGame.orchardLike())) sShowDebug = !sShowDebug;
	if (sSuspended) {
		sSuspended = false;
		pauseGame();
	}
	updateTransitions();
	updateWatch();
	streetpass::update(kDt);
	lb::tick();
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
	const bool bossSurge = inGame && !sGame.orchardHeld() && !sOrcInv && sGame.orchardLike() && sGame.orc.boss.on && !sGame.orc.boss.beaten && sGame.orc.boss.surgeT > 0;
	audio::musicSpeed(bossSurge ? 1.22f : ((inGame && sGame.chiliT > 0) ? 1.08f : 1.0f));

	// The game keeps moving during a fade, but menus ignore input while anything is sliding.
	const bool blocked = inputBlocked();
	switch (sScreen) {
		case SCREEN_TITLE: {
			if (blocked) break;
			if (sQuitT >= 0) break;  // the game is closing
#if PORT_3DS
			if (sSpModal == 0 && !sSliding && sFade == 0 && !update::wantsSign()) {
				if (!gSave.streetPassPrompted) {
					sSpModal = 1;
					sSpModalFocus = 0;
				} else if (streetpass::hasPending()) {
					sSpModal = 2;
					sSpModalFocus = 0;
				}
			}
#endif
			if (sSpModal > 0) {
				static const Button kSpModalBtns[] = {{36, 134, 108, 78, nullptr}, {176, 134, 108, 78, nullptr}};
				if (in.repeat & (KEY_LEFT | KEY_RIGHT)) sSpModalFocus ^= 1;
				int pick = touchButtons(kSpModalBtns, 2, in, wasTouching, sSpModalFocus);
				if (pick < 0 && (in.down & KEY_A)) pick = sSpModalFocus;
				if (pick < 0 && (in.down & KEY_B)) pick = 1;
				if (pick >= 0) {
					audio::click();
					if (sSpModal == 1) {
						gSave.streetPassPrompted = 1;
						gSave.streetPassOn = pick == 0 ? 1 : 0;
						port::spSetEnabled(gSave.streetPassOn != 0);
						if (gSave.streetPassOn) streetpass::publishNow();
						saveWrite();
						sSpModal = 0;
					} else {
						const int pi = streetpass::pendingIndex();
						if (pick == 0 && pi >= 0) launchSpChallenge(pi);
						else {
							streetpass::dismissPending();
							sSpModal = 0;
						}
					}
				}
				break;
			}
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
			} else if (t == 3) {
				sStatsTab = 0;
				goTo(SCREEN_STATS, 1);
			} else if (t == 4) {
				sFocus[SCREEN_HOWTO] = 1;
				goTo(SCREEN_HOWTO, 1);
			} else if (t == 5) openShop();
			else if (t == 6) {
				sCreditScroll = sCreditFling = 0;  // Credits always opens at the top
				goTo(SCREEN_CREDITS, 1);
			}
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
			Button signs[kModesPerPage + 2];
			const int count = modeList(sModePage, signs);
			int& focus = sFocus[SCREEN_MODES];
			if (focus >= count) focus = count - 1;
			if (focus >= 0 && signs[focus].w <= 0) focus = count - (kMixerFruit ? 2 : 1);
			while (focus > 0 && signs[focus].w <= 0) focus--;
			int t = menuInput(signs, count, SCREEN_MODES, keys, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			int mode = t > 0 ? modeAt(sModePage, t) : -1;
			// Select opens the Mixer from any page, on a controller or a 3DS.
			if (t < 0 && !sCut.pending && (in.down & KEY_SELECT)) {
				audio::click();
				mode = SIGN_MIXER;
			}
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
			} else if (mode == MODE_BOSSRUSH && !bossBeaten(BOSS_MELON)) {
				audio::play(SFX_CLANG, 0.5f);  // locked: the info side says how to open it
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
#if PORT_MOUSE
				sBeatHover = -1;
				if (in.hovering) {
					if (n > kBeatRows && hit(kBeatUp, in.tx, in.ty)) sBeatHover = 0;
					else if (n > kBeatRows && hit(kBeatDown, in.tx, in.ty)) sBeatHover = 1;
#if PORT_CUSTOM_MUSIC
					else if (beatSong() >= kTrackCount && hit(kBeatTool, in.tx, in.ty)) sBeatHover = 2;
					else if (hit(kBeatFolder, in.tx, in.ty)) sBeatHover = 3;
#endif
				}
#endif
				if (in.touching && !wasTouching) {
					pressedRow = -1;
					sBeatArrowPress = -1;
					for (int r = 0; r < kBeatRows && sBeatScroll + r < n; r++)
						if (hit(beatRow(r), in.tx, in.ty)) pressedRow = r;
					if (n > kBeatRows && hit(kBeatUp, in.tx, in.ty) && sBeatScroll > 0) sBeatArrowPress = 0;
					else if (n > kBeatRows && hit(kBeatDown, in.tx, in.ty) && sBeatScroll + kBeatRows < n) sBeatArrowPress = 1;
#if PORT_CUSTOM_MUSIC
					else if (beatSong() >= kTrackCount && hit(kBeatTool, in.tx, in.ty)) sBeatArrowPress = 2;
					else if (hit(kBeatFolder, in.tx, in.ty)) sBeatArrowPress = 3;
#endif
				}
				if (in.touching && (sBeatArrowPress == 0 || sBeatArrowPress == 1)) {
					const Button& ab = sBeatArrowPress == 0 ? kBeatUp : kBeatDown;
					const bool can = sBeatArrowPress == 0 ? (sBeatScroll > 0) : (sBeatScroll + kBeatRows < n);
					const bool over = can && hit(ab, in.tx, in.ty);
					if (sBeatArrowHold.step(over ? sBeatArrowPress : -1, over)) {
						audio::click();
						sBeatScroll += sBeatArrowPress == 0 ? -1 : 1;
					}
				} else if (!in.touching) {
					sBeatArrowPress = -1;
					sBeatArrowHold.reset();
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
				if (((tap && hit(kBeatFolder, in.tx, in.ty)) || (in.down & KEY_Y)) && !custom::importOpen()) {
					audio::click();
					// The system's file picker; without one, the folder to copy songs into by hand.
					if (!custom::importSongs() && !custom::openFolder()) sFolderNote = 6;
				}
				beatImportTick();
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
#if PORT_MOUSE
			sToolHover = -1;
			if (in.hovering)
				for (int i = 0; i < kToolTapCount; i++)
					if (hit(kToolTaps[i], in.tx, in.ty)) sToolHover = i;
#endif
			if (in.touching) {
				if (!wasTouching) {
					sToolPress = -1;
					for (int i = 0; i < kToolTapCount; i++) {
						if (hit(kToolTaps[i], in.tx, in.ty)) {
							sToolPress = i;
							if (i != 0 && i != 1 && i != 4 && i != 5) toolChange(i);
							break;
						}
					}
				}
				if (sToolPress == 0 || sToolPress == 1 || sToolPress == 4 || sToolPress == 5) {
					const bool over = hit(kToolTaps[sToolPress], in.tx, in.ty);
					if (sToolHold.step(over ? sToolPress : -1, over)) toolChange(sToolPress);
				}
			} else {
				sToolPress = -1;
				sToolHold.reset();
			}
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
				if (kSections[sSection].id == SID_AUDIO) refreshMusicRows();
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
			int t = menuInput(kHowtoButtons, 3, SCREEN_HOWTO, in, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 0) goTo(SCREEN_TITLE, -1);
			else if (t == 1) {
				sStartMode = MODE_PRACTICE;
				fadeThen(startGame);
			} else if (t == 2) {
				goTo(SCREEN_GUIDES, 1);
			}
			break;
		}
		case SCREEN_GUIDES:
			if (!blocked) updateGuides(in, wasTouching);
			break;
		case SCREEN_CREDITS: {
			if (blocked) break;
			sFocus[SCREEN_CREDITS] = 0;
			updateCredits(in, wasTouching);
			// A drag that started on the list only scrolls it, even if it runs up over the back arrow.
			int t = sCreditDrag ? -1 : menuInput(kOnlyBack, 1, SCREEN_CREDITS, in, wasTouching);
			if (t == 0 || (in.down & KEY_B)) {
				if (t != 0) audio::click();
				goTo(SCREEN_TITLE, -1);
			}
			break;
		}
		case SCREEN_STATS: {
			if (blocked) break;
			if (in.down & KEY_L) {
				audio::click(0.6f);
				statsSwitchTab(sStatsTab - 1);
				break;
			}
			if (in.down & KEY_R) {
				audio::click(0.6f);
				statsSwitchTab(sStatsTab + 1);
				break;
			}
			if (in.touching && !wasTouching) {
				for (int tab = 0; tab < kStatsTabCount; tab++) {
					if (hit(kStatsTabs[tab], in.tx, in.ty)) {
						audio::click(0.6f);
						statsSwitchTab(tab);
						break;
					}
				}
			}
#if PORT_3DS
			if (sStatsTab == 1) {
				if (in.down & (KEY_ZL | KEY_ZR)) {
					audio::click(0.5f);
					sSpMetric ^= 1;
				}
				if (streetpass::count() > 0) {
					const int cnt = streetpass::count();
					if (in.repeat & KEY_DOWN) {
						sSpStatSel = (sSpStatSel + 1) % cnt;
						audio::click(0.5f);
					}
					if (in.repeat & KEY_UP) {
						sSpStatSel = sSpStatSel > 0 ? sSpStatSel - 1 : cnt - 1;
						audio::click(0.5f);
					}
					if (in.touching && !wasTouching) {
						for (int row = 0; row < 4 && sSpStatScroll + row < cnt; row++) {
							const float y = 66.0f + row * 32.0f;
							if (in.tx >= 8 && in.tx < 312 && in.ty >= y && in.ty < y + 29) {
								sSpStatSel = sSpStatScroll + row;
								audio::click(0.5f);
							}
						}
					}
					Button spBtns[2] = {kBack, kSpChallengeBtn};
					int t = menuInput(spBtns, 2, sScreen, in, wasTouching);
					if ((in.down & KEY_A) && sFocus[sScreen] != 0) t = 1;
					if (t == 1) {
						audio::click();
						launchSpChallenge(sSpStatSel);
						break;
					}
					if (t == 0 || (in.down & KEY_B)) {
						if (t != 0) audio::click();
						goTo(SCREEN_TITLE, -1);
					}
					break;
				}
			}
#endif
			if (sStatsTab == kStatsGlobalTab) {
				if (in.down & (KEY_ZL | KEY_LEFT)) {
					audio::click(0.5f);
					statsSelectGlobalMode(sGlobalModeIdx - 1);
				} else if (in.down & (KEY_ZR | KEY_RIGHT)) {
					audio::click(0.5f);
					statsSelectGlobalMode(sGlobalModeIdx + 1);
				}
				const int total = lb::view().count;
				const int maxScroll = total > kGlobalRowsVis ? total - kGlobalRowsVis : 0;
				if (in.repeat & KEY_DOWN) {
					if (sGlobalScroll < maxScroll) { sGlobalScroll++; audio::click(0.4f); }
				}
				if (in.repeat & KEY_UP) {
					if (sGlobalScroll > 0) { sGlobalScroll--; audio::click(0.4f); }
				}
				if (in.wheel != 0 && maxScroll > 0) {
					sGlobalScroll = (int)clampf((float)(sGlobalScroll - in.wheel * 2), 0.0f, (float)maxScroll);
				}
				if (in.touching) {
					if (!wasTouching) {
						if (hit(kGlobalModeArrows[0], in.tx, in.ty)) {
							audio::click(0.5f);
							statsSelectGlobalMode(sGlobalModeIdx - 1);
						} else if (hit(kGlobalModeArrows[1], in.tx, in.ty)) {
							audio::click(0.5f);
							statsSelectGlobalMode(sGlobalModeIdx + 1);
						} else {
							for (int col = 0; col < kGlobalTabsVis && sGlobalTabScroll + col < kGlobalModeCount; col++) {
								if (hit(globalModeTabRect(col), in.tx, in.ty)) {
									audio::click(0.5f);
									statsSelectGlobalMode(sGlobalTabScroll + col);
									break;
								}
							}
						}
						if (in.ty >= 82 && in.ty <= 204 && in.tx >= 8 && in.tx < 312) {
							sGlobalDragging = true;
							sGlobalDragY = in.ty;
							sGlobalMoved = false;
						}
					} else if (sGlobalDragging) {
						const int dy = sGlobalDragY - in.ty;
						if (abs(dy) >= 14) {
							sGlobalScroll = (int)clampf((float)(sGlobalScroll + dy / 14), 0.0f, (float)maxScroll);
							sGlobalDragY = in.ty;
							sGlobalMoved = true;
						}
					}
				} else {
					sGlobalDragging = false;
				}
			}
			int& f = sFocus[sScreen];
			f = 0;
			int t = sGlobalMoved ? -1 : menuInput(kOnlyBack, 1, sScreen, in, wasTouching);
			if (t == 0 || (in.down & KEY_B)) {
				if (t != 0) audio::click();
				goTo(SCREEN_TITLE, -1);
			}
			break;
		}
		case SCREEN_GAME:
#if PORT_PAUSE_BUTTON
			if (touchUiActive() && in.touching && !wasTouching && hit(kPauseSign, in.tx, in.ty)) {
				audio::click();
				pauseGame();
				break;
			}
			if (touchUiActive() && in.touching && !wasTouching && sGame.mode == MODE_ORCHARD && !sGame.orchardHeld() && hit(kInvSign, in.tx, in.ty)) {
				audio::click();
				if (sOrcInv) {
					sOrcInv = false;
					sGame.orcResume();
				} else {
					sOrcInv = true;
					sOrcInvFocus = -1;
					sOrcPassiveScroll = 0;
					sOrcConfirm = 0;
					sBlade.reset();
					audio::fuse(false);
				}
				break;
			}
#endif
			{
				const bool wasInv = sOrcInv;
				const bool wasDevPrompt = sGame.orc.devPrompt;
				const bool orcTook = updateOrchardMenus(in, wasTouching);
				if (wasDevPrompt && (in.down & KEY_START)) break;
				if (sOrcInv || wasInv) {
					if (sOrcInv && (in.down & KEY_START)) {
						sOrcInv = false;
						pauseGame();
					}
					break;  // the Inventory holds the run, and closing via Esc returns to play cleanly
				}
				Input bin = in;
#if PORT_PAUSE_BUTTON
				if (touchUiActive() && (hit(kPauseSign, in.tx, in.ty) || (sGame.mode == MODE_ORCHARD && !sGame.orchardHeld() && hit(kInvSign, in.tx, in.ty)))) bin.touching = false;
#endif
				if (orcTook || sOrcSlotTouch) bin.touching = false;
				sBlade.update(sGame.bladeInput(bin));
			}
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
				onResultEntered();
			}
			break;
		case SCREEN_PAUSE: {
			if (blocked) break;
			if (sPauseStartOverConfirm) {
				int t = touchButtons(kStartOverConfirmBtns, 2, in, wasTouching, sPauseConfirmFocus);
				if (in.repeat & (KEY_LEFT | KEY_RIGHT)) {
					sPauseConfirmFocus ^= 1;
					audio::click(0.5f);
				}
				if (in.down & KEY_A) t = sPauseConfirmFocus;
				if (in.down & (KEY_B | KEY_START)) t = 1;
				if (t == 0) {
					audio::click();
					sPauseStartOverConfirm = false;
					sOptionsFromPause = false;
					audio::bossMusicEnd();
					audio::tensionMusic(false);
					Game::orchardClearSave();
					fadeThen(startGame);
				} else if (t == 1) {
					audio::click();
					sPauseStartOverConfirm = false;
				}
				break;
			}
			const int count = sGame.mode == MODE_ORCHARD ? 5 : 4;
			Button pauseBtns[5];
			for (int i = 0; i < count; i++) pauseBtns[i] = sGame.mode == MODE_ORCHARD ? kPauseOrchardButtons[i] : kPauseButtons[i];
			if (!PORT_TWO_SCREENS) pauseBtns[0] = {};
			int t = menuInput(pauseBtns, count, SCREEN_PAUSE, in, wasTouching);
			if (t < 0 && (in.down & (KEY_START | KEY_B))) {
				t = 1;
				audio::click();
			}
			if (t == 0 || t == 1) {
				sOptionsFromPause = false;
				sScreen = SCREEN_GAME;
				sBlade.reset();
				if (sGame.orchardLike()) sGame.orcResume();
			} else if (t == 2) {
				sOptionsFromPause = true;
				sFocus[SCREEN_OPTIONS] = 1;
				goTo(SCREEN_OPTIONS, 1);
			} else if (sGame.mode == MODE_ORCHARD && t == 3) {
				audio::click();
				sPauseStartOverConfirm = true;
				sPauseConfirmFocus = 1;
			} else if (t == count - 1) {
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
			if (sLbTypingName) {
				updateLbNameEntry(in, wasTouching);
				break;
			}
			const bool elig = lbEligible();
			const int btnCount = elig ? kResultCount : kResultCount - 1;
			if (sFocus[SCREEN_RESULT] >= btnCount) sFocus[SCREEN_RESULT] = btnCount - 1;
			Button resultBtns[kResultCount];
			for (int i = 0; i < kResultCount; i++) resultBtns[i] = kResultButtons[i];
			if (!PORT_TWO_SCREENS) resultBtns[0] = {};
			int t = menuInput(resultBtns, btnCount, SCREEN_RESULT, in, wasTouching);
			if (elig && (in.down & KEY_Y)) {
				audio::click();
				t = kResultTopIdx;
			}
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 1) {
				sCardFrame = false;
				sResultBoard = false;
				sStartMode = sGame.mode;
				fadeThen(startGame);
			} else if (t == 0 || t == 2) {
				sCardFrame = false;
				sResultBoard = false;
				fadeThen(toModes);
			} else if (elig && t == kResultTopIdx) {
				sResultBoard = !sResultBoard;
				if (sResultBoard && (lb::view().status == lb::STATUS_IDLE || lb::view().status == lb::STATUS_OFFLINE)) {
					lb::fetch(sGame.mode, false);
				}
			}
#if !PORT_3DS
			else if (t == 3 && !sCardFrame) {
				sResultBoard = false;
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
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
		case SCREEN_BOSS_TEST:
			updateBossTest(in, wasTouching);
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
	gHudPaused = hudShouldHide();  // hide HUD/slots behind Pause, Result, or Inventory
	beginHighlightFrame();
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
	beginHighlightFrame();
	gfx::beginFrame();
	gfx::drawBackdrop(field ? WHITE : rgba(205, 205, 205));
	const float mixerIn = sScreen == SCREEN_MODES ? (sSliding ? e : 1) : (sSliding && sSlideFrom == SCREEN_MODES ? 1 - e : 0);
	const bool mixerShown = mixerIn > 0;
	if (field) {
		gfx::beginField();
		gHudPaused = hudShouldHide();
		sGame.drawField(sBlade);
		if (sScreen == SCREEN_GAME) sGame.drawHud();
		drawStickPoint();
		if (sScreen == SCREEN_GAME) drawOrchardMenus();
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
		if (sScreen == SCREEN_GAME && !touchUiActive()) {
			gfx::text(gSave.artStyle == ART_3D ? "TAB: 3D MODELS" : "TAB: 2D STICKERS",
			          kFieldW * 0.5f, kFieldH - 11.0f, 0.25f, withAlpha(COL_CREAM, 0.65f), ALIGN_CENTER);
		}
#endif
#if PORT_PAUSE_BUTTON
		if (sScreen == SCREEN_GAME && touchUiActive()) {
			// Half see-through, so fruit passing behind it stays easy to see.
			gfx::sign(sprites_plank_idx, kPauseSign.x + 3, kPauseSign.y + 3, 30, 30, withAlpha(COL_PLANK, 0.55f));
			gfx::rect(kPauseSign.x + 12, kPauseSign.y + 10, 4, 16, withAlpha(COL_CREAM, 0.55f));
			gfx::rect(kPauseSign.x + 20, kPauseSign.y + 10, 4, 16, withAlpha(COL_CREAM, 0.55f));
			if (sGame.mode == MODE_ORCHARD && (!sGame.orchardHeld() || sOrcInv)) {
				gfx::sign(sprites_plank_idx, kInvSign.x + 3, kInvSign.y + 3, 30, 30, withAlpha(sOrcInv ? COL_ACCENT : COL_PLANK, 0.65f));
				gfx::sprite(SHEET_SPRITES, sprites_cd_seedpouch_idx, kInvSign.x + 18, kInvSign.y + 18, 0.48f, 0.48f);
			}
		}
#endif
		if (sScreen == SCREEN_PAUSE || sScreen == SCREEN_RESULT) gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(24, 14, 8, sScreen == SCREEN_RESULT ? 165 : 150));
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
		if (pass == 1 && menuBladeShown() && !mixerShown) {
			sMenuBlade.draw();
			if (!sSliding) drawMenuStickPoint();
		}
	}
	// The Mixer fruit sits under Pick a Game's info sign, so it is drawn over the whole window, with its cut,
	// and the blade over it, since a swipe can cross from the signs to the fruit.
	if (mixerShown) {
		gfx::beginCorner();
		sDrawing = SCREEN_MODES;
		drawMixerFruit(mixerIn);
		drawCut(SCREEN_MODES, true);
		if (menuBladeShown()) {
			sMenuBlade.draw();
			if (!sSliding) drawMenuStickPoint();
		}
	}
	// The Quit Game bomb sits in the window's own corner, outside both sides, and fades with a slide.
	{
		const float quitIn = sScreen == SCREEN_TITLE ? (sSliding ? e : 1) : (sSliding && sSlideFrom == SCREEN_TITLE ? 1 - e : 0);
		if (PORT_QUIT_BOMB && (quitIn > 0 || sQuitT >= 0)) {
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
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
	sScreen = SCREEN_BOSS_TEST;
	sFocus[SCREEN_BOSS_TEST] = 4;
#else
	sScreen = SCREEN_TITLE;
#endif
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
#if defined(AUTOTEST)
	gSave.streetPassPrompted = 1;  // avoid blocking automated test menus unless explicitly tested
#endif
	streetpass::init();
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
#if PORT_3DS && defined(TEST_READINGS)
	readingsStart();
#endif
	return true;
}

void app::notifySuspended() {
#if defined(DEV_TOOLS) || defined(LAB)
	if (!sPauseOnUnfocus) return;
#endif
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
	sPointY = clampf(sPointY + vy * kDt, kFieldTop + 4, kFieldH - 4);
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

#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
#if PORT_3DS
		if (in.down & KEY_SELECT) {
#else
		if (in.down & KEY_TAB) {
#endif
			gSave.artStyle = gSave.artStyle == ART_3D ? ART_2D : ART_3D;
			audio::play(SFX_CLICK, 0.7f);
		}
#endif
		if (!PORT_3DS) {
			if (in.mode) {
				gInputMode = (InputMode)(in.mode - 1);
			} else if (fabsf(in.stickX) > 0.25f || fabsf(in.stickY) > 0.25f || in.stickSlice) {
				gInputMode = INPUT_PAD;
			} else if (in.hovering || in.wheel != 0) {
				gInputMode = INPUT_KEYMOUSE;
			}
		}
		stickBlade(in);
		gStickInUse = sPointShow > 0 || padUiActive();
		updateFrame(in, sWasTouching);
#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
		if (gBossTestDone) return false;
#endif
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
		quitTick();
		if (sLeaving || sUpdateQuit || sQuitT > kQuitBlast) return false;
		// A press belongs to the first step only.
		in.down = 0;
		in.repeat = 0;
		in.wheel = 0;
	}

	// New scenery, boards and models load here, between frames. The Journal, Guides (also while they slide away),
	// Smoothie Recipe HUD, and High Contrast draw 2D stickers even with 3D art, so the sticker sheet stays in for them.
	gfx::keepStickers(sScreen == SCREEN_JOURNAL || sScreen == SCREEN_GUIDES || sScreen == SCREEN_HOWTO ||
	                  ((sScreen == SCREEN_GAME || sScreen == SCREEN_PAUSE) && sGame.mode == MODE_RECIPE) ||
	                  (sSliding && (sSlideFrom == SCREEN_JOURNAL || sSlideFrom == SCREEN_GUIDES || sSlideFrom == SCREEN_HOWTO)) || gSave.highContrast);
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
	streetpass::shutdown();
	jobs::shutdown();
	m3d::shutdown();
	audio::shutdown();
	gfx::shutdown();
}
