// Orchard Blast's bosses: The Big Melon (room 5) and The Bomb King (room 10).
// A boss attacks on its own clock, so Armor and Max HP matter. It can only be sliced while its guard
// is down; slicing the guard costs a little HP ("Deflected!"). A clean slice leaves it confused for a
// few seconds, then the guard goes back up.
#include <cstring>
#include <math.h>
#include <stdio.h>

#include "audio.h"
#include "boom.h"
#include "fruit.h"
#include "fruitids.h"
#include "game.h"
#include "model3d.h"
#include "progress.h"
#include "render.h"
#include "shop.h"
#include "sprites.h"

namespace {

// A chain of attacks: only its last one tires the boss out and drops the guard, so a fight is a real fight.
// Tuned with the Lab's timed fights (labBossFight).
int chainLength(const Boss& b) { return b.enraged ? 2 : (b.id >= BOSS_DURIAN ? 3 : 4); }

float endlessRoomBoost(int room, bool endless) {
	if (!endless || room <= 10) return 1.0f;
	return clampf(1.0f + (room - 10) * 0.012f, 1.0f, 1.55f);
}

// The Giant Bomb: hits come from blade travel across it, like the Sunburst.
constexpr int kGiantNeed[DIFF_COUNT] = {12, 15, 18, 20};
constexpr float kGiantTime[DIFF_COUNT] = {8.5f, 7.5f, 6.5f, 6.0f};
constexpr float kCrownLife = 3.6f;
constexpr float kGiantTravelPerHit = 70.0f;
constexpr int kGiantDamage = 7;  // heavy hit without instant one-shotting full health

float segDist2(float px, float py, const Blade& b) {
	const float dx = b.x - b.px, dy = b.y - b.py;
	const float len2 = dx * dx + dy * dy;
	const float t = len2 > 0 ? clampf(((px - b.px) * dx + (py - b.py) * dy) / len2, 0, 1) : 0;
	const float qx = b.px + dx * t - px, qy = b.py + dy * t - py;
	return qx * qx + qy * qy;
}

float fieldMidY() { return kFieldTop + (kFieldH - kFieldTop) * 0.36f; }

const float kArtDiv[BOSS_COUNT] = {40.0f, 35.5f, 39.0f, 38.0f, 40.0f, 39.0f, 40.0f};
const float kMeshDiv[BOSS_COUNT] = {23.0f, 19.0f, 20.5f, 18.0f, 21.0f, 21.0f, 22.0f};
const int kBossMeshId[BOSS_COUNT] = {m3d::MESH_MELON, m3d::MESH_BOMB, m3d::MESH_PINEAPPLE, m3d::MESH_CHILI, m3d::MESH_SPIKY, m3d::MESH_LEMON, m3d::MESH_APPLE};
const int kBossFaceIdx[BOSS_COUNT] = {
    sprites_bface_melon_guard_idx, sprites_bface_king_guard_idx, sprites_bface_pineapple_guard_idx, sprites_bface_chili_guard_idx,
    sprites_bface_durian_guard_idx, sprites_bface_lemon_guard_idx, sprites_bface_apple_guard_idx};
const int kBossBodyIdx[BOSS_COUNT] = {
    sprites_boss_melon_guard_idx, sprites_boss_king_guard_idx, sprites_boss_pineapple_guard_idx, sprites_boss_chili_guard_idx,
    sprites_boss_durian_guard_idx, sprites_boss_lemon_guard_idx, sprites_boss_apple_guard_idx};
}  // namespace

void Game::bossHitSlice(float x, float y, float dx, float dy) {
	Boss& b = orc.boss;
	if (b.left > 0) b.left--;
	b.hurtT = 0.4f;
	b.dazeT = 3.0f;
	b.openT = 0;
	b.guard = b.id == BOSS_PINEAPPLE;
	b.crownOut = 0;
	b.surgeT = 0;
	audio::musicSpeed(1.0f);
	for (auto& w : b.worms) w.on = false;
	for (auto& s : orc.shots)
		if (s.kind == SHOT_CROWN) s.on = false;
	if (b.attack == KING_FUSE) audio::fuse(false);
	b.attack = ATK_NONE;
	shake = 0.6f;
	flash = 0.15f;
	flashColor = rgba(255, 255, 255);
	static const u32 kHitJuice[BOSS_COUNT] = {
	    rgba(255, 70, 90), rgba(255, 180, 80), rgba(255, 224, 90), rgba(255, 110, 48),
	    rgba(198, 178, 92), rgba(246, 226, 72), rgba(226, 48, 60)};
	emitJuice(x, y, kHitJuice[b.id % BOSS_COUNT], juiceAmount(30), dx, dy);
	audio::play(b.id == BOSS_MELON || b.id == BOSS_APPLE ? SFX_MELON0 : SFX_SHELL, 1.0f, fieldPan(x), b.id == BOSS_CHILI ? 1.15f : 0.8f);
	PORT_VIBRATE(0.6f, 140);
	orcGainXp(6);
	if (orc.upgrade[CARD_JUICE]) orc.hp = fminf(orc.maxHp, orc.hp + 1.0f);
	if (orc.upgrade[CARD_LIFESTEAL]) {
		const float heal = 1.0f + 0.5f * orc.upgrade[CARD_BLADE];
		orc.hp = fminf(orc.maxHp, orc.hp + heal);
		char lbuf[16];
		snprintf(lbuf, sizeof lbuf, "+%.2g HP", heal);
		addPopup(lbuf, x, y - b.radius + 4, rgba(160, 255, 175), 0.65f);
	}
	sliced++;
	const int done = b.total - b.left;
	if (b.left == 0) {
		b.beaten = true;
		b.leaveT = 0;
		b.boomT = 0;
		b.dazeT = 0;
		b.attack = ATK_NONE;
		b.hidden = false;
		b.giant.on = false;
		b.drainOn = false;
		if (b.lemonadeOn) {
			b.lemStage = 3;
			audio::play(SFX_BUBBLES, 0.9f, fieldPan(b.x), 0.85f);
		}
		for (auto& w : b.worms) w.on = false;
		for (auto& s : orc.shots) s.on = false;
		orc.bossesBeaten++;
		gSave.bossesBeaten |= 1u << b.id;
		if (!orc.bossTookHit) gSave.achFlags2 |= (1 << 1);
		if (b.id >= kFirstMilestoneBoss) gSave.achFlags2 |= (1 << 3);
		if (mode == MODE_BOSSRUSH && orc.bossesBeaten >= 5) gSave.achFlags2 |= (1 << 2);
		u8 newlyFound = 0;
		bossUnlockAllTracks(b.id, &newlyFound);
		orc.newTracks += newlyFound;
		orc.bossCoins += progress::bossWin(b.id);
		char buf[40];
		static const char* const kBeatName[BOSS_COUNT] = {
		    "BIG MELON", "BOMB KING", "PINEAPPLE GUARD", "INFERNO PEPPER",
		    "DUSTY DURIAN", "LORD LEMON", "AGGRO APPLE"};
		snprintf(buf, sizeof buf, "%s BEATEN!", kBeatName[b.id % BOSS_COUNT]);
		setCallout(buf, rgba(255, 230, 140), 0.8f);
		audio::bossMusicEnd();
		if (b.id >= kFirstMilestoneBoss) {
			b.boomT = 0.02f;
			audio::play(SFX_BOOM, 0.9f, fieldPan(b.x), 0.95f);
			shake = fmaxf(shake, 0.35f);
			flash = 0.25f;
			flashColor = rgba(255, 220, 140);
		} else {
			audio::play(SFX_VICTORY, 1.0f);
		}
		orcGainXp(20);
		if (mode == MODE_BOSSRUSH) award(1);
		return;
	}
	const int phase = 1 + done * kBosses[b.id].phases / b.total;
	bool triggeredEnrage = false;
	if (b.id >= kFirstMilestoneBoss && !b.enraged && b.left > 0 && b.left * 2 <= b.total) {
		b.enraged = true;
		b.roarT = 1.15f;
		b.chain = (u8)chainLength(b);
		triggeredEnrage = true;
		audio::play(SFX_TRAIN_WHISTLE, 1.0f, fieldPan(x));
	}
	if (phase > b.phase) {
		b.phase = (u8)phase;
		if (!triggeredEnrage) {
			b.roarT = 1.2f;
			audio::play(SFX_ROAR, 1.0f, fieldPan(x));
		}
	}
}

int Game::bossDamageBase(int base) const {
	const float roomMul = (mode == MODE_ORCHARD && orc.room > 10) ? clampf(1.0f + (orc.room / 25) * 0.18f, 1.0f, 1.85f) : 1.0f;
	const int d = (int)(base * orchardLevel(gSave.difficulty).bossDamage * roomMul + 0.5f);
	return d < 1 ? 1 : d;
}
#define bossDamage(b) bossDamageBase(b)

void Game::bossStart(int id, bool royal) {
	Boss& b = orc.boss;
	b = {};
	b.id = (u8)id;
	b.on = true;
	orc.bossRoom = true;
	orc.bossTookHit = false;
	b.guard = true;
	b.phase = 1;
	b.royal = royal;
	b.total = (u8)(kBosses[id].slices + (royal ? 1 : 0) + (mode == MODE_ORCHARD && orc.room > 20 ? (orc.room / 25) : 0));
	b.left = b.total;
	static const float kBossRadius[BOSS_COUNT] = {50.0f, 44.0f, 48.0f, 46.0f, 50.0f, 46.0f, 48.0f};
	b.radius = kBossRadius[id % BOSS_COUNT] * (PORT_3DS ? 0.85f : 1.0f);
	b.x = kFieldW * 0.5f;
	b.baseY = fieldMidY();
	b.y = -b.radius * 2;
	b.enterT = 1.6f;
	b.nextAttack = 1.1f;
	b.chain = (u8)chainLength(b);
	b.lemCooldown = 0.0f;
	b.thornCd = id == BOSS_DURIAN ? 4.4f : 0.0f;
	b.thornExt = 0.0f;
	b.thornStage = 0;
	for (auto& s : orc.shots) s.on = false;
	for (auto& w : b.worms) w = {};
	orc.fightT = 0;
	audio::musicSpeed(1.0f);
	if (royal) {
		char name[32];
		const char* nm = kBosses[id].name;
		if (strncmp(nm, "The ", 4) == 0) nm += 4;
		snprintf(name, sizeof name, "Royal %s", nm);
		setCallout(name, rgba(255, 220, 110), 0.8f);
	} else {
		setCallout(kBosses[id].name, id >= BOSS_DURIAN ? rgba(255, 130, 110) : rgba(255, 200, 120), 0.8f);
	}
	showCaption("A boss drops in.");
	audio::play(SFX_BOSSIN, 1.0f);
	audio::play(SFX_WHOOSH, 0.9f, 0, 0.8f);
	int track = bossMusicResolve(id, gRng.next());
	b.track = (s8)(track >= 0 ? track : -1);
	if (gSave.bossMusic && track >= 0 && track < audio::trackCount()) audio::bossMusic(track);
}

void Game::bossOpen(float seconds, bool laugh) {
	Boss& b = orc.boss;
	if (!b.on || b.beaten || b.dazeT > 0) return;
	if (b.enraged && b.id >= kFirstMilestoneBoss) laugh = false;  // enraged milestone bosses don't laugh when exposed
	if (b.id == BOSS_DURIAN && (b.thornStage == 1 || b.thornStage == 2)) {
		b.thornStage = 3;
		b.thornT = 0;
	}
	float t = seconds + 0.25f * orc.upgrade[CARD_BLADE];
	if (orc.upgrade[CARD_GLASS]) t *= 2;
	if (b.royal) t *= 0.75f;  // a Royal opens up for less time
	const bool wasUp = b.guard;
	if (t > b.openT) b.openT = t;
	b.guard = false;
	if (wasUp) {
		audio::play(SFX_OPEN, 0.9f, fieldPan(b.x));
		static const float kPitch[BOSS_COUNT] = {1.25f, 0.80f, 1.02f, 1.38f, 0.78f, 1.42f, 0.92f};
		if (laugh) audio::play(SFX_LAUGH, 0.8f, fieldPan(b.x), kPitch[b.id % BOSS_COUNT]);
	}
}

void Game::bossFire(int kind, float x, float y, float vx, float vy) {
	for (auto& s : orc.shots) {
		if (s.on) continue;
		s = {};
		s.on = true;
		s.kind = (u8)kind;
		s.x = x;
		s.y = y;
		s.vx = vx;
		s.vy = vy;
		s.hits = 1;
		s.life = kind == SHOT_CROWN ? kCrownLife : 0;
		return;
	}
}

