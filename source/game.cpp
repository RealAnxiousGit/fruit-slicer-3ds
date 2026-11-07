#include "game.h"

#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "fruit.h"
#include "jobs.h"
#include "model3d.h"
#include "progress.h"
#include "render.h"
#include "sprites.h"

Rng gRng;
Rng gSpawn;
bool gStickInUse = false;

const ModeInfo kModes[MODE_COUNT] = {
	{"Classic", "Three misses and you are out.", "A bomb ends the run."},
	{"Arcade", "60 seconds. Bombs cost 10 points.", "Grab power-ups. Big finale fruit!"},
	{"Zen", "90 calm seconds. No bombs.", "No misses. Just slice."},
	{"Frenzy", "30 seconds of nonstop fruit.", "Bombs take 5 seconds."},
	{"Sudden Death", "One miss or one bomb.", "Fruit is worth double."},
	{"Minefield", "Bombs everywhere. Misses are free.", "How long can you last?"},
	{"Combo Rush", "45 seconds. Doubled combo bonus.", "Finish on the big finale fruit!"},
	{"Daily Fruit", "60 seconds, the same run for everyone.", "A new one every day. Bombs cost 10."},
};
const char* modeName(Mode m) { return m < MODE_COUNT ? kModes[m].name : "Practice"; }

enum CutClass { CUT_CRISP, CUT_JUICY, CUT_SOFT, CUT_MELON, CUT_SHELL };

struct FruitDef {
	int whole, left, right;
	u32 juice;
	CutClass cut;
	int mesh;
};
static const FruitDef kFruit[] = {
	{fruit_apple_idx, fruit_apple_l_idx, fruit_apple_r_idx, 0xFF6AD8F5u, CUT_CRISP, m3d::MESH_APPLE},
	{fruit_orange_idx, fruit_orange_l_idx, fruit_orange_r_idx, 0xFF2E9CFFu, CUT_JUICY, m3d::MESH_ORANGE},
	{fruit_lemon_idx, fruit_lemon_l_idx, fruit_lemon_r_idx, 0xFF52E6FFu, CUT_JUICY, m3d::MESH_LEMON},
	{fruit_pear_idx, fruit_pear_l_idx, fruit_pear_r_idx, 0xFF8AEBDDu, CUT_CRISP, m3d::MESH_PEAR},
	{fruit_plum_idx, fruit_plum_l_idx, fruit_plum_r_idx, 0xFF9A4AC2u, CUT_SOFT, m3d::MESH_PLUM},
	{fruit_melon_idx, fruit_melon_l_idx, fruit_melon_r_idx, 0xFF4A3CF0u, CUT_MELON, m3d::MESH_MELON},
	{fruit_kiwi_idx, fruit_kiwi_l_idx, fruit_kiwi_r_idx, 0xFF52D69Au, CUT_SOFT, m3d::MESH_KIWI},
	{fruit_berry_idx, fruit_berry_l_idx, fruit_berry_r_idx, 0xFF5A32F0u, CUT_SOFT, m3d::MESH_BERRY},
	{fruit_peach_idx, fruit_peach_l_idx, fruit_peach_r_idx, 0xFF80B0FFu, CUT_SOFT, m3d::MESH_PEACH},
	{fruit_pineapple_idx, fruit_pineapple_l_idx, fruit_pineapple_r_idx, 0xFF50E0FFu, CUT_JUICY, m3d::MESH_PINEAPPLE},
	{fruit_coconut_idx, fruit_coconut_l_idx, fruit_coconut_r_idx, 0xFFF0F5FFu, CUT_SHELL, m3d::MESH_COCONUT},
	// 1.0: more everyday fruit.
	{fruit_banana_idx, fruit_banana_l_idx, fruit_banana_r_idx, 0xFF8AF0FFu, CUT_SOFT, m3d::MESH_BANANA},
	{fruit_mango_idx, fruit_mango_l_idx, fruit_mango_r_idx, 0xFF28AAFFu, CUT_JUICY, m3d::MESH_MANGO},
	{fruit_cherry_idx, fruit_cherry_l_idx, fruit_cherry_r_idx, 0xFF3214C8u, CUT_JUICY, m3d::MESH_CHERRY},
	{fruit_dragon_idx, fruit_dragon_l_idx, fruit_dragon_r_idx, 0xFFA03CE6u, CUT_SOFT, m3d::MESH_DRAGON},
	// 1.0: trick fruit, each with its own rule. Rarer than the rest.
	{fruit_spiky_idx, fruit_spiky_l_idx, fruit_spiky_r_idx, 0xFF78E6DCu, CUT_SHELL, m3d::MESH_SPIKY},
	{fruit_grapes_idx, fruit_grapes_l_idx, fruit_grapes_r_idx, 0xFFAA3C82u, CUT_JUICY, m3d::MESH_GRAPES},
	{fruit_grape_idx, fruit_grape_l_idx, fruit_grape_r_idx, 0xFFAA3C82u, CUT_JUICY, m3d::MESH_GRAPE},
	{fruit_honey_idx, fruit_honey_l_idx, fruit_honey_r_idx, 0xFF28B4FAu, CUT_MELON, m3d::MESH_HONEY},
	{fruit_jelly_idx, fruit_jelly_l_idx, fruit_jelly_r_idx, 0xFFC88CFFu, CUT_SOFT, m3d::MESH_JELLY},
	{fruit_chili_idx, fruit_chili_l_idx, fruit_chili_r_idx, 0xFF1E3CF0u, CUT_CRISP, m3d::MESH_CHILI},
};
static constexpr int kFruitKinds = sizeof(kFruit) / sizeof(kFruit[0]);
static_assert(kFruitKinds <= kMaxFruitKinds, "the Fruit Journal needs a count for every fruit");
static_assert(kFruitKinds == progress::kNamedFruit, "every fruit needs a name in progress.cpp");
static constexpr int kCoconut = 10;
static constexpr int kFirstNew = 11, kNewNormal = 4;  // banana to dragon fruit
enum { kSpiky = 15, kGrapes, kGrape, kHoney, kJelly, kChili };

// Trick fruit start after the first half minute, about one fruit in twenty. Zen never throws the spiky one.
// Arcade's power-ups, picked evenly.
static const ObjKind kPowers[] = {OBJ_FREEZE, OBJ_DOUBLE, OBJ_FRENZY, OBJ_MAGNET, OBJ_WIDE, OBJ_SHIELD, OBJ_TIME};
static constexpr int kPowerCount = sizeof kPowers / sizeof kPowers[0];
static constexpr float kTrickChance = 0.05f;
static int pickTrick(Mode mode, Rng& rng) {
	static const int kTricks[] = {kSpiky, kGrapes, kHoney, kJelly, kChili};
	int n = mode == MODE_ZEN ? 4 : 5;
	return kTricks[(mode == MODE_ZEN ? 1 : 0) + rng.below(n)];
}

// Any everyday fruit but the coconut, which has its own odds.
static int normalFruit(Rng& rng) {
	int i = rng.below(kCoconut + kNewNormal);
	return i < kCoconut ? i : kFirstNew + (i - kCoconut);
}
static const FruitDef kGold = {fruit_gold_idx, fruit_gold_l_idx, fruit_gold_r_idx, 0xFF40D8FFu, CUT_JUICY, m3d::MESH_GOLD};

static constexpr float kSpawnY = Game::kThrowY;
static constexpr float kKillY = 276.0f;
// Bomb and fruit radii are 19 and 23, so paths this far apart never visually touch.
static constexpr float kWantClearance = 62.0f;
static constexpr float kComboFade = 0.35f;  // seconds the COMBO text takes to fade once the meter runs dry
static constexpr float kFeverDrain = 1.0f / 18.0f;  // full to empty in 18 seconds, on Normal at the default slider
static constexpr float kFrenzySeconds = 7.0f;
static constexpr float kAfterFrenzy = 0.25f;  // the meter restarts here, so a good run can chain
// About the longest a fruit thrown by a Frenzy stays in the air. Two screens throw higher.
static constexpr float kFrenzyGrace = PORT_TWO_SCREENS ? 3.5f : 2.5f;

// Top-screen placement of a field position: the top screen hangs above the bottom one, with a gap.
static constexpr float kTopOffsetX = 40.0f;
static constexpr float kTopOffsetY = 264.0f;

struct DifficultyInfo {
	float gravity, interval, bombs;
	int extra;
	float feverDrain, feverGain;  // how fast the Frenzy meter empties, and how much a slice adds
	int bombsPerWave;
};
static const DifficultyInfo kDifficulty[DIFF_COUNT] = {
	{0.88f, 1.2f, 0.6f, -1, 0.55f, 1.5f, 1},
	{1.0f, 1.0f, 1.0f, 0, 1.0f, 1.0f, 1},
	{1.12f, 0.85f, 1.25f, 1, 1.5f, 0.65f, 2},
	{1.25f, 0.7f, 1.6f, 2, 2.0f, 0.5f, 2},
};

int fruitSprite(int fruit) { return kFruit[fruit >= 0 && fruit < kFruitKinds ? fruit : 0].whole; }

u32 hueColor(float t) {
	t = t - floorf(t);
	float h = t * 6.0f;
	float f = h - floorf(h);
	u8 up = (u8)(f * 255), down = (u8)((1 - f) * 255);
	switch ((int)h % 6) {
		case 0: return rgba(255, up, 60);
		case 1: return rgba(down, 255, 60);
		case 2: return rgba(60, 255, up);
		case 3: return rgba(60, down, 255);
		case 4: return rgba(up, 60, 255);
		default: return rgba(255, 60, down);
	}
}

// Calm Effects: colours that cycle through the rainbow hold still instead.
static u32 cycleColor(float t) { return gSave.calmFx ? COL_ACCENT : hueColor(t); }

// ---------------------------------------------------------------- Sound Captions
static constexpr int kCaptionLines = 3;
static constexpr float kCaptionTime = 1.6f;
static const char* sCaptions[kCaptionLines];
static float sCaptionT[kCaptionLines];

void showCaption(const char* words) {
	if (!gSave.captions) return;
	// The same words again just stay up longer.
	if (sCaptions[0] == words) {
		sCaptionT[0] = kCaptionTime;
		return;
	}
	for (int i = kCaptionLines - 1; i > 0; i--) {
		sCaptions[i] = sCaptions[i - 1];
		sCaptionT[i] = sCaptionT[i - 1];
	}
	sCaptions[0] = words;
	sCaptionT[0] = kCaptionTime;
}

static void tickCaptions(float dt) {
	for (auto& t : sCaptionT) t -= dt;
}

static void clearCaptions() {
	for (int i = 0; i < kCaptionLines; i++) {
		sCaptions[i] = nullptr;
		sCaptionT[i] = 0;
	}
}

// Newest at the bottom of the field, older ones above it, each fading out.
static void drawCaptions(float bottom, float cx) {
	const float s = 0.36f * uiScale();
	for (int i = 0; i < kCaptionLines; i++) {
		if (!sCaptions[i] || sCaptionT[i] <= 0) continue;
		float a = clampf(sCaptionT[i] * 3, 0, 1);
		float w = gfx::textWidth(sCaptions[i], s), h = 22 * s / 0.36f;
		float y = bottom - (i + 1) * (h + 3);
		gfx::rect(cx - w / 2 - 6, y, w + 12, h, rgba(0, 0, 0, (u8)(170 * a)));
		gfx::text(sCaptions[i], cx, y + 2, s, withAlpha(COL_CREAM, a), ALIGN_CENTER);
	}
}

// ---------------------------------------------------------------- Colour-blind and High Contrast marks
// A bomb that never relies on red: a ring of dashes that turns, and spikes all round it.
static void drawBombMark(float x, float y, float r, float age) {
	const int dashes = 12;
	const float ring = r * 2.0f, thick = 3.5f, spin = age * 1.6f;  // outside the bomb's own knobs
	const u32 light = gSave.colorBlind == 3 ? rgba(255, 255, 255) : rgba(255, 225, 40), dark = rgba(10, 10, 10);
	for (int i = 0; i < dashes; i++) {
		float a0 = spin + i * 6.2832f / dashes, a1 = a0 + 0.32f;
		float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
		u32 c = (i & 1) ? dark : light;
		float ix0 = x + c0 * (ring - thick), iy0 = y + s0 * (ring - thick), ox0 = x + c0 * (ring + thick), oy0 = y + s0 * (ring + thick);
		float ix1 = x + c1 * (ring - thick), iy1 = y + s1 * (ring - thick), ox1 = x + c1 * (ring + thick), oy1 = y + s1 * (ring + thick);
		gfx::tri(ix0, iy0, c, ox0, oy0, c, ox1, oy1, c);
		gfx::tri(ix0, iy0, c, ox1, oy1, c, ix1, iy1, c);
	}
	const int spikes = 8;
	for (int i = 0; i < spikes; i++) {
		float a = -spin * 0.5f + i * 6.2832f / spikes, w = 0.12f;
		gfx::tri(x + cosf(a - w) * r * 2.15f, y + sinf(a - w) * r * 2.15f, light, x + cosf(a) * r * 2.6f, y + sinf(a) * r * 2.6f, light,
		         x + cosf(a + w) * r * 2.15f, y + sinf(a + w) * r * 2.15f, light);
	}
}

// Frenzy throws more than anything else in the game. Lighter ports trim it so it can't overrun the hardware.
static bool sLightFrenzy;
static constexpr int kLightMaxObjs = 14;
static constexpr int kLightMaxHalves = 24;

static int juiceCount(int base) {
	if (sLightFrenzy) base /= 2;
	// A New 3DS has the headroom for a bigger splash on the top setting.
	const float full = gIsNew3ds ? 1.3f : 1.0f;
	const float scale[] = {0.0f, 0.35f, 0.7f, full};
	return (int)(base * scale[gSave.effects & 3]);
}

static inline bool use3D() { return gSave.artStyle == ART_3D && m3d::ready(); }

