// Orchard Blast: rooms, HP, XP, level-up cards and charms. The bosses are in boss.cpp,
// and the level-up screen and Inventory in orchard_ui.inc.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "audio_port.h"  // the Lab gives the 3DS music reader real time
#include "fruitids.h"
#include "game.h"
#include "render.h"
#include "shop.h"
#include "sprites.h"

// ---------------------------------------------------------------- the cards
const CardInfo kCards[CARD_COUNT] = {
	// Upgrades
	{"Ring", "+20% XP from everything.", "", KIND_UPGRADE, RARE_COMMON, 5, 0, 0},
	{"Health", "+5 Max HP, and heals 5.", "", KIND_UPGRADE, RARE_COMMON, 5, 0, 0},
	{"Armor", "Every hit does 1 less.", "A hit still does at least 1.", KIND_UPGRADE, RARE_COMMON, 4, 0, 0},
	{"Blade", "A boss's guard stays down longer.", "Coconuts need one slice less.", KIND_UPGRADE, RARE_COMMON, 5, 0, 0},
	{"Ring of Regrowth", "You heal 20% faster.", "", KIND_UPGRADE, RARE_COMMON, 5, 0, 0},
	{"Long Edge", "A wider slice, so fruit", "are easier to catch.", KIND_UPGRADE, RARE_COMMON, 5, 0, 0},
	{"Steady Hands", "Fruit fall 5% slower.", "", KIND_UPGRADE, RARE_COMMON, 3, 0, 0},
	{"Lucky Coin", "+25% coins from this run.", "", KIND_UPGRADE, RARE_COMMON, 5, 0, 0},
	{"Parry", "+10% chance a Deflected!", "costs you no HP.", KIND_UPGRADE, RARE_RARE, 5, 0, 0},
	// Passive charms
	{"Juice Formula", "Each fruit heals 0.25 HP,", "on top of your slow healing.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Power-up Magnet", "Arcade's power-ups show up", "more often.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Bomb Squad", "The first bomb you slice in", "each room does no damage.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Second Wind", "Once a run, when you'd go down,", "come back with half your HP.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Combo Battery", "Combos of 5 or more", "give double XP.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Golden Touch", "Gold fruit come 3 times", "as often.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Seed Pouch", "Seeds and loose pieces give", "XP like whole fruit.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Sharp Rind", "In a boss fight, a bomb you slice", "knocks its guard down too.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Echo Blade", "Each swipe cuts again, faintly,", "half a second later.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Clean Sweep", "Get through a room without", "dropping a fruit: heal 3 HP.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Lucky Leaf", "+2 Rerolls on the", "level-up screen.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Overflow", "XP past a level up carries", "over twice as much.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Ripe Rhythm", "Slices on the music's beat", "give +50% XP.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Thick Peel", "A shield that blocks one hit,", "fresh at the start of every room.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Glass Cannon", "Double XP, boss guards stay down", "twice as long. Max HP halved.", KIND_PASSIVE, RARE_LEGEND, 1, 0, 0},
	// Reusable charms
	{"Blender", "For 10 s, slices every fruit", "that comes up. Bombs are left.", KIND_REUSABLE, RARE_RARE, 1, 10, 30},
	{"Freeze Pop", "For 5 s, everything freezes", "in midair, boss attacks too.", KIND_REUSABLE, RARE_COMMON, 1, 5, 40},
	{"Defuser", "Every bomb on screen", "fizzles out.", KIND_REUSABLE, RARE_COMMON, 1, 0, 45},
	{"Smoothie", "Heals 30% of your Max HP.", "", KIND_REUSABLE, RARE_COMMON, 1, 0, 60},
	{"Cleaver", "Your next slice on a boss goes", "straight through its guard.", KIND_REUSABLE, RARE_RARE, 1, 0, 90},
	{"Fruit Rain", "For 4 s, a burst of fruit", "with no bombs.", KIND_REUSABLE, RARE_COMMON, 1, 4, 35},
	{"Magnet Pull", "For 6 s, fruit, power-ups", "and coins drift to your blade.", KIND_REUSABLE, RARE_COMMON, 1, 6, 30},
	{"Slow-Mo", "For 6 s, everything moves", "at half speed.", KIND_REUSABLE, RARE_COMMON, 1, 6, 50},
	// Single-use charms
	{"Golden Apple", "Heals you to full HP.", "", KIND_SINGLE, RARE_COMMON, 1, 0, 0},
	{"Fruit Bomb", "Slices everything on screen.", "Bombs turn into XP.", KIND_SINGLE, RARE_RARE, 1, 0, 0},
	{"Rind Wall", "Blocks all damage", "for 8 seconds.", KIND_SINGLE, RARE_COMMON, 1, 0, 0},
	{"Ripe Seed", "Gain a level on the spot.", "", KIND_SINGLE, RARE_RARE, 1, 0, 0},
	{"Skip Peel", "Jump to the next room.", "Not in a boss room.", KIND_SINGLE, RARE_COMMON, 1, 0, 0},
	{"Boss Bane", "The boss's guard drops", "for 3 seconds.", KIND_SINGLE, RARE_RARE, 1, 0, 0},
	{"Lucky Pit", "Your next level up shows", "5 cards instead of 3.", KIND_SINGLE, RARE_COMMON, 1, 0, 0},
	{"Coin Jar", "Coins count twice for", "the rest of the room.", KIND_SINGLE, RARE_COMMON, 1, 0, 0},
	{"Last Stand", "For 10 s nothing takes you below", "1 HP, and Deflected! is free.", KIND_SINGLE, RARE_RARE, 1, 0, 0},
	{"Juice Flood", "For 5 s, the screen floods", "with fruit, at double XP.", KIND_SINGLE, RARE_RARE, 1, 0, 0},
};

// Each card's badge on the sprite sheet: drawn in CardId order, so the first one leads.
int cardIcon(int card) { return card >= 0 && card < CARD_COUNT ? sprites_cd_ring_idx + card : sprites_cd_ring_idx; }
static_assert(sprites_cd_flood_idx - sprites_cd_ring_idx == CARD_FLOOD, "a card icon for every card, in order");

const char* cardKindName(CardKind k) {
	switch (k) {
		case KIND_UPGRADE: return "Upgrade";
		case KIND_PASSIVE: return "Passive charm";
		case KIND_REUSABLE: return "Reusable charm";
		default: return "Single use";
	}
}

void cardNextLine(int card, int level, char* out, int size) {
	out[0] = 0;
	if (card < 0 || card >= CARD_COUNT || kCards[card].kind != KIND_UPGRADE) return;
	if (level >= kCards[card].maxLevel) {
		snprintf(out, size, "Maxed out.");
		return;
	}
	const int n = level + 1;
	switch (card) {
		case CARD_RING: snprintf(out, size, "Level %d: +%d%% XP.", n, n * 20); break;
		case CARD_HEALTH: snprintf(out, size, "Level %d: +%d Max HP in all.", n, n * 5); break;
		case CARD_ARMOR: snprintf(out, size, "Level %d: hits do %d less.", n, n); break;
		case CARD_BLADE: snprintf(out, size, "Level %d: guards stay down +%.2g s.", n, n * 0.25f); break;
		case CARD_REGROWTH: snprintf(out, size, "Level %d: heal %d%% faster.", n, n * 20); break;
		case CARD_LONGEDGE: snprintf(out, size, "Level %d: a %d%% wider slice.", n, n * 12); break;
		case CARD_STEADY: snprintf(out, size, "Level %d: fruit %d%% slower.", n, n * 5); break;
		case CARD_LUCKY: snprintf(out, size, "Level %d: +%d%% coins.", n, n * 25); break;
		case CARD_PARRY: snprintf(out, size, "Level %d: %d%% free Deflected!", n, n * 10); break;
		default: break;
	}
}

const BossInfo kBosses[BOSS_COUNT] = {
	{"The Big Melon", 3, 3},
	{"The Bomb King", 5, 3},
};

static const OrchardLevel kLevels[DIFF_COUNT] = {
	{25, 4, 0.75f, 2.5f, 1, 0.85f},
	{20, 5, 1.0f, 2.0f, 2, 1.0f},
	{20, 6, 1.25f, 1.5f, 2, 1.1f},
	{15, 8, 1.5f, 1.2f, 3, 1.2f},
};
const OrchardLevel& orchardLevel(int diff) { return kLevels[diff % DIFF_COUNT]; }
int orchardXpFor(int level) { return 6 + 4 * level; }