// Picks what comes next. Attacks come in chains, and only the last of a chain (one that can tire it out) drops
// the guard. A chain that ends without a slice starts again one attack shorter.
void Game::bossNextAttack() {
	Boss& b = orc.boss;
	if (b.chain == 0) b.chain = (u8)(chainLength(b) > 2 ? chainLength(b) - 1 : 1);
	b.finisher = --b.chain == 0;
	u8 choices[8];
	int n = 0;
	if (b.id == BOSS_MELON) {
		if (!b.finisher && b.lastAttack != MEL_SEEDS) choices[n++] = MEL_SEEDS;
		choices[n++] = MEL_FLOP;
		if (b.phase >= 2) choices[n++] = MEL_CHARGE;
	} else if (b.id == BOSS_PINEAPPLE) {
		b.attackCycle++;
		if (b.attackCycle >= 2) {
			b.attackCycle = 0;
			choices[n++] = PINE_BOMB;
		} else {
			choices[n++] = PINE_DARTS;
			choices[n++] = PINE_WHIRL;
			choices[n++] = PINE_SLAM;
		}
	} else if (b.id == BOSS_CHILI) {
		b.attackCycle++;
		if (b.attackCycle >= 3 && b.surgeT <= 0) {
			b.attackCycle = 0;
			b.surgeT = 4.5f;
			setCallout("HEAT SURGE!", rgba(255, 130, 60), 0.75f);
			audio::play(SFX_FEVER, 0.7f);
			audio::musicSpeed(1.22f);
		}
		if (!b.finisher && b.lastAttack != CHILI_EMBERS) choices[n++] = CHILI_EMBERS;
		if (b.phase >= 2 && !b.finisher && b.lastAttack != CHILI_MIRAGE) choices[n++] = CHILI_MIRAGE;
		choices[n++] = CHILI_RUSH;
	} else if (b.id == BOSS_DURIAN) {
		if (!b.finisher) choices[n++] = DUR_THORNS;
		choices[n++] = DUR_SLAM;
		choices[n++] = DUR_SPIN;
	} else if (b.id == BOSS_LEMON) {
		b.attackCycle++;
		if (!b.lemonadeOn && b.lemCooldown <= 0 && b.attackCycle >= (b.enraged ? 3 : 5) && b.lastAttack != LEM_LEMONADE) {
			choices[n++] = LEM_LEMONADE;
			if (b.enraged) choices[n++] = LEM_LEMONADE;  // more frequent in Phase 2, rarer & balanced in Phase 1
		}
		if (!b.finisher) choices[n++] = LEM_ACID;
		if (!b.finisher) choices[n++] = LEM_ZEST;
		choices[n++] = LEM_BLITZ;
	} else if (b.id == BOSS_APPLE) {
		choices[n++] = APP_WORMS;
		if (!b.finisher) choices[n++] = APP_BARRAGE;
		choices[n++] = APP_CRUSH;
	} else if (b.finisher && b.phase >= 2 && !(b.giantPhases >> b.phase & 1)) {
		b.giantPhases |= (u8)(1u << b.phase);  // once in each of the later phases
		choices[n++] = KING_GIANT;
	} else {
		b.attackCycle++;
		choices[n++] = KING_VOLLEY;
		if (!b.finisher && b.lastAttack != KING_LOB) choices[n++] = KING_LOB;
		if (b.finisher || (b.phase >= 2 && b.attackCycle >= 3 && b.lastAttack != KING_CROWN)) choices[n++] = KING_CROWN;
		if (b.phase >= 3 && b.lastAttack != KING_FUSE) choices[n++] = KING_FUSE;
	}
#ifdef TEST_GIANT
	if (b.id == BOSS_KING) {  // screenshots of the Giant Bomb
		choices[0] = KING_GIANT;
		n = 1;
		b.lastAttack = ATK_NONE;
	}
#endif
	int pick = gRng.below(n);
	if (choices[pick] == b.lastAttack && n > 1) pick = (pick + 1) % n;
	const u8 a = choices[pick];
	b.attack = a;
	b.lastAttack = a;
	b.attackT = 0;
	b.volleyT = 0;
	switch (a) {
		case MEL_SEEDS:
			b.volley = 3 + b.phase;
			break;
		case MEL_FLOP:
			b.volley = b.phase >= 3 ? 2 : 1;
			break;
		case MEL_CHARGE:
			b.volley = 0;
			showCaption("It's charging. Swipe it to block.");
			break;
		case KING_VOLLEY:
			b.volley = 2 + b.phase;
			break;
		case KING_LOB:
			b.volley = 1;
			showCaption("Some of that fruit is a trick.");
			break;
		case KING_CROWN:
			b.volley = 1;
			b.attackCycle = 0;
			break;
		case KING_FUSE:
			b.fuseT = 5.2f - 0.45f * (gSave.difficulty >= DIFF_HARD);
			b.fuseCuts = 0;
			b.deflectStroke = 0;
			setCallout("SNUFF THE FUSE!", rgba(255, 170, 90));
			showCaption("Its fuse is lit. Swipe the fuse three times.");
			audio::fuse(true, fieldPan(b.x));
			break;
		case KING_GIANT:
			b.volley = 0;  // the stage: floating off, the bomb, coming back
			audio::play(SFX_WHOOSH, 1.0f, fieldPan(b.x), 0.7f);
			break;
		case PINE_DARTS:
			b.volley = 4 + b.phase;
			break;
		case PINE_WHIRL:
			b.volley = 2 + (b.phase >= 3);
			break;
		case PINE_SLAM:
			b.volley = b.phase >= 3 ? 2 : 1;
			break;
		case PINE_BOMB:
			b.volley = 1;
			showCaption("Slice the lobbed bomb back into The Pineapple Guard!");
			break;
		case CHILI_EMBERS:
			b.volley = 4 + b.phase;
			break;
		case CHILI_MIRAGE:
			b.volley = 1;
			showCaption("Watch the shimmering decoys!");
			break;
		case CHILI_RUSH:
			b.volley = 3;
			break;
		case DUR_THORNS:
			b.volley = 5 + b.phase + (b.enraged ? 2 : 0);
			break;
		case DUR_SLAM:
			b.volley = b.enraged ? 3 : 2;
			break;
		case DUR_SPIN:
			b.volley = 3 + b.phase + (b.enraged ? 2 : 0);
			break;
		case LEM_ACID:
			b.volley = 5 + b.phase + (b.enraged ? 2 : 0);
			break;
		case LEM_ZEST:
			b.volley = 3 + b.phase + (b.enraged ? 2 : 0);
			break;
		case LEM_BLITZ:
			b.volley = 4 + (b.enraged ? 2 : 0);
			break;
		case LEM_LEMONADE:
			b.volley = 1;
			b.attackCycle = 0;
			audio::play(SFX_POP, 0.85f, fieldPan(b.x), 0.85f);
			break;
		case APP_WORMS:
			b.volley = 1;
			showCaption("Slice or tap peeking worms before they lunge!");
			break;
		case APP_BARRAGE:
			b.volley = 6 + b.phase + (b.enraged ? 2 : 0);
			break;
		case APP_CRUSH:
			b.volley = b.enraged ? 3 : 2;
			break;
		default: break;
	}
}