// ---------------------------------------------------------------- lifecycle
void Game::start(Mode m) {
	clearCaptions();
	jobs::wait();
	m3d::cutReleaseAll();
	memset((void*)this, 0, sizeof(Game));
	mode = m;
	sLightFrenzy = PORT_LIGHT_FRENZY && m == MODE_FRENZY;
	phase = PHASE_READY;
	assisted = assistedRun();
	best = (assisted ? gSave.bestAssisted : gSave.best)[m];
	gravity = 300;
	waveTimer = 0.2f;
	switch (m) {
		case MODE_CLASSIC: maxStrikes = gSave.misses; break;
		case MODE_ARCADE: timeLeft = 60; break;
		case MODE_ZEN: timeLeft = 90; break;
		case MODE_FRENZY: timeLeft = 30; break;
		case MODE_SUDDEN: maxStrikes = 1; break;
		case MODE_COMBO: timeLeft = 45; break;
		case MODE_DAILY: timeLeft = 60; break;
		default: break;
	}
	// Daily Fruit throws the day's sequence; every other run gets a fresh one.
	gSpawn.s = gRng.next() | 1;
	if (m == MODE_DAILY) {
		day = progress::today();
		gSpawn.s = progress::dailySeed(day);
	}
	memcpy(journalStart, gSave.fruitSliced, sizeof journalStart);
	if (m < MODE_COUNT && gSave.modePlays[m] < 0xFFFF) gSave.modePlays[m]++;
	if (m == MODE_PRACTICE) practiceStart();
#ifdef TEST_BALANCE
	// Measures what each mode throws, so nothing ends a game early.
	if (timeLeft <= 0 && m != MODE_PRACTICE) timeLeft = 120;
	maxStrikes = 0;
#endif
	audio::music(m == MODE_ZEN ? MUSIC_ZEN : MUSIC_PLAY);
}

int Game::diffIndex() const { return mode == MODE_DAILY ? DIFF_NORMAL : gSave.difficulty % DIFF_COUNT; }
float Game::slider(u8 value) const { return mode == MODE_DAILY ? 50.0f : (float)value; }

float Game::difficulty() const {
	// Skill and time both push difficulty; each mode ramps at its own pace.
	float rampTime = 150, rampScore = 400;
	switch (mode) {
		case MODE_ARCADE: rampTime = 60; rampScore = 400; break;
		case MODE_ZEN: rampTime = 240; rampScore = 1200; break;
		case MODE_FRENZY: return clampf(0.6f + elapsed / 30.0f * 0.4f, 0, 1);
		case MODE_SUDDEN: rampTime = 120; rampScore = 300; break;
		case MODE_MINEFIELD: rampTime = 90; rampScore = 200; break;
		case MODE_COMBO: rampTime = 45; rampScore = 600; break;
		case MODE_DAILY: return clampf(dailyWave / 27.0f, 0, 1);  // by wave, so nothing the player does changes what is thrown
		default: break;
	}
	return clampf(elapsed / rampTime + score / rampScore, 0, 1);
}

// ---------------------------------------------------------------- spawning
void Game::queue(float delay, float x, float vx, float vy, ObjKind kind, int fruit) {
	for (auto& p : pending) {
		if (!p.on) {
			p = {delay, x, vx, vy, kind, (u8)fruit, true};
			return;
		}
	}
}

void Game::launch(const Pending& p) {
	if (sLightFrenzy && p.kind != OBJ_BOMB) {
		int inAir = 0;
		for (const auto& o : objs) inAir += o.on;
		if (inAir >= kLightMaxObjs) return;
	}
	for (auto& o : objs) {
		if (o.on) continue;
		o = {};
		o.on = true;
		o.kind = p.kind;
		o.fruit = p.fruit;
		o.x = p.x;
		o.y = kSpawnY;
		o.vx = p.vx;
		o.vy = p.vy;
		o.rot = gRng.range(0, 6.28f);
		// Spin follows the direction of travel, like a tossed object.
		o.rotv = (p.vx >= 0 ? 1.0f : -1.0f) * gRng.range(1.2f, 4.2f);
		o.rx = gRng.range(-0.5f, 0.5f);
		o.ry = gRng.range(0, 6.28f);
		o.rxv = gRng.range(-1.6f, 1.6f);
		o.ryv = gRng.range(-2.4f, 2.4f);
		o.radius = p.kind == OBJ_BOMB ? 19 : (p.kind == OBJ_COIN ? 18 : (p.kind == OBJ_FRUIT && p.fruit == kGrape ? 13 : 23));
		if (p.kind == OBJ_COIN) {
			o.rotv = (p.vx >= 0 ? 1.0f : -1.0f) * gRng.range(6.5f, 8.5f);  // end over end, as a flipped coin
			audio::play(SFX_COIN, 0.75f, fieldPan(p.x), gRng.range(0.96f, 1.06f));
		}
		o.hits = (p.kind == OBJ_FRUIT && p.fruit == kCoconut) ? 2 : 1;
		audio::play(SFX_THROW, 0.3f, fieldPan(p.x), gRng.range(0.9f, 1.15f));
		if (p.kind == OBJ_BOMB) showCaption("Bomb!");
#ifdef AUTOTEST
		statThrows++;
		if (feverT > 0) statFeverThrows++;
		else {
			const int at = elapsed < 30 ? 0 : (elapsed < 60 ? 1 : 2);
			if (p.kind == OBJ_BOMB) statBombsAt[at]++;
			else statFruitAt[at]++;
		}
		u32 live = 0;
		for (const auto& other : objs) live += other.on;
		if (live > statPeakObjs) statPeakObjs = live;
#endif
		return;
	}
#ifdef AUTOTEST
	statDropped++;
#endif
}

// Closest a throw comes to anything of the opposite type (bomb vs fruit), over its whole flight.
float Game::clearance(float x, float vx, float vy, float delay, bool bomb) const {
	float closest = 1e6f;
	for (float t = delay; t < delay + 2.8f; t += 0.14f) {
		float tt = t - delay;
		float px = x + vx * tt, py = kSpawnY + vy * tt + 0.5f * gravity * tt * tt;
		if (tt > 0.2f && py > kSpawnY) break;
		for (const auto& o : objs) {
			if (!o.on || (o.kind == OBJ_BOMB) == bomb) continue;
			float ox = o.x + o.vx * t, oy = o.y + o.vy * t + 0.5f * gravity * t * t;
			if (oy > kKillY) continue;
			float dx = ox - px, dy = oy - py, dist = dx * dx + dy * dy;
			if (dist < closest) closest = dist;
		}
		for (const auto& q : pending) {
			if (!q.on || (q.kind == OBJ_BOMB) == bomb || t < q.delay) continue;
			float qt = t - q.delay;
			float ox = q.x + q.vx * qt, oy = kSpawnY + q.vy * qt + 0.5f * gravity * qt * qt;
			if (oy > kKillY) continue;
			float dx = ox - px, dy = oy - py, dist = dx * dx + dy * dy;
			if (dist < closest) closest = dist;
		}
	}
	return closest >= 1e6f ? 1e3f : sqrtf(closest);
}

// Tries a few throws and keeps the one that stays furthest from the other type.
void Game::pickThrow(bool bomb, float delay, float& x, float& vx, float& vy) {
	float bestClear = -1;
#ifdef NO_BOMB_SPACING
	static constexpr int kTries = 1;
#else
	static constexpr int kTries = 7;
#endif
	for (int i = 0; i < kTries; i++) {
		// About half the throws leave the top of the board and carry on up the top screen in full view,
		// peaking below the score sign, before falling back down onto the board.
		float apexY = gRng.range(kFieldTop + 20, kFieldTop + 90);
		if (PORT_TWO_SCREENS && gRng.unit() < 0.5f) apexY = gRng.range(-170, -60);
		float cvy = -sqrtf(2.0f * gravity * (kSpawnY - apexY));
		float cx = gRng.range(36, kFieldW - 36);
		float air = -2.0f * cvy / gravity;
		float cvx = clampf((gRng.range(50, kFieldW - 50) - cx) / air, -75, 75);
		float c = kTries > 1 ? clearance(cx, cvx, cvy, delay, bomb) : 0;
		if (c > bestClear) {
			bestClear = c;
			x = cx;
			vx = cvx;
			vy = cvy;
		}
		if (c >= kWantClearance) break;
	}
}

bool Game::bombAround() const {
	for (const auto& o : objs)
		if (o.on && o.kind == OBJ_BOMB) return true;
	for (const auto& q : pending)
		if (q.on && q.kind == OBJ_BOMB) return true;
	return false;
}

// Choreographed waves built for big combos. They always stay bomb-free.
bool Game::spawnPattern(float d) {
	if (bombAround()) return false;
	const float rise = 200;
	const float vy = -sqrtf(2.0f * gravity * rise);
	switch (gSpawn.below(4)) {
		case 0: {  // Row: rises level, so one straight swipe can take them all.
			int n = 4 + (int)(d * 3) + gSpawn.below(2);
			float gap = n > 1 ? (kFieldW - 60.0f) / (n - 1) : 0;
			float wobble = gRng.range(-14, 14);
			for (int i = 0; i < n; i++) queue(0, 30 + gap * i, wobble, vy, OBJ_FRUIT, normalFruit(gSpawn));
			break;
		}
		case 1: {  // Fountain: a fan from the middle.
			int n = 5 + (int)(d * 3);
			for (int i = 0; i < n; i++) {
				float t = n > 1 ? (float)i / (n - 1) : 0.5f;
				queue(0.07f * i, kFieldW * 0.5f + (t - 0.5f) * 40, lerpf(-85, 85, t), vy * gRng.range(0.92f, 1.0f), OBJ_FRUIT, normalFruit(gSpawn));
			}
			break;
		}
		case 2: {  // Crossfire: alternating from both edges.
			int n = 6 + (int)(d * 2);
			for (int i = 0; i < n; i++) {
				bool left = i % 2 == 0;
				queue(0.13f * i, left ? -14.0f : kFieldW + 14.0f, (left ? 1 : -1) * gRng.range(95, 130), vy * gRng.range(0.86f, 0.98f), OBJ_FRUIT, normalFruit(gSpawn));
			}
			break;
		}
		default: {  // Staircase: rising row, easy to catch with a diagonal stroke.
			int n = 5 + (int)(d * 2);
			for (int i = 0; i < n; i++) queue(0.1f * i, 34 + (kFieldW - 68.0f) * i / (n - 1), 0, vy, OBJ_FRUIT, normalFruit(gSpawn));
			break;
		}
	}
	return true;
}

void Game::spawnWave() {
	// Daily Fruit seeds every wave from the day and its number, so whatever happened in earlier waves
	// (a pattern held back by a bomb, say), wave n throws the same for everyone.
	if (mode == MODE_DAILY) gSpawn.s = progress::dailySeed(day + 0x10000u * ++dailyWave);
	float d = difficulty();
#ifdef AUTOTEST
	statWaves++;
	statPendingMask = 0;
	for (int i = 0; i < kMaxPending; i++) statPendingMask |= (u32)pending[i].on << i;
#endif
	// Low gravity gives long, floaty arcs with plenty of hang time near the top.
	const DifficultyInfo& level = kDifficulty[diffIndex()];
	gravity = lerpf(300, 430, d) * (mode == MODE_ZEN ? 0.9f : 1.0f) * level.gravity;

	float patternChance = lerpf(0.10f, 0.28f, d);
	if (mode == MODE_COMBO) patternChance = 0.7f;
	if (mode == MODE_MINEFIELD || mode == MODE_SUDDEN) patternChance = 0.0f;
	if (mode == MODE_FRENZY) patternChance = 0.06f;  // waves are so frequent that this is still one every few seconds
	// Daily Fruit counts its time in waves, so slow motion or a chili rush never changes what comes.
	const float t = mode == MODE_DAILY ? dailyWave * 2.2f : elapsed;
	const bool pattern = t > 6 && gSpawn.unit() < patternChance;
	if (pattern && mode == MODE_DAILY && bombAround()) {
		// Patterns wait for bombs to clear. Daily Fruit holds the same wave back a moment instead of swapping it.
		dailyWave--;
		waveTimer = 0.4f;
		return;
	}
	if (pattern && spawnPattern(d)) {
		waveTimer = (mode == MODE_FRENZY ? 0.7f : lerpf(3.1f, 2.1f, d)) * level.interval;
		maybeCoin(d);
#ifdef AUTOTEST
		logWave();
#endif
		return;
	}

	float bombChance = 0;
	int maxExtra = (int)lerpf(1, 5, d);
	switch (mode) {
		case MODE_CLASSIC: bombChance = lerpf(0.05f, 0.24f, d); break;
		case MODE_ARCADE: bombChance = lerpf(0.08f, 0.20f, d); maxExtra += 1; break;
		case MODE_FRENZY: bombChance = lerpf(0.06f, 0.10f, d); maxExtra = 1; break;
		case MODE_SUDDEN: bombChance = lerpf(0.05f, 0.18f, d); maxExtra = (int)lerpf(0, 3, d); break;
		case MODE_MINEFIELD: bombChance = lerpf(0.26f, 0.45f, d); maxExtra = (int)lerpf(1, 4, d); break;
		case MODE_COMBO: bombChance = lerpf(0.12f, 0.20f, d); maxExtra += 2; break;
		case MODE_DAILY: bombChance = lerpf(0.08f, 0.20f, d); maxExtra += 1; break;
		default: break;
	}
	float bombSetting = slider(gSave.bombs) / 50.0f;
	if (mode == MODE_MINEFIELD && bombSetting < 0.5f) bombSetting = 0.5f;  // with no bombs, this mode could never end
	// Even a minefield has to leave some fruit worth going for.
	const float bombLimit = mode == MODE_MINEFIELD ? (bombSetting > 1 ? 0.65f * bombSetting : 0.65f) : 0.9f;
	bombChance = clampf(bombChance * level.bombs * bombSetting, 0, bombLimit > 0.9f ? 0.9f : bombLimit);
	maxExtra = maxExtra + level.extra < 0 ? 0 : maxExtra + level.extra;
	if (mode == MODE_MINEFIELD && maxExtra > 4) maxExtra = 4;
	// One touch ends Sudden Death, so it never throws two at once.
	const int bombsAllowed = mode == MODE_MINEFIELD ? 8 : (mode == MODE_SUDDEN ? 1 : level.bombsPerWave);
	int count = 1 + gSpawn.below(maxExtra + 1);
	float stagger = gSpawn.below(3) == 0 ? lerpf(0.22f, 0.11f, d) : 0.0f;
	int bombsUsed = 0;

	// Decide the types first, so fruit can route around bombs from the same wave.
	ObjKind kinds[8];
	if (count > 8) count = 8;
	for (int i = 0; i < count; i++) {
		ObjKind kind = OBJ_FRUIT;
		if (gSpawn.unit() < bombChance && bombsUsed < bombsAllowed) {
			kind = OBJ_BOMB;
			bombsUsed++;
#ifdef AUTOTEST
		} else if (mode == MODE_ARCADE && gSpawn.unit() < 0.3f) {
			// Test builds cycle the power-ups so that every one gets exercised.
			static int nextPower = 0;
			kind = kPowers[nextPower];
			nextPower = (nextPower + 1) % kPowerCount;
#else
		} else if (mode == MODE_ARCADE && gSpawn.unit() < 0.09f) {
			kind = kPowers[gSpawn.below(kPowerCount)];
#endif
		} else if (gSpawn.unit() < 0.035f) {
			kind = OBJ_GOLD;
		}
		kinds[i] = kind;
	}
	// Bombs go first; every fruit is then placed against them.
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < count; i++) {
			bool isBomb = kinds[i] == OBJ_BOMB;
			if ((pass == 0) != isBomb) continue;
			float x = 160, vx = 0, vy = -300;
			pickThrow(isBomb, stagger * i, x, vx, vy);
			int fruit = gSpawn.unit() < 0.09f ? kCoconut : normalFruit(gSpawn);
			if (kinds[i] == OBJ_FRUIT && t > 30 && gSpawn.unit() < kTrickChance) fruit = pickTrick(mode, gSpawn);
			queue(stagger * i, x, vx, vy, kinds[i], fruit);
		}
	}
	float interval = lerpf(2.1f, 0.95f, d);
	if (mode == MODE_FRENZY) interval = lerpf(0.42f, 0.30f, d);
	if (mode == MODE_ZEN) interval *= 0.95f;
	if (mode == MODE_SUDDEN) interval *= 0.85f;
	waveTimer = interval * level.interval + stagger * count;
	maybeCoin(d);
