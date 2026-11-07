// The shared game: application flow and menus. Each port's main loop drives it through app.h.
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "blade.h"
#include "common.h"
#include "fruit.h"
#include "game.h"
#include "jobs.h"
#include "model3d.h"
#include "render.h"
#include "sprites.h"

#if PORT_3DS
#include <sys/stat.h>
#include "bgtop0.h"
#endif
#include "logo.h"
#include "port_api.h"

#define VERSION_STRING "0.4.0-alpha"

#if PORT_TWO_SCREENS
static constexpr float kSlideTop = 400, kSlideBottom = 320;
#else
static constexpr float kSlideTop = 900, kSlideBottom = 900;
#endif

enum Screen {
	SCREEN_LOADING, SCREEN_TITLE, SCREEN_MODES, SCREEN_OPTIONS, SCREEN_SECTION, SCREEN_CREDITS, SCREEN_HOWTO,
	SCREEN_GAME, SCREEN_PAUSE, SCREEN_RESULT, SCREEN_BENCH, SCREEN_COUNT
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

static void goTo(Screen to, float dir) {
	sPressed = -1;
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
	{40, 14, 240, 80, "Play"},
	{22, 102, 134, 54, "Options"},
	{164, 102, 134, 54, "Credits"},
	{60, 166, 200, 46, "How to Play"},
};
// Index 0 of every list below is its back arrow, so D-pad and touch treat it like any other sign.
static const Button kModeButtons[] = {
	kBack,
	{8, 46, 148, 44, "Classic"},        {164, 46, 148, 44, "Arcade"},
	{8, 94, 148, 44, "Zen"},            {164, 94, 148, 44, "Frenzy"},
	{8, 142, 148, 44, "Sudden Death"},  {164, 142, 148, 44, "Minefield"},
	{86, 190, 148, 44, "Combo Rush"},
};
static const Button kOptionButtons[] = {
	kBack,
	{36, 46, 248, 44, "Audio"},
	{36, 94, 248, 44, "Visuals"},
	{36, 142, 248, 44, "Gameplay"},
	{36, 190, 248, 44, "Scores and Data"},
};
static const Button kOnlyBack[] = {kBack};
static const Button kResultButtons[] = {kBack, {20, 170, 134, 52, "Again"}, {166, 170, 134, 52, "Modes"}};
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

static void drawSign(const Button& b, const char* label, bool focused, bool pressed, float scale = 0.56f, int icon = -1) {
	drawPlank(b.x, b.y, b.w, b.h, focused, pressed);
	float dy = pressed ? 2 : 0;
	float w = gfx::textWidth(label, scale);
	float room = b.w - (icon >= 0 ? 52 : 22);
	if (w > room) scale *= room / w;
	float cx = b.x + b.w * 0.5f + (icon >= 0 ? 12 : 0);
	if (icon >= 0) gfx::sprite(SHEET_SPRITES, icon, b.x + 21, b.y + b.h * 0.5f + dy, 0.8f, 0.8f);
	gfx::text(label, cx, b.y + b.h * 0.5f - 20 * scale - 1 + dy, scale, COL_CREAM, ALIGN_CENTER);
}

static void drawBackArrow(bool focused, bool pressed) {
	drawPlank(kBack.x, kBack.y, kBack.w, kBack.h, focused, pressed);
	gfx::sprite(SHEET_SPRITES, sprites_arrow_back_idx, kBack.x + kBack.w * 0.5f, kBack.y + kBack.h * 0.5f + (pressed ? 2 : 0), 0.8f, 0.8f, 0, COL_CREAM);
}

static void drawHeader(const char* title) {
	// Centred on the screen, like the signs below it. A long title shrinks to stay clear of the back arrow.
	float scale = 0.62f, w = gfx::textWidth(title, scale), room = 320 - 2 * (kBack.x + kBack.w + 4);
	if (w > room) scale *= room / w;
	gfx::text(title, 160, 8 + (0.62f - scale) * 20, scale, COL_CREAM, ALIGN_CENTER);
}

// Menus sit on the board chosen in Visuals, a little dimmed so that the signs stand out on it.
#if PORT_TWO_SCREENS
static void drawMenuBack() { gfx::image(SHEET_BOARD, 0, 0, 0, 320, 240, rgba(205, 205, 205)); }
#else
static void drawMenuBack() {}  // one screen, one board: it's already behind everything
#endif

// Press highlights, release on the same sign activates: a swipe that ends on a sign does nothing.
// A sign clicks the moment a finger or stylus lands on it, since a tap takes a tenth of a second to
// lift, and acts on release, so sliding off still cancels. It plays the click itself.
static int touchButtons(const Button* btns, int n, const Input& in, bool wasTouching, int& focus) {
	static bool clicked;
	int result = -1;
	if (in.touching) {
		if (!wasTouching) {
			sPressed = -1;
			for (int i = 0; i < n; i++)
				if (hit(btns[i], in.tx, in.ty)) sPressed = focus = i;
			clicked = sPressed >= 0;
			if (clicked) audio::click();
		} else if (sPressed >= 0 && !hit(btns[sPressed], in.tx, in.ty)) {
			sPressed = -1;
		}
	} else if (wasTouching) {
		if (sPressed >= 0) result = sPressed;
		if (result >= 0 && !clicked) audio::click();  // pressed while the screen was still locked
		sPressed = -1;
		clicked = false;
	}
	return result;
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

static int menuInput(const Button* btns, int n, Screen screen, const Input& in, bool wasTouching) {
	int& focus = sFocus[screen];
	dpadNav(btns, n, in.repeat, focus);
#if PORT_MOUSE
	if (in.hovering)
		for (int i = 0; i < n; i++)
			if (hit(btns[i], in.tx, in.ty)) focus = i;
#endif
	int tapped = touchButtons(btns, n, in, wasTouching, focus);
	if (tapped < 0 && (in.down & KEY_A)) {
		tapped = focus;
		audio::click();
	}
	return tapped;
}

// ---------------------------------------------------------------- settings rows
enum RowKind { ROW_SLIDER, ROW_CHOICE, ROW_ACTION };
enum RowAction { ACT_NONE, ACT_RESET_AUDIO, ACT_RESET_VISUALS, ACT_RESET_GAMEPLAY, ACT_RESET_SCORES, ACT_RESET_ALL, ACT_SPEED_TEST };

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
static const char* const kBoardNames[kBoardCount] = {"Picnic Blanket", "Cutting Board", "Plate", "Marble", "Beach", "Starlight"};
#if PORT_TWO_SCREENS
static const char* const kSceneNames[kSceneCount] = {"Dusk Orchard", "Morning Meadow", "Moonlit Bay"};
#endif
static const char* const kArtNames[] = {"2D Stickers", "3D Models"};
static const char* const kEffectNames[] = {"None", "Low", "Medium", "Full"};
static const char* const kOffOn[] = {"Off", "On"};
#if PORT_TWO_SCREENS
static const char* const kBgNames[] = {"Simple", "Full"};
#endif
static const char* const kDetailNames[] = {"Low", "High"};
static const char* const kSliceNames[] = {"Quick", "Exact"};
static const char* const kDifficultyNames[DIFF_COUNT] = {"Easy", "Normal", "Hard", "Insane"};

static const char* const kTrackRowNames[kTrackCount + 1] = {
	"Auto", "Orchard Morning", "Picnic Stroll", "Sleepy Meadow", "Lantern Walk", "Still Water", "Moss Garden", "Bamboo Breeze", "Drifting Clouds",
};

static const Row kAudioRows[] = {
	{ROW_SLIDER, "Sound Effects", &gSave.sfx, 0, 100, nullptr, 0, 100, ACT_NONE, "Slices, combos and bombs.", ""},
	{ROW_SLIDER, "Music", &gSave.music, 0, 100, nullptr, 0, 100, ACT_NONE, "Menu and game music.", ""},
	{ROW_SLIDER, "Menu Sounds", &gSave.menuSfx, 0, 100, nullptr, 0, 100, ACT_NONE, "The tap when you press a sign.", ""},
	{ROW_CHOICE, "Menu Music", &gSave.menuTrack, 0, kTrackCount, kTrackRowNames, 0, 0, ACT_NONE, "What plays in the menus.", "Auto uses Orchard Morning."},
	{ROW_CHOICE, "Game Music", &gSave.gameTrack, 0, kTrackCount, kTrackRowNames, 0, 0, ACT_NONE, "What plays during a game.", "Auto suits each mode."},
	{ROW_ACTION, "Reset Audio", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_AUDIO, "Puts these back to their defaults.", ""},
};
static const Row kVisualRows[] = {
	{ROW_CHOICE, "Art Style", &gSave.artStyle, 0, 1, kArtNames, 0, 0, ACT_NONE, "3D models or flat stickers.", PORT_ART_HINT},
	{ROW_CHOICE, "Board", &gSave.board, 0, kBoardCount - 1, kBoardNames, 0, 0, ACT_NONE, "What you slice over.", ""},
#if PORT_TWO_SCREENS
	{ROW_CHOICE, "Scenery", &gSave.scene, 0, kSceneCount - 1, kSceneNames, 0, 0, ACT_NONE, "The view on the top screen.", ""},
#endif
	{ROW_CHOICE, "Blade", &gSave.blade, 0, kBladeCount - 1, kBladeNames, 0, 0, ACT_NONE, "The colour of your slice.", ""},
	{ROW_CHOICE, "Effects", &gSave.effects, 0, 3, kEffectNames, 0, 0, ACT_NONE, "Juice, sparks and rings.", "Lower saves the most speed."},
#if PORT_3DS
	{ROW_SLIDER, "3D Depth", &gSave.depth, 0, 100, nullptr, 0, 100, ACT_NONE, "How far things stand out,", "with the console's 3D slider."},
#endif
	{ROW_SLIDER, "Screen Shake", &gSave.shake, 0, 100, nullptr, 0, 100, ACT_NONE, "How hard big hits shake the field.", ""},
#if PORT_TWO_SCREENS
	{ROW_CHOICE, "Background", &gSave.bgDetail, 0, 1, kBgNames, 0, 0, ACT_NONE, "Simple draws fewer scenery layers", "and helps an Old 3DS."},
#endif
	{ROW_CHOICE, "3D Detail", &gSave.detail, 0, 1, kDetailNames, 0, 0, ACT_NONE, "Polygons per fruit, 3D style only.", PORT_DETAIL_HINT},
	{ROW_CHOICE, "Slice Style", &gSave.sliceStyle, 0, 1, kSliceNames, 0, 0, ACT_NONE, "Exact cuts along your swipe.", "Quick uses ready-made halves."},
	{ROW_CHOICE, "Transitions", &gSave.transitions, 0, 1, kOffOn, 0, 0, ACT_NONE, "Slides between menus.", ""},
	PORT_EXTRA_VISUAL_ROWS
	{ROW_ACTION, "Reset Visuals", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_VISUALS, PORT_RESET_HINT, ""},
};
static const Row kGameplayRows[] = {
#if PORT_HAS_SENSITIVITY
	{ROW_SLIDER, PORT_SENSITIVITY_LABEL, &gSave.stylus, 0, 100, nullptr, 0, 100, ACT_NONE, PORT_SENSITIVITY_HINT, "Lower smooths out a shaky line."},
#endif
	{ROW_CHOICE, "Difficulty", &gSave.difficulty, 0, DIFF_COUNT - 1, kDifficultyNames, 0, 0, ACT_NONE, "Changes fruit, bombs and how", "fast the Frenzy meter moves."},
	{ROW_SLIDER, "Fruit Speed", &gSave.fruitSpeed, 0, 100, nullptr, 70, 130, ACT_NONE, "How fast everything flies.", "100% is normal."},
	{ROW_SLIDER, "Bomb Amount", &gSave.bombs, 0, 100, nullptr, 0, 200, ACT_NONE, "More or fewer bombs than usual.", "100% is normal."},
	{ROW_SLIDER, "Slice Forgiveness", &gSave.forgiveness, 0, 100, nullptr, 70, 130, ACT_NONE, "How close a swipe must pass.", "100% is normal."},
	{ROW_SLIDER, "Frenzy Drain", &gSave.frenzyDrain, 0, 100, nullptr, 40, 160, ACT_NONE, "How fast the meter drops.", "100% is normal."},
	{ROW_SLIDER, "Misses Allowed", &gSave.misses, 1, 5, nullptr, 1, 5, ACT_NONE, "Misses before Classic ends.", ""},
	{ROW_ACTION, "Reset Gameplay", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_GAMEPLAY, "Puts these back to their defaults.", "Your scores stay."},
};
static const Row kDataRows[] = {
#if PORT_3DS
	{ROW_ACTION, "Speed Test", nullptr, 0, 0, nullptr, 0, 0, ACT_SPEED_TEST, "A 90 second test of this console.", "Results go in 3ds/FruitSlicer/bench.txt"},
#endif
	{ROW_ACTION, "Reset Scores", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_SCORES, "Erases best scores and totals.", "Settings stay."},
	{ROW_ACTION, "Reset Everything", nullptr, 0, 0, nullptr, 0, 0, ACT_RESET_ALL, "Erases scores and all settings.", ""},
};

struct Section {
	const char* title;
	const Row* rows;
	int count;
};
static const Section kSections[] = {
	{"Audio", kAudioRows, sizeof kAudioRows / sizeof(Row)},
	{"Visuals", kVisualRows, sizeof kVisualRows / sizeof(Row)},
	{"Gameplay", kGameplayRows, sizeof kGameplayRows / sizeof(Row)},
	{"Scores and Data", kDataRows, sizeof kDataRows / sizeof(Row)},
};

static bool sOptionsFromPause = false;  // Options opened from the pause menu, with a game waiting

static constexpr float kListTop = 46;
static constexpr float kRowH = 38;
static constexpr float kTrackX0 = 162, kTrackX1 = 258;

static int sSection = 0;
static float sScroll = 0;
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

static bool rowDisabled(const Row& r) { return r.value == &gSave.depth && !gHas3dScreen; }

static void rowValueText(const Row& r, char* buf, size_t size) {
	buf[0] = 0;
	if (r.kind == ROW_CHOICE) {
		snprintf(buf, size, "%s", r.names[*r.value]);
	} else if (r.kind == ROW_SLIDER) {
		if (rowDisabled(r)) {
			snprintf(buf, size, "-");
		} else if (r.value == &gSave.misses) {
			snprintf(buf, size, "%d", *r.value);
		} else {
			float t = (*r.value - r.lo) / (float)(r.hi - r.lo);
			snprintf(buf, size, "%d%%", (int)(lerpf(r.showLo, r.showHi, t) + 0.5f));
		}
	}
}

static void changedSetting(const Row& r) {
	sDirty = true;
	if (r.value == &gSave.music) audio::refreshVolumes();
	// Hear a track as soon as it is picked. Leaving the section puts the right one back.
	if (r.value == &gSave.menuTrack) audio::preview(MUSIC_MENU);
	if (r.value == &gSave.gameTrack) audio::preview(MUSIC_PLAY);
}

static void adjustRow(const Row& r, int dir) {
	if (rowDisabled(r) || r.kind == ROW_ACTION) return;
	int v = *r.value;
	if (r.kind == ROW_SLIDER) {
		int step = r.hi - r.lo >= 50 ? 5 : 1;
		v = v + dir * step;
		v = v < r.lo ? r.lo : (v > r.hi ? r.hi : v);
	} else {
		v += dir;
		if (v < r.lo) v = r.hi;
		if (v > r.hi) v = r.lo;
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

static void rowAction(int index, const Row& r) {
#if PORT_3DS
	if (r.action == ACT_SPEED_TEST) {
		audio::click();
		if (sDirty) saveWrite();
		sDirty = false;
		fadeThen(startSpeedTest);
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
		case ACT_RESET_AUDIO: saveDefaults(true, 0); break;
		case ACT_RESET_VISUALS: saveDefaults(true, 1); break;
		case ACT_RESET_GAMEPLAY: saveDefaults(true, 2); break;
		case ACT_RESET_SCORES:
			memset(gSave.best, 0, sizeof gSave.best);
			gSave.totalFruit = 0;
			gSave.bestCombo = 0;
			break;
		case ACT_RESET_ALL: saveDefaults(false, -1); break;
		default: break;
	}
	audio::refreshVolumes();
	audio::reapply();
	audio::play(SFX_GOLD, 0.7f);
	saveWrite();
	sDirty = false;
}

static void leaveSection(bool clicked = false) {
	if (!clicked) audio::click();
	if (sDirty) saveWrite();
	sDirty = false;
	sArmed = -1;
	sTouchMode = TOUCH_NONE;
	audio::reapply();
	goTo(SCREEN_OPTIONS, -1);
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

static void updateSection(const Input& in, bool wasTouching) {
	const Section& sec = kSections[sSection];
	int& focus = sFocus[SCREEN_SECTION];  // -1 is the back arrow

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
		leaveSection();
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
				audio::click();
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
			leaveSection(true);
			return;
		}
		if (sTouchMode == TOUCH_SLIDER) {
			const Row& r = sec.rows[sTouchRow];
			if (r.value == &gSave.sfx) audio::play(SFX_CLICK);
			else audio::click(0.6f);
		} else if (sTouchMode == TOUCH_ROW && !sTouchMoved) {
			const Row& r = sec.rows[sTouchRow];
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
	gSave.topFruit = 1;
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
static void drawTopContent(Screen screen) {
	char buf[48];
	switch (screen) {
		case SCREEN_LOADING:
			break;
		case SCREEN_TITLE:
			gfx::setDepth(1.2f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 66 + sinf(sTime * 1.6f) * 2);
			drawHero(200, 172, 1.25f, 0.8f);
			if (sNoSound) {
				gfx::setDepth(0.6f);
				gfx::text(PORT_NO_SOUND_TEXT, 200, 222, 0.34f, rgba(255, 190, 150), ALIGN_CENTER);
			}
			break;
		case SCREEN_MODES: {
			int f = sFocus[SCREEN_MODES];
			topPanel(30, 96, 340, 134);
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			gfx::setDepth(0.7f);
			if (f >= 1) {
				const ModeInfo& m = kModes[f - 1];
				gfx::text(m.name, 200, 104, 0.6f, COL_ACCENT, ALIGN_CENTER);
				gfx::text(m.line1, 200, 134, 0.38f, COL_CREAM, ALIGN_CENTER);
				gfx::text(m.line2, 200, 152, 0.38f, COL_CREAM, ALIGN_CENTER);
				snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)gSave.best[f - 1]);
				gfx::text(buf, 200, 184, 0.6f, COL_CREAM, ALIGN_CENTER);
			} else {
				gfx::text("Back to the title", 200, 150, 0.46f, COL_CREAM, ALIGN_CENTER);
			}
			break;
		}
		case SCREEN_OPTIONS: {
			static const char* const kLines[][2] = {
				{"", "Back to the title"},
				{"Sound, music and menu volume.", ""},
				{PORT_VISUALS_HINT, "effects and speed savers."},
				{PORT_GAMEPLAY_HINT, "game plays."},
				{PORT_DATA_HINT, ""},
			};
			int f = sFocus[SCREEN_OPTIONS];
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 50, 0.62f, 0.62f);
			topPanel(40, 112, 320, 84);
			gfx::setDepth(0.7f);
			gfx::text(f >= 1 ? kOptionButtons[f].label : "Options", 200, 118, 0.5f, COL_ACCENT, ALIGN_CENTER);
			gfx::text(kLines[f][0], 200, 146, 0.38f, COL_CREAM, ALIGN_CENTER);
			gfx::text(kLines[f][1], 200, 164, 0.38f, COL_CREAM, ALIGN_CENTER);
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
			} else if (r && r->value == &gSave.blade) {
				gfx::setDepth(0.6f);
				sPreviewBlade.draw();
			} else if (sSection == 3) {
				gfx::setDepth(0.6f);
				snprintf(buf, sizeof buf, "FRUIT SLICED %lu", (unsigned long)gSave.totalFruit);
				gfx::text(buf, 200, 20, 0.5f, COL_CREAM, ALIGN_CENTER);
				snprintf(buf, sizeof buf, "BEST COMBO %lu", (unsigned long)gSave.bestCombo);
				gfx::text(buf, 200, 50, 0.5f, COL_CREAM, ALIGN_CENTER);
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
		case SCREEN_CREDITS:
			gfx::setDepth(1.0f);
			gfx::sprite(SHEET_LOGO, logo_logo_idx, 200, 54, 0.72f, 0.72f);
			topPanel(30, 110, 340, 116);
			gfx::setDepth(0.7f);
			gfx::text("Free, non-commercial fan game.", 200, 118, 0.38f, COL_CREAM, ALIGN_CENTER);
			gfx::text("Not affiliated with any publisher.", 200, 136, 0.38f, COL_CREAM, ALIGN_CENTER);
			snprintf(buf, sizeof buf, "FRUIT SLICED %lu  BEST COMBO %lu", (unsigned long)gSave.totalFruit, (unsigned long)gSave.bestCombo);
			gfx::text(buf, 200, 164, 0.34f, COL_DIM, ALIGN_CENTER);
			gfx::text("V" VERSION_STRING, 200, 190, 0.42f, COL_ACCENT, ALIGN_CENTER);
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
			break;
		case SCREEN_RESULT: {
			float in = clampf(sGame.phaseT * 4, 0, 1);
			gfx::setDepth(0.5f);
			gfx::sign(sprites_plank_idx, 30, 12, 340, 212, withAlpha(COL_PLANK, in));
			gfx::setDepth(1.0f);
			gfx::text(kModes[sGame.mode].name, 200, 26, 0.56f, COL_CREAM, ALIGN_CENTER);
			gfx::setDepth(1.7f);
			snprintf(buf, sizeof buf, "%lu", (unsigned long)sGame.score);
			gfx::text(buf, 200, 50, 1.7f, COL_ACCENT, ALIGN_CENTER);
			gfx::setDepth(1.0f);
			if (sGame.newBest) {
				gfx::text("NEW BEST!", 200, 116, 0.7f + sinf(sTime * 6) * 0.04f, COL_CREAM, ALIGN_CENTER);
			} else {
				snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)sGame.best);
				gfx::text(buf, 200, 116, 0.6f, COL_DIM, ALIGN_CENTER);
			}
			snprintf(buf, sizeof buf, "FRUIT SLICED %d", sGame.sliced);
			gfx::text(buf, 200, 154, 0.44f, COL_CREAM, ALIGN_CENTER);
			snprintf(buf, sizeof buf, "BEST COMBO %d", sGame.bestCombo);
			gfx::text(buf, 200, 176, 0.44f, COL_CREAM, ALIGN_CENTER);
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
}

// ---------------------------------------------------------------- screens: bottom
static void drawRows() {
	const Section& sec = kSections[sSection];
	int focus = sFocus[SCREEN_SECTION];
	char buf[32];
	for (int i = 0; i < sec.count; i++) {
		const Row& r = sec.rows[i];
		float y = kListTop + i * kRowH - sScroll;
		if (y + kRowH < kListTop || y > 240) continue;
		bool pressed = sTouchMode == TOUCH_ROW && sTouchRow == i && !sTouchMoved && r.kind == ROW_ACTION;
		drawPlank(8, y + 1, 304, kRowH - 3, focus == i, pressed);
		float ty = y + kRowH * 0.5f - 8;
		u32 labelColor = rowDisabled(r) ? COL_DIM : COL_CREAM;
		if (r.kind == ROW_ACTION) {
			const char* label = sArmed == i ? PORT_CONFIRM_TEXT : r.label;
			gfx::text(label, 160, ty - 1, 0.42f, sArmed == i ? COL_ACCENT : COL_CREAM, ALIGN_CENTER);
			continue;
		}
		float ls = 0.36f;
		float lw = gfx::textWidth(r.label, ls);
		if (lw > 134) ls *= 134 / lw;
		gfx::text(r.label, 18, ty + (0.36f - ls) * 10, ls, labelColor);
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
	// A quiet scroll mark, only when the list is longer than the screen.
	float most = maxScroll();
	if (most > 0) {
		float room = 240 - kListTop;
		float size = room * room / (room + most);
		gfx::rect(315, kListTop + (room - size) * (sScroll / most), 3, size, COL_DIM);
	}
}

static void drawBottomContent(Screen screen) {
	static const int kIcons[] = {sprites_pip_on_idx, sprites_ic_clock_idx, sprites_ic_leaf_idx, sprites_ic_fast_idx, sprites_ic_one_idx, sprites_ic_mine_idx, sprites_ic_chain_idx};
	switch (screen) {
		case SCREEN_LOADING:
			break;
		case SCREEN_TITLE: {
			drawMenuBack();
			gfx::image(SHEET_SPRITES, sprites_post_idx, 150, 0, 20, 240, COL_PLANK);
			int f = sFocus[SCREEN_TITLE];
			for (int i = 0; i < 4; i++) drawSign(kTitleButtons[i], kTitleButtons[i].label, f == i, sPressed == i, i == 0 ? 1.0f : 0.5f);
			break;
		}
		case SCREEN_MODES: {
			drawMenuBack();
			int f = sFocus[SCREEN_MODES];
			drawHeader("Pick a Game");
			drawBackArrow(f == 0, sPressed == 0);
			for (int i = 1; i < 8; i++) drawSign(kModeButtons[i], kModeButtons[i].label, f == i, sPressed == i, 0.46f, kIcons[i - 1]);
			break;
		}
		case SCREEN_OPTIONS: {
			drawMenuBack();
			int f = sFocus[SCREEN_OPTIONS];
			drawHeader("Options");
			drawBackArrow(f == 0, sPressed == 0);
			for (int i = 1; i < (sOptionsFromPause ? 4 : 5); i++) drawSign(kOptionButtons[i], kOptionButtons[i].label, f == i, sPressed == i, 0.56f);
			break;
		}
		case SCREEN_SECTION:
			drawMenuBack();
			drawRows();
			// The header covers rows as they scroll up beneath it.
			gfx::rect(0, 0, 320, 44, gfx::themeBar());
			drawHeader(kSections[sSection].title);
			drawBackArrow(sFocus[SCREEN_SECTION] < 0, sPressed == 0);
			break;
		case SCREEN_CREDITS:
			drawMenuBack();
			drawHeader("Credits");
			drawBackArrow(true, sPressed == 0);
			gfx::sign(sprites_plank_idx, 10, 50, 300, 176, COL_PLANK);
			gfx::text("PROGRAMMING", 90, 57, 0.3f, COL_ACCENT, ALIGN_CENTER);
			gfx::text("Claude", 90, 69, 0.46f, COL_CREAM, ALIGN_CENTER);
			gfx::text("PROMPT ENGINEERING", 225, 57, 0.3f, COL_ACCENT, ALIGN_CENTER);
			gfx::text("RealAnxiousGit", 225, 69, 0.46f, COL_CREAM, ALIGN_CENTER);
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
				line("Art, sound and music made from code.", 207, COL_DIM);
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
			drawBackArrow(true, sPressed == 0);
			gfx::sign(sprites_plank_idx, 8, 48, 304, 186, COL_PLANK);
			// One size for every line, narrow enough to stay clear of the plank's screws.
			float s = 0.36f;
			for (int i = 0; i < 7; i++) {
				float w = gfx::textWidth(kLines[i], s);
				if (w > 262) s *= 262 / w;
			}
			for (int i = 0; i < 7; i++)
				gfx::text(kLines[i], 160, 58 + i * 24, s, COL_CREAM, ALIGN_CENTER);
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
			for (int i = 1; i < 4; i++) drawSign(kPauseButtons[i], kPauseButtons[i].label, f == i, sPressed == i);
			break;
		}
		case SCREEN_RESULT: {
#if PORT_TWO_SCREENS
			sGame.drawField(sBlade);
#endif
			int f = sFocus[SCREEN_RESULT];
			drawBackArrow(f == 0, sPressed == 0);
			for (int i = 1; i < 3; i++) drawSign(kResultButtons[i], kResultButtons[i].label, f == i, sPressed == i);
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
}

// ---------------------------------------------------------------- flow
static Mode sStartMode = MODE_CLASSIC;

static void startGame() {
	sGame.start(sStartMode);
	sBlade.reset();
	gfx::setShake(0, 0);
	sScreen = SCREEN_GAME;
	sPressed = -1;
}

static bool sLeaving = false;

static void toModes() {
	sScreen = SCREEN_MODES;
	sPressed = -1;
	gfx::setShake(0, 0);
	audio::fuse(false);
	audio::music(MUSIC_MENU);
}

static void toTitle() {
	sOptionsFromPause = false;
	toModes();
	sScreen = SCREEN_TITLE;
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

	// The game keeps moving during a fade, but menus ignore input while anything is sliding.
	const bool blocked = inputBlocked();
	switch (sScreen) {
		case SCREEN_TITLE: {
			if (blocked) break;
			int t = menuInput(kTitleButtons, 4, SCREEN_TITLE, in, wasTouching);
			if (t == 0) goTo(SCREEN_MODES, 1);
			else if (t == 1) goTo(SCREEN_OPTIONS, 1);
			else if (t == 2) goTo(SCREEN_CREDITS, 1);
			else if (t == 3) goTo(SCREEN_HOWTO, 1);
#if PORT_ANDROID
			if (in.down & KEY_B) sLeaving = true;  // Back on the first screen leaves, as on any phone app
#endif
			break;
		}
		case SCREEN_MODES: {
			if (blocked) break;
			int t = menuInput(kModeButtons, 8, SCREEN_MODES, in, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 0) goTo(SCREEN_TITLE, -1);
			else if (t > 0) {
				sStartMode = (Mode)(t - 1);
				fadeThen(startGame);
			}
			break;
		}
		case SCREEN_OPTIONS: {
			if (blocked) break;
			// Mid-game, the data section stays out of reach: its speed test would replace the paused game.
			int t = menuInput(kOptionButtons, sOptionsFromPause ? 4 : 5, SCREEN_OPTIONS, in, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 0) {
				if (sOptionsFromPause) goTo(SCREEN_PAUSE, -1);
				else goTo(SCREEN_TITLE, -1);
			}
			else if (t > 0) {
				sSection = t - 1;
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
			if (f >= 0 && rows[f].value == &gSave.blade) {
				Input fake{};
				fake.touching = true;
				fake.tx = (int)(200 + sinf(sTime * 3.1f) * 60);
				fake.ty = (int)(50 + sinf(sTime * 6.2f) * 30);
				sPreviewBlade.update(fake);
			}
			break;
		}
		case SCREEN_CREDITS:
		case SCREEN_HOWTO: {
			if (blocked) break;
			int& f = sFocus[sScreen];
			int t = touchButtons(kOnlyBack, 1, in, wasTouching, f);
			if (t == 0 || (in.down & (KEY_A | KEY_B))) {
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
			sBlade.update(in);
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
				fadeThen(quitRun);
			}
			break;
		}
		case SCREEN_RESULT: {
			sGame.update(sBlade);  // keeps debris falling behind the buttons
			sGame.kickParticles();
			if (blocked) break;
			// A short lock-out stops a frantic swipe from tapping a sign by accident. Keys can't do that.
			if (sGame.phaseT <= (in.touching || wasTouching ? 0.7f : 0.25f)) break;
			int t = menuInput(kResultButtons, 3, SCREEN_RESULT, in, wasTouching);
			if (in.down & KEY_B) {
				audio::click();
				t = 0;
			}
			if (t == 1) {
				sStartMode = sGame.mode;
				fadeThen(startGame);
			} else if (t == 0 || t == 2) {
				fadeThen(toModes);
			}
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

#if PORT_TWO_SCREENS
static void drawFrame() {
	float e = sSlideT * sSlideT * (3 - 2 * sSlideT);  // eases in and out
	gfx::beginFrame();
	for (int eye = 0; eye < 2; eye++) {
		if (!gfx::beginTop(eye)) continue;
		gfx::setBase(0, 0);
		drawTopBackground();
		if (sSliding) {
			gfx::setBase(-sSlideDir * kSlideTop * e, 0);
			drawTopContent(sSlideFrom);
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
		drawBottomContent(sSlideFrom);
		gfx::setBase(sSlideDir * kSlideBottom * (1 - e), 0);
		drawBottomContent(sScreen);
		gfx::setBase(0, 0);
	} else {
		drawBottomContent(sScreen);
	}
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
		sGame.drawHud();
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
			content(sSlideFrom);
			gfx::setBase(sSlideDir * slide * (1 - e), 0);
			content(sScreen);
			gfx::setBase(0, 0);
		} else {
			content(sScreen);
		}
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
	sScreen = SCREEN_TITLE;
#if PORT_3DS
	sLastTick = osGetTime();
#endif
}

bool app::init() {
	saveLoad();
#ifdef TEST_ART
	gSave.artStyle = TEST_ART;
#endif
#ifdef TEST_DIFF
	gSave.difficulty = TEST_DIFF;
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

bool app::frame(Input& in, int steps) {
	for (int i = 0; i < steps; i++) {
#ifdef AUTOTEST
		autotestInput(in);
		if (autotestDone()) return false;
#elif PORT_3DS
		// START+SELECT from the title quits, for builds launched from the Homebrew Launcher.
		if (sScreen == SCREEN_TITLE && (in.held & KEY_START) && (in.held & KEY_SELECT)) return false;
#endif

		updateFrame(in, sWasTouching);
		sWasTouching = in.touching;
		if (sLeaving) return false;
		// A press belongs to the first step only.
		in.down = 0;
		in.repeat = 0;
		in.wheel = 0;
	}

	// New scenery, boards and models load here, between frames.
	gfx::sync();
	m3d::sync();
	audio::update(steps * kDt);

	drawFrame();
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