void Game::bossAttack(float dt) {
	Boss& b = orc.boss;
	const OrchardLevel& lv = orchardLevel(gSave.difficulty);
	const float rmBoost = endlessRoomBoost(orc.room, mode == MODE_ORCHARD);
	const float phaseMul = b.enraged ? 1.22f : 1.0f;
	const float spdScale = fminf(1.55f, lv.bossSpeed * (1.0f + (rmBoost - 1.0f) * 0.35f) * phaseMul);
	const float atkScale = fminf(1.60f, lv.attackSpeed * (1.0f + (rmBoost - 1.0f) * 0.35f) * phaseMul);
	const float step = dt * atkScale;
	b.attackT += step;
	b.volleyT -= step;
	const float targetH = (b.lemonadeOn && b.lemLevel > 0.1f) ? lemonadeSurfaceY(b) : (float)kFieldH;
	const float floorY = targetH - b.radius * 0.75f;
	switch (b.attack) {
		case MEL_SEEDS:
			if (b.volley > 0 && b.volleyT <= 0) {
				const float tx = gRng.range(30, kFieldW - 30), ty = (float)kFieldH;
				const float dx = tx - b.x, dy = ty - b.y, len = sqrtf(dx * dx + dy * dy) + 1;
				const float speed = (92 + 13 * b.phase) * kFieldK * spdScale;
				bossFire(SHOT_SEED, b.x + dx / len * b.radius * 0.7f, b.y + dy / len * b.radius * 0.7f, dx / len * speed, dy / len * speed);
				audio::play(SFX_THROW, 0.5f, fieldPan(b.x), 1.4f);
				b.squash = 0.4f;
				b.volley--;
				b.volleyT = fmaxf(0.24f, 0.44f - 0.05f * b.phase);
			}
			if (b.volley <= 0 && b.volleyT < -0.5f) b.attack = ATK_NONE;
			break;
		case MEL_FLOP: {
			const float t = b.attackT;
			if (t <= step * 1.5f) audio::play(SFX_WHOOSH, 0.8f, fieldPan(b.x), 1.15f);  // up it hops
			if (t >= 0.85f && t - step < 0.85f) audio::play(SFX_WHOOSH, 1.0f, fieldPan(b.x), 0.75f);  // and down
			if (t < 0.55f) {
				b.y = lerpf(b.y, kFieldTop + b.radius * 0.6f, 1 - powf(0.0005f, dt));
			} else if (t < 0.85f) {
				// hangs at the top: the tell
			} else if (t < 1.1f) {
				b.y = lerpf(kFieldTop + b.radius * 0.6f, floorY, (t - 0.85f) / 0.25f);
			} else if (b.volleyT > -100) {
				b.y = floorY;
				b.squash = 1;
				shake = 0.5f;
				audio::play(SFX_BOOM, 0.6f, fieldPan(b.x), 0.7f);
				PORT_VIBRATE(0.4f, 90);
				for (int i = 0; i < 3; i++) {
					const float side = (i - 1) * 70.0f * acrossScale();
					bossFire(SHOT_SPLASH, b.x + (i - 1) * 20, floorY - b.radius * 0.5f, side, -gRng.range(260, 320) * kFieldK);
				}
				if (--b.volley > 0) {
					b.attackT = 0;  // the second flop
				} else {
					b.volleyT = -1000;  // landed for good
					if (b.finisher) bossOpen(lv.openTime + 0.5f);
					b.attack = ATK_NONE;
				}
			}
			break;
		}
		case MEL_CHARGE: {
			const float t = b.attackT;
			b.x = lerpf(b.x, kFieldW * 0.5f, 1 - powf(0.02f, dt));
			b.rx = clampf(t / 1.8f, 0.0f, 1.0f) * (6.2831853f * 2.0f);
			if (t >= 0.5f && t - step < 0.5f) audio::play(SFX_WHOOSH, 1.0f, fieldPan(b.x), 0.6f);  // the roll comes at you
			if (t > 0.5f) b.squash = -clampf((t - 0.5f) / 1.3f, 0, 1);  // negative squash: it grows toward the screen
			if (t >= 1.8f) {
				b.rx = 0.0f;
				b.squash = 0;
				shake = 0.8f;
				orcHurt(bossDamage(3), b.x, b.y, "CHARGE");
				if (b.finisher) bossOpen(lv.openTime);
				b.attack = ATK_NONE;
			}
			break;
		}
		case KING_VOLLEY:
			if (b.volley > 0 && b.volleyT <= 0) {
				const float tx = gRng.range(40, kFieldW - 40), ty = (float)kFieldH;
				const float dx = tx - b.x, dy = ty - b.y, len = sqrtf(dx * dx + dy * dy) + 1;
				const float speed = (70 + 10 * b.phase) * kFieldK * lv.bossSpeed;
				bossFire(SHOT_CANNON, b.x + dx / len * b.radius, b.y + dy / len * b.radius, dx / len * speed, dy / len * speed);
				audio::play(SFX_BOOM, 0.35f, fieldPan(b.x), 1.5f);
				b.squash = 0.6f;
				b.volley--;
				b.volleyT = 0.65f;
			}
			if (b.volley <= 0 && b.volleyT < -0.6f) {
				if (b.finisher) bossOpen(lv.openTime);
				b.attack = ATK_NONE;
			}
			break;
		case KING_LOB:
			if (b.volley > 0) {
				b.volley = 0;
				const int bombs = 1 + (b.phase >= 3), decoys = 2;
				for (int i = 0; i < bombs + decoys; i++) {
					float x = 160, vx = 0, vy = -300;
					const bool bomb = i < bombs;
					pickThrow(bomb, 0.25f * i, x, vx, vy);
					// A decoy is queued as a fruit with the top bit set; launch() starts its timer.
					// A King's lobbed bomb has fruit=1 to distinguish from ordinary wave bombs.
					queue(0.25f * i, x, vx, vy, bomb ? OBJ_BOMB : OBJ_FRUIT, bomb ? 1 : (normalFruitOf(gSpawn) | 0x80));
				}
				audio::play(SFX_THROW, 0.7f, fieldPan(b.x), 0.9f);
			}
			if (b.attackT > 1.8f) b.attack = ATK_NONE;
			break;
		case KING_CROWN:
			if (b.volley > 0) {
				b.volley = 0;
				bossFire(SHOT_CROWN, b.x, b.y - b.radius, b.x, b.y);
				b.crownOut = kCrownLife;
				audio::play(SFX_SWISH0, 0.8f, fieldPan(b.x), 0.8f);
			}
			if (b.crownOut <= 0) {
				b.attack = ATK_NONE;
			}
			break;
		case KING_FUSE:
			b.fuseT -= dt * lerpf(1.0f, lv.attackSpeed, 0.55f);
			if (b.fuseCuts >= 3) {
				audio::fuse(false);
				if (b.finisher) bossOpen(lv.openTime * 1.5f);
				b.attack = ATK_NONE;
			} else if (b.fuseT <= 0) {
				audio::fuse(false);
				audio::play(SFX_BOOM, 1.0f, fieldPan(b.x));
				flash = 0.6f;
				flashColor = rgba(255, 210, 150);
				shake = 1;
				emitSparks(b.x, b.y - b.radius, rgba(255, 190, 90), juiceAmount(24), 220);
				orcHurt(bossDamage(5), b.x, b.y, "BLAST");
				b.attack = ATK_NONE;
			}
			break;
		case KING_GIANT: bossGiant(dt); break;
		case PINE_DARTS:
			if (b.volley > 0 && b.volleyT <= 0) {
				const float tx = gRng.range(36, kFieldW - 36), ty = (float)kFieldH;
				const float dx = tx - b.x, dy = ty - b.y, len = sqrtf(dx * dx + dy * dy) + 1;
				const float speed = (92 + 12 * b.phase) * kFieldK * spdScale;
				bossFire(SHOT_SPIKE, b.x + dx / len * b.radius * 0.75f, b.y + dy / len * b.radius * 0.75f, dx / len * speed, dy / len * speed);
				audio::play(SFX_THROW, 0.55f, fieldPan(b.x), 1.2f);
				b.squash = 0.35f;
				b.volley--;
				b.volleyT = fmaxf(0.22f, 0.38f - 0.045f * b.phase);
			}
			if (b.volley <= 0 && b.volleyT < -0.5f) b.attack = ATK_NONE;
			break;
		case PINE_WHIRL: {
			const float sweep = kFieldW * 0.5f + sinf(b.attackT * 5.2f) * kFieldW * 0.34f;
			b.x = lerpf(b.x, sweep, 1 - powf(0.01f, dt));
			const float prevRx = b.rx;
			b.rx += dt * 8.5f;
			if (b.volley > 0 && b.volleyT <= 0) {
				for (int s = -1; s <= 1; s += 2) {
					bossFire(SHOT_SPIKE, b.x + s * b.radius * 0.6f, b.y + 10, s * 65.0f * acrossScale(), 110 * kFieldK * lv.bossSpeed);
				}
				audio::play(SFX_SWISH0, 0.65f, fieldPan(b.x), 1.15f);
				b.volley--;
				b.volleyT = 0.55f;
			}
			const int prevTurn = (int)(prevRx / 6.2831853f), curTurn = (int)(b.rx / 6.2831853f);
			if (b.volley <= 0 && b.volleyT < -0.45f && (curTurn > prevTurn || fmodf(b.rx, 6.2831853f) < 0.08f)) {
				b.rx = 0.0f;
				b.attack = ATK_NONE;
			}
			break;
		}
		case PINE_SLAM: {
			const float t = b.attackT;
			if (t <= step * 1.5f) audio::play(SFX_WHOOSH, 0.85f, fieldPan(b.x), 1.05f);
			if (t >= 0.82f && t - step < 0.82f) audio::play(SFX_WHOOSH, 1.0f, fieldPan(b.x), 0.7f);
			if (t < 0.52f) b.y = lerpf(b.y, kFieldTop + b.radius * 0.55f, 1 - powf(0.0005f, dt));
			else if (t < 0.82f) {}
			else if (t < 1.06f) b.y = lerpf(kFieldTop + b.radius * 0.55f, floorY, (t - 0.82f) / 0.24f);
			else if (b.volleyT > -100) {
				b.y = floorY;
				b.squash = 1;
				shake = 0.55f;
				audio::play(SFX_BOOM, 0.65f, fieldPan(b.x), 0.85f);
				PORT_VIBRATE(0.45f, 95);
				for (int i = -1; i <= 1; i++) {
					bossFire(SHOT_SPIKE, b.x + i * 24, floorY - b.radius * 0.45f, i * 80.0f * acrossScale(), -gRng.range(240, 300) * kFieldK);
				}
				if (--b.volley > 0) b.attackT = 0;
				else {
					b.volleyT = -1000;
					// The Pineapple Guard keeps its guard raised; damage comes from knocking its lobbed bombs back.
					b.attack = ATK_NONE;
				}
			}
			break;
		}
		case PINE_BOMB: {
			if (b.volley > 0) {
				b.volley = 0;
				float x = 160, vx = 0, vy = -300;
				pickThrow(true, 0.0f, x, vx, vy);
				// tag = 2 marks a Pineapple Guard lob bomb that slices launch back up at him.
				queue(0.05f, x, vx, vy, OBJ_BOMB, 2);
				audio::play(SFX_THROW, 0.8f, fieldPan(b.x), 0.85f);
			}
			if (b.attackT > 1.8f) b.attack = ATK_NONE;
			break;
		}
		case CHILI_EMBERS:
			if (b.volley > 0 && b.volleyT <= 0) {
				const float tx = gRng.range(36, kFieldW - 36), ty = (float)kFieldH;
				const float dx = tx - b.x, dy = ty - b.y, len = sqrtf(dx * dx + dy * dy) + 1;
				const float speed = (98 + 15 * b.phase) * kFieldK * spdScale;
				bossFire(SHOT_EMBER, b.x + dx / len * b.radius * 0.7f, b.y + dy / len * b.radius * 0.7f, dx / len * speed, dy / len * speed);
				audio::play(SFX_THROW, 0.5f, fieldPan(b.x), 1.55f);
				b.squash = 0.45f;
				b.volley--;
				b.volleyT = fmaxf(0.24f, 0.40f - 0.045f * b.phase);
			}
			if (b.volley <= 0 && b.volleyT < -0.5f) b.attack = ATK_NONE;
			break;
		case CHILI_MIRAGE:
			if (b.volley > 0) {
				b.volley = 0;
				const int bombs = 1 + (b.phase >= 2), decoys = 2;
				for (int i = 0; i < bombs + decoys; i++) {
					float x = 160, vx = 0, vy = -300;
					const bool bomb = i < bombs;
					pickThrow(bomb, 0.22f * i, x, vx, vy);
					queue(0.22f * i, x, vx, vy, bomb ? OBJ_BOMB : OBJ_FRUIT, bomb ? 1 : (normalFruitOf(gSpawn) | 0x80));
				}
				audio::play(SFX_THROW, 0.75f, fieldPan(b.x), 1.15f);
			}
			if (b.attackT > 1.7f) b.attack = ATK_NONE;
			break;
		case CHILI_RUSH: {
			const float rushSpeed = (4.5f + b.phase * 0.6f) * spdScale;
			const float sweep = kFieldW * 0.5f + sinf(b.attackT * rushSpeed) * kFieldW * 0.36f;
			b.x = lerpf(b.x, sweep, 1 - powf(0.005f, dt));
			if (b.volley > 0 && b.volleyT <= 0) {
				bossFire(SHOT_EMBER, b.x, b.y + b.radius * 0.6f, gRng.range(-40, 40) * acrossScale(), 118 * kFieldK * spdScale);
				audio::play(SFX_SWISH0, 0.6f, fieldPan(b.x), 1.35f);
				b.volley--;
				b.volleyT = 0.44f;
			}
			if (b.volley <= 0 && b.volleyT < -0.45f) {
				if (b.finisher) bossOpen(lv.openTime + 0.3f);
				b.attack = ATK_NONE;
			}
			break;
		}
		case DUR_THORNS:
			if (b.volley > 0 && b.volleyT <= 0) {
				for (int s = -1; s <= 1; s++) {
					if (s == 0 && !b.enraged) continue;
					const float vx = (s == 0 ? 0.0f : s * gRng.range(45, 95)) * acrossScale() * spdScale;
					const float vy = (98 + 11 * b.phase + (s == 0 ? 18 : 0)) * kFieldK * spdScale;
					bossFire(SHOT_THORN, b.x + s * b.radius * 0.55f, b.y + 8, vx, vy);
				}
				audio::play(SFX_THROW, 0.65f, fieldPan(b.x), b.enraged ? 0.90f : 0.78f);
				b.squash = 0.4f;
				b.volley--;
				b.volleyT = fmaxf(0.18f, (b.enraged ? 0.30f : 0.38f) - 0.04f * b.phase);
			}
			if (b.volley <= 0 && b.volleyT < -0.45f) b.attack = ATK_NONE;
			break;
		case DUR_SLAM: {
			const float t = b.attackT;
			if (t <= step * 1.5f) audio::play(SFX_WHOOSH, 0.9f, fieldPan(b.x), 0.9f);
			if (t < 0.48f) b.y = lerpf(b.y, kFieldTop + b.radius * 0.5f, 1 - powf(0.0005f, dt));
			else if (t < 0.78f) {}
			else if (t < 1.02f) b.y = lerpf(kFieldTop + b.radius * 0.5f, floorY, (t - 0.78f) / 0.24f);
			else if (b.volleyT > -100) {
				b.y = floorY;
				b.squash = 1.05f;
				shake = 0.7f;
				audio::play(SFX_BOOM, 0.8f, fieldPan(b.x), 0.65f);
				PORT_VIBRATE(0.55f, 110);
				for (int i = -2; i <= 2; i++) {
					if (i == 0 && !b.enraged) continue;
					bossFire(SHOT_THORN, b.x + i * 18, floorY - b.radius * 0.4f, i * 64.0f * acrossScale(), -gRng.range(240, 315) * kFieldK);
				}
				if (--b.volley > 0) b.attackT = 0;
				else {
					b.volleyT = -1000;
					if (b.finisher) bossOpen(lv.openTime);
					b.attack = ATK_NONE;
				}
			}
			break;
		}
		case DUR_SPIN: {
			const float sweep = kFieldW * 0.5f + sinf(b.attackT * (b.enraged ? 8.2f : 5.8f) * spdScale) * kFieldW * 0.36f;
			b.x = lerpf(b.x, sweep, 1 - powf(0.005f, dt));
			const float prevRx = b.rx;
			b.rx += dt * (b.enraged ? 15.0f : 11.0f);
			if (b.volley > 0 && b.volleyT <= 0) {
				bossFire(SHOT_THORN, b.x, b.y + b.radius * 0.6f, gRng.range(-55, 55) * acrossScale(), 112 * kFieldK * spdScale);
				audio::play(SFX_SWISH1, 0.65f, fieldPan(b.x), 0.9f);
				b.volley--;
				b.volleyT = b.enraged ? 0.26f : 0.36f;
			}
			const int prevTurn = (int)(prevRx / 6.2831853f), curTurn = (int)(b.rx / 6.2831853f);
			if (b.volley <= 0 && b.volleyT < -0.45f && (curTurn > prevTurn || fmodf(b.rx, 6.2831853f) < 0.08f)) {
				b.rx = 0.0f;
				if (b.finisher) bossOpen(lv.openTime);
				b.attack = ATK_NONE;
			}
			break;
		}
		case LEM_ACID:
			if (b.volley > 0 && b.volleyT <= 0) {
				const int shots = b.enraged ? 2 : 1;
				for (int s = 0; s < shots; s++) {
					if (b.lemonadeOn && b.lemLevel > 0.1f) {
						const float dir = gRng.below(2) ? 1.0f : -1.0f;
						const float vx = dir * gRng.range(52, 112) * acrossScale() * spdScale;
						const float vy = -gRng.range(18, 36) * kFieldK;
						bossFire(SHOT_ACID, b.x + dir * b.radius * 0.45f, fminf(b.y, targetH - 36.0f), vx, vy);
					} else {
						const float tx = gRng.range(30, kFieldW - 30), ty = targetH;
						const float dx = tx - b.x, dy = ty - b.y, len = sqrtf(dx * dx + dy * dy) + 1;
						const float speed = (96 + 12 * b.phase) * kFieldK * spdScale;
						bossFire(SHOT_ACID, b.x + dx / len * b.radius * 0.7f, b.y + dy / len * b.radius * 0.7f, dx / len * speed, dy / len * speed);
					}
				}
				audio::play(SFX_THROW, 0.55f, fieldPan(b.x), 1.6f);
				b.squash = 0.35f;
				b.volley--;
				b.volleyT = fmaxf(0.18f, (b.enraged ? 0.26f : 0.34f) - 0.03f * b.phase);
			}
			if (b.volley <= 0 && b.volleyT < -0.45f) b.attack = ATK_NONE;
			break;
		case LEM_ZEST:
			if (b.volley > 0 && b.volleyT <= 0) {
				const int dir = (b.volley & 1) ? 1 : -1;
				const float sx = dir > 0 ? 22.0f : kFieldW - 22.0f;
				const float sy = (b.lemonadeOn && b.lemLevel > 0.1f) ? fminf(b.y, kFieldTop + 44.0f) : (b.y + 10.0f);
				const float vy = (b.lemonadeOn && b.lemLevel > 0.1f ? -14.0f : 92.0f) * kFieldK * spdScale;
				bossFire(SHOT_ACID, sx, sy, dir * 105.0f * acrossScale() * spdScale, vy);
				if (b.enraged) {
					const float vy2 = (b.lemonadeOn && b.lemLevel > 0.1f ? -12.0f : 86.0f) * kFieldK * spdScale;
					bossFire(SHOT_ACID, kFieldW - sx, sy, -dir * 95.0f * acrossScale() * spdScale, vy2);
				}
				audio::play(SFX_POP, 0.65f, fieldPan(sx), 1.3f);
				b.volley--;
				b.volleyT = b.enraged ? 0.28f : 0.42f;
			}
			if (b.volley <= 0 && b.volleyT < -0.5f) b.attack = ATK_NONE;
			break;
		case LEM_BLITZ: {
			const float sweep = kFieldW * 0.5f + sinf(b.attackT * (b.enraged ? 8.4f : 6.4f) * spdScale) * kFieldW * 0.38f;
			b.x = lerpf(b.x, sweep, 1 - powf(0.004f, dt));
			const bool lemUp = b.lemonadeOn && b.lemLevel > 0.1f;
			const float minBase = lemUp ? fminf(b.baseY, lemonadeSurfaceY(b) - b.radius * 1.10f) : b.baseY;
			b.y = lerpf(b.y, minBase + sinf(b.attackT * 3.2f) * (lemUp ? 8.0f : 18.0f), 1 - powf(0.01f, dt));
			if (b.volley > 0 && b.volleyT <= 0) {
				const float sy = lemUp ? (b.y + b.radius * 0.25f) : (b.y + b.radius * 0.55f);
				const float vy = (lemUp ? 16.0f : 112.0f) * kFieldK * spdScale;
				bossFire(SHOT_ACID, b.x, sy, gRng.range(-48, 48) * acrossScale(), vy);
				audio::play(SFX_SWISH2, 0.65f, fieldPan(b.x), 1.4f);
				b.volley--;
				b.volleyT = b.enraged ? 0.27f : 0.38f;
			}
			if (b.volley <= 0 && b.volleyT < -0.45f) {
				if (b.finisher) bossOpen(lv.openTime);
				b.attack = ATK_NONE;
			}
			break;
		}
		case LEM_LEMONADE: {
			const float tx = kFieldW * 0.5f;
			const float ty = kFieldTop + (kFieldH - kFieldTop) * 0.28f;
			b.x = lerpf(b.x, tx, 1 - powf(0.004f, dt));
			b.y = lerpf(b.y, ty, 1 - powf(0.004f, dt));
			const float t = b.attackT;
			if (t >= 0.55f) {
				// Squeezes himself while floating in the swim ring until the bottom half fills with lemonade.
				b.lemonadeOn = true;
				b.lemStage = 1;
				b.lemLevel = clampf((t - 0.55f) / 1.65f, 0.0f, 1.0f);
				b.squash = 0.25f + 0.25f * sinf(t * 16.0f);
				if (b.volleyT <= 0) {
					b.volleyT = 0.24f;
					emitJuice(b.x + gRng.range(-18, 18), b.y + b.radius * 0.5f, rgba(248, 226, 62), juiceAmount(8), 0, 1);
					audio::play(SFX_JUICY0, 0.6f, fieldPan(b.x), gRng.range(1.1f, 1.4f));
				}
			}
			if (t >= 2.35f) {
				b.lemStage = 2;
				b.lemLevel = 1.0f;
				b.lemTimer = b.enraged ? 2.6f : 3.6f;
				b.drainOn = false;
				b.squash = 0;
				if (b.finisher) bossOpen(lv.openTime);
				b.attack = ATK_NONE;
			}
			break;
		}
		case APP_WORMS: {
			if (b.volley > 0) {
				b.volley = 0;
				static const float kHoles[4][2] = {{-0.62f, -0.22f}, {0.62f, -0.20f}, {-0.56f, 0.42f}, {0.56f, 0.42f}};
				const int cnt = (b.enraged || b.phase >= 3) ? 4 : 3;
				const float life = clampf((b.enraged ? 2.75f : 3.2f) - 0.30f * (gSave.difficulty >= DIFF_HARD) - 0.12f * b.phase, 1.65f, 3.2f);
				for (int i = 0; i < cnt; i++) {
					b.worms[i].on = true;
					b.worms[i].ox = kHoles[i][0] * b.radius;
					b.worms[i].oy = kHoles[i][1] * b.radius;
					b.worms[i].emergeT = 0.0f;
					b.worms[i].timer = life + i * 0.20f;
				}
				audio::play(SFX_POP, 0.8f, fieldPan(b.x), 0.9f);
			}
			bool anyWorm = false;
			for (const auto& w : b.worms) anyWorm |= w.on;
			if (!anyWorm || b.attackT > (b.enraged ? 3.5f : 4.2f)) {
				if (b.finisher) bossOpen(lv.openTime);
				b.attack = ATK_NONE;
			}
			break;
		}
		case APP_BARRAGE:
			if (b.volley > 0 && b.volleyT <= 0) {
				const int shots = b.enraged ? 2 : 1;
				for (int s = 0; s < shots; s++) {
					const float tx = gRng.range(28, kFieldW - 28), ty = targetH;
					const float dx = tx - b.x, dy = ty - b.y, len = sqrtf(dx * dx + dy * dy) + 1;
					const float speed = (96 + 12 * b.phase) * kFieldK * spdScale;
					bossFire(SHOT_SEED, b.x + dx / len * b.radius * 0.7f, b.y + dy / len * b.radius * 0.7f, dx / len * speed, dy / len * speed);
				}
				audio::play(SFX_THROW, 0.55f, fieldPan(b.x), 1.35f);
				b.squash = 0.35f;
				b.volley--;
				b.volleyT = fmaxf(0.18f, (b.enraged ? 0.24f : 0.32f) - 0.025f * b.phase);
			}
			if (b.volley <= 0 && b.volleyT < -0.45f) b.attack = ATK_NONE;
			break;
		case APP_CRUSH: {
			const float t = b.attackT;
			if (t <= step * 1.5f) audio::play(SFX_WHOOSH, 0.9f, fieldPan(b.x), 1.0f);
			if (t < 0.45f) b.y = lerpf(b.y, kFieldTop + b.radius * 0.5f, 1 - powf(0.0005f, dt));
			else if (t < 0.74f) {}
			else if (t < 0.98f) b.y = lerpf(kFieldTop + b.radius * 0.5f, floorY, (t - 0.74f) / 0.24f);
			else if (b.volleyT > -100) {
				b.y = floorY;
				b.squash = 1.0f;
				shake = 0.65f;
				audio::play(SFX_BOOM, 0.75f, fieldPan(b.x), 0.8f);
				const int spread = b.enraged ? 2 : 1;
				for (int i = -spread; i <= spread; i++) {
					if (i == 0 && b.enraged) continue;
					bossFire(SHOT_SPLASH, b.x + i * 20, floorY - b.radius * 0.45f, i * 74.0f * acrossScale(), -gRng.range(250, 320) * kFieldK);
				}
				if (--b.volley > 0) b.attackT = 0;
				else {
					b.volleyT = -1000;
					if (b.finisher) bossOpen(lv.openTime);
					b.attack = ATK_NONE;
				}
			}
			break;
		}
		default: b.attack = ATK_NONE; break;
	}
	const float jit = gRng.range(0.82f, 1.16f);
	if (b.attack == ATK_NONE) b.nextAttack = lerpf(2.3f, 1.35f, (b.phase - 1) * 0.5f) * jit / atkScale * (b.royal ? 0.8f : 1.0f) * (b.enraged ? 0.55f : 1.0f);
}