#ifdef AUTOTEST
	logWave();
#endif
}

// Now and then a coin flies up with a wave: a few more on harder settings, never beside a bomb, and only
// so many a run. Frenzy throws waves so often that each one carries less chance.
void Game::maybeCoin(float d) {
	static constexpr int kCoinCap = 24;
	if (coinsThrown >= kCoinCap || phase != PHASE_PLAY) return;
	float chance = mode == MODE_FRENZY ? 0.03f : 0.075f;
	chance *= 1 + 0.15f * diffIndex() + 0.3f * d;
#ifdef TEST_COINS
	chance = 1;  // test builds: a coin with every wave, to see and hear them
#endif
	if (gSpawn.unit() >= chance) return;
	float x = 160, vx = 0, vy = -300;
	pickThrow(false, 0.35f, x, vx, vy);
	queue(0.35f, x, vx, vy, OBJ_COIN, 0);
	coinsThrown++;
}

#ifdef AUTOTEST
void Game::logWave() const {
	if (mode != MODE_DAILY) return;
	char s[kMaxPending + 1];
	int n = 0;
	for (int i = 0; i < kMaxPending; i++) {
		const Pending& p = pending[i];
		if (!p.on || (statPendingMask >> i & 1)) continue;
		char c = p.kind == OBJ_BOMB ? '*' : (p.kind == OBJ_COIN ? '$' : (p.kind == OBJ_GOLD ? '+' : (char)('a' + p.fruit)));
		int j = n++;
		while (j > 0 && s[j - 1] > c) {  // sorted: queue slots fill in any order
			s[j] = s[j - 1];
			j--;
		}
		s[j] = c;
	}
	s[n] = 0;
	DLOG("[daily] wave %lu %s\n", (unsigned long)dailyWave, s);
}
#endif

#ifdef TEST_SPAWN
// Runs the real wave code many times for each mode, difficulty and stage of a game, and prints what it throws.
void Game::spawnReport() {
	static const char* const names[MODE_COUNT] = {"Classic", "Arcade", "Zen", "Frenzy", "Sudden", "Minefield", "ComboRush"};
	for (int m = 0; m < MODE_COUNT; m++) {
		for (int diff = 0; diff < DIFF_COUNT; diff++) {
			gSave.difficulty = (u8)diff;
			for (int stage = 0; stage < 3; stage++) {
				const float want[3] = {0.0f, 0.5f, 1.0f};
				start((Mode)m);
				float ramp = 150;
				switch (m) {
					case MODE_ARCADE: ramp = 60; break;
					case MODE_ZEN: ramp = 240; break;
					case MODE_FRENZY: ramp = 30; break;
					case MODE_SUDDEN: ramp = 120; break;
					case MODE_MINEFIELD: ramp = 90; break;
					case MODE_COMBO: ramp = 45; break;
					default: break;
				}
				double fruit = 0, bombs = 0, seconds = 0, biggest = 0;
				static constexpr int kWaves = 20000;
				for (int i = 0; i < kWaves; i++) {
					elapsed = 7 + ramp * want[stage];
					spawnWave();
					seconds += waveTimer;
					int n = 0;
					for (auto& p : pending) {
						if (!p.on) continue;
						n++;
						if (p.kind == OBJ_BOMB) bombs++;
						else fruit++;
						p.on = false;
					}
					if (n > biggest) biggest = n;
				}
				fprintf(stderr, "SPAWN %s %d %.1f fruit/min=%.1f bombs/min=%.1f bomb%%=%.1f biggest=%.0f gravity=%.0f\n", names[m], diff, want[stage], fruit / seconds * 60, bombs / seconds * 60, bombs / (fruit + bombs) * 100, biggest, gravity);
			}
		}
	}
}
#endif

// ---------------------------------------------------------------- effects
void Game::emitJuice(float x, float y, u32 color, int count, float dirX, float dirY) {
	for (int i = 0; i < count; i++) {
		Particle& p = parts[partCursor];
		partCursor = (partCursor + 1) % kMaxParticles;
		// Most drops spray along the stroke; the rest burst outward.
		float a, s;
		if (i % 3 != 0) {
			a = atan2f(dirY, dirX) + gRng.range(-0.55f, 0.55f);
			s = gRng.range(70, 240);
		} else {
			a = gRng.range(0, 6.28f);
			s = gRng.range(25, 110);
		}
		p = {x, y, cosf(a) * s, sinf(a) * s - 20, 0, gRng.range(0.3f, 0.65f), gRng.range(0.35f, 0.95f), 340, color, 0, true};
		p.life = p.maxLife;
	}
}

void Game::emitSparks(float x, float y, u32 color, int count, float speed) {
	for (int i = 0; i < count; i++) {
		Particle& p = parts[partCursor];
		partCursor = (partCursor + 1) % kMaxParticles;
		float a = gRng.range(0, 6.28f), s = gRng.range(speed * 0.3f, speed);
		p = {x, y, cosf(a) * s, sinf(a) * s, 0, gRng.range(0.35f, 0.8f), gRng.range(0.4f, 1.0f), 90, color, 1, true};
		p.life = p.maxLife;
	}
}

// True if a popup of half-width hw at (x, y) would land on fresh text already on the field.
bool Game::popupBlocked(float x, float y, float hw, float hh) const {
	for (const Popup& o : popups) {
		if (!o.on || o.life < 0.45f) continue;  // fading ones have drifted away already
		float ow = gfx::textWidth(o.text, o.scale) * 0.5f;
		if (fabsf(o.x - x) < ow + hw + 4 && fabsf(o.y - y) < hh + 9 * o.scale) return true;
	}
#if !PORT_TWO_SCREENS
	// One screen: the big callout shares the field (it sits on the top screen of a 3DS).
	if (calloutT > 0.3f && phase != PHASE_FINALE) {
		float cw = fminf(gfx::textWidth(callout, calloutSize), kFieldW - 20) * 0.5f;
		if (fabsf(kFieldW * 0.5f - x) < cw + hw && fabsf(96 - y) < hh + 14 * calloutSize) return true;
	}
#endif
	return false;
}

void Game::addPopup(const char* s, float x, float y, u32 color, float scale) {
	Popup& p = popups[popupCursor];
	popupCursor = (popupCursor + 1) % kMaxPopups;
	p.on = false;  // the slot being reused must not block its own replacement
	snprintf(p.text, sizeof p.text, "%s", s);
	p.x = clampf(x, 26, kFieldW - 26);
	p.y = clampf(y, kFieldTop + 24, 220);
	// Step clear of text already showing: down a line, or up once the bottom is reached.
	float hw = gfx::textWidth(p.text, scale) * 0.5f, hh = 9 * scale, step = 20 * scale;
	for (int tries = 0; tries < 6 && popupBlocked(p.x, p.y, hw, hh); tries++) {
		if (p.y + step > 220) step = -fabsf(step);
		p.y = clampf(p.y + step, kFieldTop + 24, 220);
	}
	p.life = 0.9f;
	p.color = color;
	p.scale = scale;
	p.on = true;
}

void Game::addRing(float x, float y, u32 color, float maxScale, float life) {
	if (gSave.effects == FX_NONE) return;
	Ring& r = rings[ringCursor];
	ringCursor = (ringCursor + 1) % kMaxRings;
	r = {x, y, life, life, maxScale, color, true};
}

void Game::setCallout(const char* s, u32 color, float size) {
	snprintf(callout, sizeof callout, "%s", s);
	DLOG("[game] %s (score %lu)\n", callout, (unsigned long)score);
	calloutT = 1.4f;
	calloutColor = color;
	calloutSize = size;
}

// ---------------------------------------------------------------- scoring
void Game::award(int points) {
	if (mode == MODE_SUDDEN) points *= 2;
	if (doubleT > 0) points *= 2;
	if (chiliT > 0) points *= 2;
	if (feverT > 0) points *= 2;
	score += points;
	scorePulse = 1;
}

void Game::addFever(float amount) {
	if (feverT > 0 || phase != PHASE_PLAY) return;
	feverMeter += amount * kDifficulty[diffIndex()].feverGain * (mode == MODE_PRACTICE ? 3.0f : 1.0f);  // a lesson fills it fast
	if (feverMeter >= 1) {
		feverMeter = 0;
		feverT = kFrenzySeconds;
		feverTimer = 0;
		// A Frenzy promises no bombs, so any on their way fizzle out.
		for (auto& p : pending)
			if (p.on && p.kind == OBJ_BOMB) p.on = false;
		for (auto& o : objs) {
			if (o.on && o.kind == OBJ_BOMB) {
				emitSparks(o.x, o.y, rgba(255, 190, 110), juiceCount(8), 110);
				o.on = false;
			}
		}
		audio::fuse(false);
#ifdef AUTOTEST
		statFrenzies++;
#endif
		setCallout("FRUIT FRENZY! X2", hueColor(0.1f), 0.86f);
		audio::play(SFX_FEVER, 1.0f);
		addRing(kFieldW * 0.5f, 120, rgba(255, 220, 90), 5, 0.7f);
		if (gSave.effects != FX_NONE) {
			flash = 0.45f;
			flashColor = rgba(255, 210, 90);
		}
	}
}

void Game::finishCombo() {
	int n = comboCount;
	comboCount = 0;
	comboTimer = 0;
	if (mode == MODE_PRACTICE) practiceSwipe(n);
	if (n < 3) return;
	if (n > swipeBest && strokeLen > 1) {  // kept for the replay after a new best
		swipeBest = n;
		swipeLen = strokeLen;
		memcpy(swipeX, strokeX, sizeof swipeX);
		memcpy(swipeY, strokeY, sizeof swipeY);
	}

	// Every tier is louder, bigger and longer than the one before.
	struct Tier { int at; const char* name; Sfx sfx; int mult; u32 color; float size, slow, shakeAmount; int sparks, rings; };
	static const Tier kTiers[] = {
		{10, "LEGENDARY", SFX_COMBO10, 4, 0xFF40E0FFu, 1.0f, 0.5f, 0.55f, 80, 5},
		{8, "INSANE", SFX_COMBO8, 3, 0xFFE070FFu, 0.92f, 0.7f, 0.42f, 60, 4},
		{6, "SUPER", SFX_COMBO6, 2, 0xFFFFD060u, 0.84f, 0.4f, 0.25f, 40, 3},
		{5, "AWESOME", SFX_COMBO5, 2, 0xFF70F090u, 0.78f, 0.22f, 0.12f, 26, 2},
		{4, "GREAT", SFX_COMBO4, 1, 0xFF60D8FFu, 0.72f, 0.0f, 0.0f, 14, 1},
		{3, "NICE", SFX_COMBO3, 1, 0xFF5AD6FFu, 0.68f, 0.0f, 0.0f, 8, 1},
	};
	const Tier* t = &kTiers[5];
	for (const auto& candidate : kTiers) {
		if (n >= candidate.at) {
			t = &candidate;
			break;
		}
	}
	int bonus = n * t->mult;
	if (mode == MODE_COMBO) bonus *= 2;
	award(bonus);

	char buf[40];
	snprintf(buf, sizeof buf, "%s! %d COMBO +%d", t->name, n, bonus);
	setCallout(buf, t->color, t->size);
	audio::play(t->sfx);
	if (t->slow > 0 && !gSave.calmFx) slowT = t->slow;  // slow motion; a 10-fruit swipe gets half a second
	if (t->shakeAmount > shake) shake = t->shakeAmount;
	for (int i = 0; i < t->rings; i++) addRing(comboX, comboY, hueColor(i * 0.18f), 2.0f + i * 1.3f, 0.35f + i * 0.12f);
	for (int i = 0; i < juiceCount(t->sparks); i++) {
		Particle& p = parts[partCursor];
		partCursor = (partCursor + 1) % kMaxParticles;
		float a = gRng.range(0, 6.28f), s = gRng.range(60, 300);
		p = {comboX, comboY, cosf(a) * s, sinf(a) * s - 40, 0, gRng.range(0.5f, 1.0f), gRng.range(0.5f, 1.1f), 200, hueColor(gRng.unit()), 1, true};
		p.life = p.maxLife;
	}
	if (n >= 8) {
		flash = 0.5f;
		flashColor = t->color;
	}
	snprintf(buf, sizeof buf, "+%d", bonus);
	addPopup(buf, comboX, comboY - 16, t->color, 0.7f + n * 0.05f);
	addFever(n < 15 ? n * 0.02f : 0.3f);  // a big single stroke tops the meter up on top of each fruit's own share
	if (n > bestCombo) bestCombo = n;
}

