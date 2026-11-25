// Boss Rush, Recipe, Sharpshooter and Juggle: the four modes 1.2 adds next to Orchard Blast.
// Boss Rush runs on Orchard Blast's HP, charms and bosses (orchard.cpp, boss.cpp); the other three are
// played on the usual throws, with a rule of their own on top.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "boom.h"
#include "fruitids.h"
#include "game.h"
#include "progress.h"
#include "render.h"
#include "sprites.h"

const RushBuildInfo kRushBuilds[BUILD_COUNT] = {
	{"Tank", "Double HP and 2 Armor.", "A Smoothie in a slot."},
	{"Glass Cannon", "Half HP. Guards stay down", "far longer. Cleaver, Boss Bane."},
	{"Charmer", "Less HP, both slots filled", "and 2 passive charms."},
};

const char* sharpName(int c) {
	static const char* const kNames[kSharpColours] = {"red circle", "blue square", "yellow triangle", "green star"};
	return kNames[(c % kSharpColours + kSharpColours) % kSharpColours];
}

u32 sharpColour(int c, bool strong) {
	// Strong is for Colour-blind Mode and High Contrast: brighter, further apart. The shapes carry the meaning anyway.
	switch (c) {
		case 0: return strong ? rgba(255, 60, 40) : rgba(232, 78, 70);
		case 1: return strong ? rgba(40, 140, 255) : rgba(84, 150, 250);
		case 2: return strong ? rgba(255, 230, 0) : rgba(250, 210, 70);
		default: return strong ? rgba(40, 220, 90) : rgba(96, 200, 100);
	}
}

static bool sharpStrong() { return gSave.colorBlind != 0 || gSave.highContrast != 0; }

// A filled shape for a Sharpshooter colour, with a dark edge so it reads on any fruit or board.
static void sharpShape(int c, float x, float y, float r, u32 col) {
	const u32 ink = rgba(30, 20, 30, (col >> 24) & 0xFF);
	for (int pass = 0; pass < 2; pass++) {
		const float rr = pass == 0 ? r + 1.6f : r;
		const u32 k = pass == 0 ? ink : col;
		switch (c % kSharpColours) {
			case 0:
				for (int i = 0; i < 12; i++) {
					const float a0 = i * 0.5236f, a1 = (i + 1) * 0.5236f;
					gfx::tri(x, y, k, x + cosf(a0) * rr, y + sinf(a0) * rr, k, x + cosf(a1) * rr, y + sinf(a1) * rr, k);
				}
				break;
			case 1: gfx::rect(x - rr * 0.85f, y - rr * 0.85f, rr * 1.7f, rr * 1.7f, k); break;
			case 2: gfx::tri(x, y - rr * 1.1f, k, x + rr * 1.05f, y + rr * 0.75f, k, x - rr * 1.05f, y + rr * 0.75f, k); break;
			default:
				for (int i = 0; i < 10; i++) {
					const float a0 = -1.5708f + i * 0.6283f, a1 = -1.5708f + (i + 1) * 0.6283f;
					const float r0 = i % 2 ? rr * 0.48f : rr * 1.15f, r1 = i % 2 ? rr * 1.15f : rr * 0.48f;
					gfx::tri(x, y, k, x + cosf(a0) * r0, y + sinf(a0) * r0, k, x + cosf(a1) * r1, y + sinf(a1) * r1, k);
				}
				break;
		}
	}
}

// ---------------------------------------------------------------- start
void Game::m12Start() {
	switch (mode) {
		case MODE_BOSSRUSH:
			// No rooms: a build first, then the bosses one after another.
			orc.room = 0;
			orc.bossRoom = true;
			m12.rushPicking = true;
			m12.rushBuild = BUILD_NONE;
			phase = PHASE_PLAY;
			gravity = lerpf(300, 430, 0.5f) * kFieldK * levelGravity(gSave.difficulty) * (mix[MIX_LOW_GRAVITY] ? 0.6f : 1.0f);
			setCallout("PICK A BUILD", rgba(255, 230, 140));
			break;
		case MODE_RECIPE: recipeNew(); break;
		case MODE_SHARP:
			m12.call = (u8)gRng.below(kSharpColours);
			m12.callT = 7;
			m12.callFlash = 1;
			break;
		case MODE_JUGGLE: break;
		default: break;
	}
}