void Game::shotsUpdate(float dt) {
	Boss& b = orc.boss;
	for (auto& s : orc.shots) {
		if (!s.on) continue;
		s.age += dt;
		s.rot += dt * 6;
		if (s.kind == SHOT_CROWN) {
			// Out and back in a smooth clamped boomerang arc so it never leaves narrow/wide screens.
			const float k = s.age / kCrownLife * 6.2832f;
			const float ox = s.vx, oy = s.vy;
			const float ampX = clampf(fminf(ox - 24.0f, kFieldW - 24.0f - ox), kFieldW * 0.20f, kFieldW * 0.34f);
			s.x = clampf(ox + sinf(k) * ampX, 22.0f, kFieldW - 22.0f);
			s.y = clampf(oy - b.radius * 0.6f + (1 - cosf(k)) * (kFieldH - oy - 24.0f) * 0.44f, kFieldTop + 24.0f, kFieldH - 34.0f);
			s.rot = sinf(k) * 0.48f;
			s.life -= dt;
			if (s.life <= 0) {
				s.on = false;
				b.crownOut = 0;
				b.attack = ATK_NONE;
				orcHurt(bossDamage(2), b.x, b.y - b.radius, "CROWN");
				if (b.finisher) bossOpen(orchardLevel(gSave.difficulty).openTime);
			}
			continue;
		}
		if (s.kind == SHOT_SPLASH) s.vy += gravity * 0.8f * dt;
		if (s.kind == SHOT_SEED || s.kind == SHOT_SPIKE || s.kind == SHOT_EMBER || s.kind == SHOT_THORN || s.kind == SHOT_ACID) s.vy += 45 * kFieldK * dt;
		s.x += s.vx * dt;
		s.y += s.vy * dt;
		if (s.x < -40 || s.x > kFieldW + 40) {
			s.on = false;
			continue;
		}
		const float hitY = (b.lemonadeOn && b.lemLevel > 0.1f) ? lemonadeSurfaceY(b) - 2.0f : (float)kFieldH - 4.0f;
		if (s.y > hitY && s.vy > 0) {
			s.on = false;
			const int dmg = (s.kind == SHOT_CANNON || s.kind == SHOT_EMBER || s.kind == SHOT_THORN || s.kind == SHOT_ACID) ? 2 : 1;
			const u32 col = s.kind == SHOT_CANNON ? rgba(90, 90, 100) : (s.kind == SHOT_EMBER ? rgba(255, 130, 40) : (s.kind == SHOT_ACID ? rgba(220, 240, 70) : (s.kind == SHOT_THORN ? rgba(165, 145, 70) : (s.kind == SHOT_SPIKE ? rgba(210, 190, 70) : rgba(255, 90, 110)))));
			const char* msg = s.kind == SHOT_CANNON ? "CANNON" : (s.kind == SHOT_EMBER ? "EMBER" : (s.kind == SHOT_ACID ? "ACID" : (s.kind == SHOT_THORN ? "THORN" : (s.kind == SHOT_SPIKE ? "SPIKE" : (s.kind == SHOT_SEED ? "SEED" : "SPLASH")))));
			emitJuice(s.x, hitY - 2.0f, col, juiceAmount(6), 0, -1);
			orcHurt(bossDamage(dmg), s.x, fminf(hitY, (float)kFieldH - 20.0f), msg);
		}
	}
	if (b.crownOut > 0) b.crownOut -= dt;
}

void Game::bossSpawnPieces(int id, float cx, float cy) {
	static const int kFruitSprL[BOSS_COUNT] = {
	    fruit_melon_l_idx, fruit_coconut_l_idx, fruit_pineapple_l_idx, fruit_chili_l_idx,
	    fruit_spiky_l_idx, fruit_lemon_l_idx, fruit_apple_l_idx};
	static const int kFruitSprR[BOSS_COUNT] = {
	    fruit_melon_r_idx, fruit_coconut_r_idx, fruit_pineapple_r_idx, fruit_chili_r_idx,
	    fruit_spiky_r_idx, fruit_lemon_r_idx, fruit_apple_r_idx};
	static const int kMeshId[BOSS_COUNT] = {
	    m3d::MESH_MELON, m3d::MESH_BOMB, m3d::MESH_PINEAPPLE, m3d::MESH_CHILI,
	    m3d::MESH_SPIKY, m3d::MESH_LEMON, m3d::MESH_APPLE};
	static const u32 kBlastJuice[BOSS_COUNT] = {
	    rgba(255, 70, 90), rgba(255, 180, 80), rgba(255, 224, 90), rgba(255, 110, 48),
	    rgba(198, 178, 92), rgba(246, 226, 72), rgba(226, 48, 60)};
	const int bi = id >= 0 ? id % BOSS_COUNT : 0;
	boom::start(cx, cy, 1.55f);
	flash = 0.72f;
	flashColor = rgba(255, 230, 145);
	shake = 0.65f;
	for (int r = 0; r < 4; r++)
		addRing(cx, cy, hueColor(0.06f + r * 0.16f), 2.4f + r * 1.4f, 0.38f + r * 0.12f);
	emitJuice(cx, cy, kBlastJuice[bi], juiceAmount(36), 0, -1);
	emitJuice(cx, cy, rgba(255, 235, 130), juiceAmount(22), 0, 1);
	emitSparks(cx, cy, rgba(255, 220, 110), juiceAmount(32), 260);
	Splat& sp = splats[splatCursor];
	splatCursor = (splatCursor + 1) % kMaxSplats;
	sp = {cx, cy, gRng.range(0, 6.28f), 2.2f, 3.2f, kBlastJuice[bi], (u8)gRng.below(3), true};
	const int pieces = PORT_3DS ? 10 : 12;
	for (int i = 0; i < pieces; i++) {
		Half& h = halves[halfCursor];
		halfCursor = (halfCursor + 1) % kMaxHalves;
		if (h.on) m3d::cutRelease(h.cut);
		h = {};
		const float a = (6.283185f * i) / pieces + gRng.range(-0.18f, 0.18f);
		const float spV = gRng.range(115.0f, 250.0f) * acrossScale();
		const bool right = (i & 1) != 0;
		h.on = true;
		h.x = cx + cosf(a) * 14.0f;
		h.y = cy + sinf(a) * 14.0f;
		h.vx = cosf(a) * spV;
		h.vy = sinf(a) * spV - 85.0f * kFieldK;
		h.rot = gRng.range(0.0f, 6.28f);
		h.rotv = (right ? 1.0f : -1.0f) * gRng.range(3.5f, 7.5f);
		h.rx = gRng.range(-1.2f, 1.2f);
		h.ry = gRng.range(-1.2f, 1.2f);
		h.axisX = cosf(a);
		h.axisY = sinf(a);
		h.tiltv = gRng.range(-4.5f, 4.5f);
		h.sprite = (short)(right ? kFruitSprR[bi] : kFruitSprL[bi]);
		h.mesh = (u8)kMeshId[bi];
		h.part = right ? m3d::PART_RIGHT : m3d::PART_LEFT;
		h.cut = -1;
	}
}

static float clampBossVisX(float x, float radius) {
	const float m = fminf(kFieldW * 0.45f, radius + 8.0f);
	return clampf(x, m, kFieldW - m);
}

static void wormHeadPos(const Boss& b, const AppleWorm& w, float bx, float by, float* wx, float* wy, float* nxOut = nullptr, float* nyOut = nullptr) {
	const float len = sqrtf(w.ox * w.ox + w.oy * w.oy) + 0.001f;
	const float nx = w.ox / len, ny = w.oy / len;
	const float ext = b.radius * (0.06f + 0.40f * w.emergeT);
	*wx = bx + w.ox + nx * ext;
	*wy = by + w.oy + ny * ext;
	if (nxOut) *nxOut = nx;
	if (nyOut) *nyOut = ny;
}