// A run's score goes on record once, however it ends: played out, quit from the pause menu, or window closed.
// The app may be closed for good while it is away. Only the best score goes in now: the fruit count
// waits for commitScore, or a run that carries on would count twice.
bool Game::keepBestSoFar() {
	u32* bests = assisted ? gSave.bestAssisted : gSave.best;
	if (mode >= MODE_COUNT || scoreCommitted || score <= bests[mode]) return false;
	bests[mode] = score;
	return true;
}

void Game::commitScore() {
	if (scoreCommitted) return;
	scoreCommitted = true;
	journalCoins = progress::journalPay(journalStart, gSave.fruitSliced);
	gSave.playSeconds += (u32)elapsed;
	if (mode == MODE_PRACTICE) {
		// No score to keep. Journal milestones still pay, and the lessons are marked done.
		coinsEarned = journalCoins;
		gSave.coins += journalCoins;
		gSave.coinsLifetime += journalCoins;
		gSave.practiceDone = 1;
		saveWrite();
		return;
	}
	u32* bests = assisted ? gSave.bestAssisted : gSave.best;
	if (score > bests[mode]) bests[mode] = score;
	if (score > best) {
		best = score;
		newBest = score > 0;
		if (newBest) coinsBonus = 10;
	}
	gSave.totalFruit += sliced;
	// Each coin caught is worth 5, and every 100 points adds one more.
	if (mode == MODE_DAILY) dailyCoins = progress::dailyRecord(day, score);
	coinsEarned = runCoins() + (u32)coinsBonus + journalCoins + dailyCoins;
	u32 earned = coinsEarned;
	gSave.coins = gSave.coins + earned < gSave.coins ? 0xFFFFFFFFu : gSave.coins + earned;
	gSave.coinsLifetime += earned;
	if ((u32)bestCombo > gSave.bestCombo) gSave.bestCombo = bestCombo;
	saveWrite();
}

void Game::addStrike(float x) {
	if (maxStrikes == 0 || feverT > 0 || feverGrace > 0) return;
	strikes++;
	strikePulse = 1;
	audio::play(SFX_STRIKE, 0.9f, fieldPan(x));
	PORT_VIBRATE(0.45f, 110);
	addPopup("MISS", x, 214, rgba(255, 90, 90), 0.62f);
	if (strikes >= maxStrikes) endGame();
}

void Game::addHalves(const Obj& o, const FruitDef& def, const Blade& blade, float dx, float dy) {
	// The half sprites and ready-made meshes are cut down their local Y axis; line that up with the stroke.
	float cutRot = atan2f(dy, dx) - kPi * 0.5f;
	float nx = -dy, ny = dx;
	// Local -X must separate towards -normal after rotation.
	float lx = cosf(cutRot), ly = sinf(cutRot);
	if (lx * nx + ly * ny < 0) { nx = -nx; ny = -ny; }
	// The sense of the turn that rolls each cut face towards the player, not away.
	const float roll = (dx * ny - dy * nx) >= 0 ? 1.0f : -1.0f;

	// Cutting along the real swipe needs two free slots; otherwise this fruit uses ready-made halves.
	bool exact = use3D() && gSave.sliceStyle == 1 && !sLightFrenzy;
	int slots[2] = {-1, -1};
	if (exact) {
		slots[0] = m3d::cutAcquire();
		slots[1] = m3d::cutAcquire();
		if (slots[0] < 0 || slots[1] < 0) {
			m3d::cutRelease(slots[0]);
			m3d::cutRelease(slots[1]);
			exact = false;
		}
	}
	float mx = 0, my = 0, mz = 0, offset = 0;
	if (exact) {
		// The stroke's normal, carried back through the fruit's own rotation (Rz Rx Ry) into model space.
		float cz = cosf(o.rot), sz = sinf(o.rot);
		float x1 = cz * nx + sz * ny, y1 = -sz * nx + cz * ny;
		float cx = cosf(o.rx), sx = sinf(o.rx);
		float y2 = cx * y1, z2 = -sx * y1;
		float cy = cosf(o.ry), sy = sinf(o.ry);
		mx = cy * x1 - sy * z2;
		my = y2;
		mz = sy * x1 + cy * z2;
		// A swipe near the edge cuts near the edge, but never off the fruit entirely.
		offset = clampf((blade.x - o.x) * nx + (blade.y - o.y) * ny, -o.radius * 0.5f, o.radius * 0.5f);
	}

	const float kick = 42;  // how fast the halves drift apart along the cut's normal
	for (int s = 0; s < 2; s++) {
		Half& h = halves[halfCursor];
		halfCursor = (halfCursor + 1) % (sLightFrenzy ? kLightMaxHalves : kMaxHalves);
		if (h.on) m3d::cutRelease(h.cut);
		float side = s == 0 ? -1.0f : 1.0f;
		h.on = true;
		h.x = o.x;
		h.y = o.y;
		// Halves keep the fruit's momentum, plus a nudge from the blade.
		h.vx = o.vx * 0.9f + nx * kick * side + dx * 30;
		h.vy = o.vy * 0.8f + ny * kick * side + dy * 30;
		h.rotv = side * gRng.range(1.5f, 4.0f);
		h.sprite = (short)(s == 0 ? def.left : def.right);
		h.mesh = (u8)def.mesh;
		h.part = s == 0 ? m3d::PART_LEFT : m3d::PART_RIGHT;
		h.axisX = dx;
		h.axisY = dy;
		h.tilt = 0;
		h.tiltv = -side * roll * 2.4f;
		h.cut = -1;
		h.rot = cutRot;
		h.rx = h.ry = 0;
		if (exact) {
			h.rot = o.rot;
			h.rx = o.rx;
			h.ry = o.ry;
			h.rotv *= 0.4f;
			h.cut = (short)slots[s];
			m3d::cutRequest(slots[s], def.mesh, mx * side, my * side, mz * side, offset * side);
		}
	}
}

void Game::sliceFruit(Obj& o, const Blade& blade) {
	PORT_VIBRATE(0.25f, 18);
	const FruitDef& def = o.kind == OBJ_GOLD ? kGold : kFruit[o.fruit];
	float pan = fieldPan(o.x);
	float dx = blade.x - blade.px, dy = blade.y - blade.py;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.01f) { dx = 1; dy = 0; len = 1; }
	dx /= len;
	dy /= len;

	if (o.hits > 1) {
		// Tough shell: the first hit only cracks it and knocks it back up.
		o.hits--;
		o.hitCooldown = 0.2f;
		o.vy -= 70;
		o.vx += dx * 40;
		o.rotv *= -1.4f;
		audio::play(SFX_KNOCK, 0.95f, pan, gRng.range(0.94f, 1.06f));
		emitSparks(o.x, o.y, rgba(255, 240, 190), juiceCount(10), 160);
		addRing(o.x, o.y, rgba(255, 240, 190), 1.3f, 0.2f);
		return;
	}

	addHalves(o, def, blade, dx, dy);

	if (gSave.effects != FX_NONE) {
		Splat& sp = splats[splatCursor];
		splatCursor = (splatCursor + 1) % kMaxSplats;
		sp = {o.x, o.y, gRng.range(0, 6.28f), gRng.range(0.7f, 1.05f), gSave.effects == FX_LOW ? 1.6f : 3.2f, def.juice, (u8)gRng.below(3), true};
	}
	emitJuice(o.x, o.y, def.juice, juiceCount(def.cut == CUT_MELON ? 22 : 14), dx, dy);

	// Two takes per fruit class, plus slight pitch variation, so repeat cuts don't sound identical.
	Sfx cut = SFX_CRISP0;
	switch (def.cut) {
		case CUT_CRISP: cut = (Sfx)(SFX_CRISP0 + gRng.below(2)); break;
		case CUT_JUICY: cut = (Sfx)(SFX_JUICY0 + gRng.below(2)); break;
		case CUT_SOFT: cut = (Sfx)(SFX_SOFT0 + gRng.below(2)); break;
		case CUT_MELON: cut = (Sfx)(SFX_MELON0 + gRng.below(2)); break;
		case CUT_SHELL: cut = SFX_SHELL; break;
	}
	audio::play(cut, 0.95f, pan, gRng.range(0.92f, 1.09f));

	if (o.kind == OBJ_FRUIT) {
		u32& count = gSave.fruitSliced[progress::journalKind(o.fruit)];
		if (count < 0xFFFFFFFFu) count++;
		if (mode == MODE_PRACTICE) practiceSliced(o);
	}
	if (o.kind == OBJ_FRUIT && o.fruit >= kSpiky) {
		trickEffect(o, dx, dy);
		if (o.fruit == kSpiky) {  // no points and no combo: it only gets in the way
			o.on = false;
			return;
		}
	}
	int pts = o.kind == OBJ_GOLD ? 5 : (def.cut == CUT_SHELL ? 3 : (o.kind == OBJ_FRUIT && o.fruit == kJelly ? 2 : 1));
	award(pts);
	sliced++;
	chain++;
	if (chain > bestCombo) bestCombo = chain;
	addFever(o.kind == OBJ_GOLD ? 0.14f : 0.065f);
	if (pts > 1) {
		char buf[8];
		snprintf(buf, sizeof buf, "+%d", pts);
		addPopup(buf, o.x, o.y - 18, rgba(255, 220, 90), 0.8f);
	}
	if (o.kind == OBJ_GOLD) {
		audio::play(SFX_GOLD, 0.9f, pan);
		emitSparks(o.x, o.y, rgba(255, 226, 120), juiceCount(14), 180);
	}
	if (chain % 15 == 0) {
		award(5);
		char buf[24];
		snprintf(buf, sizeof buf, "COMBO %d +5", chain);
		addPopup(buf, kFieldW * 0.5f, kFieldTop + 60, rgba(170, 240, 255), 0.6f);
	}

	if (comboStroke != blade.strokeId && comboCount > 0) finishCombo();
	comboStroke = blade.strokeId;
	comboCount++;
	comboTimer = 0.32f;
	comboX = o.x;
	comboY = o.y;
	if (comboCount >= 3) addRing(o.x, o.y, hueColor(comboCount * 0.11f), 1.2f + comboCount * 0.08f, 0.25f);
	o.on = false;
}

void Game::hitBomb(Obj& o) {
	o.on = false;
	if (shield) {
		// The shield takes it: the bomb fizzles out with a puff, and nothing is lost.
		shield = false;
		audio::play(SFX_KNOCK, 0.9f, fieldPan(o.x), 0.7f);
		emitSparks(o.x, o.y, rgba(190, 170, 255), juiceCount(20), 200);
		addRing(o.x, o.y, rgba(190, 170, 255), 2.4f, 0.35f);
		addPopup("BLOCKED", o.x, o.y - 18, rgba(200, 180, 255), 0.7f);
		return;
	}
#ifdef AUTOTEST
	statBombHits++;
#endif
	flash = 1;
	flashColor = WHITE;
	shake = 0.6f;
	chain = 0;
	feverMeter *= 0.5f;
	emitSparks(o.x, o.y, rgba(255, 170, 60), juiceCount(42), 280);
	emitSparks(o.x, o.y, rgba(255, 240, 200), juiceCount(16), 180);
	addRing(o.x, o.y, rgba(255, 190, 90), 4, 0.45f);
	audio::play(SFX_BOOM, 1.0f, fieldPan(o.x));
	PORT_VIBRATE(0.8f, 260);
	switch (mode) {
		case MODE_ARCADE:
		case MODE_COMBO:
		case MODE_DAILY:
			score = score > 10 ? score - 10 : 0;
			addPopup("-10", o.x, o.y, rgba(255, 90, 90), 0.9f);
			setCallout("BOMB! -10", rgba(255, 110, 90));
			break;
		case MODE_FRENZY:
			addPopup("-5 SEC", o.x, o.y, rgba(255, 90, 90), 0.8f);
			if (timeLeft > 5) {
				timeLeft -= 5;
				setCallout("BOMB! -5 SECONDS", rgba(255, 110, 90));
			} else {
				timeLeft = 0;
				startFinale();  // the clock has run out, and only a running clock would bring it in
			}
			break;
		case MODE_PRACTICE:
			practiceBomb();
			break;
		default:
			setCallout("BOOM!", rgba(255, 110, 90));
#ifndef TEST_BALANCE
			endGame();
#endif
			break;
	}
}