// ---------------------------------------------------------------- Boss Rush
void Game::orcApplyRushPreset(int build) {
	const OrchardLevel& lv = orchardLevel(gSave.difficulty);
	auto passive = [this](int card) {
		orc.upgrade[card] = 1;
		orc.passiveOrder[orc.passiveCount++] = (u8)card;
	};
	switch (build) {
		case BUILD_TANK:
			orc.maxHp = (float)lv.startHp * 2;
			orc.upgrade[CARD_ARMOR] = 2;
			orc.slot[0] = CARD_SMOOTHIE;
			break;
		case BUILD_GLASS:
			orc.maxHp = (float)lv.startHp;
			passive(CARD_GLASS);
			orcApply(CARD_GLASS);  // halves Max HP
			orc.upgrade[CARD_BLADE] = 3;
			orc.slot[0] = CARD_CLEAVER;
			orc.slot[1] = CARD_BOSSBANE;
			break;
		default:
			orc.maxHp = floorf(lv.startHp * 0.8f);
			passive(CARD_JUICE);
			passive(CARD_THICKPEEL);
			orc.slot[0] = CARD_FREEZEPOP;
			orc.slot[1] = CARD_GOLDAPPLE;
			break;
	}
	orc.hp = orc.maxHp;
}

void Game::rushPick(int build) {
	if (mode != MODE_BOSSRUSH || !m12.rushPicking || build < 0 || build >= BUILD_COUNT) return;
	m12.rushBuild = (u8)build;
	orcApplyRushPreset(build);
	orc.peelShield = orc.upgrade[CARD_THICKPEEL] != 0;
	m12.rushPicking = false;
	m12.rushStage = 0;
	phase = PHASE_READY;
	phaseT = 0;
	audio::play(SFX_POWER, 0.8f);
	bossStart(BOSS_MELON);
}

// After each boss: half the HP comes back, then the next one. The second lap is the Royals.
void Game::rushNext() {
	if (score < (u32)(m12.rushStage + 1)) award(1);
	m12.rushStage++;
	if (m12.rushStage >= kRushStages) {
		orcFinish(true);
		return;
	}
	orc.hp = fminf(orc.maxHp, orc.hp + orc.maxHp * 0.5f);
	orc.peelShield = orc.upgrade[CARD_THICKPEEL] != 0;
	orc.bombSquadUsed = false;
	addPopup("HALF YOUR HP BACK", kFieldW * 0.5f, kFieldTop + 70, rgba(160, 240, 170), 0.6f);
	for (auto& s : orc.shots) s.on = false;
	if (m12.rushStage == kRushStages - 1) {
		const int mid = kFirstMilestoneBoss + gRng.below(kMilestoneBossCount);
		bossStart(mid, false);
		return;
	}
	const int bossId = m12.rushStage % kRegularBossCount;
	bossStart(bossId, m12.rushStage >= 2);
}

// ---------------------------------------------------------------- Recipe
void Game::recipeNew() {
	// Two kinds of fruit to start, three from the fourth cup; and one more fruit in every cup.
	const int kinds = m12.cups >= 3 ? 3 : 2;
	int total = 3 + m12.cups;
	if (total > 9) total = 9;
	m12.recipeKinds = kinds;
	for (int i = 0; i < kinds; i++) {
		int f = normalFruitOf(gRng);
		for (int tries = 0; tries < 20; tries++) {
			bool used = false;
			for (int j = 0; j < i; j++) used |= m12.recipeFruit[j] == f;
			if (!used) break;
			f = normalFruitOf(gRng);
		}
		m12.recipeFruit[i] = (u8)f;
		m12.recipeNeed[i] = 1;
		m12.recipeHave[i] = 0;
	}
	for (int left = total - kinds; left > 0; left--) m12.recipeNeed[gRng.below(kinds)]++;
	m12.cupT = 0;
}

static int recipeLeft(const Modes12& m) {
	int n = 0;
	for (int i = 0; i < m.recipeKinds; i++) n += m.recipeNeed[i] - m.recipeHave[i];
	return n;
}