// What each room throws. Rooms 5 and 10 are boss rooms.
struct RoomPlan {
	float d;          // how hard the throws are, 0 to 1
	float bombs;      // chance a thrown thing is a bomb
	int extra;        // up to this many more than one a wave
	float interval;   // seconds between waves
	bool tricks, powers;
};
static const RoomPlan kRooms[kOrchardRooms + 1] = {
	{0, 0, 0, 2, false, false},  // unused: rooms count from 1
	{0.00f, 0.00f, 1, 2.3f, false, false},
	{0.15f, 0.10f, 2, 2.0f, false, false},
	{0.30f, 0.12f, 3, 1.8f, false, false},
	{0.45f, 0.16f, 3, 1.6f, false, true},
	{0.30f, 0.00f, 1, 2.6f, false, false},  // The Big Melon: a trickle of fruit for XP
	{0.20f, 0.13f, 2, 1.9f, true, true},
	{0.40f, 0.16f, 4, 1.6f, true, true},
	{0.55f, 0.20f, 4, 1.45f, true, true},
	{0.75f, 0.22f, 5, 1.25f, true, true},
	{0.50f, 0.00f, 1, 2.6f, true, false},   // The Bomb King throws its own bombs
};
static const RoomPlan& plan(int room) { return kRooms[room < 1 ? 1 : (room > kOrchardRooms ? kOrchardRooms : room)]; }

static const ObjKind kOrchardPowers[] = {OBJ_FREEZE, OBJ_DOUBLE, OBJ_FRENZY, OBJ_MAGNET, OBJ_WIDE, OBJ_SHIELD};

// ---------------------------------------------------------------- the run
void Game::orcStart() {
	const OrchardLevel& lv = orchardLevel(gSave.difficulty);
	orc.room = 1;
	orc.maxHp = orc.hp = (float)lv.startHp;
	orc.level = 1;
	orc.rerolls = 1;
	orc.slot[0] = orc.slot[1] = CARD_NONE;
	maxStrikes = 0;  // HP, not misses
	setCallout("ROOM 1", rgba(255, 230, 140));
#ifdef TEST_ORCHARD
	// Screenshots: a level up straight away, a charm in each slot, and the first boss soon after.
	orc.level = 2;
	orc.levelsWaiting = 1;
	orc.slot[0] = CARD_BLENDER;
	orc.slot[1] = CARD_GOLDAPPLE;
	orc.room = 4;
	orc.roomT = 9;
	orc.upgrade[CARD_ARMOR] = 2;
#ifdef TEST_ORCHARD_ROOM
	orc.room = TEST_ORCHARD_ROOM;  // a later boss, with HP to see the whole fight
	orc.maxHp = orc.hp = 400;
#endif
#endif
}

float Game::orcDifficulty() const { return plan(orc.room).d; }

float Game::orcObjScale() const {
	float k = 1.0f - 0.05f * orc.upgrade[CARD_STEADY];
	if (orc.slowmoT > 0) k *= 0.5f;
	if (orc.stillT > 0) k = 0;
	return k;
}

float Game::orcReach() const { return 1.0f + 0.12f * orc.upgrade[CARD_LONGEDGE]; }

int Game::orcXpNeed() const { return (int)(orchardXpFor(orc.level) * orchardLevel(gSave.difficulty).xpNeed + 0.5f); }

bool Game::orcWave() {
	const RoomPlan& p = plan(orc.room);
	gravity = lerpf(300, 430, p.d) * kFieldK * levelGravity(gSave.difficulty) * (mix[MIX_LOW_GRAVITY] ? 0.6f : 1.0f);
	if (orc.bossRoom) {
		// Boss rooms throw a little fruit to keep the XP coming; the boss is the danger.
		const int n = 1 + gSpawn.below(2) + (orc.boss.on && orc.boss.id == BOSS_MELON && orc.boss.phase == 3 ? 1 : 0);
		for (int i = 0; i < n; i++) {
			float x = 160, vx = 0, vy = -300;
			pickThrow(false, 0.15f * i, x, vx, vy);
			queue(0.15f * i, x, vx, vy, OBJ_FRUIT, normalFruitOf(gSpawn));
		}
		waveTimer = p.interval;
		maybeCoin(p.d);
		return true;
	}
	const float bombSetting = gSave.bombs / 50.0f;
	const float bombChance = clampf(p.bombs * levelBombs(gSave.difficulty) * bombSetting * (mix[MIX_DOUBLE_BOMBS] ? 2.0f : 1.0f), 0, 0.6f);
	const float powerChance = p.powers ? (orc.upgrade[CARD_MAGNET] ? 0.12f : 0.04f) : 0.0f;
	const float goldChance = 0.035f * (orc.upgrade[CARD_GOLDTOUCH] ? 3 : 1);
	int count = 1 + gSpawn.below(p.extra + 1);
	if (count > 8) count = 8;
	const int bombsAllowed = (gSave.difficulty >= DIFF_HARD ? 2 : 1) * (mix[MIX_DOUBLE_BOMBS] ? 2 : 1);
	const float stagger = gSpawn.below(3) == 0 ? lerpf(0.22f, 0.11f, p.d) : 0.0f;
	ObjKind kinds[8];
	int bombsUsed = 0;
	for (int i = 0; i < count; i++) {
		ObjKind kind = OBJ_FRUIT;
		if (gSpawn.unit() < bombChance && bombsUsed < bombsAllowed) {
			kind = OBJ_BOMB;
			bombsUsed++;
		} else if (gSpawn.unit() < powerChance) {
			kind = kOrchardPowers[gSpawn.below((int)(sizeof kOrchardPowers / sizeof kOrchardPowers[0]))];
		} else if (gSpawn.unit() < goldChance) {
			kind = OBJ_GOLD;
		}
		kinds[i] = kind;
	}
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < count; i++) {
			const bool isBomb = kinds[i] == OBJ_BOMB;
			if ((pass == 0) != isBomb) continue;
			float x = 160, vx = 0, vy = -300;
			pickThrow(isBomb, stagger * i, x, vx, vy);
			int fruit = gSpawn.unit() < 0.08f ? kCoconut : normalFruitOf(gSpawn);
			if (kinds[i] == OBJ_FRUIT && p.tricks && gSpawn.unit() < 0.12f) {
				fruit = trickFruitOf(MODE_ORCHARD, gSpawn, pinataThrown < kPinataMax);
				if (fruit == kPinata) pinataThrown++;
			}
			queue(stagger * i, x, vx, vy, kinds[i], fruit);
		}
	}
	waveTimer = p.interval * levelInterval(gSave.difficulty) + stagger * count;
	maybeCoin(p.d);
	return true;
}

void Game::orcGainXp(float amount) {
	if (amount <= 0 || mode == MODE_BOSSRUSH) return;  // Boss Rush has no level ups
	amount *= 1.0f + 0.2f * orc.upgrade[CARD_RING];
	if (orc.upgrade[CARD_GLASS]) amount *= 2;
	if (orc.floodT > 0) amount *= 2;
	orc.xp += amount;
	orc.xpPulse = 1;
	for (int guard = 0; guard < 8; guard++) {
		const float need = (float)orcXpNeed();
		if (orc.xp < need) break;
		orc.xp -= need;
		if (orc.upgrade[CARD_OVERFLOW]) orc.xp *= 2;
		orc.level++;
		orc.levelsWaiting++;
	}
}

void Game::orcSliced(const Obj& o) {
	float xp = 0;
	switch (o.kind) {
		case OBJ_FRUIT:
			if (o.loose) xp = orc.upgrade[CARD_SEEDPOUCH] ? 3 : 1;
			else xp = o.fruit >= kSpiky ? 3 : 1;
			if (orc.upgrade[CARD_JUICE]) orc.hp = fminf(orc.maxHp, orc.hp + 0.25f);
			break;
		case OBJ_GOLD:
			xp = 3;
			if (orc.upgrade[CARD_JUICE]) orc.hp = fminf(orc.maxHp, orc.hp + 0.25f);
			break;
		case OBJ_SEED: xp = orc.upgrade[CARD_SEEDPOUCH] ? 1 : 0; break;
		case OBJ_COIN:
			if (orc.coinJar) coinsCaught++;  // counts twice
			break;
		default: break;
	}
	if (xp > 0 && orc.upgrade[CARD_RHYTHM] && orcOnBeat()) xp *= 1.5f;
	orcGainXp(xp);
}