void Game::sliceObj(Obj& o, const Blade& blade) {
	float pan = fieldPan(o.x);
	switch (o.kind) {
		case OBJ_BOMB: hitBomb(o); return;
		case OBJ_FRUIT:
		case OBJ_GOLD: sliceFruit(o, blade); return;
		case OBJ_COIN:
			// One coin and five points. It bursts into sparkles instead of splitting.
			coinsCaught++;
			coinPulse = 1;
			if (mode == MODE_PRACTICE) practiceSliced(o);
			award(5);
			addPopup("+5", o.x, o.y - 18, rgba(255, 220, 90), 0.8f);
			audio::play(SFX_PLINK, 0.85f, pan);
			emitSparks(o.x, o.y, rgba(255, 226, 120), juiceCount(16), 170);
			addRing(o.x, o.y, rgba(255, 220, 110), 1.6f, 0.28f);
			o.on = false;
			return;
		case OBJ_FREEZE:
			freezeT = 6;
			audio::play(SFX_FREEZE, 1.0f, pan);
			setCallout("FROST! TIME SLOWS", rgba(150, 220, 255));
			break;
		case OBJ_DOUBLE:
			doubleT = 8;
			audio::play(SFX_POWER, 1.0f, pan);
			setCallout("DOUBLE POINTS!", rgba(255, 140, 190));
			break;
		case OBJ_FRENZY:
			frenzyT = 5;
			frenzyTimer = 0;
			audio::play(SFX_POWER, 1.0f, pan);
			setCallout("FRUIT STORM!", rgba(255, 180, 80));
			break;
		case OBJ_MAGNET:
			magnetT = 6;
			audio::play(SFX_POWER, 1.0f, pan);
			setCallout("MAGNET!", rgba(255, 120, 120));
			break;
		case OBJ_WIDE:
			wideT = 8;
			audio::play(SFX_POWER, 1.0f, pan);
			setCallout("WIDE BLADE!", rgba(150, 230, 130));
			break;
		case OBJ_SHIELD:
			shield = true;
			audio::play(SFX_POWER, 1.0f, pan);
			setCallout("BOMB SHIELD!", rgba(180, 160, 255));
			break;
		case OBJ_TIME:
			timeLeft += 5;
			audio::play(SFX_POWER, 1.0f, pan);
			setCallout("+5 SECONDS!", rgba(120, 230, 230));
			break;
	}
	emitSparks(o.x, o.y, WHITE, juiceCount(18), 200);
	addRing(o.x, o.y, WHITE, 2, 0.3f);
	o.on = false;
}

// ---------------------------------------------------------------- trick fruit
static constexpr float kStunTime = 2.0f, kStickyTime = 4.0f, kChiliTime = 5.0f;
static constexpr float kJellySpeed = 11.0f;  // blade speed, in field units per step, a jelly fruit needs

// Too slow a swipe bounces off a jelly fruit: it wobbles back up and can be tried again.
void Game::bounceJelly(Obj& o, const Blade& blade) {
	float dx = blade.x - blade.px, dy = blade.y - blade.py, len = sqrtf(dx * dx + dy * dy);
	if (len > 0.01f) o.vx += dx / len * 90;
	if (o.vy > -150) o.vy = -150;
	o.rotv *= -1.3f;
	o.hitCooldown = 0.3f;
	audio::play(SFX_KNOCK, 0.7f, fieldPan(o.x), 1.6f);
	addPopup("BOING", o.x, o.y - 20, rgba(255, 170, 220), 0.55f);
	showCaption("Boing! Swipe faster.");
}

void Game::trickEffect(const Obj& o, float dx, float dy) {
	const float pan = fieldPan(o.x);
	switch (o.fruit) {
		case kSpiky:
			stunT = gSave.sliceAssist ? kStunTime * 0.5f : kStunTime;
			PORT_VIBRATE(0.5f, 140);
			audio::play(SFX_KNOCK, 1.0f, pan, 0.8f);
			emitSparks(o.x, o.y, rgba(220, 230, 160), juiceCount(16), 220);
			addPopup("STUNNED!", o.x, o.y - 20, rgba(230, 230, 150), 0.7f);
			showCaption("Stunned! The blade can't cut.");
			break;
		case kGrapes: {
			// The bunch bursts into loose grapes, each one worth a point.
			int n = 3 + gRng.below(2);
			for (auto& g : objs) {
				if (g.on) continue;
				g = {};
				g.on = true;
				g.kind = OBJ_FRUIT;
				g.fruit = kGrape;
				g.x = o.x;
				g.y = o.y;
				float side = n % 2 ? 1.0f : -1.0f;
				g.vx = side * gRng.range(50, 150) + dy * 40;
				g.vy = gRng.range(-280, -180) - dx * 20;
				g.rot = gRng.range(0, 6.28f);
				g.rotv = gRng.range(-5, 5);
				g.ry = gRng.range(0, 6.28f);
				g.ryv = gRng.range(-3, 3);
				g.radius = 13;
				g.hits = 1;
				g.hitCooldown = 0.12f;
				if (--n == 0) break;
			}
			addPopup("BURST!", o.x, o.y - 20, rgba(200, 150, 255), 0.6f);
			break;
		}
		case kHoney:
			stickyT = gSave.sliceAssist ? kStickyTime * 0.5f : kStickyTime;
			addPopup("STICKY!", o.x, o.y - 20, rgba(255, 200, 80), 0.7f);
			showCaption("Sticky! The blade drags.");
			break;
		case kChili:
			chiliT = kChiliTime;
			audio::play(SFX_POWER, 0.9f, pan, 1.15f);
			setCallout("HOT! X2", rgba(255, 110, 70));
			showCaption("Hot! Faster, double points.");
			break;
		default:
			break;
	}
}

// Timers for the trick fruit and the Arcade power-ups, and the magnet's pull.
void Game::updateTricks(const Blade& blade) {
	if (stunT > 0) stunT -= kDt;
	if (stickyT > 0) stickyT -= kDt;
	if (chiliT > 0) chiliT -= kDt;
	if (wideT > 0) wideT -= kDt;
	if (magnetT > 0) {
		magnetT -= kDt;
		if (blade.active) {
			for (auto& o : objs) {
				if (!o.on || o.kind == OBJ_BOMB || (o.kind > OBJ_GOLD && o.kind != OBJ_COIN) || (o.kind == OBJ_FRUIT && o.fruit == kSpiky)) continue;
				float mx = blade.x - o.x, my = blade.y - o.y;
				o.vx += clampf(mx * 2.6f, -260, 260) * kDt;
				o.vy += clampf(my * 1.6f, -200, 200) * kDt;
			}
		}
	}
}

bool Game::hitTest(const Obj& o, const Blade& blade, float radius) const {
	float dx = blade.x - blade.px, dy = blade.y - blade.py;
	float len2 = dx * dx + dy * dy;
	float t = len2 > 0 ? clampf(((o.x - blade.px) * dx + (o.y - blade.py) * dy) / len2, 0, 1) : 0;
	float qx = blade.px + dx * t - o.x, qy = blade.py + dy * t - o.y;
	return qx * qx + qy * qy <= radius * radius;
}

void Game::testSlices(Blade& blade) {
	if (!blade.active || blade.strokeStart) return;
	float dx = blade.x - blade.px, dy = blade.y - blade.py;
	// A resting stylus never cuts. Higher sensitivity lets smaller movements count.
	if (dx * dx + dy * dy < lerpf(4.0f, 0.6f, gSave.stylus / 100.0f) * (gSave.sliceAssist ? 0.4f : 1.0f)) return;
	if (blade.speed > 6 && blade.swishCooldown <= 0) {
		float pan = fieldPan(blade.x);
		if (blade.speed > 17) audio::play(SFX_SHING, 0.34f, pan, gRng.range(0.95f, 1.08f));
		else audio::play((Sfx)(SFX_SWISH0 + gRng.below(3)), 0.5f, pan, gRng.range(0.9f, 1.12f));
		blade.swishCooldown = 0.2f;
	}
	if (stunT > 0) return;  // a spiky fruit stunned the blade: nothing cuts, bombs included
	// Fruit is tested first. If this movement cut a fruit, a nearby bomb only counts on a much
	// closer pass, so going for a fruit that's near a bomb is fair.
	bool cutFruit = false;
	for (auto& o : objs) {
		if (!o.on || o.kind == OBJ_BOMB || o.hitCooldown > 0) continue;
		if (hitTest(o, blade, o.radius * (0.7f + slider(gSave.forgiveness) * 0.006f) * kCutReach * (gSave.sliceAssist ? 1.3f : 1.0f) * (wideT > 0 ? 1.6f : 1.0f))) {
			if (o.kind == OBJ_FRUIT && o.fruit == kJelly && blade.speed < kJellySpeed) {
				bounceJelly(o, blade);
				continue;
			}
			sliceObj(o, blade);
			cutFruit = true;
			if (phase != PHASE_PLAY) return;
		}
	}
	for (auto& o : objs) {
		if (!o.on || o.kind != OBJ_BOMB) continue;
		if (hitTest(o, blade, cutFruit ? 11.0f : o.radius)) {
			sliceObj(o, blade);
			if (phase != PHASE_PLAY) return;
		}
	}
}

// ---------------------------------------------------------------- finale fruit
static constexpr float kFinaleRise = 0.7f;
static constexpr float kFinaleWindow = 4.0f;
static constexpr float kFinaleRadius = 46.0f;
static constexpr float kFinaleTravelPerHit = 70.0f;

void Game::startFinale() {
	phase = PHASE_FINALE;  // first, so that a last combo can't set off a Frenzy that gets cancelled
	finishCombo();
	phaseT = 0;
	finaleOn = true;
	finaleT = 0;
	finaleX = kFieldW * 0.5f;
	finaleY = 300;
	finaleTravel = kFinaleTravelPerHit;  // the first touch counts straight away
	freezeT = doubleT = frenzyT = feverT = slowT = 0;
	for (auto& p : pending) p.on = false;
	for (auto& o : objs) {
		// Nothing should punish you here, so leftover bombs fizzle out.
		if (o.on && o.kind == OBJ_BOMB) {
			emitSparks(o.x, o.y, rgba(255, 190, 110), juiceCount(8), 110);
			o.on = false;
		}
	}
	audio::fuse(false);
	audio::play(SFX_FINALE_RISE);
	setCallout("TIME! SLICE THE SUNBURST!", rgba(255, 210, 90), 0.72f);
}

void Game::finaleHit(const Blade& blade) {
	finaleHits++;
	float pan = fieldPan(finaleX);
	float dx = blade.x - blade.px, dy = blade.y - blade.py;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.01f) len = 1;
	dx /= len;
	dy /= len;

	award(1);
	finaleSwell = clampf(finaleHits * 0.007f, 0, 0.42f);
	finaleJoltX += dx * 7;
	finaleJoltY += dy * 7;
	scorePulse = 1;
	audio::play(SFX_FINALE_HIT, 0.85f, pan, clampf(0.8f + finaleHits * 0.022f, 0.8f, 2.0f));
	if (finaleHits % 3 == 0) audio::play((Sfx)(SFX_JUICY0 + gRng.below(2)), 0.55f, pan, gRng.range(0.95f, 1.2f));
	static const u32 kJuice[] = {0xFF2E9CFFu, 0xFF52E6FFu, 0xFF4A3CF0u};
	emitJuice(blade.x, blade.y, kJuice[gRng.below(3)], juiceCount(5), dx, dy);
	if (finaleHits >= 10) emitSparks(blade.x, blade.y, hueColor(finaleHits * 0.03f), juiceCount(2), 150);

	// Every ten hits is a bonus, and the milestones climb through the combo stingers.
	if (finaleHits % 10 == 0) {
		award(5);
		char buf[16];
		snprintf(buf, sizeof buf, "+5");
		addPopup(buf, blade.x, blade.y - 20, rgba(255, 226, 120), 0.8f);
		addRing(finaleX, finaleY, hueColor(finaleHits * 0.02f), 2.4f + finaleHits * 0.02f, 0.3f);
	}
	struct Stage { int at; const char* name; Sfx sfx; u32 color; float size; };
	static const Stage kStages[] = {
		{10, "NICE! 10 HITS", SFX_COMBO3, 0xFF5AD6FFu, 0.76f},
		{20, "AWESOME! 20 HITS", SFX_COMBO5, 0xFF70F090u, 0.82f},
		{30, "SUPER! 30 HITS", SFX_COMBO6, 0xFFFFD060u, 0.88f},
		{40, "INSANE! 40 HITS", SFX_COMBO8, 0xFFE070FFu, 0.94f},
		{60, "LEGENDARY! 60 HITS", SFX_COMBO10, 0xFF40E0FFu, 1.0f},
	};
	for (const auto& s : kStages) {
		if (finaleHits == s.at) {
			setCallout(s.name, s.color, s.size);
			audio::play(s.sfx, 0.8f);
			shake = 0.2f;
		}
	}
}

void Game::finaleBurst() {
	finaleOn = false;
	if (finaleHits == 0) {
		setCallout("IT GOT AWAY...", rgba(200, 190, 230));
		endGame();
		return;
	}
	int bonus = finaleHits * 2;
	award(bonus);
	char buf[40];
	snprintf(buf, sizeof buf, "SUNBURST! %d HITS +%d", finaleHits, bonus);
	setCallout(buf, rgba(255, 220, 90), 0.95f);
	calloutT = 2.2f;
	audio::play(SFX_FINALE_BURST);
	flash = 0.7f;
	flashColor = rgba(255, 220, 130);
	shake = 0.6f;
	for (int i = 0; i < 5; i++) addRing(finaleX, finaleY, hueColor(0.05f + i * 0.14f), 2.5f + i * 1.5f, 0.4f + i * 0.14f);
	emitJuice(finaleX, finaleY, 0xFF2E9CFFu, juiceCount(46), 0, -1);
	emitJuice(finaleX, finaleY, 0xFF52E6FFu, juiceCount(30), 0, 1);
	for (int i = 0; i < juiceCount(70); i++) {
		Particle& p = parts[partCursor];
		partCursor = (partCursor + 1) % kMaxParticles;
		float a = gRng.range(0, 6.28f), s = gRng.range(60, 340);
		p = {finaleX, finaleY, cosf(a) * s, sinf(a) * s - 50, 0, gRng.range(0.6f, 1.2f), gRng.range(0.5f, 1.1f), 220, hueColor(gRng.unit()), 1, true};
		p.life = p.maxLife;
	}
	Splat& sp = splats[splatCursor];
	splatCursor = (splatCursor + 1) % kMaxSplats;
	sp = {finaleX, finaleY, gRng.range(0, 6.28f), 2.1f, 3.2f, 0xFF2E9CFFu, (u8)gRng.below(3), true};
	endGame();
}