// ---------------------------------------------------------------- waves
bool Game::m12Wave() {
	const float d = difficulty();
	if (mode == MODE_RECIPE) {
		gravity = lerpf(300, 400, d) * kFieldK * levelGravity(gSave.difficulty) * (mix[MIX_LOW_GRAVITY] ? 0.6f : 1.0f);
		const int n = 2 + gSpawn.below(2 + (int)(d * 2));
		const float bombChance = clampf((0.1f + 0.1f * d) * gSave.bombs / 50.0f * (mix[MIX_DOUBLE_BOMBS] ? 2.0f : 1.0f), 0, 0.5f);
		bool bombed = false;
		for (int i = 0; i < n; i++) {
			const bool bomb = !bombed && gSpawn.unit() < bombChance;
			bombed |= bomb;
			float x = 160, vx = 0, vy = -300;
			pickThrow(bomb, 0.18f * i, x, vx, vy);
			// About half are something the cup still needs.
			int fruit = normalFruitOf(gSpawn);
			if (gSpawn.unit() < 0.5f) {
				int want[kRecipeMax], w = 0;
				for (int k = 0; k < m12.recipeKinds; k++)
					if (m12.recipeHave[k] < m12.recipeNeed[k]) want[w++] = m12.recipeFruit[k];
				if (w > 0) fruit = want[gSpawn.below(w)];
			}
			queue(0.18f * i, x, vx, vy, bomb ? OBJ_BOMB : OBJ_FRUIT, bomb ? 0 : fruit);
		}
		waveTimer = lerpf(1.9f, 1.25f, d) * levelInterval(gSave.difficulty);
		maybeCoin(d);
		return true;
	}
	if (mode == MODE_JUGGLE) {
		// Slow, floaty throws, one at a time, a little more often as it goes. Bombs from 20 seconds in.
		gravity = 300 * kFieldK * (PORT_3DS ? 0.48f : 0.55f) * (mix[MIX_LOW_GRAVITY] ? 0.6f : 1.0f);
		const bool bomb = elapsed > 20 && gSpawn.unit() < 0.18f * gSave.bombs / 50.0f * (mix[MIX_DOUBLE_BOMBS] ? 2.0f : 1.0f);
		float x = 160, vx = 0, vy = -300;
		pickThrow(bomb, 0, x, vx, vy);
		queue(0, x, vx * 0.6f, vy, bomb ? OBJ_BOMB : OBJ_FRUIT, bomb ? 0 : normalFruitOf(gSpawn));
		waveTimer = elapsed < 1 ? 1.2f : lerpf(4.5f, 2.2f, d);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------- slices
bool Game::m12Sliced(const Obj& o) {
	if (mode == MODE_RECIPE) {
		if (o.kind != OBJ_FRUIT || o.fruit >= kSpiky || o.loose) return true;  // gold fruit and the like score as usual
		for (int i = 0; i < m12.recipeKinds; i++) {
			if (m12.recipeFruit[i] != o.fruit || m12.recipeHave[i] >= m12.recipeNeed[i]) continue;
			m12.recipeHave[i]++;
			if (recipeLeft(m12) > 0) return true;
			// The cup is full: points, a speed bonus, and 8 more seconds.
			const int size = 3 + m12.cups > 9 ? 9 : 3 + m12.cups;
			const int quick = 20 - (int)m12.cupT;
			const int bonus = 10 * size + (quick > 0 ? quick * 2 : 0);
			award(bonus);
			char buf[24];
			snprintf(buf, sizeof buf, "CUP! +%d", bonus);
			addPopup(buf, o.x, o.y - 30, rgba(255, 220, 120), 0.8f);
			timeLeft += kRecipeBonusTime;
			m12.cups++;
			m12.cupPulse = 1;
			setCallout("SMOOTHIE! +8 SECONDS", rgba(255, 200, 220));
			showCaption("Cup done: 8 more seconds.");
			audio::play(SFX_BEST, 0.8f);
			recipeNew();
			return true;
		}
		// The wrong fruit, or one too many: the cup is spoiled and a new recipe starts.
		m12.spoiled++;
		m12.spoilPulse = 1;
		addPopup("SPOILED!", o.x, o.y - 20, rgba(255, 120, 100), 0.7f);
		setCallout("SPOILED! NEW RECIPE", rgba(255, 140, 110));
		showCaption("Wrong fruit: the cup is spoiled.");
		audio::play(SFX_KNOCK, 0.9f, fieldPan(o.x), 0.6f);
		recipeNew();
		return false;
	}
	if (mode == MODE_SHARP) {
		if (!o.tag) return true;
		if (o.tag - 1 == m12.call) {
			m12.calledHits++;
			return true;
		}
		m12.wrongHits++;
		score = score > 3 ? score - 3 : 0;
		strikePulse = 1;
		flash = 0.35f;
		flashColor = rgba(255, 90, 90);
		shake = 0.25f;
		addPopup("WRONG! -3", o.x, o.y - 20, rgba(255, 110, 95), 0.78f);
		setCallout("WRONG FRUIT! -3", rgba(255, 110, 95), 0.72f);
		showCaption("Wrong fruit! -3 points.");
		audio::play(SFX_STRIKE, 0.85f, fieldPan(o.x));
		PORT_VIBRATE(0.35f, 80);
		return false;
	}
	return true;
}

// Juggle: the blade knocks fruit back up the way it moved. Each knock scores the number of fruit in the air.
void Game::juggleTest(Blade& blade) {
	float dx = blade.x - blade.px, dy = blade.y - blade.py;
	const float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.01f) return;
	dx /= len;
	dy /= len;
	for (auto& o : objs) {
		if (!o.on || o.hitCooldown > 0) continue;
		if (o.kind == OBJ_BOMB) {
			if (!hitTest(o, blade, o.radius)) continue;
			// A knocked bomb goes off, and counts as one of the three.
			o.on = false;
			flash = 1;
			flashColor = WHITE;
			shake = 0.6f;
			boom::start(o.x, o.y);
			emitSparks(o.x, o.y, rgba(255, 170, 60), juiceAmount(14), 260);
			audio::play(SFX_BOOM, 1.0f, fieldPan(o.x));
			PORT_VIBRATE(0.8f, 260);
			chain = 0;
			addStrike(o.x);
			if (phase != PHASE_PLAY) return;
			continue;
		}
		if (o.kind != OBJ_FRUIT && o.kind != OBJ_GOLD) continue;
		if (!hitTest(o, blade, o.radius * (gSave.sliceAssist ? 1.3f : 1.05f))) continue;
		// Up to the knock height, but never out of sight: the top of the arc stays under the HUD.
		float rise = (150 + 40 * gRng.unit()) * kFieldK;
		const float room = o.y - (kFieldTop + 20 + o.radius);
		if (rise > room) rise = room > 30 ? room : 30;
		o.vy = -sqrtf(2.0f * gravity * rise);
		o.vx = clampf(o.vx * 0.3f + dx * 190 * acrossScale(), -230, 230);
		o.rotv = (dx >= 0 ? 1 : -1) * gRng.range(2.0f, 5.0f);
		o.hitCooldown = 0.3f;
		int air = 0;
		for (const auto& f : objs) air += f.on && (f.kind == OBJ_FRUIT || f.kind == OBJ_GOLD) && f.y < kFieldH;
		if (air < 1) air = 1;
		m12.knocks++;
		if (air > m12.bestAir) m12.bestAir = air;
		award(air * (o.kind == OBJ_GOLD ? 3 : 1));
		sliced++;
		chain++;
		if (chain > bestCombo) bestCombo = chain;
		char buf[12];
		snprintf(buf, sizeof buf, "+%d", air * (o.kind == OBJ_GOLD ? 3 : 1));
		addPopup(buf, o.x, o.y - 18, air >= 3 ? rgba(255, 220, 90) : COL_CREAM, 0.6f + 0.05f * (air > 6 ? 6 : air));
		audio::play(SFX_KNOCK, 0.8f, fieldPan(o.x), 0.9f + 0.06f * (air > 8 ? 8 : air));
		emitSparks(o.x, o.y, rgba(255, 240, 190), juiceAmount(6), 120);
	}
}

// ---------------------------------------------------------------- each step
void Game::m12Update() {
	const float dt = kDt;
	if (m12.cupPulse > 0) m12.cupPulse -= dt * 2;
	if (m12.spoilPulse > 0) m12.spoilPulse -= dt * 2;
	if (m12.callFlash > 0) m12.callFlash -= dt * 1.5f;
	if (phase != PHASE_PLAY || finaleOn) return;
	switch (mode) {
		case MODE_RECIPE: m12.cupT += dt; break;
		case MODE_SHARP:
			m12.callT -= dt;
			if (m12.callT <= 0) {
				static const char* const kCalls[kSharpColours] = {"Call: red circle", "Call: blue square", "Call: yellow triangle", "Call: green star"};
				m12.call = (u8)((m12.call + 1 + gRng.below(kSharpColours - 1)) % kSharpColours);
				m12.callT = gRng.range(6.0f, 8.0f);
				m12.callFlash = 1;
				audio::play(SFX_PLINK, 0.8f, 0, 1.5f);
				showCaption(kCalls[m12.call]);
			}
			break;
		case MODE_JUGGLE:
			// Fruit bounce off the sides of the field.
			for (auto& o : objs) {
				if (!o.on || (o.kind != OBJ_FRUIT && o.kind != OBJ_GOLD)) continue;
				if (o.x < o.radius && o.vx < 0) o.vx = -o.vx * 0.85f;
				if (o.x > kFieldW - o.radius && o.vx > 0) o.vx = -o.vx * 0.85f;
			}
			break;
		default: break;
	}
}

// ---------------------------------------------------------------- on screen
void Game::drawModes12Field() const {
	char buf[48];
	const float top = kFieldTop + 6;
	if (mode == MODE_RECIPE) {
		if (phase != PHASE_PLAY || finaleOn || gHudPaused) return;
		// The recipe: a cup, then each fruit with how many are in and how many it needs.
		const float baseS = gSave.largeText ? 1.25f : 1.0f;
		const float rawW = (30.0f + m12.recipeKinds * 52.0f) * baseS;
		const float s = baseS * (rawW > kFieldW - 18.0f ? (kFieldW - 18.0f) / rawW : 1.0f);
		const float step = 52 * s, w = 30 * s + m12.recipeKinds * step;
		float x = kFieldW * 0.5f - w * 0.5f;
		const float pulse = 1 + clampf(m12.cupPulse, 0, 1) * 0.3f;
		gfx::rect(x - 6, top - 2, w + 12, 30 * s, rgba(20, 12, 8, 120));
		gfx::sprite(SHEET_SPRITES, sprites_ic_cup_idx, x + 12 * s, top + 13 * s, 0.5f * s * pulse, 0.5f * s * pulse, 0,
		            m12.spoilPulse > 0 ? rgba(255, 140, 120) : WHITE);
		x += 30 * s;
		for (int i = 0; i < m12.recipeKinds; i++) {
			const bool full = m12.recipeHave[i] >= m12.recipeNeed[i];
			gfx::sprite(SHEET_FRUIT, fruitSprite(m12.recipeFruit[i]), x + 12 * s, top + 13 * s, 0.4f * s, 0.4f * s, 0, full ? rgba(255, 255, 255, 120) : WHITE);
			snprintf(buf, sizeof buf, "%d/%d", m12.recipeHave[i], m12.recipeNeed[i]);
			gfx::text(buf, x + 26 * s, top + 5 * s, 0.32f * s, full ? rgba(160, 240, 160) : COL_CREAM);
			x += step;
		}
		return;
	}
	if (mode == MODE_SHARP) {
		const bool strong = sharpStrong();
		// Each ringed fruit: a ring in its colour, and its shape as a badge.
		for (const auto& o : objs) {
			if (!o.on || !o.tag || o.kind != OBJ_FRUIT) continue;
			const int c = o.tag - 1;
			const float r = o.radius * 1.35f / 30;
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, o.x, o.y, r, r, 0, sharpColour(c, strong));
			sharpShape(c, o.x + o.radius * 0.8f, o.y - o.radius * 0.8f, 5.5f, sharpColour(c, strong));
		}
		if (phase != PHASE_PLAY || finaleOn || gHudPaused) return;
		// The call, along the top: its shape, colour and name. It grows for a moment when it changes.
		float k = 1 + clampf(m12.callFlash, 0, 1) * 0.35f;
		snprintf(buf, sizeof buf, "SLICE: %s", sharpName(m12.call));
		for (char* p = buf; *p; p++)
			if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
		float tw = gfx::textWidth(buf, 0.36f * k);
		if (tw + 38.0f > kFieldW - 12.0f && tw > 0) {
			k *= (kFieldW - 50.0f) / tw;
			tw = gfx::textWidth(buf, 0.36f * k);
		}
		gfx::rect(kFieldW * 0.5f - tw * 0.5f - 24, top - 2, tw + 34, 22 * k, rgba(20, 12, 8, 130));
		sharpShape(m12.call, kFieldW * 0.5f - tw * 0.5f - 10, top + 9 * k, 6 * k, sharpColour(m12.call, strong));
		gfx::text(buf, kFieldW * 0.5f - tw * 0.5f + 2, top + 2, 0.36f * k, sharpColour(m12.call, true));
		return;
	}
	if (mode == MODE_JUGGLE) {
		if (phase != PHASE_PLAY || finaleOn || gHudPaused) return;
		int air = 0;
		for (const auto& o : objs) air += o.on && (o.kind == OBJ_FRUIT || o.kind == OBJ_GOLD) && o.y < kFieldH;
		snprintf(buf, sizeof buf, "IN THE AIR: %d   KNOCKS: %d", air, m12.knocks);
		float s = 0.32f;
		const float tw = gfx::textWidth(buf, s);
		if (tw > kFieldW - 16.0f && tw > 0) s *= (kFieldW - 16.0f) / tw;
		gfx::text(buf, kFieldW * 0.5f, top, s, COL_CREAM, ALIGN_CENTER);
	}
}