void Game::bossUpdate(float dt) {
	Boss& b = orc.boss;
	if (!b.on) return;
	b.x = clampBossVisX(b.x, b.radius);
	if (b.giant.on) {
		const float gm = fminf(kFieldW * 0.40f, 34.0f);
		b.giant.x = clampf(b.giant.x, gm, kFieldW - gm);
	}
	const OrchardLevel& lv = orchardLevel(gSave.difficulty);
	if (b.surgeT > 0) {
		b.surgeT -= dt;
		if (b.surgeT <= 0) {
			b.surgeT = 0;
			audio::musicSpeed(1.0f);
		} else {
			dt *= 1.55f;
		}
	}
	b.t += dt;
	if (b.hurtT > 0) b.hurtT -= kDt;
	if (b.deflectT > 0) b.deflectT -= kDt;
	b.squash *= powf(0.02f, kDt);
	// Always recover/settle upright (b.rx == 0) whenever a rolling/spinning attack stops or is interrupted.
	{
		constexpr float kTau = 6.2831853f;
		const bool spinning = !b.beaten && b.dazeT <= 0 && (b.attack == MEL_CHARGE || b.attack == PINE_WHIRL || b.attack == DUR_SPIN);
		if (!spinning && b.rx != 0.0f) {
			b.rx = fmodf(b.rx, kTau);
			if (b.rx < 0.0f) b.rx += kTau;
			if (b.rx >= kTau * 0.5f) {
				b.rx += kDt * 12.0f;
				if (b.rx >= kTau) b.rx = 0.0f;
			} else {
				b.rx -= kDt * 12.0f;
				if (b.rx <= 0.0f) b.rx = 0.0f;
			}
		}
	}
	if (b.beaten) {
		b.leaveT += kDt;
		if (b.lemonadeOn) {
			b.lemLevel = fmaxf(0.0f, b.lemLevel - kDt * 0.90f);
			if (b.lemLevel <= 0.0f) b.lemonadeOn = false;
			b.drainOn = false;
		}
		if (b.id >= kFirstMilestoneBoss) {
			const float cx = kFieldW * 0.5f;
			const float cy = kFieldTop + (kFieldH - kFieldTop) * 0.46f;
			const float k = clampf(kDt * 5.2f, 0.0f, 1.0f);
			b.x += (cx - b.x) * k;
			b.y += (cy - b.y) * k;
			b.boomT -= kDt;
			if (b.boomT <= 0 && b.leaveT < 4.92f) {
				const float p = clampf(b.leaveT / 5.0f, 0.0f, 1.0f);
				const bool onBoss = gRng.unit() < 0.58f;
				const float bx = onBoss ? clampf(b.x + gRng.range(-b.radius * 1.4f, b.radius * 1.4f), 20.0f, kFieldW - 20.0f)
				                        : gRng.range(24.0f, kFieldW - 24.0f);
				const float by = onBoss ? clampf(b.y + gRng.range(-b.radius * 1.3f, b.radius * 1.3f), kFieldTop + 20.0f, kFieldH - 20.0f)
				                        : gRng.range(kFieldTop + 22.0f, kFieldH - 22.0f);
				boom::start(bx, by, gRng.range(0.65f, 1.05f + 0.45f * p));
				emitSparks(bx, by, hueColor(gRng.unit()), juiceAmount(10), 160);
				audio::play(SFX_BOOM, 0.55f + 0.35f * p, fieldPan(bx), gRng.range(0.88f, 1.25f));
				shake = fmaxf(shake, 0.22f + 0.28f * p);
				b.boomT = lerpf(0.32f, 0.09f, p);
			}
			if (b.leaveT >= 5.0f) {
				bossSpawnPieces(b.id, b.x, b.y);
				audio::play(SFX_FINALE_BURST, 1.0f);
				audio::play(SFX_VICTORY, 1.0f);
				b.on = false;
#ifdef BOSS_TEST_APP
				extern bool gBossTestDone;
				gBossTestDone = true;
				return;
#endif
				orcNextRoom();
			}
			return;
		}
		b.y -= 40 * kDt;
		if (b.leaveT > 1.6f) {
			b.on = false;
#ifdef BOSS_TEST_APP
			extern bool gBossTestDone;
			gBossTestDone = true;
			return;
#endif
			orcNextRoom();
		}
		return;
	}
	if (b.enterT > 0) {
		b.enterT -= kDt;
		b.y = lerpf(b.baseY, -b.radius * 2, clampf(b.enterT / 1.6f, 0, 1) * clampf(b.enterT / 1.6f, 0, 1));
		return;
	}
	if (b.lemHurtCd > 0) b.lemHurtCd -= kDt;
	if (b.lemCooldown > 0 && !b.lemonadeOn) b.lemCooldown -= kDt;
	// Dusty Durian: periodically extends thorns sharply outward, holds hazard, and retracts.
	if (b.id == BOSS_DURIAN && !b.beaten) {
		if (b.dazeT > 0 || !b.guard) {
			if (b.thornStage == 1 || b.thornStage == 2) b.thornStage = 3;
		}
		if (b.thornStage == 0) {
			if (b.guard && b.dazeT <= 0 && b.attack != DUR_SPIN) {
				b.thornCd -= dt;
				if (b.thornCd <= 0) {
					b.thornStage = 1;
					b.thornT = 0;
					audio::play(SFX_KNOCK, 0.7f, fieldPan(b.x), 1.4f);
				}
			}
		} else if (b.thornStage == 1) {
			b.thornT += dt;
			if (b.thornT >= 0.35f) {
				const float prev = b.thornExt;
				b.thornExt = clampf((b.thornT - 0.35f) / 0.24f, 0.0f, 1.0f);
				if (prev == 0.0f && b.thornExt > 0.0f) audio::play(SFX_SHING, 0.55f, fieldPan(b.x), 1.35f);
				if (b.thornExt >= 1.0f) {
					b.thornStage = 2;
					b.thornT = 0;
				}
			}
		} else if (b.thornStage == 2) {
			b.thornExt = 1.0f;
			b.thornT += dt;
			if (b.thornT >= (b.enraged ? 1.45f : 1.25f)) {
				b.thornStage = 3;
				b.thornT = 0;
			}
		} else if (b.thornStage == 3) {
			b.thornExt = fmaxf(0.0f, b.thornExt - dt * 2.8f);
			if (b.thornExt <= 0.0f) {
				b.thornStage = 0;
				b.thornT = 0;
				b.thornCd = b.enraged ? gRng.range(3.6f, 5.2f) : gRng.range(5.4f, 7.6f);
			}
		}
	}
	// Milestone Phase 2 steam jets shooting from both sides.
	if (b.enraged && b.id >= kFirstMilestoneBoss && !gSave.calmFx) {
		b.steamT -= kDt;
		if (b.steamT <= 0) {
			b.steamT = 0.075f;
			for (int side = -1; side <= 1; side += 2) {
				for (auto& p : parts) {
					if (p.on) continue;
					p = {};
					p.on = true;
					p.x = b.x + side * b.radius * 0.85f;
					p.y = b.y - b.radius * 0.10f + gRng.range(-5, 5);
					p.vx = side * gRng.range(72, 128) * acrossScale();
					p.vy = gRng.range(-48, -14) * kFieldK;
					p.gravity = -45.0f * kFieldK;
					p.life = p.maxLife = gRng.range(0.28f, 0.45f);
					p.size = gRng.range(0.75f, 1.15f);
					p.color = rgba(242, 246, 255, 220);
					break;
				}
			}
		}
	}
	// Lord Lemon's Lemonade & rising bubbling drain lifecycle.
	if (b.lemonadeOn) {
		if (b.lemStage == 2) {
			if (!b.drainOn) {
				b.lemTimer -= dt;
				if (b.lemTimer <= 0) {
					b.drainOn = true;
					b.drainX = clampf(kFieldW * 0.22f + gRng.range(0, kFieldW * 0.56f), 44.0f, kFieldW - 44.0f);
					b.drainY = (float)kFieldH + 14.0f;
					b.drainRise = 0.0f;
					audio::play(SFX_BUBBLES, 0.90f, fieldPan(b.drainX));
				}
			} else {
				b.drainRise = fminf(1.0f, b.drainRise + dt * 1.15f);
				b.drainY = lerpf((float)kFieldH + 14.0f, lemonadeSurfaceY(b), b.drainRise) + sinf(b.t * 4.5f) * 3.0f;
				if ((int)(b.t * 5.0f) != (int)((b.t - dt) * 5.0f)) {
					emitSparks(b.drainX + gRng.range(-8, 8), b.drainY - 4, rgba(255, 250, 175), juiceAmount(2), 60);
				}
			}
		} else if (b.lemStage == 3) {
			b.lemLevel = fmaxf(0.0f, b.lemLevel - dt * 0.78f);
			if (b.lemLevel <= 0.0f) {
				b.lemonadeOn = false;
				b.lemStage = 0;
				b.drainOn = false;
				b.lemCooldown = b.enraged ? 4.8f : 11.5f;
			}
		}
	}
	shotsUpdate(dt);
	// Check returning Pineapple Guard bombs (tag == 4) colliding with the boss, and Aggro Apple peeking worms.
	for (auto& o : objs) {
		if (!o.on || o.kind != OBJ_BOMB || o.tag != 4) continue;
		const float dx = o.x - b.x, dy = o.y - b.y;
		const float hr = b.radius + o.radius;
		if (dx * dx + dy * dy <= hr * hr) {
			const bool parryBack = b.id == BOSS_PINEAPPLE && !b.beaten && !b.dazeT && o.hits < 2 && gRng.below(100) < (b.phase >= 2 ? 28 : 18);
			if (parryBack) {
				o.tag = 3;
				o.hits++;
				o.hitCooldown = 0.25f;
				o.vx = clampf((gRng.range(48, kFieldW - 48) - o.x) * 1.15f, -180.0f, 180.0f);
				o.vy = gRng.range(160, 220) * kFieldK;
				audio::play(SFX_CLANG, 0.95f, fieldPan(b.x), 1.15f);
				emitSparks(b.x, b.y, rgba(255, 225, 110), juiceAmount(12), 180);
			} else {
				o.on = false;
				boom::start(b.x, b.y);
				emitSparks(b.x, b.y, rgba(255, 200, 90), juiceAmount(24), 240);
				audio::play(SFX_BOOM, 1.0f, fieldPan(b.x));
				bossHitSlice(b.x, b.y, o.vx, o.vy);
			}
		}
	}
	if (!b.beaten && b.id == BOSS_APPLE && b.dazeT <= 0) {
		for (auto& w : b.worms) {
			if (!w.on) continue;
			if (w.emergeT < 1.0f) w.emergeT = fminf(1.0f, w.emergeT + dt * 2.8f);
			w.timer -= dt;
			if (w.timer <= 0) {
				w.on = false;
				float wx, wy, nx, ny;
				wormHeadPos(b, w, b.x, b.y, &wx, &wy, &nx, &ny);
				audio::play(SFX_CHUCKLE, 0.8f, fieldPan(wx), 1.45f);
				emitJuice(wx, wy, rgba(250, 145, 175), juiceAmount(10), nx, ny);
				orcHurt(bossDamage(2), wx, wy, "WORM");
			}
		}
	}
	if (b.dazeT > 0) {
		b.dazeT -= dt;
		b.x += sinf(b.t * 9) * 20 * dt;  // wobbles, dizzy
		if (b.dazeT <= 0) {
			b.dazeT = 0;
			b.guard = true;
			b.openT = 0;
			b.nextAttack = 0.8f;
			b.chain = (u8)chainLength(b);
		}
		return;
	}
	if (b.openT > 0) {
		b.openT -= dt;
		if (b.openT <= 0 && b.crownOut <= 0) {
			b.openT = 0;
			b.guard = true;
		}
	}
	if (b.roarT > 0) {
		b.roarT -= dt;
		return;
	}
	// Drifts about between attacks, and floats back up after a flop/slam.
	b.baseY = fieldMidY();
	b.x = clampBossVisX(b.x, b.radius);
	if (b.drainOn) b.drainX = clampf(b.drainX, 44.0f, kFieldW - 44.0f);
	if (b.attack != MEL_FLOP && b.attack != MEL_CHARGE && b.attack != KING_GIANT && b.attack != PINE_SLAM && b.attack != PINE_WHIRL && b.attack != CHILI_RUSH && b.attack != DUR_SLAM && b.attack != DUR_SPIN && b.attack != LEM_BLITZ && b.attack != LEM_LEMONADE && b.attack != APP_CRUSH) {
		const float speed = (b.id == BOSS_KING ? 0.9f : (b.id == BOSS_CHILI || b.id >= BOSS_DURIAN ? 1.15f : 0.65f)) * lv.bossSpeed * (b.enraged ? 1.22f : 1.0f);
		const float tx = kFieldW * 0.5f + sinf(b.t * speed) * kFieldW * 0.28f;
		b.x = lerpf(b.x, tx, 1 - powf(0.2f, dt));
		const float targetBaseY = (b.lemonadeOn && b.lemLevel > 0.1f) ? fminf(b.baseY, lemonadeSurfaceY(b) - b.radius * 1.10f) : b.baseY;
		if (b.openT <= 0) b.y = lerpf(b.y, targetBaseY + sinf(b.t * 1.3f) * 6, 1 - powf(0.1f, dt));
	}
	if (b.attack != ATK_NONE) {
		bossAttack(dt);
	} else if (b.openT <= 0) {
		b.nextAttack -= dt;
		if (b.nextAttack <= 0) bossNextAttack();
	}
}