void Game::orcCombo(int n) {
	if (n < 3) return;
	float xp = (float)(n - 2);
	if (orc.upgrade[CARD_BATTERY] && n >= 5) xp *= 2;
	orcGainXp(xp);
}

bool Game::orcOnBeat() const {
	const int track = audio::currentTrack();
	const float t = audio::musicTime();
	if (track < 0 || t < 0) return false;
	const float beat = audio::trackBeat(track);
	if (beat <= 0.05f) return false;
	float ph = fmodf(t - audio::trackFirst(track) - beatLatencySec(), beat);
	if (ph < 0) ph += beat;
	return fminf(ph, beat - ph) < 0.09f;
}

void Game::orcHurt(int damage, float x, float y, const char* what) {
	if (phase != PHASE_PLAY || damage <= 0) return;
	char buf[32];
	if (orc.invulnT > 0) {
		addPopup("BLOCKED", x, y - 18, rgba(200, 220, 255), 0.7f);
		return;
	}
	if (orc.peelShield) {
		orc.peelShield = false;
		audio::play(SFX_KNOCK, 0.9f, fieldPan(x), 0.7f);
		addRing(x, y, rgba(190, 230, 140), 2.2f, 0.3f);
		addPopup("PEEL BLOCKED", x, y - 18, rgba(200, 240, 160), 0.65f);
		return;
	}
	int d = damage - orc.upgrade[CARD_ARMOR];
	if (d < 1) d = 1;
	float hp = orc.hp - d;
	if (orc.lastStandT > 0 && hp < 1) hp = 1;
	orc.hp = hp;
	orc.damageTaken += d;
	orc.hurtFlash = 1;
	strikePulse = 1;
	audio::play(SFX_STRIKE, 0.9f, fieldPan(x));
	PORT_VIBRATE(0.5f, 120);
	snprintf(buf, sizeof buf, "%s -%d", what, d);
	addPopup(buf, x, y - 18, rgba(255, 110, 100), 0.7f);
	showCaption("Hit! HP lost.");
	if (orc.hp > 0) return;
	if (orc.upgrade[CARD_SECONDWIND] && !orc.usedSecondWind) {
		orc.usedSecondWind = true;
		orc.hp = orc.maxHp * 0.5f;
		setCallout("SECOND WIND!", rgba(160, 240, 170));
		audio::play(SFX_FEVER, 0.9f);
		addRing(kFieldW * 0.5f, kFieldH * 0.5f, rgba(160, 240, 170), 5, 0.6f);
		return;
	}
	orc.hp = 0;
	orcFinish(false);
}

void Game::orcBomb(Obj& o) {
	if (orc.upgrade[CARD_SHARPRIND] && orc.boss.on && orc.boss.dazeT <= 0 && !orc.boss.beaten) bossOpen(1.5f);
	if (orc.upgrade[CARD_BOMBSQUAD] && !orc.bombSquadUsed) {
		orc.bombSquadUsed = true;
		addPopup("BOMB SQUAD!", o.x, o.y - 18, rgba(200, 220, 255), 0.7f);
		setCallout("BOOM! NO DAMAGE", rgba(200, 220, 255));
		return;
	}
	setCallout("BOOM!", rgba(255, 110, 90));
	orcHurt(orchardLevel(gSave.difficulty).bombDamage, o.x, o.y, "BOMB");
}

void Game::orcDropped(const Obj& o) {
	(void)o;
	orc.droppedThisRoom = true;
}

void Game::orcFinish(bool won) {
	orc.won = won;
	if (won) setCallout(mode == MODE_BOSSRUSH ? "RUSH CLEARED!" : "ORCHARD CLEARED!", rgba(255, 230, 140), 0.86f);
	else setCallout("OUT OF HP!", rgba(255, 120, 100), 0.8f);
	for (auto& s : orc.shots) s.on = false;
	endGame();
}

void Game::orcNextRoom() {
	if (mode == MODE_BOSSRUSH) {
		rushNext();  // no rooms: the next boss
		return;
	}
	if (orc.upgrade[CARD_SWEEP] && !orc.droppedThisRoom && !orc.bossRoom) {
		orc.hp = fminf(orc.maxHp, orc.hp + 3);
		addPopup("CLEAN SWEEP +3 HP", kFieldW * 0.5f, kFieldTop + 70, rgba(160, 240, 170), 0.6f);
	}
	award(25 * orc.room);
	if (orc.room >= kOrchardRooms) {
		orcFinish(true);
		return;
	}
	orc.room++;
	orc.roomT = 0;
	orc.bombSquadUsed = false;
	orc.droppedThisRoom = false;
	orc.coinJar = false;
	orc.peelShield = orc.upgrade[CARD_THICKPEEL] != 0;
	orc.bossRoom = orc.room % 5 == 0;
	char buf[32];
	if (orc.bossRoom) {
		bossStart(orc.room == 5 ? BOSS_MELON : BOSS_KING);
	} else {
		snprintf(buf, sizeof buf, orc.room == 6 ? "ROOM 6: TRICK FRUIT!" : "ROOM %d", orc.room);
		setCallout(buf, rgba(255, 230, 140));
		audio::play(SFX_GO, 0.8f);
	}
}

// ---------------------------------------------------------------- level ups
void Game::orcOffer() {
	finishCombo();
	int pool[CARD_COUNT], weight[CARD_COUNT], n = 0, total = 0;
	const bool afterBoss = orc.bossesBeaten > 0 && (orc.room == 6 || orc.room == 11);
	for (int c = 0; c < CARD_COUNT; c++) {
		const CardInfo& info = kCards[c];
		if (orc.upgrade[c] >= info.maxLevel) continue;
		if (info.kind == KIND_REUSABLE || info.kind == KIND_SINGLE) {
			if (orc.slot[0] == c || orc.slot[1] == c) continue;  // already in a slot
		}
		int w = info.rarity == RARE_LEGEND ? 5 : (info.rarity == RARE_RARE ? 25 : 70);
		if (info.kind == KIND_SINGLE && afterBoss) w = w * 8 / 5;
		pool[n] = c;
		weight[n] = w;
		total += w;
		n++;
	}
	orc.offerCount = orc.luckyPit ? 5 : 3;
	orc.luckyPit = false;
	if (orc.offerCount > n) orc.offerCount = n;
	for (int i = 0; i < orc.offerCount; i++) {
		int r = gRng.below(total > 0 ? total : 1), k = 0;
		while (k < n - 1 && r >= weight[k]) r -= weight[k++];
		orc.offer[i] = (u8)pool[k];
		total -= weight[k];
		pool[k] = pool[n - 1];  // drawn without putting back
		weight[k] = weight[n - 1];
		n--;
	}
	orc.choosing = true;
	orc.placing = false;
	orc.focus = 0;
	audio::play(SFX_BEST, 0.8f);
	showCaption("Level up! Pick a card.");
}

void Game::orcReroll() {
	if (!orcCanReroll()) return;
	orc.rerolls--;
	orc.luckyPit = orc.offerCount > 3;  // a Lucky Pit's five stay five
	orcOffer();
}

void Game::orcClose() {
	orc.choosing = false;
	orc.placing = false;
	if (orc.levelsWaiting > 0) orc.levelsWaiting--;
	if (orc.levelsWaiting > 0) orcOffer();
	else orc.resumeT = 3.0f;
}

void Game::orcApply(int card) {
	switch (card) {
		case CARD_HEALTH:
			orc.maxHp += 5;
			orc.hp = fminf(orc.maxHp, orc.hp + 5);
			break;
		case CARD_GLASS:
			orc.maxHp = fmaxf(1, floorf(orc.maxHp * 0.5f));
			if (orc.hp > orc.maxHp) orc.hp = orc.maxHp;
			break;
		case CARD_LUCKYLEAF: orc.rerolls += 2; break;
		case CARD_THICKPEEL: orc.peelShield = true; break;
		default: break;
	}
}