void Game::updateFinale(Blade& blade) {
	finaleT += kDt;
	updateObjects(kDt);  // leftover fruit falls away, with no strikes now
	float k = clampf(finaleT / kFinaleRise, 0, 1);
	finaleY = lerpf(300, 120, 1 - (1 - k) * (1 - k) * (1 - k));  // it comes to rest in the middle of the screen
	finaleJoltX *= 0.82f;
	finaleJoltY *= 0.82f;

	// Hits come from blade distance inside the fruit, so faster scrubbing really does score more.
	if (finaleT > kFinaleRise * 0.6f && blade.active && !blade.strokeStart) {
		float dx = blade.x - blade.px, dy = blade.y - blade.py;
		float len = sqrtf(dx * dx + dy * dy);
		Obj probe = {};
		probe.x = finaleX;
		probe.y = finaleY;
		if (len >= 1.5f && hitTest(probe, blade, kFinaleRadius + finaleSwell * 40)) {
			finaleTravel += len;
			if (blade.swishCooldown <= 0) {
				audio::play((Sfx)(SFX_SWISH0 + gRng.below(3)), 0.3f, 0, gRng.range(1.0f, 1.3f));
				blade.swishCooldown = 0.16f;
			}
			if (finaleTravel >= kFinaleTravelPerHit) {
				finaleTravel -= kFinaleTravelPerHit;
				finaleHit(blade);
			}
		}
	}
	if (finaleT >= kFinaleRise + kFinaleWindow) finaleBurst();
}

// The path of the stroke in progress, a point a step, so the run's biggest swipe can be replayed.
void Game::recordStroke(const Blade& blade) {
	if (!blade.active) return;
	if (blade.strokeId != strokeOf) {
		strokeOf = blade.strokeId;
		strokeLen = 0;
	}
	if (strokeLen == kSwipePoints) {
		memmove(strokeX, strokeX + 1, sizeof(float) * (kSwipePoints - 1));
		memmove(strokeY, strokeY + 1, sizeof(float) * (kSwipePoints - 1));
		strokeLen--;
	}
	strokeX[strokeLen] = blade.x;
	strokeY[strokeLen] = blade.y;
	strokeLen++;
}

// After a new best, the run's biggest swipe plays again slowly across the field, over and over.
void Game::drawSwipeReplay() const {
	if (phase != PHASE_OVER || !newBest || swipeLen < 2 || gSave.calmFx) return;
	static constexpr float kDraw = 1.3f, kPause = 0.9f;
	float t = fmodf(phaseT, kDraw + kPause);
	float headF = clampf(t / kDraw, 0, 1) * (swipeLen - 1);
	int headI = (int)headF;
	Blade b;
	b.strokeId = 7;
	b.count = 0;
	b.head = 0;
	const float after = t > kDraw ? t - kDraw : 0;  // once drawn, the trail fades as a real one does
	for (int i = headI - 10 < 0 ? 0 : headI - 10; i <= headI && b.count < Blade::kTrail; i++) {
		float age = (headF - i) * 0.012f + after;
		if (age > 0.16f) continue;
		b.trail[b.head] = {swipeX[i], swipeY[i], age};
		b.head = (b.head + 1) % Blade::kTrail;
		b.count++;
	}
	b.draw();
}

void Game::endGame() {
	if (phase != PHASE_PLAY && phase != PHASE_FINALE) return;
	phase = PHASE_DYING;
	phaseT = 0;
	audio::fuse(false);
	finishCombo();
}

// ---------------------------------------------------------------- simulation
void Game::updateObjects(float dt) {
	for (auto& o : objs) {
		if (!o.on) continue;
		o.vy += gravity * dt;
		o.x += o.vx * dt;
		o.y += o.vy * dt;
		o.rot += o.rotv * dt;
		o.rx += o.rxv * dt;
		o.ry += o.ryv * dt;
		o.age += dt;
		if (o.hitCooldown > 0) o.hitCooldown -= dt;
		if (o.vy > 0 && o.y > kKillY) {
			o.on = false;
			// A dropped spiky fruit or loose grape is never a miss.
			bool counts = o.kind == OBJ_GOLD || (o.kind == OBJ_FRUIT && o.fruit != kSpiky && o.fruit != kGrape);
			if (counts && phase == PHASE_PLAY) addStrike(o.x);
		}
	}
}

void Game::updateEffects(float dt) {
	for (auto& h : halves) {
		if (!h.on) continue;
		h.vy += gravity * dt;
		h.x += h.vx * dt;
		h.y += h.vy * dt;
		h.rot += h.rotv * dt;
		if (fabsf(h.tilt) < 1.15f) h.tilt += h.tiltv * dt;
		if (h.y > kKillY + 20) {
			h.on = false;
			m3d::cutRelease(h.cut);
		}
	}
	for (auto& s : splats) {
		if (!s.on) continue;
		s.life -= dt;
		if (s.life <= 0) s.on = false;
	}
	for (auto& p : popups) {
		if (!p.on) continue;
		p.life -= dt;
		p.y -= 26 * dt;
		if (p.life <= 0) p.on = false;
	}
	for (auto& r : rings) {
		if (!r.on) continue;
		r.life -= dt;
		if (r.life <= 0) r.on = false;
	}
	if (flash > 0) flash -= dt * 2.4f;
	if (shake > 0) shake -= dt;
	if (calloutT > 0) calloutT -= dt;
	if (scorePulse > 0) scorePulse -= dt * 4;
	if (coinPulse > 0) coinPulse -= dt * 4;
	if (strikePulse > 0) strikePulse -= dt * 2.5f;
	float s = shake > 0 ? shake * 9 * (gSave.shake / 100.0f) : 0;
	gfx::setShake(s > 0 ? gRng.range(-s, s) : 0, s > 0 ? gRng.range(-s, s) : 0);
}

static void stepParticles(void* arg) {
	Game* g = (Game*)arg;
	for (auto& p : g->parts) {
		if (!p.on) continue;
		p.vy += p.gravity * kDt;
		p.x += p.vx * kDt;
		p.y += p.vy * kDt;
		p.life -= kDt;
		if (p.life <= 0) p.on = false;
	}
}

void Game::kickParticles() { jobs::run(stepParticles, this); }

void Game::update(Blade& blade) {
	tickCaptions(kDt);
	jobs::wait();
	phaseT += kDt;
	updateEffects(kDt);

	if (phase == PHASE_READY) {
		if (phaseT >= 1.3f) {
			phase = PHASE_PLAY;
			phaseT = 0;
			audio::play(SFX_GO);
		}
		return;
	}

	if (phase == PHASE_PLAY) {
		elapsed += kDt;
		if (!assisted && assistedRun()) {  // turned on from the pause menu: this run is assisted now
			assisted = true;
			best = gSave.bestAssisted[mode];
		}
		float objDt = (freezeT > 0 ? kDt * 0.45f : kDt) * (0.7f + slider(gSave.fruitSpeed) * 0.006f) * gameSpeedScale();
		if (chiliT > 0) objDt *= 1.3f;  // the chili rush
		updateTricks(blade);
		if (slowT > 0) {
			objDt *= 0.3f;
			slowT -= kDt;
		}
		if (freezeT > 0) freezeT -= kDt;
		if (doubleT > 0) doubleT -= kDt;

		// The meter never stops draining, in step with the fruit. The combo only lives while it has some left.
		if (comboFadeT > 0) comboFadeT -= kDt;
		if (feverT <= 0 && feverMeter > 0) {
			feverMeter -= kFeverDrain * kDifficulty[diffIndex()].feverDrain * (0.4f + slider(gSave.frenzyDrain) * 0.012f) * objDt;
			if (feverMeter <= 0) {
				feverMeter = 0;
				if (chain >= 2) {
					fadingChain = chain;
					comboFadeT = kComboFade;
				}
				chain = 0;
#ifdef AUTOTEST
				statResets++;
#endif
			}
		}

		if (timeLeft > 0) {
			timeLeft -= kDt * gameSpeedScale();
			if (timeLeft <= 0) {
				timeLeft = 0;
				if (mode == MODE_ZEN) {
					setCallout("TIME!", rgba(255, 230, 140));
					endGame();
				} else {
					startFinale();  // Arcade, Frenzy and Combo Rush end on the giant fruit
				}
			}
		}

		if (phase == PHASE_PLAY) {
			if (feverGrace > 0) feverGrace -= objDt;
			if (mode == MODE_PRACTICE) practiceUpdate();
			if (feverT > 0) {
				// Frenzy: a shower of bomb-free fruit, routed around anything already airborne.
				feverT -= kDt;
#ifdef AUTOTEST
				statFeverTime += kDt;
#endif
				if (feverT <= 0) {
					feverT = 0;
					feverGrace = kFrenzyGrace;
					feverMeter = kAfterFrenzy;
				}
				feverTimer -= kDt;
				if (feverTimer <= 0) {
					feverTimer = mode == MODE_FRENZY && !sLightFrenzy ? 0.12f : 0.2f;
					float x = 160, vx = 0, vy = -300;
					pickThrow(false, 0, x, vx, vy);
					queue(0, x, vx, vy, OBJ_FRUIT, normalFruit(gRng));
				}
			} else if (frenzyT > 0) {
				frenzyT -= kDt;
				frenzyTimer -= kDt;
				if (frenzyTimer <= 0) {
					frenzyTimer = 0.16f;
					bool left = gRng.below(2);
					float x = left ? -18.0f : kFieldW + 18.0f;
					float vy = -gRng.range(270, 350);
					float vx = (left ? 1 : -1) * gRng.range(90, 150);
					// Try the other edge if this arc would cross a bomb.
					if (clearance(x, vx, vy, 0, false) < kWantClearance) {
						x = kFieldW - x;
						vx = -vx;
					}
					queue(0, x, vx, vy, OBJ_FRUIT, normalFruit(gRng));
				}
			} else if (mode != MODE_PRACTICE) {
				waveTimer -= objDt;
				if (waveTimer <= 0) spawnWave();
			}
			for (auto& p : pending) {
				if (!p.on) continue;
				p.delay -= objDt;
				if (p.delay <= 0) {
					launch(p);
					p.on = false;
				}
			}
		}
		updateObjects(objDt);
		if (phase == PHASE_PLAY) {
			recordStroke(blade);
			testSlices(blade);
		}
		if (comboCount > 0) {
			comboTimer -= kDt;
			if (comboTimer <= 0 || !blade.active) finishCombo();
		}

		// The fuse sizzles whenever a bomb is in the air, panned towards it.
		const Obj* bomb = nullptr;
		for (const auto& o : objs)
			if (o.on && o.kind == OBJ_BOMB && o.y < 250) bomb = &o;
		audio::fuse(bomb != nullptr && phase == PHASE_PLAY, bomb ? fieldPan(bomb->x) : 0);

#ifdef AUTOTEST
		bool anyBomb = false, overlap = false;
		for (const auto& b : objs) {
			if (!b.on || b.kind != OBJ_BOMB || b.y > 240) continue;
			anyBomb = true;
			for (const auto& f : objs) {
				if (!f.on || f.kind == OBJ_BOMB || f.y > 240) continue;
				float dx = f.x - b.x, dy = f.y - b.y, r = f.radius + b.radius;
				if (dx * dx + dy * dy < r * r) overlap = true;
			}
		}
		statBombFrames += anyBomb;
		statOverlapFrames += overlap;
#endif
		return;
	}

	if (phase == PHASE_FINALE) {
		updateFinale(blade);
		return;
	}

	if (phase == PHASE_DYING) {
		updateObjects(kDt * 0.5f);
		if (phaseT >= (finaleHits > 0 ? 2.1f : 1.2f)) {
			phase = PHASE_OVER;
			phaseT = 0;
			commitScore();
			audio::play(newBest ? SFX_BEST : SFX_OVER);
		}
		return;
	}

	updateObjects(kDt);
}

int Game::activeObjects() const {
	int n = 0;
	for (const auto& o : objs) n += o.on;
	return n;
}
int Game::activeParticles() const {
	int n = 0;
	for (const auto& p : parts) n += p.on;
	return n;
}

const Obj* Game::botTarget(bool wantBomb) const {
	const Obj* bestO = nullptr;
	for (const auto& o : objs) {
		if (!o.on || o.y > 225 || o.y < 15 || o.x < 15 || o.x > kFieldW - 15) continue;
		if (o.hitCooldown > 0) continue;
		bool isBomb = o.kind == OBJ_BOMB;
		if (isBomb != wantBomb) continue;
		if (!bestO || o.y > bestO->y) bestO = &o;
	}
	return bestO;
}

// ---------------------------------------------------------------- drawing
static inline bool onThisScreen(float y, float r, bool top, float oy) {
	float sy = y + oy;
	if (top) return sy + r > 0 && sy - r < 240;
	return sy + r > 0 && sy - r < 300;  // screen shake and tall windows can show a little past the field
}

static constexpr float kFruitZ = 8.0f;
static constexpr float kBombZ = 42.0f;  // in front of the fruit, so it's never hidden behind one