void Game::bossTest(Blade& blade) {
	Boss& b = orc.boss;
	if (!b.on || b.beaten || b.enterT > 0) return;
	const float reach = orcReach();
	// Aggro Apple peeking worms respond to both touch-tap/click and swiping near the worm.
	if (b.id == BOSS_APPLE && b.dazeT <= 0 && blade.active) {
		const float wr = 20.0f * reach;
		for (auto& w : b.worms) {
			if (!w.on) continue;
			float wx, wy;
			wormHeadPos(b, w, b.x, b.y, &wx, &wy);
			const float dx = blade.x - wx, dy = blade.y - wy;
			if ((blade.strokeStart && dx * dx + dy * dy <= wr * wr) || (!blade.strokeStart && segDist2(wx, wy, blade) <= wr * wr)) {
				w.on = false;
				b.deflectStroke = blade.strokeId;  // swatting a worm off the rim never triggers Deflected!
				audio::play(SFX_POP, 0.85f, fieldPan(wx), 1.35f);
				emitSparks(wx, wy, rgba(255, 150, 175), juiceAmount(8), 110);
				orcGainXp(2);
				sliced++;
			}
		}
	}
	if (!blade.active || blade.strokeStart) return;
	// Lord Lemon: sliceable Drain plug takes priority over sour Lemonade damage on the surface.
	bool hitDrain = false;
	if (b.lemonadeOn && b.drainOn && b.lemStage == 2) {
		const float dr = 24.0f * reach;
		if (segDist2(b.drainX, b.drainY, blade) <= dr * dr) {
			hitDrain = true;
			b.drainOn = false;
			b.lemStage = 3;
			b.deflectStroke = blade.strokeId;
			audio::play(SFX_BUBBLES, 0.95f, fieldPan(b.drainX), 0.78f);
			audio::play(SFX_OPEN, 0.75f, fieldPan(b.drainX), 1.15f);
			emitSparks(b.drainX, b.drainY, rgba(255, 248, 160), juiceAmount(14), 170);
		}
	}
	if (!hitDrain && b.lemonadeOn && b.lemLevel > 0.25f && b.lemHurtCd <= 0 && blade.y >= lemonadeSurfaceY(b) + 4.0f) {
		b.lemHurtCd = 0.65f;
		audio::play(SFX_JUICY1, 0.85f, fieldPan(blade.x), 1.45f);
		emitSparks(blade.x, blade.y, rgba(245, 240, 95), juiceAmount(8), 140);
		orcHurt(bossDamage(1), blade.x, blade.y, "SOUR!");
	}
	// Shots first: they're small and quick.
	for (auto& s : orc.shots) {
		if (!s.on || s.stroke == blade.strokeId) continue;
		const float r = (s.kind == SHOT_CANNON ? 17.0f : (s.kind == SHOT_CROWN ? 22.0f : 15.0f)) * reach + 4;
		if (segDist2(s.x, s.y, blade) > r * r) continue;
		s.stroke = blade.strokeId;
		if (--s.hits > 0) {
			audio::play(SFX_KNOCK, 0.8f, fieldPan(s.x), 0.8f);
			emitSparks(s.x, s.y, rgba(200, 200, 210), juiceAmount(6), 90);
			continue;
		}
		s.on = false;
		if (s.kind == SHOT_CROWN) {
			b.crownOut = 0;
			b.attack = ATK_NONE;
			audio::play(SFX_GOLD, 0.8f, fieldPan(s.x));
			bossOpen(orchardLevel(gSave.difficulty).openTime);
		} else {
			audio::play((Sfx)(SFX_JUICY0 + gRng.below(2)), 0.7f, fieldPan(s.x), 1.3f);
		}
		const u32 jCol = s.kind == SHOT_CANNON ? rgba(110, 110, 120) : (s.kind == SHOT_EMBER ? rgba(255, 150, 50) : (s.kind == SHOT_ACID ? rgba(235, 245, 80) : (s.kind == SHOT_THORN ? rgba(185, 165, 80) : (s.kind == SHOT_SPIKE ? rgba(240, 210, 80) : rgba(255, 110, 120)))));
		emitJuice(s.x, s.y, jCol, juiceAmount(8), blade.x - blade.px, blade.y - blade.py);
		orcGainXp(s.kind == SHOT_CANNON ? 2 : 1);
		if (orc.upgrade[CARD_JUICE]) orc.hp = fminf(orc.maxHp, orc.hp + 0.25f);
		sliced++;
	}
	if (b.attack == KING_GIANT) {
		bossGiantTest(blade, reach);
		return;  // the King is away
	}
	if (b.dazeT > 0) return;  // confused: swipes pass straight through
	// The Bomb King's fuse sits on top of it.
	if (b.attack == KING_FUSE) {
		const float fx = b.x, fy = b.y - b.radius - 8, r = 18 * reach;
		if (segDist2(fx, fy, blade) <= r * r) {
			if (blade.strokeId != b.deflectStroke) {
				b.deflectStroke = blade.strokeId;
				b.fuseCuts++;
				emitSparks(fx, fy, rgba(255, 200, 90), juiceAmount(8), 120);
				audio::play(SFX_KNOCK, 0.6f, fieldPan(fx), 1.6f);
			}
			return;
		}
	}
	// Dusty Durian extended thorns hazard: extends well past the core and hurts on contact.
	if (b.id == BOSS_DURIAN && b.thornExt > 0.45f) {
		const float thornR = b.radius * (0.98f + 0.54f * b.thornExt) * reach;
		if (segDist2(b.x, b.y, blade) <= thornR * thornR) {
			if (blade.strokeId == b.deflectStroke || b.deflectT > 0) return;
			b.deflectStroke = blade.strokeId;
			b.deflectT = 0.55f;
			audio::play(SFX_CLANG, 0.9f, fieldPan(b.x), 1.35f);
			emitSparks(blade.x, blade.y, rgba(242, 224, 128), juiceAmount(12), 165);
			blade.speed *= 0.3f;
			orcHurt(bossDamage(1), blade.x, blade.y, "THORNS");
			return;
		}
	}
	float scale = 1 - (b.squash < 0 ? b.squash * 0.8f : 0);
	const float r = b.radius * scale * 0.92f * reach;
	if (segDist2(b.x, b.y, blade) > r * r) return;
	if (b.attack == MEL_CHARGE || b.attack == DUR_SPIN) {
		b.attack = ATK_NONE;
		b.squash = 0;
		b.nextAttack = 2;
		b.deflectStroke = blade.strokeId;
		b.deflectT = 1.2f;
		audio::play(SFX_CLANG, 1.0f, fieldPan(b.x), 0.8f);
		if (b.finisher) bossOpen(orchardLevel(gSave.difficulty).openTime * 1.3f);
		return;
	}
	if (b.guard && !orc.cleaver) {
		if (blade.strokeId == b.deflectStroke || b.deflectT > 0) return;
		b.deflectStroke = blade.strokeId;
		b.deflectT = 0.5f;
		audio::play(SFX_CLANG, 1.0f, fieldPan(b.x));
		emitSparks(blade.x, blade.y, rgba(170, 210, 255), juiceAmount(10), 150);
		static const float kChuckPitch[BOSS_COUNT] = {1.25f, 0.80f, 1.02f, 1.38f, 0.78f, 1.42f, 0.92f};
		audio::play(SFX_CHUCKLE, 0.7f, fieldPan(b.x), kChuckPitch[b.id % BOSS_COUNT]);
		blade.speed *= 0.3f;
		const float parryChance = 1.0f - powf(0.90f, (float)orc.upgrade[CARD_PARRY]);
		if (orc.lastStandT > 0 || (orc.upgrade[CARD_PARRY] > 0 && gRng.unit() < parryChance)) {
			return;
		}
		const int deflect = orchardLevel(gSave.difficulty).deflectDamage + (mode == MODE_ORCHARD && orc.room >= 40 ? 1 : 0);
		orcHurt(deflect, blade.x, blade.y, "DEFLECTED!");
		return;
	}
	// A clean slice.
	orc.cleaver = false;
	bossHitSlice(b.x, b.y, blade.x - blade.px, blade.y - blade.py);
}

// The Giant Bomb. Stage 0: the King floats off the top. Stage 1: the bomb sits ticking in the middle until it is
// scrubbed off the stage or goes off. Stage 2: the King drops back in, stunned if it was launched.
void Game::bossGiant(float dt) {
	Boss& b = orc.boss;
	GiantBomb& g = b.giant;
	const int d = gSave.difficulty < DIFF_COUNT ? gSave.difficulty : DIFF_NORMAL;
	if (b.volley == 0) {
		b.y = lerpf(b.y, -b.radius * 3, 1 - powf(0.002f, dt));
		if (b.attackT >= 0.9f) {
			b.volley = 1;
			b.attackT = 0;
			b.hidden = true;
			g = {};
			g.on = true;
			g.x = kFieldW * 0.5f;
			g.y = -b.radius * 2;
			g.need = (u8)kGiantNeed[d];
			g.t = g.total = kGiantTime[d];
			g.travel = kGiantTravelPerHit;  // the first touch counts straight away
			setCallout("GIANT BOMB!", rgba(255, 150, 90));
			showCaption("Scrub the giant bomb to launch it away before it blows.");
			audio::play(SFX_WHOOSH, 1.0f, 0, 0.55f);
		}
	} else if (b.volley == 1) {
		if (g.launched) {
			g.vy -= 400 * kFieldK * dt;
			g.y += g.vy * dt;
			if (g.y < -b.radius * 2.5f) {
				g.on = false;
				b.volley = 2;
				b.attackT = 0;
			}
			return;
		}
		// It settles onto its spot; every hit knocks it up a little.
		g.y = lerpf(g.y, fieldMidY() + 14, 1 - powf(0.12f, dt));
		g.jolt *= powf(0.004f, dt);
		g.t -= dt;
		// The quit bomb's tick-tock, faster as the end comes.
		g.tick += dt;
		const float gap = 0.5f - 0.36f * clampf(1 - g.t / g.total, 0, 1);
		if (g.tick >= gap) {
			g.tick = 0;
			audio::play(SFX_KNOCK, 0.7f, fieldPan(g.x), g.ticks++ % 2 ? 1.5f : 1.85f);
			PORT_VIBRATE(0.25f, 20);
		}
		if (g.t <= 0) {
			g.on = false;
			audio::play(SFX_BOOM, 1.0f, fieldPan(g.x), 0.7f);
			audio::play(SFX_FINALE_BURST, 0.6f, fieldPan(g.x), 0.6f);
			flash = 0.7f;
			flashColor = rgba(255, 200, 140);
			shake = 1.6f;
			PORT_VIBRATE(1.0f, 300);
			emitSparks(g.x, g.y, rgba(255, 190, 90), juiceAmount(40), 300);
			addRing(g.x, g.y, rgba(255, 170, 80), 4.0f, 0.4f);
			orcHurt(bossDamage(kGiantDamage), g.x, g.y, "GIANT BOMB");
			b.volley = 2;
			b.attackT = 0;
		}
	} else {
		if (b.hidden) {
			b.hidden = false;
			audio::play(SFX_WHOOSH, 1.0f, 0, 0.75f);
		}
		b.y = lerpf(b.y, b.baseY, 1 - powf(0.003f, dt));
		if (b.attackT >= 0.9f) {
			b.attack = ATK_NONE;
			if (g.launched) {
				bossOpen(orchardLevel(gSave.difficulty).openTime * 1.5f, false);
			} else {
				audio::play(SFX_LAUGH, 1.0f, fieldPan(b.x), 0.8f);
			}
			g.launched = false;
		}
	}
}

void Game::bossGiantTest(const Blade& blade, float reach) {
	Boss& b = orc.boss;
	GiantBomb& g = b.giant;
	if (!g.on || g.launched || b.volley != 1) return;
	const float dx = blade.x - blade.px, dy = blade.y - blade.py, len = sqrtf(dx * dx + dy * dy);
	Obj probe = {};
	probe.x = g.x;
	probe.y = g.y;
	if (len < 1.5f || !hitTest(probe, blade, b.radius * reach)) return;
	g.travel += len;
	while (g.travel >= kGiantTravelPerHit && !g.launched) {
		g.travel -= kGiantTravelPerHit;
		g.hits++;
		g.y -= 7 * kFieldK;
		g.jolt = 1;
		emitSparks(blade.x, blade.y, rgba(255, 200, 120), juiceAmount(3), 120);
		audio::play(SFX_FINALE_HIT, 0.75f, fieldPan(g.x), clampf(0.8f + g.hits * 0.03f, 0.8f, 1.8f));
		if (g.hits >= g.need) {
			g.launched = true;
			g.vy = -260 * kFieldK;
			audio::play(SFX_LAUNCH, 1.0f, fieldPan(g.x));
			orcGainXp(6);
		}
	}
}

// ---------------------------------------------------------------- drawing
static void drawCrown(float x, float y, float s, u32 gold, float tilt = 0.0f) {
	const u32 dark = rgba(200, 140, 30, (gold >> 24) & 0xFF);
	const float c = cosf(tilt), sn = sinf(tilt);
	auto rx = [&](float dx, float dy) { return x + (dx * c - dy * sn) * s; };
	auto ry = [&](float dx, float dy) { return y + (dx * sn + dy * c) * s; };
	auto quad = [&](float x0, float y0, float x1, float y1, u32 col) {
		const float ax = rx(x0, y0), ay = ry(x0, y0), bx = rx(x1, y0), by = ry(x1, y0);
		const float cx = rx(x1, y1), cy = ry(x1, y1), dx = rx(x0, y1), dy = ry(x0, y1);
		gfx::tri(ax, ay, col, bx, by, col, cx, cy, col);
		gfx::tri(ax, ay, col, cx, cy, col, dx, dy, col);
	};
	quad(-14, -2, 14, 6, dark);
	for (int i = -1; i <= 1; i++) {
		const float ox = i * 10.0f;
		gfx::tri(rx(ox - 6, 0), ry(ox - 6, 0), gold, rx(ox + 6, 0), ry(ox + 6, 0), gold, rx(ox, i == 0 ? -15.0f : -11.0f), ry(ox, i == 0 ? -15.0f : -11.0f), gold);
	}
	quad(-14, 0, 14, 5, gold);
}