void Game::orcPick(int i) {
	if (!orc.choosing || orc.placing || i < 0 || i >= orc.offerCount) return;
	const int card = orc.offer[i];
	const CardInfo& info = kCards[card];
	if (info.kind == KIND_REUSABLE || info.kind == KIND_SINGLE) {
		// A slotted charm: it goes straight into an empty slot, or the player picks one to replace.
		for (int s = 0; s < kSlots; s++) {
			if (orc.slot[s] == CARD_NONE) {
				orc.slot[s] = (u8)card;
				orc.slotCool[s] = orc.slotOn[s] = 0;
				audio::play(SFX_POWER, 0.8f);
				orcClose();
				return;
			}
		}
		orc.placing = true;
		orc.placingCard = (u8)card;
		return;
	}
	if (orc.upgrade[card] < 255) orc.upgrade[card]++;
	if (info.kind == KIND_PASSIVE && orc.passiveCount < (int)sizeof orc.passiveOrder) orc.passiveOrder[orc.passiveCount++] = (u8)card;
	orcApply(card);
	audio::play(SFX_POWER, 0.8f);
	orcClose();
}

void Game::orcPlace(int s) {
	if (!orc.placing) return;
	if (s >= 0 && s < kSlots) {
		orc.slot[s] = orc.placingCard;
		orc.slotCool[s] = orc.slotOn[s] = 0;
		audio::play(SFX_POWER, 0.8f);
	} else {
		audio::click();
	}
	orcClose();
}

bool Game::orcSlotReady(int s) const {
	return s >= 0 && s < kSlots && orc.slot[s] != CARD_NONE && orc.slotCool[s] <= 0;
}

void Game::orcSwapSlots() {
	u8 c = orc.slot[0];
	orc.slot[0] = orc.slot[1];
	orc.slot[1] = c;
	float t = orc.slotCool[0];
	orc.slotCool[0] = orc.slotCool[1];
	orc.slotCool[1] = t;
	t = orc.slotOn[0];
	orc.slotOn[0] = orc.slotOn[1];
	orc.slotOn[1] = t;
}

void Game::orcDrop(int s) {
	if (s < 0 || s >= kSlots) return;
	orc.slot[s] = CARD_NONE;
	orc.slotCool[s] = orc.slotOn[s] = 0;
}

// ---------------------------------------------------------------- charms in play
void Game::orcUse(int s) {
	if (!orchardLike() || phase != PHASE_PLAY || orchardHeld() || s < 0 || s >= kSlots) return;
	const int card = orc.slot[s];
	if (card == CARD_NONE) return;
	if (orc.slotCool[s] > 0) {
		audio::play(SFX_KNOCK, 0.5f, 0, 1.4f);
		showCaption("That charm is still cooling down.");
		return;
	}
	const CardInfo& info = kCards[card];
	const bool fighting = orc.boss.on && !orc.boss.beaten;
	switch (card) {
		case CARD_BLENDER: orc.blenderT = info.lasts; orc.blenderCut = 0; break;
		case CARD_FREEZEPOP: orc.stillT = info.lasts; audio::play(SFX_FREEZE, 1.0f); break;
		case CARD_DEFUSER:
			for (auto& p : pending)
				if (p.on && p.kind == OBJ_BOMB) p.on = false;
			for (auto& o : objs) {
				if (o.on && o.kind == OBJ_BOMB) {
					emitSparks(o.x, o.y, rgba(255, 190, 110), juiceAmount(8), 110);
					o.on = false;
				}
			}
			audio::fuse(false);
			break;
		case CARD_SMOOTHIE: orc.hp = fminf(orc.maxHp, orc.hp + orc.maxHp * 0.3f); break;
		case CARD_CLEAVER: orc.cleaver = true; break;
		case CARD_FRUITRAIN: frenzyT = info.lasts; frenzyTimer = 0; break;
		case CARD_PULL: magnetT = info.lasts; break;
		case CARD_SLOWMO: orc.slowmoT = info.lasts; break;
		case CARD_GOLDAPPLE: orc.hp = orc.maxHp; break;
		case CARD_FRUITBOMB: {
			Blade cut;
			for (auto& o : objs) {
				if (!o.on) continue;
				if (o.kind == OBJ_BOMB) {
					emitSparks(o.x, o.y, rgba(255, 220, 120), juiceAmount(10), 140);
					o.on = false;
					orcGainXp(2);
					continue;
				}
				if (o.kind == OBJ_FRUIT && o.fruit == kSpiky) continue;
				cut.px = o.x - 20;
				cut.x = o.x + 20;
				cut.py = cut.y = o.y;
				cut.active = true;
				cut.speed = 30;
				cut.strokeId = 0xF8B00000u;
				o.hits = 1;
				sliceObj(o, cut);
			}
			for (auto& p : pending)
				if (p.on && p.kind == OBJ_BOMB) p.on = false;
			audio::fuse(false);
			if (fighting) bossOpen(2.0f);
			flash = 0.4f;
			flashColor = rgba(255, 230, 150);
			break;
		}
		case CARD_RINDWALL: orc.invulnT = 8; break;
		case CARD_RIPESEED:
			orc.level++;
			orc.levelsWaiting++;
			break;
		case CARD_SKIPPEEL:
			if (orc.bossRoom) {
				showCaption("Skip Peel can't skip a boss.");
				audio::play(SFX_KNOCK, 0.5f, 0, 1.4f);
				return;
			}
			orc.roomT = kRoomSeconds;
			break;
		case CARD_BOSSBANE:
			if (!fighting || orc.boss.dazeT > 0) {
				showCaption("Boss Bane needs a boss.");
				audio::play(SFX_KNOCK, 0.5f, 0, 1.4f);
				return;
			}
			bossOpen(3.0f);
			break;
		case CARD_LUCKYPIT: orc.luckyPit = true; break;
		case CARD_COINJAR: orc.coinJar = true; break;
		case CARD_LASTSTAND: orc.lastStandT = 10; break;
		case CARD_FLOOD:
			orc.floodT = 5;
			feverT = 5;
			feverTimer = 0;
			break;
		default: break;
	}
	orc.charmsUsed++;
	char buf[32];
	snprintf(buf, sizeof buf, "%s!", info.name);
	for (char* c = buf; *c; c++)
		if (*c >= 'a' && *c <= 'z') *c -= 32;
	setCallout(buf, rgba(190, 240, 255));
	audio::play(info.kind == KIND_SINGLE ? SFX_POP : SFX_POWER, 1.0f);
	addRing(kFieldW * 0.5f, kFieldH * 0.6f, rgba(190, 240, 255), 3, 0.4f);
	if (info.kind == KIND_SINGLE) {
		orc.slot[s] = CARD_NONE;
		orc.slotOn[s] = 0;
		orc.slotCool[s] = 0;
	} else {
		orc.slotOn[s] = info.lasts;
		orc.slotCool[s] = info.cooldown;
	}
}

// The Blender takes one fruit at a time as it comes up, never a bomb or a spiky fruit.
void Game::orcBlender() {
	orc.blenderCut -= kDt;
	if (orc.blenderCut > 0) return;
	for (auto& o : objs) {
		if (!o.on || o.age < 0.25f || o.y > kFieldH - 16 || o.y < kFieldTop) continue;
		if (o.kind != OBJ_FRUIT && o.kind != OBJ_GOLD && o.kind != OBJ_SEED) continue;
		if (o.kind == OBJ_FRUIT && o.fruit == kSpiky) continue;
		Blade cut;
		cut.px = o.x - 18;
		cut.x = o.x + 18;
		cut.py = o.y - 10;
		cut.y = o.y + 10;
		cut.active = true;
		cut.speed = 30;
		cut.strokeId = 0xB1E00000u;
		o.hits = 1;
		sliceObj(o, cut);
		orc.blenderCut = 0.12f;
		return;
	}
}