void Game::drawObjects3D(bool top, float ox, float oy) const {
	bool any = finaleOn && !top;
	for (const auto& h : halves) any = any || (h.on && onThisScreen(h.y, 30, top, oy));
	for (const auto& o : objs) any = any || (o.on && o.kind <= OBJ_BOMB && onThisScreen(o.y, 34, top, oy));
	if (!any) return;

	jobs::wait();  // any cuts asked for this frame must be finished before the GPU sees them
	m3d::begin(top ? 400 : kFieldW, gfx::stereoPerZ());
	for (const auto& h : halves) {
		if (!h.on || !onThisScreen(h.y, 30, top, oy)) continue;
		m3d::Pose pose = {h.x + ox, h.y + oy, kFruitZ, h.rx, h.ry, h.rot, 1, h.axisX, h.axisY, h.tilt};
		if (h.cut >= 0) m3d::drawCut(h.cut, pose);
		else m3d::draw(h.mesh, (m3d::Part)h.part, pose);
	}
	// Bombs go last, so they always show on top of fruit even where there's no depth buffer.
	for (int pass = 0; pass < 2; pass++) {
		for (const auto& o : objs) {
			if (!o.on || o.kind > OBJ_BOMB || (o.kind == OBJ_BOMB) != (pass == 1) || !onThisScreen(o.y, 34, top, oy)) continue;
			int mesh = o.kind == OBJ_BOMB ? m3d::MESH_BOMB : (o.kind == OBJ_GOLD ? m3d::MESH_GOLD : kFruit[o.fruit].mesh);
			m3d::Pose pose = {o.x + ox, o.y + oy, o.kind == OBJ_BOMB ? kBombZ : kFruitZ, o.rx, o.ry, o.rot, 1, 0, 0, 0};
			m3d::draw(mesh, m3d::PART_WHOLE, pose);
		}
	}
	if (finaleOn && !top) {
		float s = 1.0f + finaleSwell + sinf(finaleT * 11) * 0.015f;
		m3d::Pose pose = {finaleX + finaleJoltX, finaleY + finaleJoltY, 0, 0.25f, finaleT * 1.3f, sinf(finaleT * 3.3f) * 0.1f + finaleJoltX * 0.012f, s, 0, 0, 0};
		m3d::draw(m3d::MESH_SUNBURST, m3d::PART_WHOLE, pose);
	}
	m3d::end();
}

void Game::drawObjects2D(bool top, float ox, float oy) const {
	if (top) gfx::setDepth(0.7f);
	for (const auto& h : halves) {
		if (h.on && onThisScreen(h.y, 30, top, oy)) gfx::sprite(SHEET_FRUIT, h.sprite, h.x + ox, h.y + oy, 1, 1, h.rot);
	}
	// Fruit first and bombs afterwards, so a bomb is never hidden behind a fruit.
	for (const auto& o : objs) {
		if (!o.on || o.kind > OBJ_GOLD || !onThisScreen(o.y, 34, top, oy)) continue;
		gfx::sprite(SHEET_FRUIT, o.kind == OBJ_GOLD ? kGold.whole : kFruit[o.fruit].whole, o.x + ox, o.y + oy, 1, 1, o.rot);
	}
	if (top) gfx::setDepth(1.1f);
	for (const auto& o : objs) {
		if (o.on && o.kind == OBJ_BOMB && onThisScreen(o.y, 34, top, oy)) gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, o.x + ox, o.y + oy, 1, 1, o.rot);
	}
	if (finaleOn && !top) {
		float s = 1.0f + finaleSwell + sinf(finaleT * 11) * 0.015f;
		float wobble = sinf(finaleT * 3.3f) * 0.1f + finaleJoltX * 0.012f;
		gfx::sprite(SHEET_FRUIT, fruit_sunburst_idx, finaleX + finaleJoltX, finaleY + finaleJoltY, s, s, wobble);
	}
}

void Game::drawMarks(bool top, float ox, float oy) const {
	if (!gSave.highContrast && !gSave.colorBlind) return;
	for (const auto& o : objs) {
		if (!o.on || o.kind > OBJ_GOLD || !onThisScreen(o.y, 40, top, oy)) continue;
		if (gSave.highContrast) {
			int spr = o.kind == OBJ_BOMB ? fruit_bomb_idx : (o.kind == OBJ_GOLD ? kGold.whole : kFruit[o.fruit].whole);
			gfx::sprite(SHEET_FRUIT, spr, o.x + ox, o.y + oy, 1.14f, 1.14f, o.rot, rgba(240, 240, 240, 235));  // light, on the dark board
		}
		if (o.kind == OBJ_BOMB) drawBombMark(o.x + ox, o.y + oy, o.radius, o.age);
	}
}

static int powerSprite(ObjKind k) {
	switch (k) {
		case OBJ_FREEZE: return sprites_pw_freeze_idx;
		case OBJ_DOUBLE: return sprites_pw_double_idx;
		case OBJ_FRENZY: return sprites_pw_frenzy_idx;
		case OBJ_MAGNET: return sprites_pw_magnet_idx;
		case OBJ_WIDE: return sprites_pw_wide_idx;
		case OBJ_SHIELD: return sprites_pw_shield_idx;
		case OBJ_TIME: return sprites_pw_time_idx;
		default: return -1;
	}
}

// The coin flips end over end with only 2D draws: it narrows as it turns, shows its back past the
// halfway point, shows its milled edge while side-on, and a shine slides across its face.
static void drawCoin(const Obj& o, float ox, float oy) {
	float ph = o.age * o.rotv, c = cosf(ph), w = fabsf(c);
	float x = o.x + ox, y = o.y + oy, tilt = sinf(o.age * 1.7f) * 0.2f;
	if (w > 0.1f) gfx::sprite(SHEET_SPRITES, c >= 0 ? sprites_coin_idx : sprites_coin_back_idx, x, y, w * 0.82f, 0.82f, tilt);
	if (w < 0.4f) gfx::sprite(SHEET_SPRITES, sprites_coin_rim_idx, x, y, 0.82f * (1 - w / 0.4f) + 0.2f, 0.82f, tilt);
	if (w > 0.3f && gSave.effects != FX_NONE) gfx::sprite(SHEET_SPRITES, sprites_spark_idx, x + sinf(ph) * 11 * w, y - 6, 0.45f, 0.45f, 0, withAlpha(WHITE, 0.75f * w));
}

void Game::drawField(const Blade& blade) const {
	const bool solid = use3D();
#if PORT_TWO_SCREENS
	if (gSave.highContrast) gfx::rect(-6, -6, 332, 252, rgba(16, 16, 20));
	else gfx::image(SHEET_BOARD, 0, -6, -6, 332, 252);
#endif
	if (freezeT > 0) gfx::rect(kFillX, 0, kFillW, 240, rgba(120, 200, 255, 46));
	if (feverT > 0) gfx::rect(kFillX, 0, kFillW, 240, gSave.calmFx ? withAlpha(COL_ACCENT, 0.08f) : withAlpha(hueColor(elapsed * 0.25f), 0.13f + sinf(elapsed * 7) * 0.03f));

	for (const auto& s : splats) {
		if (!s.on) continue;
		gfx::sprite(SHEET_SPRITES, sprites_splat0_idx + s.variant, s.x, s.y, s.scale, s.scale, s.rot,
		            withAlpha(s.color, clampf(s.life, 0, 1) * 0.75f));
	}
	// Soft shadows on the board give solid fruit somewhere to float above.
	// Soft adds a wide, faint outer shadow around the simple one.
	const int shadows = PORT_SHADOWS;
	if (solid && shadows > 0 && !sLightFrenzy) {
		for (const auto& o : objs) {
			if (!o.on || o.y <= -20 || o.kind > OBJ_BOMB) continue;
			if (shadows >= 2) gfx::sprite(SHEET_SPRITES, sprites_glow_idx, o.x + 10, o.y + 14, 1.6f, 1.6f, 0, rgba(0, 0, 0, 55));
			gfx::sprite(SHEET_SPRITES, sprites_glow_idx, o.x + 7, o.y + 10, 1.15f, 1.15f, 0, rgba(0, 0, 0, shadows >= 2 ? 95 : 110));
		}
	}
	// Bloom: Off keeps only the glows that warn or guide; High widens them and adds a faint outer halo.
	const int bloom = PORT_BLOOM;
	const float bloomSize = bloom >= 2 ? 1.25f : 1.0f;
	// Glows sit behind the fruit. A bomb's warning glow stays on at every Effects setting.
	for (const auto& o : objs) {
		if (!o.on || o.y < -30) continue;
		if (o.kind == OBJ_BOMB) {
			float g = 1.25f + sinf(o.age * 14) * 0.25f;
			gfx::sprite(SHEET_SPRITES, sprites_glow_idx, o.x, o.y, g * 1.5f, g * 1.5f, 0, gSave.colorBlind == 1 || gSave.colorBlind == 2 ? rgba(110, 130, 255, 200) : rgba(255, 50, 40, 190));
		} else if (o.kind == OBJ_GOLD && gSave.effects != FX_NONE && bloom > 0) {
			float g = (1.1f + sinf(o.age * 9) * 0.1f) * bloomSize;
			if (bloom >= 2) gfx::sprite(SHEET_SPRITES, sprites_glow_idx, o.x, o.y, g * 2.4f, g * 2.4f, 0, rgba(255, 220, 90, 70));
			gfx::sprite(SHEET_SPRITES, sprites_glow_idx, o.x, o.y, g * 1.5f, g * 1.5f, 0, rgba(255, 220, 90, 200));
		}
	}
	if (finaleOn) {
		float urgency = clampf((finaleT - kFinaleRise) / kFinaleWindow, 0, 1);
		float g = 1.9f + finaleSwell * 3 + sinf(finaleT * (6 + urgency * 14)) * (0.12f + urgency * 0.2f);
		g *= bloom > 0 ? bloomSize : 0.8f;
		if (bloom >= 2) gfx::sprite(SHEET_SPRITES, sprites_glow_idx, finaleX, finaleY, g * 2.3f, g * 2.3f, 0, rgba(255, 190, 70, 60));
		gfx::sprite(SHEET_SPRITES, sprites_glow_idx, finaleX, finaleY, g * 1.5f, g * 1.5f, 0, rgba(255, 190, 70, 170));
	}

	drawMarks(false, 0, 0);
	if (solid) drawObjects3D(false, 0, 0);
	else drawObjects2D(false, 0, 0);

	for (const auto& o : objs) {
		if (!o.on) continue;
		switch (o.kind) {
			case OBJ_FRUIT:
				if (o.hits == 1 && o.fruit == kCoconut) gfx::sprite(SHEET_SPRITES, sprites_crack_idx, o.x, o.y, 1, 1, o.rot);
				break;
			case OBJ_COIN: drawCoin(o, 0, 0); break;
			default:
				if (powerSprite(o.kind) >= 0) gfx::sprite(SHEET_SPRITES, powerSprite(o.kind), o.x, o.y, 1, 1, sinf(o.age * 3) * 0.3f);
				break;
		}
	}
	if (finaleOn && finaleT > kFinaleRise) {
		// Time left, as a bar along the top of the field.
		float left = 1 - (finaleT - kFinaleRise) / kFinaleWindow;
		gfx::rect(kFieldW * 0.5f - 130, kFieldTop + 8, 260, 8, rgba(16, 10, 34, 190));
		gfx::rect(kFieldW * 0.5f - 130, kFieldTop + 8, 260 * clampf(left, 0, 1), 8, left < 0.3f ? rgba(255, 90, 80) : rgba(255, 210, 90));
	}
	for (const auto& r : rings) {
		if (!r.on) continue;
		float t = 1 - r.life / r.maxLife;
		float s = 0.2f + t * r.maxScale;
		gfx::sprite(SHEET_SPRITES, sprites_ring_idx, r.x, r.y, s, s, 0, withAlpha(r.color, (1 - t) * 0.9f));
	}
	jobs::wait();
	for (const auto& p : parts) {
		if (!p.on || p.y < -8) continue;
		float life = p.life / p.maxLife;
		gfx::sprite(SHEET_SPRITES, p.sprite ? sprites_spark_idx : sprites_drop_idx, p.x, p.y, p.size, p.size, 0,
		            withAlpha(p.color, clampf(life * 1.6f, 0, 1)));
	}
	blade.draw();
	for (const auto& p : popups) {
		if (!p.on) continue;
		gfx::text(p.text, p.x, p.y - 12, p.scale * uiScale(), withAlpha(p.color, clampf(p.life * 2.5f, 0, 1)), ALIGN_CENTER);
	}
	drawSwipeReplay();
	if (mode == MODE_PRACTICE) drawPractice();
	if (flash > 0) gfx::rect(kFillX, 0, kFillW, 240, withAlpha(flashColor, clampf(flash, 0, 1) * (gSave.calmFx ? 0.25f : 1.0f)));
	drawCaptions(240 - 4, kFieldW * 0.5f);
}

#if PORT_TWO_SCREENS
void Game::drawFieldTop() const {
	drawMarks(true, kTopOffsetX, kTopOffsetY);
	if (use3D()) drawObjects3D(true, kTopOffsetX, kTopOffsetY);
	else drawObjects2D(true, kTopOffsetX, kTopOffsetY);
	// Power-ups can fly high too; juice follows them up when Effects allows it.
	gfx::setDepth(0.7f);
	for (const auto& o : objs) {
		if (!o.on || o.kind < OBJ_FREEZE || !onThisScreen(o.y, 34, true, kTopOffsetY)) continue;
		if (o.kind == OBJ_COIN) {
			drawCoin(o, kTopOffsetX, kTopOffsetY);
			continue;
		}
		int spr = powerSprite(o.kind);
		if (spr < 0) continue;
		gfx::sprite(SHEET_SPRITES, spr, o.x + kTopOffsetX, o.y + kTopOffsetY, 1, 1, sinf(o.age * 3) * 0.3f);
	}
	if (gSave.effects >= FX_MEDIUM) {
		jobs::wait();
		gfx::setDepth(0.9f);
		for (const auto& p : parts) {
			if (!p.on || p.y > -16) continue;
			float life = p.life / p.maxLife;
			gfx::sprite(SHEET_SPRITES, p.sprite ? sprites_spark_idx : sprites_drop_idx, p.x + kTopOffsetX, p.y + kTopOffsetY, p.size, p.size, 0,
			            withAlpha(p.color, clampf(life * 1.6f, 0, 1)));
		}
	}
}