void Game::drawShots() const {
	const Boss& b = orc.boss;
	const bool in3D = drawing3D();
	if (in3D) {
		m3d::begin(kFieldW, gfx::stereoPerZ(), true);
		for (const auto& s : orc.shots) {
			if (!s.on) continue;
			const float ang = atan2f(s.vy, s.vx) + 1.5708f;
			if (s.kind == SHOT_SEED) {
				m3d::Pose pose = {s.x, s.y, 26, s.age * 3.0f, s.rot * 0.7f, ang, 0.66f, 0, 0, 0};
				m3d::draw(m3d::MESH_SEED, m3d::PART_WHOLE, pose);
			} else if (s.kind == SHOT_SPLASH) {
				m3d::Pose pose = {s.x, s.y, 26, s.age * 2.5f, s.rot * 0.8f, ang, 0.72f, 0, 0, 0};
				m3d::draw(m3d::MESH_SPLASH, m3d::PART_WHOLE, pose);
			} else if (s.kind == SHOT_CANNON) {
				m3d::Skin skin = {s.hits == 1 ? 0.45f : 0.0f, 1.0f, 0.35f, 0.25f, 1.0f, 0.35f};
				m3d::setSkin(&skin);
				m3d::Pose pose = {s.x, s.y, 26, s.age * 2.0f, s.rot, s.rot * 0.6f, 0.8f, 0, 0, 0};
				m3d::draw(m3d::MESH_BOMB, m3d::PART_WHOLE, pose);
				m3d::setSkin(nullptr);
			} else if (s.kind == SHOT_CROWN) {
				m3d::Skin skin = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.75f};
				m3d::setSkin(&skin);
				m3d::Pose pose = {s.x, s.y, 28, 0.35f, s.age * 7.5f, s.rot * 0.4f, 1.15f, 0, 0, 0};
				m3d::draw(m3d::MESH_CROWN, m3d::PART_WHOLE, pose);
				m3d::setSkin(nullptr);
			} else if (s.kind == SHOT_SPIKE || s.kind == SHOT_THORN) {
				m3d::Pose pose = {s.x, s.y, 26, s.age * 4.0f, s.rot * 1.2f, ang, s.kind == SHOT_THORN ? 0.56f : 0.52f, 0, 0, 0};
				m3d::draw(m3d::MESH_SPIKY, m3d::PART_WHOLE, pose);
			} else if (s.kind == SHOT_ACID) {
				m3d::Skin skin = {0.5f, 0.82f, 1.0f, 0.18f, 1.0f, 0.45f};
				m3d::setSkin(&skin);
				m3d::Pose pose = {s.x, s.y, 26, s.age * 3.0f, s.rot, ang, 0.52f, 0, 0, 0};
				m3d::draw(m3d::MESH_LEMON, m3d::PART_WHOLE, pose);
				m3d::setSkin(nullptr);
			} else if (s.kind == SHOT_EMBER) {
				m3d::Skin skin = {0.55f, 1.0f, 0.55f, 0.15f, 1.0f, 0.5f};
				m3d::setSkin(&skin);
				m3d::Pose pose = {s.x, s.y, 26, s.age * 3.5f, s.rot, s.rot * 0.8f, 0.36f, 0, 0, 0};
				m3d::draw(m3d::MESH_SUNBURST, m3d::PART_WHOLE, pose);
				m3d::setSkin(nullptr);
			}
		}
		m3d::end();
	}
	for (const auto& s : orc.shots) {
		if (!s.on) continue;
		if (in3D && (s.kind == SHOT_SEED || s.kind == SHOT_SPLASH || s.kind == SHOT_CANNON || s.kind == SHOT_CROWN || s.kind == SHOT_SPIKE || s.kind == SHOT_THORN || s.kind == SHOT_ACID || s.kind == SHOT_EMBER)) continue;
		switch (s.kind) {
			case SHOT_SEED: gfx::sprite(SHEET_SPRITES, sprites_drop_idx, s.x, s.y, 1.15f, 1.45f, atan2f(s.vy, s.vx) - 1.5708f, rgba(70, 40, 30)); break;
			case SHOT_SPLASH: gfx::sprite(SHEET_SPRITES, sprites_drop_idx, s.x, s.y, 1.5f, 1.5f, s.rot * 0.3f, rgba(255, 80, 110)); break;
			case SHOT_CANNON:
				gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, s.x, s.y, 0.8f, 0.8f, s.rot, s.hits > 1 ? rgba(120, 120, 130) : rgba(200, 120, 110));
				break;
			case SHOT_CROWN: drawCrown(s.x, s.y, 1.1f, rgba(255, 210, 70), s.rot); break;
			case SHOT_SPIKE: gfx::sprite(SHEET_SPRITES, sprites_spark_idx, s.x, s.y, 1.15f, 1.15f, s.rot, rgba(210, 235, 95)); break;
			case SHOT_THORN: gfx::sprite(SHEET_SPRITES, sprites_spark_idx, s.x, s.y, 1.3f, 1.3f, s.rot, rgba(180, 155, 70)); break;
			case SHOT_ACID: gfx::sprite(SHEET_SPRITES, sprites_drop_idx, s.x, s.y, 1.35f, 1.35f, s.rot, rgba(225, 245, 75)); break;
			case SHOT_EMBER: gfx::sprite(SHEET_SPRITES, sprites_spark_idx, s.x, s.y, 1.3f, 1.3f, -s.rot, rgba(255, 145, 50)); break;
			default: break;
		}
	}
	// Lord Lemon's sour Lemonade pool & sliceable Drain plug (volumetric 3D pool + 3D drain in 3D style, flat 2D in Stickers).
	if (b.on && b.lemonadeOn && b.lemLevel > 0.01f) {
		const float surfY = lemonadeSurfaceY(b);
		const float bottomY = kFillX + kFillW;
		if (in3D) {
			const float poolH = fmaxf(1.0f, bottomY - surfY);
			const float persp = clampf(poolH * 0.15f, 8.0f, 22.0f);
			const float backY = surfY - persp;
			const float midY = surfY + poolH * 0.45f;
			auto quadGrad = [](float x0, float y0, float x1, float y1, u32 topCol, u32 botCol) {
				gfx::tri(x0, y0, topCol, x1, y0, topCol, x1, y1, botCol);
				gfx::tri(x0, y0, topCol, x1, y1, botCol, x0, y1, botCol);
			};
			// Submerged depth gradient: sunlit citrus top -> golden mid -> deep amber bottom.
			const u32 cTop = rgba(252, 234, 64, 126), cMid = rgba(228, 184, 24, 168), cBot = rgba(176, 124, 12, 198);
			quadGrad(kFillX, surfY, kFillX + kFillW, midY, cTop, cMid);
			quadGrad(kFillX, midY, kFillX + kFillW, bottomY, cMid, cBot);
			// Subsurface caustic light beams.
			const float calm = gSave.calmFx ? 0.0f : 1.0f;
			for (int i = 0; i < 5; i++) {
				const float bx0 = kFillX + kFillW * (0.10f + i * 0.20f) + sinf(b.t * 1.4f + i * 1.7f) * 8.0f * calm;
				const float sway = sinf(b.t * 1.1f + i * 2.1f) * 14.0f * calm;
				const u32 rayTop = rgba(255, 252, 182, 34), rayBot = rgba(255, 245, 130, 0);
				gfx::tri(bx0 - 10.0f, surfY, rayTop, bx0 + 10.0f, surfY, rayTop, bx0 + sway + 22.0f, bottomY, rayBot);
				gfx::tri(bx0 - 10.0f, surfY, rayTop, bx0 + sway + 22.0f, bottomY, rayBot, bx0 + sway - 22.0f, bottomY, rayBot);
			}
			// Receding 3D top liquid surface plane with specular meniscus crest.
			const int segs = 20;
			const float segW = kFillW / segs;
			const u32 surfBack = rgba(255, 250, 178, 155), surfFront = rgba(250, 232, 74, 215);
			const u32 crestCol = rgba(255, 254, 200, 238), rimCol = rgba(255, 254, 215, 175);
			for (int i = 0; i < segs; i++) {
				const float x0 = kFillX + i * segW, x1 = x0 + segW + 0.6f;
				const float fw0 = calm * sinf(b.t * 4.0f + i * 0.55f) * 2.2f;
				const float fw1 = calm * sinf(b.t * 4.0f + (i + 1) * 0.55f) * 2.2f;
				const float bw0 = calm * sinf(b.t * 3.1f + i * 0.45f + 1.2f) * 1.2f;
				const float bw1 = calm * sinf(b.t * 3.1f + (i + 1) * 0.45f + 1.2f) * 1.2f;
				gfx::tri(x0, backY + bw0, surfBack, x1, backY + bw1, surfBack, x1, surfY + fw1, surfFront);
				gfx::tri(x0, backY + bw0, surfBack, x1, surfY + fw1, surfFront, x0, surfY + fw0, surfFront);
				gfx::rect(x0, backY + bw0 - 1.0f, segW + 0.8f, 2.0f, rimCol);
				gfx::rect(x0, surfY + fw0 - 1.5f, segW + 0.8f, 3.2f, crestCol);
			}
			// Depth-layered rising bubbles inside the pool.
			for (int i = 0; i < 10; i++) {
				const float ph = fmodf(b.t * (0.28f + (i % 3) * 0.08f) * (gSave.calmFx ? 0.25f : 1.0f) + i * 0.37f, 1.0f);
				const float bx = kFillX + kFillW * (0.06f + ((i * 73 + 19) % 88) * 0.01f) + sinf(b.t * 2.3f + i) * 4.0f * calm;
				const float by = lerpf(bottomY - 6.0f, surfY + 4.0f, ph);
				const bool front = (i & 1) != 0;
				const float r = front ? (2.6f + (i % 3) * 0.9f) : (1.8f + (i % 2) * 0.6f);
				gfx::rect(bx - r, by - r, r * 2.0f, r * 2.0f, front ? rgba(255, 250, 175, 165) : rgba(215, 175, 40, 130));
				if (front) gfx::rect(bx - r * 0.5f, by - r * 0.5f, r * 0.65f, r * 0.65f, rgba(255, 255, 240, 210));
			}
			if (b.drainOn) {
				const float pulse = 0.85f + 0.15f * sinf(b.t * 8.0f);
				gfx::sprite(SHEET_SPRITES, sprites_ring_idx, b.drainX, surfY, pulse * 0.95f, pulse * 0.35f, 0, rgba(255, 252, 200, 185));
				gfx::sprite(SHEET_SPRITES, sprites_ring_idx, b.drainX, b.drainY, pulse * 0.9f, pulse * 0.9f, 0, rgba(160, 245, 255, 225));
				m3d::begin(kFieldW, gfx::stereoPerZ(), true);
				m3d::Skin drainSkin = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.72f};
				m3d::setSkin(&drainSkin);
				m3d::Pose dpose = {b.drainX, b.drainY, 34.0f, 0.52f, sinf(b.t * 2.1f) * 0.45f, sinf(b.t * 4.0f) * 0.14f, 1.05f, 0, 0, 0};
				m3d::draw(m3d::MESH_DRAIN, m3d::PART_WHOLE, dpose);
				m3d::setSkin(nullptr);
				m3d::end();
			}
		} else {
			gfx::rect(kFillX, surfY, kFillW, fmaxf(0.0f, bottomY - surfY), rgba(246, 224, 48, 136));
			const int segs = 16;
			const float segW = kFillW / segs;
			for (int i = 0; i < segs; i++) {
				const float wave = gSave.calmFx ? 0.0f : sinf(b.t * 4.0f + i * 0.65f) * 2.2f;
				gfx::rect(kFillX + i * segW, surfY + wave - 2.0f, segW + 1.0f, 5.0f, rgba(255, 248, 156, 210));
			}
			if (b.drainOn) {
				const float pulse = 0.85f + 0.15f * sinf(b.t * 8.0f);
				gfx::sprite(SHEET_SPRITES, sprites_ring_idx, b.drainX, b.drainY, pulse * 0.9f, pulse * 0.9f, 0, rgba(160, 245, 255, 225));
				gfx::sprite(SHEET_SPRITES, sprites_lem_drain_idx, b.drainX, b.drainY, 1.0f, 1.0f, sinf(b.t * 4.0f) * 0.12f);
			}
		}
	}
}