// Echo Blade: the blade's path, cut again half a second later. It only cuts fruit.
void Game::orcEcho(const Blade& blade) {
	const int h = orc.echoHead;
	orc.echoX[h] = blade.x;
	orc.echoY[h] = blade.y;
	orc.echoOn[h] = blade.active;
	orc.echoHead = (h + 1) % kEchoPoints;
	if (!orc.upgrade[CARD_ECHO]) return;
	const int b = (h - 30 + kEchoPoints) % kEchoPoints, a = (b - 1 + kEchoPoints) % kEchoPoints;
	if (!orc.echoOn[a] || !orc.echoOn[b]) return;
	Blade ghost;
	ghost.px = orc.echoX[a];
	ghost.py = orc.echoY[a];
	ghost.x = orc.echoX[b];
	ghost.y = orc.echoY[b];
	ghost.active = true;
	ghost.speed = 20;
	ghost.strokeId = 0xEC000000u;
	for (auto& o : objs) {
		if (!o.on || o.hitCooldown > 0 || (o.kind != OBJ_FRUIT && o.kind != OBJ_GOLD && o.kind != OBJ_SEED)) continue;
		if (o.kind == OBJ_FRUIT && o.fruit == kSpiky) continue;
		if (hitTest(o, ghost, o.radius * 0.85f)) sliceObj(o, ghost);
	}
}

void Game::orcUpdate(Blade& blade) {
	const float dt = kDt;
	if (orc.hurtFlash > 0) orc.hurtFlash -= dt * 2.5f;
	if (orc.xpPulse > 0) orc.xpPulse -= dt * 3;
	if (orc.invulnT > 0) orc.invulnT -= dt;
	if (orc.lastStandT > 0) orc.lastStandT -= dt;
	if (orc.slowmoT > 0) orc.slowmoT -= dt;
	if (orc.stillT > 0) orc.stillT -= dt;
	if (orc.floodT > 0) orc.floodT -= dt;
	for (int s = 0; s < kSlots; s++) {
		if (orc.slotOn[s] > 0) orc.slotOn[s] -= dt;
		if (orc.slotCool[s] > 0) orc.slotCool[s] -= dt;
	}
	if (orc.blenderT > 0) {
		orc.blenderT -= dt;
		orcBlender();
	}
	orcEcho(blade);
	// Slow healing, faster with Ring of Regrowth, but never in a boss fight.
	if (!orc.bossRoom && orc.hp < orc.maxHp) orc.hp = fminf(orc.maxHp, orc.hp + dt * 0.2f * (1 + 0.2f * orc.upgrade[CARD_REGROWTH]));
	if (orc.bossRoom) {
		orc.fightT += dt;
		bossUpdate(dt * (orc.stillT > 0 ? 0.0f : (orc.slowmoT > 0 ? 0.5f : 1.0f)));
	} else {
		orc.roomT += dt * gameSpeedScale();
		if (orc.roomT >= kRoomSeconds) orcNextRoom();
	}
	// A level up waits for the slicing in hand to finish, then opens the cards.
	if (orc.levelsWaiting > 0 && !orc.choosing && phase == PHASE_PLAY && !blade.active) orcOffer();
}

float Game::orcStat(int card) const {
	switch (card) {
		case CARD_RING: return 100 + 20.0f * orc.upgrade[CARD_RING] * (orc.upgrade[CARD_GLASS] ? 2 : 1);
		case CARD_HEALTH: return orc.maxHp;
		case CARD_ARMOR: return orc.upgrade[CARD_ARMOR];
		case CARD_BLADE: return orchardLevel(gSave.difficulty).openTime * (orc.upgrade[CARD_GLASS] ? 2 : 1) + 0.25f * orc.upgrade[CARD_BLADE];
		case CARD_REGROWTH: return 0.2f * (1 + 0.2f * orc.upgrade[CARD_REGROWTH]);
		case CARD_LONGEDGE: return orcReach() * 100;
		case CARD_STEADY: return 100 - 5.0f * orc.upgrade[CARD_STEADY];
		case CARD_LUCKY: return 100 + 25.0f * orc.upgrade[CARD_LUCKY];
		case CARD_PARRY: return 10.0f * orc.upgrade[CARD_PARRY];
		default: return 0;
	}
}

// ---------------------------------------------------------------- on screen
void Game::orcSlotRect(int slot, float* x, float* y, float* w, float* h) {
	const float s = PORT_3DS ? 40.0f : 46.0f * uiScale();
	*w = *h = s;
	*y = kFieldH - s - 8;
	*x = slot == 0 ? 8.0f : kFieldW - s - 8;
}

void Game::drawOrchardField() const {
	drawBoss(true);
	drawShots();
}

void Game::drawOrchardHud(float x, float y, float w, float u) const {
	char buf[40];
	const Boss& b = orc.boss;
	// The room, or the boss and the slices it still needs.
	if (b.on && !b.beaten) {
		snprintf(buf, sizeof buf, "%s", b.id == BOSS_MELON ? (b.royal ? "ROYAL MELON" : "BIG MELON") : (b.royal ? "ROYAL KING" : "BOMB KING"));
		gfx::text(buf, x, y + 5 * u, 0.3f * u, rgba(255, 200, 140), ALIGN_RIGHT);
		const int all = b.total;
		for (int i = 0; i < all; i++) {
			const bool cut = i < all - b.left;
			// One mark per slice it needs: bright while still to do, dim once landed.
			const float px = x - 10 * u - (all - 1 - i) * 13 * u;
			gfx::rect(px, y + 17 * u, 9 * u, 7 * u, cut ? rgba(90, 70, 60, 200) : rgba(255, 110, 90));
		}
	} else {
		if (mode == MODE_BOSSRUSH) snprintf(buf, sizeof buf, "BOSS %d/%d", m12.rushStage + 1 > kRushStages ? kRushStages : m12.rushStage + 1, kRushStages);
		else snprintf(buf, sizeof buf, "ROOM %d/%d", orc.room > kOrchardRooms ? kOrchardRooms : orc.room, kOrchardRooms);
		gfx::text(buf, x, y + 5 * u, 0.3f * u, COL_CREAM, ALIGN_RIGHT);
		if (!orc.bossRoom) {
			const float k = clampf(1 - orc.roomT / kRoomSeconds, 0, 1);
			const float rw = 56 * u;
			gfx::rect(x - rw, y + 18 * u, rw, 4 * u, rgba(0, 0, 0, 110));
			gfx::rect(x - rw, y + 18 * u, rw * k, 4 * u, COL_DIM);
		}
	}
	const float roomW = 72 * u;
	float bw = w - roomW - 8 * u;
	if (bw > 130 * u) bw = 130 * u;
	if (bw < 50 * u) bw = 50 * u;
	const float bx = x - roomW - bw, bh = 9 * u;
	// HP: red, flashing when hit; blue while a Rind Wall or Last Stand holds.
	const float hpK = clampf(orc.hp / (orc.maxHp > 0 ? orc.maxHp : 1), 0, 1);
	const bool guarded = orc.invulnT > 0 || orc.lastStandT > 0;
	u32 hpCol = guarded ? rgba(130, 200, 255) : (hpK < 0.3f ? rgba(255, 90, 80) : rgba(240, 70, 80));
	if (orc.hurtFlash > 0 && !gSave.calmFx) hpCol = rgba(255, 230, 230);
	gfx::rect(bx - 1, y + 4 * u - 1, bw + 2, bh + 2, gfx::themeBar());
	gfx::rect(bx, y + 4 * u, bw * hpK, bh, hpCol);
	if (orc.peelShield) gfx::rect(bx, y + 4 * u, 3 * u, bh, rgba(190, 240, 150));
	snprintf(buf, sizeof buf, "HP %d/%d", (int)ceilf(orc.hp), (int)orc.maxHp);
	gfx::text(buf, bx + 4 * u, y + 3 * u, 0.26f * u, COL_CREAM);
	// XP and the level. Boss Rush has neither.
	if (mode == MODE_BOSSRUSH) return;
	const float xpK = clampf(orc.xp / (float)orcXpNeed(), 0, 1);
	const float xy = y + 16 * u;
	gfx::rect(bx - 1, xy - 1, bw + 2, 6 * u + 2, gfx::themeBar());
	gfx::rect(bx, xy, bw * xpK, 6 * u, orc.xpPulse > 0 ? rgba(200, 240, 255) : rgba(110, 190, 255));
	snprintf(buf, sizeof buf, "LV %d", orc.level);
	gfx::text(buf, bx - 6 * u, xy - 4 * u, 0.26f * u, rgba(170, 220, 255), ALIGN_RIGHT);
}