#ifdef AUTOTEST
// The Lab's checks of the four modes: builds, Royals, cups, calls, knocks and their saves.
void Game::labModes12(void (*check)(bool, const char*, const char*)) {
	char d[128];
	const u8 diffWas = gSave.difficulty;
	const u32 foundWas = gSave.bossTracksFound;
	gSave.difficulty = DIFF_NORMAL;
	const auto setup = [this](Mode m) {
		start(m);
		phase = PHASE_PLAY;
		phaseT = 0;
		waveTimer = 999;
	};
	const auto fruitAt = [this](int slot, int fruit, float x, float y) -> Obj& {
		Obj& o = objs[slot];
		o = {};
		o.on = true;
		o.kind = OBJ_FRUIT;
		o.fruit = (u8)fruit;
		o.x = x;
		o.y = y;
		o.radius = 23;
		o.hits = 1;
		return o;
	};
	const auto swipe = [](float x0, float y0, float x1, float y1, u32 stroke) {
		Blade b;
		b.px = x0;
		b.py = y0;
		b.x = x1;
		b.y = y1;
		b.active = true;
		b.speed = 20;
		b.strokeId = stroke;
		return b;
	};

	// Boss Rush: a build, the bosses back to back, half HP between them, then the Royals.
	const OrchardLevel& lv = orchardLevel(DIFF_NORMAL);
	setup(MODE_BOSSRUSH);
	check(m12.rushPicking && orchardHeld() && !orc.boss.on, "Boss Rush starts on the build picker, with play held", "");
	rushPick(BUILD_TANK);
	snprintf(d, sizeof d, "HP %.0f", orc.maxHp);
	check(!m12.rushPicking && orc.maxHp == lv.startHp * 2 && orc.upgrade[CARD_ARMOR] == 2 && orc.slot[0] == CARD_SMOOTHIE,
	      "Tank: double HP, 2 Armor and a Smoothie", d);
	check(orc.boss.on && orc.boss.id == BOSS_MELON && !orc.boss.royal, "The first boss is The Big Melon", "");
	setup(MODE_BOSSRUSH);
	rushPick(BUILD_GLASS);
	snprintf(d, sizeof d, "HP %.0f", orc.maxHp);
	check(orc.maxHp == floorf(lv.startHp * 0.5f) && orc.upgrade[CARD_GLASS] && orc.upgrade[CARD_BLADE] == 3 && orc.slot[0] == CARD_CLEAVER &&
	          orc.slot[1] == CARD_BOSSBANE,
	      "Glass Cannon: half HP, Blade 3, Cleaver and Boss Bane", d);
	setup(MODE_BOSSRUSH);
	rushPick(BUILD_CHARMER);
	snprintf(d, sizeof d, "HP %.0f", orc.maxHp);
	check(orc.maxHp == floorf(lv.startHp * 0.8f) && orc.upgrade[CARD_JUICE] && orc.upgrade[CARD_THICKPEEL] && orc.slot[0] == CARD_FREEZEPOP &&
	          orc.slot[1] == CARD_GOLDAPPLE,
	      "Charmer: less HP, two passives and both slots filled", d);
	const int levelWas = orc.level;
	orcGainXp(500);
	check(orc.level == levelWas && !orc.choosing, "No level ups in Boss Rush", "");
	orcWave();
	int queued = 0;
	for (const auto& p : pending) queued += p.on;
	check(queued == 0, "No fruit waves spawn in Boss Rush", "");
	orc.boss.enterT = 0;
	orc.hp = 10;
	Blade testBlade;
	testBlade.active = true;
	testBlade.strokeStart = false;
	testBlade.strokeId = 101;
	testBlade.px = 150;
	testBlade.py = 120;
	testBlade.x = 170;
	testBlade.y = 120;
	bossFire(SHOT_SEED, 160, 120, 0, 0);
	bossTest(testBlade);
	check(orc.hp == 10.25f, "Charmer's Juice Formula heals 0.25 HP on defusing boss shots in Boss Rush", "");
	orc.hp = 2;
	rushNext();
	snprintf(d, sizeof d, "HP %.1f of %.0f", orc.hp, orc.maxHp);
	check(m12.rushStage == 1 && orc.hp == fminf(orc.maxHp, 2 + orc.maxHp * 0.5f), "Half the HP comes back after a boss", d);
	check(orc.boss.on && orc.boss.id == BOSS_KING && !orc.boss.royal, "The Bomb King is second", "");
	rushNext();
	check(orc.boss.id == BOSS_PINEAPPLE && orc.boss.royal && orc.boss.left == kBosses[BOSS_PINEAPPLE].slices + 1, "Then a Royal Pineapple Guard, one slice tougher", "");
	rushNext();
	check(orc.boss.id == BOSS_CHILI && orc.boss.royal && orc.boss.left == kBosses[BOSS_CHILI].slices + 1, "Then a Royal Inferno Pepper", "");
	rushNext();
	check(m12.rushStage == 4 && orc.boss.id >= BOSS_DURIAN && !orc.boss.royal, "Stage 5 brings a Milestone Boss as the final Boss Rush challenge", "");
	rushNext();
	check(orc.won && score == 5, "Beating all five clears the rush and scores 1 per boss beaten", "");
	audio::fuse(false);

	// Recipe: the right fruit fill the cup, a full cup adds 8 seconds, the wrong one spoils it.
	setup(MODE_RECIPE);
	int need = 0;
	for (int i = 0; i < m12.recipeKinds; i++) need += m12.recipeNeed[i];
	check(timeLeft == 60 && m12.recipeKinds == 2 && need == 3, "Recipe starts at 60 seconds with a 3-fruit cup of 2 kinds", "");
	Blade cut = swipe(100, 120, 220, 120, 0x61000001u);
	sliceFruit(fruitAt(0, m12.recipeFruit[0], 160, 120), cut);
	check(m12.recipeHave[0] == 1, "A fruit the cup needs goes in", "");
	const float timeBefore = timeLeft;
	const u32 scoreBefore = score;
	for (int guard = 0; guard < 12 && m12.cups == 0; guard++)
		for (int i = 0; i < m12.recipeKinds; i++)
			if (m12.recipeHave[i] < m12.recipeNeed[i]) {
				sliceFruit(fruitAt(0, m12.recipeFruit[i], 160, 120), cut);
				break;
			}
	snprintf(d, sizeof d, "%.0f s, +%lu", timeLeft, (unsigned long)(score - scoreBefore));
	check(m12.cups == 1 && timeLeft == timeBefore + kRecipeBonusTime && score > scoreBefore + 30, "A full cup scores a bonus and adds 8 seconds", d);
	int wrong = -1;
	for (int f = 0; f < kSpiky && wrong < 0; f++) {
		bool used = f == kGrape || f == kCoconut;
		for (int i = 0; i < m12.recipeKinds; i++) used |= m12.recipeFruit[i] == f;
		if (!used) wrong = f;
	}
	const u32 scoreWrong = score;
	sliceFruit(fruitAt(0, wrong, 160, 120), cut);
	check(m12.spoiled == 1 && score == scoreWrong && m12.recipeHave[0] == 0, "A wrong fruit spoils the cup and starts a new recipe", "");
	m12.cups = 3;
	recipeNew();
	need = 0;
	for (int i = 0; i < m12.recipeKinds; i++) need += m12.recipeNeed[i];
	check(m12.recipeKinds == 3 && need == 6, "From the fourth cup, three kinds and more fruit", "");
	timeLeft = 40;
	Obj& rb = fruitAt(1, 0, 160, 120);
	rb.kind = OBJ_BOMB;
	hitBomb(rb);
	check(timeLeft == 30 && phase == PHASE_PLAY, "A bomb in Recipe reduces the timer by 10 seconds", "");

	// Sharpshooter: only the called colour scores, double; the call changes.
	setup(MODE_SHARP);
	check(timeLeft == 60, "Sharpshooter runs 60 seconds", "");
	int tagged = 0, called = 0;
	for (int i = 0; i < 400; i++) {
		Pending p = {};
		p.x = 160;
		p.vy = -300;
		p.kind = OBJ_FRUIT;
		p.fruit = 0;
		launch(p);
		for (auto& o : objs)
			if (o.on) {
				tagged += o.tag != 0;
				called += o.tag == m12.call + 1;
				o.on = false;
			}
	}
	snprintf(d, sizeof d, "%d of 400 ringed, %d called", tagged, called);
	check(tagged == 400 && called > 140 && called < 260, "Every fruit wears a ring, and about half are the called one", d);
	Obj& good = fruitAt(0, 0, 160, 120);
	good.tag = (u8)(m12.call + 1);
	const u32 s0 = score;
	sliceFruit(good, cut);
	check(m12.calledHits == 1 && score - s0 >= 2, "A called fruit scores double", "");
	score = 20;
	Obj& bad = fruitAt(0, 0, 160, 120);
	bad.tag = (u8)((m12.call + 1) % kSharpColours + 1);
	sliceFruit(bad, cut);
	check(m12.wrongHits == 1 && score == 17 && chain == 0, "A wrong colour costs 3 points and the combo", "");
	const int callWas = m12.call;
	m12.callT = 0.001f;
	m12Update();
	check(m12.call != callWas && m12.callT >= 6, "The call changes every few seconds", "");
	const char* const shapes[kSharpColours][2] = {{"red", "circle"}, {"blue", "square"}, {"yellow", "triangle"}, {"green", "star"}};
	bool named = true, apart = true;
	for (int c = 0; c < kSharpColours; c++) {
		named = named && strstr(sharpName(c), shapes[c][0]) && strstr(sharpName(c), shapes[c][1]);
		for (int e = c + 1; e < kSharpColours; e++) apart = apart && sharpColour(c, false) != sharpColour(e, false) && sharpColour(c, true) != sharpColour(e, true);
	}
	check(named && apart, "Each colour keeps its own shape, and the colours stay apart in every palette", "");

	// Juggle: knocks send fruit back up the way the blade moved and score the fruit in the air.
	setup(MODE_JUGGLE);
	check(maxStrikes == kJuggleDrops, "Juggle ends after three drops", "");
	gravity = 300 * kFieldK * 0.55f;
	Obj& j = fruitAt(0, 0, 160, 120);
	j.vy = 60;
	Blade right = swipe(120, 125, 200, 118, 0x62000001u);
	testSlices(right);
	check(j.on && j.vy < 0 && j.vx > 0 && m12.knocks == 1 && score == 1, "A knock sends a fruit up and the way the blade went", "");
	fruitAt(1, 0, 60, 60);
	fruitAt(2, 0, 260, 60);
	j.hitCooldown = 0;
	j.x = 160;
	j.y = 120;
	Blade left = swipe(200, 118, 120, 125, 0x62000002u);
	testSlices(left);
	snprintf(d, sizeof d, "score %lu", (unsigned long)score);
	check(j.vx < 0 && score == 4 && m12.bestAir == 3, "Each knock scores the number of fruit in the air", d);
	Obj& jb = fruitAt(3, 0, 300, 180);
	jb.kind = OBJ_BOMB;
	jb.radius = 19;
	Blade boom = swipe(260, 180, 340, 180, 0x62000003u);
	testSlices(boom);
	check(!jb.on && strikes == 1 && phase == PHASE_PLAY, "Knocking a bomb counts as a drop", "");
	addStrike(100);
	addStrike(100);
	check(phase != PHASE_PLAY, "The third drop ends the run", "");
	audio::fuse(false);

	// Each mode keeps a best of its own, through a save and a load.
	check(MODE_COUNT == 14, "Fourteen modes", "");
	for (int m = MODE_BOSSRUSH; m <= MODE_JUGGLE; m++) {
		setup((Mode)m);
		bestTable()[m] = 0;
		score = (u32)(100 + m);
		commitScore();
	}
	saveLoad();
	bool kept = true;
	for (int m = MODE_BOSSRUSH; m <= MODE_JUGGLE; m++) kept = kept && bestTable()[m] == (u32)(100 + m);
	check(kept, "Boss Rush, Recipe, Sharpshooter and Juggle each save their best", "");

	// Journal boss milestones: wins 1, 5, 15 and 40 pay 50, 150, 400 and 1,000 coins.
	{
		const u16 winsWas = gSave.bossWins[BOSS_KING];
		gSave.bossWins[BOSS_KING] = 0;
		const u32 first = progress::bossWin(BOSS_KING);
		gSave.bossWins[BOSS_KING] = 3;
		const u32 fourth = progress::bossWin(BOSS_KING), fifth = progress::bossWin(BOSS_KING);
		snprintf(d, sizeof d, "first %lu, fourth %lu, fifth %lu, wins %u", (unsigned long)first, (unsigned long)fourth, (unsigned long)fifth, (unsigned)gSave.bossWins[BOSS_KING]);
		check(first == 50 && fourth == 0 && fifth == 150 && gSave.bossWins[BOSS_KING] == 5, "Each boss win is counted, and milestones pay 50 on the first and 150 on the fifth", d);
		check(progress::bossMilestone(4) == 40 && progress::bossReward(4) == 1000 && progress::bossReward(4) == 10 * progress::journalReward(4),
		      "The last boss milestone is 40 wins for 1,000 coins, ten times a fruit's", "");
		gSave.bossWins[BOSS_KING] = 0xFFFF;
		check(progress::bossWin(BOSS_KING) == 0 && gSave.bossWins[BOSS_KING] == 0xFFFF, "A boss's win count stops at its top without wrapping", "");
		gSave.bossWins[BOSS_KING] = winsWas;
	}
	// Boss Rush keeps its fastest clear for Stats.
	{
		const u16 rushWas = gSave.rushBestSecs;
		gSave.rushBestSecs = 0;
		setup(MODE_BOSSRUSH);
		orc.won = true;
		elapsed = 200;
		commitScore();
		const u16 firstTime = gSave.rushBestSecs;
		setup(MODE_BOSSRUSH);
		orc.won = true;
		elapsed = 260;
		commitScore();
		snprintf(d, sizeof d, "%u s, then %u s", (unsigned)firstTime, (unsigned)gSave.rushBestSecs);
		check(firstTime == 200 && gSave.rushBestSecs == 200, "Clearing Boss Rush saves the fastest time, and a slower clear doesn't replace it", d);
		gSave.rushBestSecs = rushWas;
	}
	gSave.difficulty = diffWas;
	gSave.bossTracksFound = foundWas;
}
#endif