void Game::drawBoss(bool solid) const {
	(void)solid;
	const Boss& b = orc.boss;
	if (!b.on) return;
	if (b.giant.on) {
		// The Giant Bomb: it warms toward red as its time runs out, with a ring of dots for the time left and the
		// hits still needed over it. No flashing: the colour changes smoothly.
		const GiantBomb& g = b.giant;
		const float k = clampf(g.t / g.total, 0, 1), warm = 1 - k;
		const float s = b.radius / 19.0f * (1 + 0.08f * g.jolt);
		if (drawing3D()) {
			m3d::begin(kFieldW, gfx::stereoPerZ(), true);
			m3d::Skin skin = {warm * 0.55f, 1.0f, 0.3f, 0.2f, 1.0f, 0.35f};
			m3d::setSkin(&skin);
			m3d::Pose pose = {g.x, g.y, 30, 0, b.t * 0.6f, sinf(b.t * 3) * 0.08f, s, 0, 0, 0};
			m3d::draw(m3d::MESH_BOMB, m3d::PART_WHOLE, pose);
			m3d::setSkin(nullptr);
			m3d::end();
		} else {
			gfx::sprite(SHEET_FRUIT, fruit_bomb_idx, g.x, g.y, s, s, sinf(b.t * 3) * 0.08f, rgba(255, (int)(255 - 110 * warm), (int)(255 - 150 * warm)));
		}
		if (!g.launched) {
			const int dots = 32, lit = (int)ceilf(k * dots);
			const float rr = b.radius * 1.25f;
			for (int i = 0; i < dots; i++) {
				const float a = -1.5708f + i * 6.2832f / dots;
				gfx::rect(g.x + cosf(a) * rr - 2, g.y + sinf(a) * rr - 2, 4, 4, i < lit ? rgba(255, 220, 120) : rgba(60, 40, 30, 140));
			}
			char buf[8];
			snprintf(buf, sizeof buf, "%d", g.need - g.hits);
			gfx::text(buf, g.x, g.y - rr - 30, 0.9f, rgba(255, 230, 140), ALIGN_CENTER);
		}
	}
	if (b.hidden) return;
	const bool milestoneDead = b.beaten && b.id >= kFirstMilestoneBoss;
	float alpha = 1;
	if (milestoneDead) alpha = 1.0f;
	else if (b.beaten) alpha = clampf(1 - b.leaveT / 1.6f, 0, 1);
	if (b.dazeT > 0 && !milestoneDead) {
		// Confused: a flicker no faster than three times a second, or a steady see-through look with Calm Effects.
		alpha *= gSave.calmFx ? 0.55f : (((int)(b.dazeT * 6)) & 1 ? 0.35f : 0.9f);
	}
	const float deadP = milestoneDead ? clampf(b.leaveT / 5.0f, 0.0f, 1.0f) : 0.0f;
	const float grow = (b.squash < 0 ? -b.squash * 0.8f : 0) + (milestoneDead ? (0.06f * sinf(b.leaveT * 26.0f) + 0.14f * deadP) : 0.0f);
	const float flat = b.squash > 0 ? b.squash * 0.25f : 0;
	const float sx = (1 + grow) * (1 + flat), sy = (1 + grow) * (1 - flat);
	const bool hurt = b.hurtT > 0;
	const float visX = clampBossVisX(b.x, b.radius);
	const float wob = milestoneDead ? sinf(b.leaveT * 14.0f) * 0.16f : (b.dazeT > 0 ? sinf(b.t * 7) * 0.25f : sinf(b.t * 1.7f) * 0.06f);
	const float bx = visX + (milestoneDead && !gSave.calmFx ? sinf(b.leaveT * 52.0f) * (2.2f + 4.0f * deadP) : 0.0f);
	const float by = b.y + (milestoneDead && !gSave.calmFx ? cosf(b.leaveT * 43.0f) * (1.8f + 3.2f * deadP) : 0.0f);
	const bool showGuard = b.guard && !b.beaten && b.enterT <= 0 && b.dazeT <= 0;
	const bool lemonFloat = b.id == BOSS_LEMON && !b.beaten && (b.attack == LEM_LEMONADE || b.lemonadeOn);
	const float floatY = by + b.radius * (drawing3D() ? 0.56f : 0.44f) * sy;
	// 2D guard shield ring behind the boss (never any gold ring when open).
	if (!drawing3D() && showGuard) {
		const float ring = b.radius * 1.5f * (1 + grow) / 34;
		const float pulse = 0.65f + 0.25f * sinf(b.t * 5);
		gfx::sprite(SHEET_SPRITES, sprites_ring_idx, bx, by, ring, ring, 0, rgba(140, 200, 255, (int)(200 * pulse * alpha)));
	}
	// The pose: shocked X-eye death pose on milestone finales, dizzy when confused/beaten, laughing with guard down, gritting mid-attack.
	const int pose = b.beaten || b.dazeT > 0 ? BOSS_POSE_DAZE : !b.guard ? BOSS_POSE_OPEN : b.attack != ATK_NONE ? BOSS_POSE_WIND : BOSS_POSE_GUARD;
	const bool angryRed = !b.beaten && b.enraged && b.id >= kFirstMilestoneBoss;
	const u32 tint = hurt ? rgba(255, 140, 140, (int)(255 * alpha))
	                      : (milestoneDead ? rgba(255, 240 - (int)(55 * deadP), 240 - (int)(85 * deadP), 255)
	                      : (angryRed ? rgba(255, 128, 116, (int)(255 * alpha)) : rgba(255, 255, 255, (int)(255 * alpha))));
	static const int kDeadFaceIdx[3] = {
	    sprites_bface_durian_dead_idx, sprites_bface_lemon_dead_idx, sprites_bface_apple_dead_idx};
	static const int kDeadBodyIdx[3] = {
	    sprites_boss_durian_dead_idx, sprites_boss_lemon_dead_idx, sprites_boss_apple_dead_idx};
	const int bi = b.id % BOSS_COUNT;
	const int faceIdx = milestoneDead ? kDeadFaceIdx[bi - kFirstMilestoneBoss] : (kBossFaceIdx[bi] + pose);
	const int bodyIdx = milestoneDead ? kDeadBodyIdx[bi - kFirstMilestoneBoss] : (kBossBodyIdx[bi] + pose);
	const float art = b.radius / kArtDiv[bi];
	if (drawing3D()) {
		// The game's own 3D fruit, made big, with attached 3D floatie/thorns/worms and pose face overlay.
		m3d::begin(kFieldW, gfx::stereoPerZ(), true);
		m3d::Skin skin = {hurt ? 0.6f : (angryRed ? 0.42f : (b.surgeT > 0 ? 0.25f : 0.0f)), 1.0f, angryRed ? 0.22f : 0.35f, (angryRed || b.surgeT > 0) ? 0.14f : 0.35f, alpha, 0.3f};
		if (!hurt && b.surgeT <= 0 && !angryRed && milestoneDead) {
			skin = {0.22f * deadP, 1.0f, 0.62f, 0.25f, 1.0f, 0.35f};
		} else if (!hurt && b.surgeT <= 0 && !angryRed && bi == BOSS_DURIAN) {
			skin = {0.26f, 0.80f, 0.70f, 0.32f, alpha, 0.25f};
		}
		m3d::setSkin(&skin);
		const float s = b.radius / kMeshDiv[bi];
		m3d::Pose pose3 = {bx, by, 30, b.rx, b.t * 0.4f, wob, s * (1 + grow), 0, 0, 0};
		m3d::draw(kBossMeshId[bi], m3d::PART_WHOLE, pose3);
		if (b.id == BOSS_DURIAN && b.thornExt > 0.02f) {
			m3d::Pose tpose = pose3;
			tpose.scale = s * (1 + grow) * lerpf(0.60f, 1.04f, b.thornExt);
			m3d::draw(m3d::MESH_DURIAN_THORNS, m3d::PART_WHOLE, tpose);
		}
		if (lemonFloat) {
			m3d::Skin floatSkin = {0.0f, 1.0f, 1.0f, 1.0f, alpha, 0.42f};
			m3d::setSkin(&floatSkin);
			m3d::Pose fpose = {bx, floatY, 30, -0.32f, b.t * 0.4f, wob * 0.4f, b.radius * 1.15f * (1 + grow) / 22.8f, 0, 0, 0};
			m3d::draw(m3d::MESH_FLOATIE, m3d::PART_WHOLE, fpose);
		}
		if (b.id == BOSS_APPLE && !b.beaten) {
			m3d::Skin wormSkin = {hurt ? 0.5f : 0.0f, 1.0f, 0.4f, 0.4f, alpha, 0.35f};
			m3d::setSkin(&wormSkin);
			for (const auto& w : b.worms) {
				if (!w.on) continue;
				const float ang = atan2f(w.oy, w.ox);
				const float wig = sinf(b.t * 10.0f + w.ox) * 0.14f;
				m3d::Pose wpose = {bx + w.ox * 0.88f, by + w.oy * 0.88f, 30.0f + b.radius * 0.42f, 0.0f, -0.35f, ang + wig, (b.radius / 22.0f) * (0.28f + 0.65f * w.emergeT), 0, 0, 0};
				m3d::draw(m3d::MESH_WORM, m3d::PART_WHOLE, wpose);
			}
		}
		if ((b.id == BOSS_KING && b.crownOut <= 0) || (b.royal && b.id != BOSS_KING)) {
			m3d::Skin crownSkin = {0.0f, 1.0f, 1.0f, 1.0f, alpha, 0.75f};
			m3d::setSkin(&crownSkin);
			const float cs = b.radius / (b.royal ? 30.0f : 36.0f);
			m3d::Pose cpose = {bx, by - b.radius * sy - (b.id == BOSS_KING ? 1.0f : 6.0f), 30, 0.22f, b.t * 0.4f, wob, cs, 0, 0, 0};
			m3d::draw(m3d::MESH_CROWN, m3d::PART_WHOLE, cpose);
		}
		m3d::setSkin(nullptr);
		m3d::end();
		gfx::sprite(SHEET_SPRITES, faceIdx, bx, by, art * sx, art * sy, wob, tint);
		if (showGuard) {
			const float pulse = 0.72f + 0.28f * sinf(b.t * 5.0f);
			m3d::begin(kFieldW, gfx::stereoPerZ(), true);
			m3d::Skin shieldSkin = {0.35f, 0.36f, 0.86f, 1.0f, 0.32f * pulse * alpha, 0.90f};
			m3d::setSkin(&shieldSkin);
			m3d::Pose spose = {bx, by, 30, sinf(b.t * 0.9f) * 0.18f, b.t * 0.65f, 0, b.radius * 1.28f * (1 + grow) / 23.2f, 0, 0, 0};
			m3d::draw(m3d::MESH_SHIELD, m3d::PART_WHOLE, spose);
			m3d::setSkin(nullptr);
			m3d::end();
		}
	} else {
		if (b.id == BOSS_DURIAN && b.thornExt > 0.02f) {
			const float rBase = b.radius * 0.72f * (1 + grow);
			const float rTip = b.radius * (0.98f + 0.56f * b.thornExt) * (1 + grow);
			const u32 c0 = withAlpha(angryRed ? rgba(255, 175, 110) : rgba(242, 224, 128), alpha);
			const u32 c1 = withAlpha(angryRed ? rgba(195, 85, 55) : rgba(168, 148, 66), alpha);
			for (int i = 0; i < 12; i++) {
				const float a = wob + i * (6.283185f / 12.0f) + (b.attack == DUR_SPIN ? b.t * 4.0f : 0.0f);
				const float ca = cosf(a), sa = sinf(a);
				const float px = -sa * b.radius * 0.15f, py = ca * b.radius * 0.15f;
				gfx::tri(bx + ca * rBase + px, by + sa * rBase + py, c1,
				         bx + ca * rBase - px, by + sa * rBase - py, c0,
				         bx + ca * rTip, by + sa * rTip, c0);
			}
		}
		if (lemonFloat) {
			gfx::sprite(SHEET_SPRITES, sprites_lem_floatie_back_idx, bx, floatY, art * 1.18f * sx, art * 1.18f * sy, wob * 0.35f, withAlpha(WHITE, alpha));
		}
		gfx::sprite(SHEET_SPRITES, bodyIdx, bx, by, art * sx, art * sy, wob, tint);
		if (lemonFloat) {
			gfx::sprite(SHEET_SPRITES, sprites_lem_floatie_front_idx, bx, floatY, art * 1.18f * sx, art * 1.18f * sy, wob * 0.35f, withAlpha(WHITE, alpha));
		}
		if (b.id == BOSS_APPLE && !b.beaten) {
			for (const auto& w : b.worms) {
				if (!w.on) continue;
				const float hx = bx + w.ox, hy = by + w.oy;
				const float ang = atan2f(w.oy, w.ox);
				const float wig = sinf(b.t * 10.0f + w.ox) * 0.14f;
				const float len = sqrtf(w.ox * w.ox + w.oy * w.oy) + 0.001f;
				const float nx = w.ox / len, ny = w.oy / len;
				const float wx = hx + nx * b.radius * (0.04f + 0.28f * w.emergeT);
				const float wy = hy + ny * b.radius * (0.04f + 0.28f * w.emergeT);
				const float wscX = (b.radius / 48.0f) * (0.45f + 0.55f * w.emergeT);
				const float wscY = (ang > 1.5708f || ang < -1.5708f) ? -wscX : wscX;
				gfx::sprite(SHEET_SPRITES, sprites_boss_worm_hole_idx, hx, hy, b.radius / 52.0f, b.radius / 52.0f, ang);
				gfx::sprite(SHEET_SPRITES, sprites_boss_worm_idx, wx, wy, wscX, wscY, ang + wig);
			}
		}
	}
	if (b.id == BOSS_APPLE && !b.beaten) {
		for (const auto& w : b.worms) {
			if (!w.on || w.timer >= 1.2f) continue;
			float wx, wy;
			wormHeadPos(b, w, visX, b.y, &wx, &wy);
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, wx, wy, 0.58f, 0.58f, 0, rgba(255, 110, 120, (u8)(180 + 70 * sinf(b.t * 16))));
		}
	}
	// The King wears his crown; any Royal boss wears a golden crown too (2D mode draws it here; 3D mode draws MESH_CROWN above).
	if (!drawing3D() && ((b.id == BOSS_KING && b.crownOut <= 0) || (b.royal && b.id != BOSS_KING)))
		drawCrown(bx, by - b.radius * sy - (b.id == BOSS_KING ? 2 : 8), b.radius / (b.royal ? 32.0f : 40.0f), rgba(255, 210, 70, (int)(255 * alpha)));
	if (b.attack == KING_FUSE) {
		const float fx = visX, fy = b.y - b.radius - 8;
		const bool lit = ((int)(b.t * 10)) & 1;
		gfx::sprite(SHEET_SPRITES, sprites_ring_idx, fx, fy, 0.9f, 0.9f, 0, rgba(255, 170, 60, lit ? 255 : 160));
		char buf[16];
		snprintf(buf, sizeof buf, "%d", 3 - b.fuseCuts);
		gfx::text(buf, fx, fy - 30, 0.7f, rgba(255, 220, 120), ALIGN_CENTER);
	}
	if (b.attack == MEL_CHARGE && b.attackT < 0.6f && (((int)(b.attackT * 8)) & 1)) {
		gfx::text("!", visX, b.y - b.radius - 34, 1.2f, rgba(255, 120, 90), ALIGN_CENTER);
	}
	// While the guard is down, a bar shows how long it stays down.
	if (!b.guard && !b.beaten && b.dazeT <= 0 && b.openT > 0) {
		const float w = b.radius * 1.6f, k = clampf(b.openT / 3.0f, 0, 1);
		gfx::rect(visX - w * 0.5f, b.y + b.radius * sy + 8, w, 5, rgba(40, 30, 20, 160));
		gfx::rect(visX - w * 0.5f, b.y + b.radius * sy + 8, w * k, 5, rgba(255, 220, 110));
	}
}

#ifdef BOSS_TEST_APP
void Game::bossPreview(int bossId, bool royal, float x, float y, float radius, float t, float screenW) const {
	const int bi = (bossId >= 0 && bossId < BOSS_COUNT) ? bossId : 0;
	const float wob = sinf(t * 1.7f) * 0.06f;
	const float art = radius / kArtDiv[bi];
	if (gSave.artStyle == ART_3D && m3d::ready()) {
		m3d::begin(screenW, gfx::stereoPerZ(), false);
		m3d::Skin skin = {0.0f, 1.0f, 0.35f, 0.35f, 1.0f, 0.3f};
		if (bi == BOSS_DURIAN) skin = {0.26f, 0.80f, 0.70f, 0.32f, 1.0f, 0.25f};
		m3d::setSkin(&skin);
		const float s = radius / kMeshDiv[bi];
		m3d::Pose pose3 = {x, y, 0, 0, t * 0.55f, wob, s, 0, 0, 0};
		m3d::draw(kBossMeshId[bi], m3d::PART_WHOLE, pose3);
		if (bi == BOSS_KING || royal) {
			m3d::Skin crownSkin = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.75f};
			m3d::setSkin(&crownSkin);
			m3d::Pose cpose = {x, y - radius - (bi == BOSS_KING ? 1.0f : 6.0f), 0, 0.22f, t * 0.55f, wob, radius / (royal ? 30.0f : 36.0f), 0, 0, 0};
			m3d::draw(m3d::MESH_CROWN, m3d::PART_WHOLE, cpose);
		}
		m3d::setSkin(nullptr);
		m3d::end();
		gfx::sprite(SHEET_SPRITES, kBossFaceIdx[bi], x, y, art, art, wob, WHITE);
	} else {
		gfx::sprite(SHEET_SPRITES, kBossBodyIdx[bi], x, y, art, art, wob, WHITE);
		if (bi == BOSS_KING || royal)
			drawCrown(x, y - radius - (bi == BOSS_KING ? 2 : 7), radius / (royal ? 32.0f : 40.0f), rgba(255, 210, 70));
	}
}
#endif