void Game::drawOrchardOverlay() const {
	char buf[24];
	for (int s = 0; s < kSlots; s++) {
		float x, y, w, h;
		orcSlotRect(s, &x, &y, &w, &h);
		const int card = orc.slot[s];
		const bool ready = orcSlotReady(s);
		gfx::rect(x, y, w, h, rgba(20, 14, 30, card == CARD_NONE ? 90 : 170));
		gfx::rect(x, y, w, 2, ready ? rgba(150, 230, 255) : rgba(90, 90, 110));
		if (card != CARD_NONE) {
			const CardInfo& info = kCards[card];
			const float is = (w - 10) / 32;
			gfx::sprite(SHEET_SPRITES, cardIcon(card), x + w * 0.5f, y + h * 0.5f + 2, is, is, 0, ready ? WHITE : rgba(150, 150, 160, 200));
			if (orc.slotCool[s] > 0 && info.cooldown > 0) {
				const float k = clampf(orc.slotCool[s] / info.cooldown, 0, 1);
				gfx::rect(x, y + h * (1 - k), w, h * k, rgba(0, 0, 0, 120));
				snprintf(buf, sizeof buf, "%d", (int)ceilf(orc.slotCool[s]));
				gfx::text(buf, x + w * 0.5f, y + h - 14, 0.26f, COL_DIM, ALIGN_CENTER);
			} else if (orc.slotOn[s] > 0) {
				gfx::rect(x, y + h - 3, w * clampf(orc.slotOn[s] / (info.lasts > 0 ? info.lasts : 1), 0, 1), 3, rgba(150, 230, 255));
			}
		}
		const char* key = PORT_3DS ? (s == 0 ? "L" : "R") : (gStickInUse ? (s == 0 ? "LB" : "RB") : (s == 0 ? "Q" : "E"));
		gfx::text(key, x + 3, y + 2, 0.22f, COL_DIM);
	}
	if (!orc.choosing && orc.resumeT > 0 && !gHudPaused) {
		snprintf(buf, sizeof buf, "%d", (int)ceilf(orc.resumeT));
		const float f = orc.resumeT - floorf(orc.resumeT);
		gfx::text(buf, kFieldW * 0.5f, kFieldH * 0.4f - 20, 1.4f + f * 0.4f, rgba(255, 230, 140), ALIGN_CENTER);
	}
}