void Game::drawHud() const {
	char buf[32];
	// Status panel plane.
	gfx::setDepth(0.5f);
	gfx::sign(sprites_plank_idx, 6, 2, 388, 76, COL_PLANK);

	// Score: closest plane.
	gfx::setDepth(1.3f);
	float pulse = 1.0f + clampf(scorePulse, 0, 1) * 0.16f;
	snprintf(buf, sizeof buf, "%lu", (unsigned long)score);
	gfx::text(buf, 24, 14 - (pulse - 1) * 20, 1.25f * pulse, feverT > 0 ? cycleColor(elapsed * 0.6f) : COL_ACCENT);

	gfx::setDepth(0.9f);
	gfx::text(modeName(mode), 380, 10, 0.5f, COL_CREAM, ALIGN_RIGHT);
	snprintf(buf, sizeof buf, assisted ? "ASSISTED BEST %lu" : "BEST %lu", (unsigned long)(score > best ? score : best));
	gfx::text(buf, 380, 28, 0.42f, COL_DIM, ALIGN_RIGHT);

	if (maxStrikes > 0) {
		for (int i = 0; i < maxStrikes; i++) {
			bool used = i < strikes;
			float s = 0.7f + (used && i == strikes - 1 ? clampf(strikePulse, 0, 1) * 0.6f : 0);
			gfx::sprite(SHEET_SPRITES, used ? sprites_pip_on_idx : sprites_pip_off_idx,
			            368 - (maxStrikes - 1 - i) * 24, 57, s, s);
		}
	}
	if (timed()) {
		int secs = (int)ceilf(timeLeft);
		snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
		bool urgent = secs <= 10 && phase == PHASE_PLAY;
		float s = urgent ? 0.62f + 0.06f * sinf(elapsed * 12) : 0.56f;
		gfx::text(buf, 380, 46, s, urgent ? (gSave.colorBlind == 1 || gSave.colorBlind == 2 ? rgba(255, 200, 40) : rgba(255, 110, 100)) : COL_CREAM, ALIGN_RIGHT);
	}

	// Streak and combo sit to the right of the score, stacked so they never overlap it.
	gfx::setDepth(0.7f);
	snprintf(buf, sizeof buf, "%lu", (unsigned long)score);
	float px = 24 + gfx::textWidth(buf, 1.25f) + 14;
	if (chain >= 2 || comboFadeT > 0) {
		const int shown = chain >= 2 ? chain : fadingChain;
		char line[20];
		snprintf(line, sizeof line, "COMBO %d", shown);
		gfx::text(line, px, 16, 0.5f + clampf(shown * 0.004f, 0, 0.14f), chain >= 2 ? COL_CREAM : withAlpha(COL_CREAM, comboFadeT / kComboFade));
	}
	if (comboCount >= 2) {
		char line[20];
		snprintf(line, sizeof line, "SWIPE X%d", comboCount);
		gfx::text(line, px, 38, 0.42f, cycleColor(comboCount * 0.09f));
	}
	// Coins made this run, under the score.
	if (mode != MODE_PRACTICE) {
		float cp = 1.0f + clampf(coinPulse, 0, 1) * 0.3f;
		snprintf(buf, sizeof buf, "%lu", (unsigned long)runCoins());
		gfx::sprite(SHEET_SPRITES, sprites_coin_idx, 33, 61, 0.42f * cp, 0.42f * cp);
		gfx::text(buf, 46, 54, 0.44f, COL_CREAM);
	}

	// The Fruit Frenzy meter.
	gfx::setDepth(0.6f);
	const float bx = 24, by = 80, bw = 352;
	gfx::rect(bx - 2, by - 2, bw + 4, 11, gfx::themeBar());
	if (feverT > 0) {
		gfx::rect(bx, by, bw * clampf(feverT / kFrenzySeconds, 0, 1), 7, cycleColor(elapsed * 0.7f));
		gfx::text("FRUIT FRENZY X2", 200, by + 9, 0.42f, cycleColor(elapsed * 0.7f), ALIGN_CENTER);
	} else {
		gfx::rect(bx, by, bw * clampf(feverMeter, 0, 1), 7, COL_ACCENT);
	}

	// Active power-up meters.
	struct Meter { float t, max; int spr; };
	const Meter meters[] = {{freezeT, 6, sprites_pw_freeze_idx}, {doubleT, 8, sprites_pw_double_idx}, {frenzyT, 5, sprites_pw_frenzy_idx}, {magnetT, 6, sprites_pw_magnet_idx}, {wideT, 8, sprites_pw_wide_idx}, {shield ? 1.0f : 0.0f, 1, sprites_pw_shield_idx}};
	float mx = 24;
	for (const auto& m : meters) {
		if (m.t <= 0) continue;
		gfx::sprite(SHEET_SPRITES, m.spr, mx + 8, 112, 0.42f, 0.42f);
		gfx::rect(mx + 22, 108, 60, 8, rgba(0, 0, 0, 120));
		gfx::rect(mx + 22, 108, 60 * clampf(m.t / m.max, 0, 1), 8, COL_CREAM);
		mx += 94;
	}

	if (phase == PHASE_FINALE && finaleHits > 0) {
		gfx::setDepth(1.9f);
		snprintf(buf, sizeof buf, "%d", finaleHits);
		float s = 1.5f + clampf(finaleHits * 0.012f, 0, 0.7f);
		gfx::text(buf, 200, 136, s * 0.85f, cycleColor(finaleHits * 0.025f), ALIGN_CENTER);
	}

	if (phase == PHASE_READY) {
		gfx::setDepth(1.6f);
		bool go = phaseT > 0.9f;
		float s = go ? 1.8f : 1.2f + 0.1f * sinf(phaseT * 8);
		gfx::text(go ? "GO!" : "READY", 200, 118 - s * 20, s, go ? rgba(140, 240, 150) : rgba(255, 230, 140), ALIGN_CENTER);
	} else if (calloutT > 0) {
		gfx::setDepth(1.6f);
		float in = clampf((1.4f - calloutT) * 8, 0, 1);
		float s = calloutSize + (1 - in) * 0.5f;
		// Long combo lines must always fit on the top screen.
		float w = gfx::textWidth(callout, s);
		if (w > 380) s *= 380 / w;
		gfx::text(callout, 200, phase == PHASE_FINALE ? 88 : 120, s, withAlpha(calloutColor, clampf(calloutT * 2.5f, 0, 1)), ALIGN_CENTER);
	}
}
#else
void Game::drawFieldTop() const {}

// One screen: a slim score sign across the top of the field, with the meter beneath it. UI Scale makes
// the sign taller and everything on it bigger.
void Game::drawHud() const {
	char buf[32];
	const float u = uiScale();
	const float right = kFieldW - (PORT_PAUSE_BUTTON ? 38 : 22);  // clear of the sign's screws
	gfx::sign(sprites_plank_idx, 2, 1, kFieldW - 4, 29 * u, COL_PLANK);

	float pulse = 1.0f + clampf(scorePulse, 0, 1) * 0.16f;
	snprintf(buf, sizeof buf, "%lu", (unsigned long)score);
	gfx::text(buf, 16, 3 * u - (pulse - 1) * 10, 0.58f * u * pulse, feverT > 0 ? cycleColor(elapsed * 0.6f) : COL_ACCENT);
	float px = 16 + gfx::textWidth(buf, 0.58f * u) + 10;
	if (chain >= 2 || comboFadeT > 0) {
		snprintf(buf, sizeof buf, "COMBO %d", chain >= 2 ? chain : fadingChain);
		gfx::text(buf, px, 9 * u, 0.32f * u, chain >= 2 ? COL_CREAM : withAlpha(COL_CREAM, comboFadeT / kComboFade));
		px += gfx::textWidth(buf, 0.32f * u) + 10;
	}
	if (comboCount >= 2) {
		snprintf(buf, sizeof buf, "SWIPE X%d", comboCount);
		gfx::text(buf, px, 9 * u, 0.32f * u, cycleColor(comboCount * 0.09f));
	}

	float rx = right;
	if (timed()) {
		int secs = (int)ceilf(timeLeft);
		snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
		bool urgent = secs <= 10 && phase == PHASE_PLAY;
		gfx::text(buf, rx, 5 * u, 0.46f * u, urgent ? (gSave.colorBlind == 1 || gSave.colorBlind == 2 ? rgba(255, 200, 40) : rgba(255, 110, 100)) : COL_CREAM, ALIGN_RIGHT);
		rx -= 62 * u;
	}
	if (maxStrikes > 0) {
		for (int i = 0; i < maxStrikes; i++) {
			bool used = i < strikes;
			float s = (0.5f + (used && i == strikes - 1 ? clampf(strikePulse, 0, 1) * 0.4f : 0)) * u;
			gfx::sprite(SHEET_SPRITES, used ? sprites_pip_on_idx : sprites_pip_off_idx, rx - 8 * u - (maxStrikes - 1 - i) * 17 * u, 15 * u, s, s);
		}
		rx -= (maxStrikes * 17 + 6) * u;
	}
	snprintf(buf, sizeof buf, assisted ? "ASSISTED BEST %lu" : "BEST %lu", (unsigned long)(score > best ? score : best));
	gfx::text(buf, rx, 9 * u, 0.3f * u, COL_DIM, ALIGN_RIGHT);

	const float bx = 12, by = 28 * u, bw = kFieldW - 24;
	gfx::rect(bx - 1, by - 1, bw + 2, 6, gfx::themeBar());
	if (feverT > 0) {
		gfx::rect(bx, by, bw * clampf(feverT / kFrenzySeconds, 0, 1), 4, cycleColor(elapsed * 0.7f));
		gfx::text("FRUIT FRENZY X2", kFieldW * 0.5f, by + 6, 0.3f, cycleColor(elapsed * 0.7f), ALIGN_CENTER);
	} else {
		gfx::rect(bx, by, bw * clampf(feverMeter, 0, 1), 4, COL_ACCENT);
	}

	struct Meter { float t, max; int spr; };
	const Meter meters[] = {{freezeT, 6, sprites_pw_freeze_idx}, {doubleT, 8, sprites_pw_double_idx}, {frenzyT, 5, sprites_pw_frenzy_idx}, {magnetT, 6, sprites_pw_magnet_idx}, {wideT, 8, sprites_pw_wide_idx}, {shield ? 1.0f : 0.0f, 1, sprites_pw_shield_idx}};
	float mx = PORT_PAUSE_BUTTON ? 44 : 12;  // the pause sign has the corner
	// Coins made this run, just under the score, ahead of any power-up meters.
	if (mode != MODE_PRACTICE) {
		float cp = 1.0f + clampf(coinPulse, 0, 1) * 0.3f;
		snprintf(buf, sizeof buf, "%lu", (unsigned long)runCoins());
		gfx::sprite(SHEET_SPRITES, sprites_coin_idx, mx + 7, by + 16, 0.36f * cp, 0.36f * cp);
		gfx::text(buf, mx + 17, by + 10, 0.34f, COL_CREAM);
		mx += 17 + gfx::textWidth(buf, 0.34f) + 14;
	}
	for (const auto& m : meters) {
		if (m.t <= 0) continue;
		gfx::sprite(SHEET_SPRITES, m.spr, mx + 8, by + 16, 0.36f, 0.36f);
		gfx::rect(mx + 20, by + 13, 50, 6, rgba(0, 0, 0, 120));
		gfx::rect(mx + 20, by + 13, 50 * clampf(m.t / m.max, 0, 1), 6, COL_CREAM);
		mx += 80;
	}

	const float cx = kFieldW * 0.5f;
	if (phase == PHASE_FINALE && finaleHits > 0) {
		snprintf(buf, sizeof buf, "%d", finaleHits);
		float s = 1.1f + clampf(finaleHits * 0.01f, 0, 0.5f);
		gfx::text(buf, cx, 48, s, cycleColor(finaleHits * 0.025f), ALIGN_CENTER);
	}
	if (phase == PHASE_READY) {
		bool go = phaseT > 0.9f;
		float s = go ? 1.8f : 1.2f + 0.1f * sinf(phaseT * 8);
		gfx::text(go ? "GO!" : "READY", cx, 118 - s * 20, s, go ? rgba(140, 240, 150) : rgba(255, 230, 140), ALIGN_CENTER);
	} else if (calloutT > 0) {
		float in = clampf((1.4f - calloutT) * 8, 0, 1);
		float s = calloutSize + (1 - in) * 0.5f;
		float w = gfx::textWidth(callout, s);
		if (w > kFieldW - 20) s *= (kFieldW - 20) / w;
		gfx::text(callout, cx, phase == PHASE_FINALE ? 192 : 96, s, withAlpha(calloutColor, clampf(calloutT * 2.5f, 0, 1)), ALIGN_CENTER);
	}
}
#endif

bool Game::fruitAhead(float x, float y, float dx, float dy, float range, float* fx, float* fy) const {
	float best = range * range;
	bool found = false;
	for (const auto& o : objs) {
		if (!o.on || o.kind == OBJ_BOMB || o.kind > OBJ_GOLD) continue;
		float ox = o.x - x, oy = o.y - y, d2 = ox * ox + oy * oy;
		// Ahead: within about 50 degrees of the way the point is going.
		if (d2 >= best || ox * dx + oy * dy < sqrtf(d2) * 0.64f) continue;
		best = d2;
		*fx = o.x;
		*fy = o.y;
		found = true;
	}
	return found;
}