#ifdef AUTOTEST
// The Lab's checks of Orchard Blast: HP, XP, cards, charms, rooms and the bosses.
void Game::labOrchard(void (*check)(bool, const char*, const char*)) {
	char d[128];
	const u8 diffWas = gSave.difficulty;
	const u32 beatenWas = gSave.bossesBeaten;
	u16 winsWas[sizeof gSave.bossWins / sizeof gSave.bossWins[0]];
	memcpy(winsWas, gSave.bossWins, sizeof winsWas);
	gSave.difficulty = DIFF_NORMAL;
	const auto setup = [this]() {
		start(MODE_ORCHARD);
		phase = PHASE_PLAY;
		phaseT = 0;
		waveTimer = 999;
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

	// The six boss and charm sounds made for 1.2.
	if (audio::available()) {
		const Sfx made[] = {SFX_CLANG, SFX_OPEN, SFX_ROAR, SFX_BOSSIN, SFX_VICTORY, SFX_POP, SFX_LAUGH, SFX_CHUCKLE, SFX_WHOOSH, SFX_LAUNCH};
		int missing = 0;
		for (Sfx s : made) missing += !audio::loaded(s);
		snprintf(d, sizeof d, "%d missing", missing);
		check(missing == 0, "Orchard Blast's own sounds load: Deflected!, Open!, roar, intro, victory, pop, laugh, chuckle, whoosh, launch", d);

		// Boss music: the room's track hands over to a boss track, then comes back.
		const u8 musicWas = gSave.bossMusic, lastWas = gSave.bossTrackLast[BOSS_MELON];
		gSave.bossMusic = 1;
		audio::music(MUSIC_PLAY);
		for (int i = 0; i < 90; i++) audio::update(kDt);
		const int room = audio::currentTrack();
		const int fight = bossTrackPick(BOSS_MELON, 7);
		audio::bossMusic(fight);
		for (int i = 0; i < 60; i++) audio::update(kDt);
		const bool fading = audio::bossMusicOn() && audio::currentTrack() == room;
		for (int i = 0; i < 60; i++) audio::update(kDt);
		snprintf(d, sizeof d, "room %d, fight %d, now %d", room, fight, audio::currentTrack());
		check(fading && fight >= 0 && trackBoss(fight) == BOSS_MELON && audio::currentTrack() == fight,
		      "A boss fight fades the room's music out, then plays one of that boss's tracks", d);
		snprintf(d, sizeof d, "%d tracks", bossTrackCount(BOSS_MELON));
		check(bossTrackPick(BOSS_MELON, 7) != fight, "The next fight against the same boss picks a different track", d);
		int bossTracks = 0, filesThere = 0;
		for (int t = 0; t < kTrackCount; t++)
			if (trackBoss(t) >= 0) {
				bossTracks++;
				filesThere += audio::trackFileThere(t);
			}
		snprintf(d, sizeof d, "%d of %d", filesThere, bossTracks);
		check(bossTracks == 9 && filesThere == bossTracks, "Every boss track's file is in the game, 9 in all", d);
		audio::bossMusicEnd();
		for (int i = 0; i < 90; i++) audio::update(kDt);
		snprintf(d, sizeof d, "room %d, now %d", room, audio::currentTrack());
		check(audio::currentTrack() == room, "After the boss, the room's music comes back", d);
		// Real time passes too: the 3DS reads music on a background thread.
		for (int i = 0; i < 300 && audio::bossMusicOn(); i++) {
			audio::update(kDt);
			audioport::settle();
		}
		snprintf(d, sizeof d, "music at %.1f s", audio::musicTime());
		check(!audio::bossMusicOn() && audio::musicTime() >= 0, "and it fades back up to full volume, playing", d);
		gSave.bossMusic = musicWas;
		gSave.bossTrackLast[BOSS_MELON] = lastWas;

		// The Shop keeps a boss's tracks locked until it is beaten.
		const u32 beatenNow = gSave.bossesBeaten, seenNow = gSave.bossTracksSeen, ownedNow = gSave.ownedTracks, coinsNow = gSave.coins;
		const int item = fight - kBaseTracks;
		gSave.bossesBeaten = gSave.bossTracksSeen = 0;
		gSave.coins = 5000;
		check(shopLocked(SHOP_MUSIC, item) && shopBuy(SHOP_MUSIC, item) != BUY_DONE && !shopOwned(SHOP_MUSIC, item) && !shopHasNew(),
		      "A boss track is locked, and can't be bought, until its boss is beaten", "");
		gSave.bossesBeaten = 1u << BOSS_MELON;
		check(!shopLocked(SHOP_MUSIC, item) && shopHasNew() && shopBuy(SHOP_MUSIC, item) == BUY_DONE,
		      "Beating the boss unlocks its tracks to buy, and the Shop marks them new", "");
		shopSeenNew();
		check(!shopHasNew(), "Opening the Music tab clears the new mark", "");
		gSave.bossesBeaten = beatenNow;
		gSave.bossTracksSeen = seenNow;
		gSave.ownedTracks = ownedNow;
		gSave.coins = coinsNow;
	}

	// The card table.
	int bad = 0;
	for (int c = 0; c < CARD_COUNT; c++) {
		const CardInfo& k = kCards[c];
		if (!k.name || !k.name[0] || !k.line1 || !k.line1[0] || k.maxLevel < 1) bad++;
		if (k.kind == KIND_REUSABLE && k.cooldown <= 0) bad++;
		if (k.kind != KIND_UPGRADE && k.maxLevel != 1) bad++;
		const char* texts[3] = {k.name, k.line1, k.line2};
		for (const char* s : texts)
			for (; *s; s++)
				if ((unsigned char)*s >= 0x80) bad++;  // plain letters only: no dashes or fancy quotes
	}
	snprintf(d, sizeof d, "%d bad", bad);
	check(bad == 0, "Orchard Blast: every card has a name, two lines and sane numbers", d);

	// Starting a run.
	setup();
	check(orc.room == 1 && orc.hp == 20 && orc.maxHp == 20 && orc.level == 1 && maxStrikes == 0, "Orchard Blast starts in room 1 with 20 HP on Normal", "");
	check(orc.slot[0] == CARD_NONE && orc.slot[1] == CARD_NONE && orc.rerolls == 1, "Orchard Blast starts with empty charm slots and one Reroll", "");

	// Only bombs and bosses cost HP.
	Obj& bomb = objs[0];
	bomb = {};
	bomb.on = true;
	bomb.kind = OBJ_BOMB;
	bomb.x = 160;
	bomb.y = 120;
	bomb.radius = 19;
	bomb.hits = 1;
	hitBomb(bomb);
	snprintf(d, sizeof d, "HP %.1f", orc.hp);
	check(orc.hp == 15 && phase == PHASE_PLAY, "Orchard Blast: a bomb costs 5 HP on Normal and the run goes on", d);
	Obj& drop = objs[1];
	drop = {};
	drop.on = true;
	drop.kind = OBJ_FRUIT;
	drop.x = 160;
	drop.y = killY() + 5;
	drop.vy = 50;
	drop.radius = 23;
	drop.hits = 1;
	const float hpBefore = orc.hp;
	updateObjects(kDt);
	check(orc.hp == hpBefore && strikes == 0 && phase == PHASE_PLAY && orc.droppedThisRoom, "Orchard Blast: a dropped fruit never costs HP", "");
	orc.upgrade[CARD_BOMBSQUAD] = 1;
	bomb.on = true;
	hitBomb(bomb);
	check(orc.hp == hpBefore, "Bomb Squad: the first bomb in a room does no damage", "");
	orc.upgrade[CARD_BOMBSQUAD] = 0;

	// Armor, Last Stand, Second Wind and Rind Wall.
	orc.hp = 10;
	orc.upgrade[CARD_ARMOR] = 4;
	orcHurt(3, 160, 120, "TEST");
	check(orc.hp == 9, "Armor never takes a hit below 1", "");
	orc.upgrade[CARD_ARMOR] = 0;
	orc.lastStandT = 5;
	orcHurt(50, 160, 120, "TEST");
	check(orc.hp == 1 && phase == PHASE_PLAY, "Last Stand keeps you at 1 HP", "");
	orc.lastStandT = 0;
	orc.upgrade[CARD_SECONDWIND] = 1;
	orcHurt(50, 160, 120, "TEST");
	check(phase == PHASE_PLAY && orc.hp == orc.maxHp * 0.5f && orc.usedSecondWind, "Second Wind brings you back once, at half HP", "");
	orc.invulnT = 1;
	orcHurt(50, 160, 120, "TEST");
	check(orc.hp == orc.maxHp * 0.5f, "Rind Wall blocks every hit while it lasts", "");
	orc.invulnT = 0;

	// XP and level-ups.
	setup();
	const int need = orcXpNeed();
	check(need == 10, "Level 2 needs 10 XP on Normal", "");
	orcGainXp((float)need);
	check(orc.level == 2 && orc.levelsWaiting == 1, "Enough XP gains a level", "");
	Blade idle;
	orcUpdate(idle);
	check(orc.choosing && orc.offerCount == 3, "A level up opens three cards", "");
	check(orc.offer[0] != orc.offer[1] && orc.offer[1] != orc.offer[2] && orc.offer[0] != orc.offer[2], "The three cards are all different", "");
	const float elapsedBefore = elapsed;
	update(idle);
	check(orchardHeld() && elapsed == elapsedBefore, "The run holds still while the cards are up", "");
	const int rerollsBefore = orc.rerolls;
	orcReroll();
	check(orc.rerolls == rerollsBefore - 1 && orc.choosing && orc.offerCount == 3, "Reroll deals three new cards, once", "");
	orcReroll();
	check(orc.rerolls == 0, "No Reroll is left after using it", "");
	orc.offer[0] = CARD_HEALTH;
	orcPick(0);
	check(orc.maxHp == 25 && orc.upgrade[CARD_HEALTH] == 1 && !orc.choosing && orc.resumeT == 3, "Health adds 5 Max HP, then a 3, 2, 1 before play", "");
	for (int i = 0; i < 200; i++) update(idle);
	check(!orchardHeld(), "Play goes on after the 3, 2, 1", "");

	// Charms go into slots; a third asks which slot to replace.
	const auto take = [this](int card) {
		orc.choosing = true;
		orc.placing = false;
		orc.offerCount = 1;
		orc.offer[0] = (u8)card;
		orcPick(0);
	};
	take(CARD_SMOOTHIE);
	take(CARD_GOLDAPPLE);
	check(orc.slot[0] == CARD_SMOOTHIE && orc.slot[1] == CARD_GOLDAPPLE, "Charms fill the empty slots in order", "");
	take(CARD_FREEZEPOP);
	check(orc.placing && orc.choosing, "A third charm asks for a slot", "");
	orcPlace(1);
	check(orc.slot[1] == CARD_FREEZEPOP && !orc.choosing, "The new charm replaces the slot picked", "");
	orc.resumeT = 0;
	orc.hp = 5;
	orcUse(0);
	snprintf(d, sizeof d, "HP %.1f", orc.hp);
	check(orc.hp > 5 && orc.slotCool[0] == kCards[CARD_SMOOTHIE].cooldown, "Smoothie heals, then cools down", d);
	const float healed = orc.hp;
	orcUse(0);
	check(orc.hp == healed, "A cooling charm can't be used", "");
	orcSwapSlots();
	check(orc.slot[0] == CARD_FREEZEPOP && orc.slot[1] == CARD_SMOOTHIE && orc.slotCool[1] > 0, "Swap Slots swaps the charms and their cooldowns", "");
	take(CARD_RIPESEED);
	orcPlace(0);
	orc.resumeT = 0;
	const int levelBefore = orc.level;
	orcUse(0);
	check(orc.level == levelBefore + 1 && orc.slot[0] == CARD_NONE, "A single-use charm works once and leaves its slot", "");

	// Offers never repeat a maxed upgrade or a charm already slotted.
	setup();
	orc.upgrade[CARD_ARMOR] = kCards[CARD_ARMOR].maxLevel;
	orc.slot[0] = CARD_BLENDER;
	int repeats = 0;
	for (int i = 0; i < 300; i++) {
		orcOffer();
		for (int k = 0; k < orc.offerCount; k++) repeats += orc.offer[k] == CARD_ARMOR || orc.offer[k] == CARD_BLENDER;
	}
	check(repeats == 0, "Cards never offer a maxed upgrade or a charm you hold", "");
	orc.luckyPit = true;
	orcOffer();
	check(orc.offerCount == 5, "Lucky Pit shows five cards", "");

	// Rooms: 15 seconds each, then a boss in room 5.
	setup();
	Blade none;
	for (int i = 0; i < (int)(kRoomSeconds * 60) + 5; i++) orcUpdate(none);
	check(orc.room == 2 && !orc.bossRoom, "A room lasts 15 seconds", "");
	orc.room = 4;
	orc.roomT = kRoomSeconds;
	orcUpdate(none);
	check(orc.room == 5 && orc.bossRoom && orc.boss.on && orc.boss.id == BOSS_MELON && orc.boss.left == 3, "Room 5 brings in The Big Melon, 3 slices", "");

	// The boss: Deflected! with the guard up, a clean slice with it down, then a confused spell.
	Boss& b = orc.boss;
	b.enterT = 0;
	b.y = b.baseY;
	b.nextAttack = 99;
	orc.hp = 20;
	Blade s1 = swipe(b.x - 80, b.y, b.x + 80, b.y, 0x51000001u);
	bossTest(s1);
	snprintf(d, sizeof d, "HP %.1f, left %d", orc.hp, b.left);
	check(orc.hp == 18 && b.left == 3, "Slicing a guarded boss: Deflected! costs 2 HP on Normal", d);
	bossTest(s1);
	check(orc.hp == 18, "One swipe is only deflected once", "");
	Blade s2 = swipe(b.x - 80, b.y, b.x + 80, b.y, 0x51000002u);
	bossTest(s2);
	check(orc.hp == 18, "Deflected! waits half a second before it can cost HP again", "");
	bossOpen(orchardLevel(DIFF_NORMAL).openTime);
	check(!b.guard && b.openT > 0, "An opening drops the guard", "");
	Blade s3 = swipe(b.x - 80, b.y, b.x + 80, b.y, 0x51000003u);
	bossTest(s3);
	check(b.left == 2 && b.dazeT > 0 && orc.hp == 18, "A slice with the guard down lands and confuses the boss", "");
	check(b.phase == 2 && b.roarT > 0, "The Big Melon's second phase starts after its first slice", "");
	Blade s4 = swipe(b.x - 80, b.y, b.x + 80, b.y, 0x51000004u);
	bossTest(s4);
	check(b.left == 2 && orc.hp == 18, "A confused boss can't be hit, and doesn't deflect", "");
	for (int i = 0; i < 200 && b.dazeT > 0; i++) bossUpdate(kDt);
	check(b.guard && b.dazeT <= 0, "The guard goes back up after the confused spell", "");
	orc.cleaver = true;
	Blade s5 = swipe(b.x - 80, b.y, b.x + 80, b.y, 0x51000005u);
	bossTest(s5);
	check(b.left == 1 && !orc.cleaver, "Cleaver goes straight through the guard, once", "");
	b.dazeT = 0;
	b.guard = false;
	b.openT = 1;
	b.roarT = 0;
	Blade s6 = swipe(b.x - 80, b.y, b.x + 80, b.y, 0x51000006u);
	bossTest(s6);
	check(b.beaten && (gSave.bossesBeaten & 1u << BOSS_MELON), "The last slice beats the boss and marks it beaten", "");
	for (int i = 0; i < 200 && b.on; i++) bossUpdate(kDt);
	check(!b.on && orc.room == 6, "After the boss, the run moves on to room 6", "");

	// Boss attacks hurt, and their shots can be sliced away.
	setup();
	orc.room = 10;
	orc.bossRoom = true;
	bossStart(BOSS_KING);
	b.enterT = 0;
	b.y = b.baseY;
	check(b.left == 5, "The Bomb King takes 5 slices", "");
	bossFire(SHOT_CANNON, 100, 200, 0, 0);
	Shot& shot = orc.shots[0];
	Blade c1 = swipe(80, 200, 120, 200, 0x52000001u);
	bossTest(c1);
	check(shot.on && shot.hits == 1, "A cannonball takes two swipes", "");
	bossTest(c1);
	check(shot.on, "The same swipe can't cut a cannonball twice", "");
	Blade c2 = swipe(80, 200, 120, 200, 0x52000002u);
	bossTest(c2);
	check(!shot.on, "The second swipe breaks it", "");
	orc.hp = 20;
	bossFire(SHOT_SEED, 160, (float)kFieldH - 2, 0, 50);
	shotsUpdate(kDt);
	check(orc.hp < 20, "A shot that lands costs HP", "");
	int kinds = 0;
	for (int i = 0; i < 40; i++) {
		b.attack = 0;
		b.phase = 3;
		bossNextAttack();
		kinds |= 1 << b.attack;
		audio::fuse(false);
	}
	int count = 0;
	for (int k = 0; k < 16; k++) count += (kinds >> k) & 1;
	check(count == 5, "The Bomb King uses all five attacks by its last phase, the Giant Bomb among them", "");

	// The Bomb King's decoy fruit turn into bombs.
	setup();
	Pending p = {0, 160, 0, -300, OBJ_FRUIT, (u8)(1 | 0x80), true};
	launch(p);
	Obj* decoy = nullptr;
	for (auto& o : objs)
		if (o.on) decoy = &o;
	check(decoy && decoy->morph > 0 && decoy->fruit == 1, "A decoy fruit flies as a fruit at first", "");
	for (int i = 0; i < 120 && decoy; i++) updateObjects(kDt);
	check(decoy && decoy->kind == OBJ_BOMB, "A decoy fruit turns into a bomb", "");

	// Difficulty numbers.
	check(orchardLevel(DIFF_EASY).startHp == 25 && orchardLevel(DIFF_INSANE).startHp == 15 && orchardLevel(DIFF_HARD).bombDamage == 6, "Difficulty sets HP and bomb damage", "");


	// Timed fights: a bot that plays like a good player beats each boss on each difficulty, three times. It slices
	// shots after a short reaction, blocks charges, cuts fuses, scrubs the Giant Bomb at about five hits a second,
	// and slices the boss 0.4 s after its guard drops. The fights must last long enough to feel like fights.
	{
		static const char* const kDiffName[DIFF_COUNT] = {"Easy", "Normal", "Hard", "Insane"};
		float normalTime[BOSS_COUNT] = {};
		bool allBeaten = true;
		for (int boss = 0; boss < BOSS_COUNT; boss++) {
			for (int dif = 0; dif < DIFF_COUNT; dif++) {
				float total = 0, hurt = 0, longest = 0;
				int giants = 0;
				for (int run = 0; run < 3; run++) {
					gSave.difficulty = (u8)dif;
					setup();
					orc.hp = orc.maxHp = 99999;
					bossStart(boss);
					Boss& f = orc.boss;
					u32 stroke = 0x60000000u + (u32)(boss * 100000 + dif * 10000 + run * 1000);
					float openFor = 0, fuseT = 0, t = 0;
					int sawGiant = 0;
					bool inBomb = false;
					float scrub = 0;
					while (t < 400 && !f.beaten && f.on) {
						bossUpdate(kDt);
						t += kDt;
						for (auto& s : orc.shots)
							if (s.on && s.age > 0.45f && s.kind != SHOT_CROWN) {
								const float ddx = s.x - f.x, ddy = s.y - f.y;
								if (ddx * ddx + ddy * ddy < f.radius * f.radius * 2.2f) continue;
								Blade cut = swipe(s.x - 4, s.y - 4, s.x + 4, s.y + 4, ++stroke);
								bossTest(cut);
							}
						if (f.attack != KING_GIANT) inBomb = false;
						if (f.attack == KING_GIANT && f.giant.on && !f.giant.launched && f.volley == 1) {
							if (!inBomb) sawGiant++;
							inBomb = true;
							scrub += 1;
							const float side = ((int)scrub & 1) ? 1.0f : -1.0f;
							Blade sc = swipe(f.giant.x - side * 3, f.giant.y, f.giant.x + side * 3, f.giant.y, 0x5F000000u);
							bossTest(sc);
						} else if (f.attack == MEL_CHARGE && f.attackT > 0.8f) {
							Blade bl = swipe(f.x - 30, f.y, f.x + 30, f.y, ++stroke);
							bossTest(bl);
						} else if (f.attack == KING_FUSE) {
							fuseT += kDt;
							if (fuseT > 0.35f) {
								fuseT = 0;
								const float fy = f.y - f.radius - 8;
								Blade fz = swipe(f.x - 10, fy, f.x + 10, fy, ++stroke);
								bossTest(fz);
							}
						}
						if (!f.guard && f.openT > 0 && f.dazeT <= 0 && !f.hidden) {
							openFor += kDt;
							if (openFor > 0.4f) {
								Blade hit = swipe(f.x - 30, f.y - 10, f.x + 30, f.y + 10, ++stroke);
								bossTest(hit);
								openFor = 0;
							}
						} else {
							openFor = 0;
						}
					}
					allBeaten = allBeaten && f.beaten;
					total += t;
					if (t > longest) longest = t;
					hurt += orc.damageTaken;
					giants += sawGiant;
					audio::fuse(false);
				}
				snprintf(d, sizeof d, "%s %s: %.0f s on average (longest %.0f), %.0f damage taken, %d Giant Bombs", kBosses[boss].name, kDiffName[dif], total / 3, longest, hurt / 3, giants);
				check(true, "Timed fight", d);
				if (dif == DIFF_NORMAL) normalTime[boss] = total / 3;
			}
		}
		check(allBeaten, "A good player can beat both bosses on every difficulty", "");
		snprintf(d, sizeof d, "%.0f s", normalTime[BOSS_MELON]);
		check(normalTime[BOSS_MELON] >= 40 && normalTime[BOSS_MELON] <= 70, "The Big Melon takes a good player about 45 to 60 seconds on Normal", d);
		snprintf(d, sizeof d, "%.0f s", normalTime[BOSS_KING]);
		check(normalTime[BOSS_KING] >= 80 && normalTime[BOSS_KING] <= 130, "The Bomb King takes a good player about 90 to 120 seconds on Normal", d);
	}

	gSave.difficulty = diffWas;
	gSave.bossesBeaten = beatenWas;
	memcpy(gSave.bossWins, winsWas, sizeof winsWas);
	audio::fuse(false);
}
#endif
