// Orchard Blast: rooms, HP, XP, level-up cards and charms. The bosses are in boss.cpp,
// and the level-up screen and Inventory in orchard_ui.inc.
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "audio_port.h"  // the Lab gives the 3DS music reader real time
#include "boom.h"
#include "fruitids.h"
#include "game.h"
#include "port_api.h"
#include "render.h"
#include "shop.h"
#include "sprites.h"

// ---------------------------------------------------------------- the cards
const CardInfo kCards[CARD_COUNT] = {
	// Upgrades (uncapped scaling; timers/reductions soft-cap safely).
	{"Ring", "+20% XP from everything.", "", KIND_UPGRADE, RARE_COMMON, 255, 0, 0},
	{"Health", "+5 Max HP, and heals 5.", "", KIND_UPGRADE, RARE_COMMON, 255, 0, 0},
	{"Armor", "Every hit does 1 less.", "A hit still does at least 1.", KIND_UPGRADE, RARE_RARE, 255, 0, 0},
	{"Blade", "A boss's guard stays down longer.", "Coconuts need one slice less.", KIND_UPGRADE, RARE_RARE, 255, 0, 0},
	{"Ring of Regrowth", "You heal 20% faster.", "", KIND_UPGRADE, RARE_COMMON, 255, 0, 0},
	{"Long Edge", "A wider slice, so fruit", "are easier to catch.", KIND_UPGRADE, RARE_COMMON, 255, 0, 0},
	{"Steady Hands", "Fruit fall slower.", "", KIND_UPGRADE, RARE_COMMON, 255, 0, 0},
	{"Lucky Coin", "+25% coins from this run.", "", KIND_UPGRADE, RARE_COMMON, 255, 0, 0},
	{"Parry", "Chance a Deflected!", "costs you no HP.", KIND_UPGRADE, RARE_RARE, 255, 0, 0},
	// Passive charms
	{"Juice Formula", "Each fruit heals 0.25 HP,", "on top of your slow healing.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Power-up Magnet", "Arcade's power-ups show up", "more often.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Bomb Squad", "The first bomb you slice in", "each room does no damage.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Second Wind", "Once a run, when you'd go down,", "come back with half your HP.", KIND_PASSIVE, RARE_LEGEND, 1, 0, 0},
	{"Combo Battery", "Combos of 5 or more", "give double XP.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Golden Touch", "Gold fruit come 3 times", "as often.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Seed Pouch", "Seeds and loose pieces give", "XP like whole fruit.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Sharp Rind", "In a boss fight, a bomb you slice", "knocks its guard down too.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Echo Blade", "Each swipe cuts again, faintly,", "half a second later.", KIND_PASSIVE, RARE_LEGEND, 1, 0, 0},
	{"Clean Sweep", "Get through a room without", "dropping a fruit: heal 3 HP.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Lucky Leaf", "+2 Rerolls on the", "level-up screen.", KIND_PASSIVE, RARE_COMMON, 1, 0, 0},
	{"Overflow", "XP past a level up carries", "over twice as much.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Ripe Rhythm", "Slices on the music's beat", "give +50% XP.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Thick Peel", "A shield that blocks one hit,", "fresh at the start of every room.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Glass Cannon", "Double XP, boss guards stay down", "twice as long. Max HP halved.", KIND_PASSIVE, RARE_LEGEND, 1, 0, 0},
	// Reusable charms
	{"Blender", "For 10 s, slices every fruit", "that comes up. Bombs are left.", KIND_REUSABLE, RARE_RARE, 1, 10, 30},
	{"Freeze Pop", "For 5 s, everything freezes", "in midair, boss attacks too.", KIND_REUSABLE, RARE_RARE, 1, 5, 40},
	{"Defuser", "Every bomb on screen", "fizzles out.", KIND_REUSABLE, RARE_COMMON, 1, 0, 45},
	{"Smoothie", "Heals 30% of your Max HP.", "", KIND_REUSABLE, RARE_COMMON, 1, 0, 60},
	{"Cleaver", "Your next slice on a boss goes", "straight through its guard.", KIND_REUSABLE, RARE_LEGEND, 1, 0, 90},
	{"Fruit Rain", "For 4 s, a burst of fruit", "with no bombs.", KIND_REUSABLE, RARE_COMMON, 1, 4, 35},
	{"Magnet Pull", "For 6 s, fruit, power-ups", "and coins drift to your blade.", KIND_REUSABLE, RARE_COMMON, 1, 6, 30},
	{"Slow-Mo", "For 6 s, everything moves", "at half speed.", KIND_REUSABLE, RARE_COMMON, 1, 6, 50},
	// Single-use charms
	{"Golden Apple", "Heals you to full HP.", "", KIND_SINGLE, RARE_RARE, 1, 0, 0},
	{"Fruit Bomb", "Slices everything on screen.", "Bombs turn into XP.", KIND_SINGLE, RARE_LEGEND, 1, 0, 0},
	{"Rind Wall", "Blocks all damage", "for 8 seconds.", KIND_SINGLE, RARE_COMMON, 1, 0, 0},
	{"Ripe Seed", "Gain a level on the spot.", "", KIND_SINGLE, RARE_RARE, 1, 0, 0},
	{"Skip Peel", "Jump to the next room.", "Not in a boss room.", KIND_SINGLE, RARE_RARE, 1, 0, 0},
	{"Boss Bane", "The boss's guard drops", "for 3 seconds.", KIND_SINGLE, RARE_RARE, 1, 0, 0},
	{"Lucky Pit", "Your next level up shows", "5 cards instead of 3.", KIND_SINGLE, RARE_COMMON, 1, 0, 0},
	{"Coin Jar", "Coins count twice for", "the rest of the room.", KIND_SINGLE, RARE_COMMON, 1, 0, 0},
	{"Last Stand", "For 10 s nothing takes you below", "1 HP, and Deflected! is free.", KIND_SINGLE, RARE_LEGEND, 1, 0, 0},
	{"Juice Flood", "For 5 s, the screen floods", "with fruit, at double XP.", KIND_SINGLE, RARE_RARE, 1, 0, 0},
	{"Cutting Board", "Sliced fruit splits into pieces", "you can cut again for extra XP.", KIND_PASSIVE, RARE_LEGEND, 1, 0, 0},
	{"Lifesteal Tome", "Clean boss slices heal HP", "based on your Blade level.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
	{"Boss Harvest", "Fruits and shots in boss rooms", "grant XP again.", KIND_PASSIVE, RARE_RARE, 1, 0, 0},
};

// Each card's badge on the sprite sheet: older cards stay contiguous at sprites_cd_ring_idx + card.
int cardIcon(int card) {
	if (card >= 0 && card <= CARD_FLOOD) return sprites_cd_ring_idx + card;
	switch (card) {
		case CARD_CUTBOARD: return sprites_cd_cutboard_idx;
		case CARD_LIFESTEAL: return sprites_cd_lifesteal_idx;
		case CARD_BOSSHARVEST: return sprites_cd_bossharvest_idx;
		default: return sprites_cd_ring_idx;
	}
}
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
		case CARD_STEADY: snprintf(out, size, "Level %d: %.0f%% fruit speed.", n, 100.0f * fmaxf(0.35f, powf(0.95f, (float)n))); break;
		case CARD_LUCKY: snprintf(out, size, "Level %d: +%d%% coins.", n, n * 25); break;
		case CARD_PARRY: snprintf(out, size, "Level %d: %.0f%% free Deflected!", n, 100.0f * (1.0f - powf(0.90f, (float)n))); break;
		default: break;
	}
}

const BossInfo kBosses[BOSS_COUNT] = {
	{"The Big Melon", 3, 3},
	{"The Bomb King", 5, 3},
	{"The Pineapple Guard", 4, 3},
	{"The Inferno Pepper", 4, 3},
	{"Dusty Durian", 7, 3},
	{"Lord Lemon", 7, 3},
	{"Aggro Apple", 7, 3},
};

void orchardBuildBossOrder(u32 seed, u8 outOrder[kRegularBossCount]) {
	for (int i = 0; i < kRegularBossCount; i++) outOrder[i] = (u8)i;
	u32 s = seed ? seed : 0x9E3779B9u;
	for (int i = kRegularBossCount - 1; i > 0; i--) {
		s = s * 1664525u + 1013904223u;
		const int j = (int)((s >> 16) % (u32)(i + 1));
		const u8 tmp = outOrder[i];
		outOrder[i] = outOrder[j];
		outOrder[j] = tmp;
	}
}

int orchardBossForRoom(int room, u32 seed, const u8 order[kRegularBossCount], bool* royalOut) {
	if (room >= 25 && room % 25 == 0) {
		const u32 h = (seed ^ ((u32)room * 2654435761u)) * 1664525u + 1013904223u;
		if (royalOut) *royalOut = room >= 50;
		return kFirstMilestoneBoss + (int)((h >> 16) % (u32)kMilestoneBossCount);
	}
	const int enc = (room / 5) - 1;
	const int idx = enc < 0 ? 0 : enc;
	if (royalOut) *royalOut = idx >= kRegularBossCount;
	return order[idx % kRegularBossCount] % kRegularBossCount;
}

static const OrchardLevel kLevels[DIFF_COUNT] = {
	{25, 2, 0.45f, 0.65f, 0.72f, 2.60f, 1, 0.85f},  // Easy: beginners
	{20, 4, 0.85f, 0.92f, 0.92f, 2.20f, 2, 0.95f},  // Normal: casual players (bombDamage 4)
	{16, 6, 1.15f, 1.12f, 1.14f, 1.75f, 3, 1.05f},  // Hard: adds a bit of spice
	{12, 7, 1.45f, 1.32f, 1.34f, 1.40f, 4, 1.15f},  // Insane: demanding but skill-beatable
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
static RoomPlan plan(int room) {
	if (room < 1) return kRooms[1];
	if (room <= kOrchardRooms) return kRooms[room];
	const int extraRooms = room - kOrchardRooms;
	if (room % 5 == 0) return {clampf(0.50f + extraRooms * 0.015f, 0.50f, 0.90f), 0.0f, 1, clampf(2.6f - extraRooms * 0.02f, 1.8f, 2.6f), true, false};
	return {
		clampf(0.55f + extraRooms * 0.025f, 0.55f, 1.0f),
		clampf(0.20f + extraRooms * 0.006f, 0.20f, 0.32f),
		(int)clampf(4.0f + extraRooms * 0.2f, 4.0f, 7.0f),
		clampf(1.35f - extraRooms * 0.025f, 0.85f, 1.35f),
		true, true
	};
}

static const ObjKind kOrchardPowers[] = {OBJ_FREEZE, OBJ_DOUBLE, OBJ_FRENZY, OBJ_MAGNET, OBJ_WIDE, OBJ_SHIELD};

// Mid-run save for Orchard Blast (Continue / Start Over when re-entering after Pause -> Quit).
struct OrcRunSave {
	u32 magic;
	u32 size;
	u32 spawnSeed;
	u32 score, bestCombo, journalCoins;
	int sliced, coinsCaught, coinsThrown;
	float elapsed, waveTimer, gravity;
	bool assisted, mixed;
	bool mix[MIX_COUNT];
	u32 journalStart[kMaxFruitKinds];
	Orchard orc;
	Obj objs[Game::kMaxObjs];
	Pending pending[Game::kMaxPending];
	u32 sum;
};
static constexpr u32 kOrcSaveMagic = 0x4F524334u;
// Shared main-thread scratch buffer so 3DS's 32 KB thread stack never holds ~6 KB OrcRunSave locals.
static OrcRunSave sOrcSaveIo;

static u32 orcSaveHash(const void* data, size_t n) {
	const u8* p = (const u8*)data;
	u32 h = 2166136261u;
	for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
	return h;
}

static const OrcRunSave* orcReadSaveFile() {
	char file[256], temp[256];
	port::dataPath(file, sizeof file, "orc_run.bin");
	port::dataPath(temp, sizeof temp, "orc_run.tmp");
	const char* paths[2] = {file, temp};
	for (const char* path : paths) {
		FILE* f = fopen(path, "rb");
		if (!f) continue;
		memset(&sOrcSaveIo, 0, sizeof sOrcSaveIo);
		const bool readOk = fread(&sOrcSaveIo, sizeof sOrcSaveIo, 1, f) == 1;
		fclose(f);
		if (!readOk || sOrcSaveIo.magic != kOrcSaveMagic || sOrcSaveIo.size != sizeof sOrcSaveIo) continue;
		if (sOrcSaveIo.sum != orcSaveHash(&sOrcSaveIo, offsetof(OrcRunSave, sum))) continue;
		if (sOrcSaveIo.orc.room < 1 || sOrcSaveIo.orc.level < 1 || sOrcSaveIo.orc.hp <= 0) continue;
		return &sOrcSaveIo;
	}
	return nullptr;
}

bool Game::orchardHasSave(int* savedRoomOut) {
#ifdef BOSS_TEST_APP
	(void)savedRoomOut;
	return false;
#endif
	const OrcRunSave* s = orcReadSaveFile();
	if (!s) return false;
	if (savedRoomOut) *savedRoomOut = s->orc.room;
	return true;
}

void Game::orchardClearSave() {
#ifdef BOSS_TEST_APP
	return;
#endif
	char file[256], temp[256];
	port::dataPath(file, sizeof file, "orc_run.bin");
	port::dataPath(temp, sizeof temp, "orc_run.tmp");
	remove(file);
	remove(temp);
}

bool Game::orcSaveRun() const {
#ifdef BOSS_TEST_APP
	return false;
#endif
	if (mode != MODE_ORCHARD || phase == PHASE_OVER || phase == PHASE_DYING || orc.resumePrompt || passName) return false;
	port::ensureDataDir();
	char file[256], temp[256];
	port::dataPath(file, sizeof file, "orc_run.bin");
	port::dataPath(temp, sizeof temp, "orc_run.tmp");
	OrcRunSave& s = sOrcSaveIo;
	memset(&s, 0, sizeof s);
	s.magic = kOrcSaveMagic;
	s.size = (u32)sizeof s;
	s.spawnSeed = gSpawn.s ? gSpawn.s : (orc.runSeed ? orc.runSeed : 1u);
	s.score = score;
	s.bestCombo = bestCombo;
	s.journalCoins = journalCoins;
	s.sliced = sliced;
	s.coinsCaught = coinsCaught;
	s.coinsThrown = coinsThrown;
	s.elapsed = elapsed;
	s.waveTimer = waveTimer;
	s.gravity = gravity;
	s.assisted = assisted;
	s.mixed = mixed;
	memcpy(s.mix, mix, sizeof mix);
	memcpy(s.journalStart, journalStart, sizeof journalStart);
	s.orc = orc;
	s.orc.resumePrompt = false;
	s.orc.resumeFocus = 0;
	s.orc.savedRoom = 0;
	s.orc.hurtFlash = 0;
	s.orc.devPrompt = false;
	s.orc.cutscene = false;
	memset(s.orc.echoOn, 0, sizeof s.orc.echoOn);
	// Room-start snapshots keep transient mid-room projectiles/fruits empty so reloading always begins cleanly at room entry.
	memset(s.objs, 0, sizeof s.objs);
	memset(s.pending, 0, sizeof s.pending);
	s.sum = orcSaveHash(&s, offsetof(OrcRunSave, sum));

	FILE* f = fopen(temp, "wb");
	if (!f) return false;
	bool ok = fwrite(&s, sizeof s, 1, f) == 1;
	if (fflush(f) != 0) ok = false;
	if (fclose(f) != 0) ok = false;
	if (!ok) {
		remove(temp);
		return false;
	}
	if (rename(temp, file) != 0) {
		remove(file);
		if (rename(temp, file) != 0) {
			remove(temp);
			return false;
		}
	}
	return true;
}

void Game::orcBeginRoom50Cutscene(int bid) {
	orc.boss = {};
	for (auto& sShot : orc.shots) sShot.on = false;
	orc.cutscene = true;
	orc.dialogueLine = 0;
	orc.dialogueChars = 0;
	orc.dialogueLastChar = -1;
	orc.dialoguePauseT = 0;
	orc.dialogueWaitRelease = false;
	orc.tensionStarted = false;
	orc.pendingMilestoneBoss = (u8)bid;
	audio::tensionMusic(false);
}

bool Game::orcLoadRun() {
#ifdef BOSS_TEST_APP
	return false;
#endif
	const OrcRunSave* sp = orcReadSaveFile();
	if (!sp) return false;
	const OrcRunSave& s = *sp;
	gSpawn.s = s.spawnSeed ? s.spawnSeed : 1u;
	score = s.score;
	bestCombo = (int)s.bestCombo;
	journalCoins = s.journalCoins;
	sliced = s.sliced;
	coinsCaught = s.coinsCaught;
	coinsThrown = s.coinsThrown;
	elapsed = s.elapsed;
	waveTimer = s.waveTimer;
	gravity = s.gravity > 0 ? s.gravity : gravity;
	assisted = s.assisted;
	mixed = s.mixed;
	memcpy(mix, s.mix, sizeof mix);
	memcpy(journalStart, s.journalStart, sizeof journalStart);
	orc = s.orc;
	orc.endless = true;
	orc.won = false;
	orc.resumePrompt = false;
	orc.resumeFocus = 0;
	orc.savedRoom = 0;
	orc.resumeT = 0;
	orc.roomT = 0;
	orc.bombSquadUsed = false;
	orc.droppedThisRoom = false;
	orc.coinJar = false;
	orc.peelShield = orc.upgrade[CARD_THICKPEEL] != 0;
	orc.devPrompt = false;
	orc.cutscene = false;
	if (orc.runSeed == 0) orc.runSeed = gSpawn.s ? gSpawn.s : 1u;
	for (auto& o : objs) o = {};
	for (auto& p : pending) p = {};
	for (auto& h : halves) h = {};
	for (auto& pt : parts) pt = {};
	for (auto& po : popups) po = {};
	for (auto& r : rings) r = {};
	chain = 0;
	comboCount = 0;
	comboTimer = 0;
	flash = 0;
	shake = 0;
	maxStrikes = 0;
	strikes = 0;
	phase = PHASE_PLAY;
	phaseT = 0;
	if (orc.bossRoom) {
		waveTimer = 999.0f;
		bool royal = false;
		const int bid = orchardBossForRoom(orc.room, orc.runSeed, orc.bossOrder, &royal);
		if (orc.room == 25 && !gSave.seenRoom50Cutscene && !passName) {
			orcBeginRoom50Cutscene(bid);
		} else {
			bossStart(bid, royal);
		}
	} else {
		waveTimer = 0.25f;
		if (!orc.choosing) {
			char buf[32];
			snprintf(buf, sizeof buf, "ROOM %d", orc.room);
			setCallout(buf, rgba(255, 230, 140));
		}
	}
	return true;
}

// ---------------------------------------------------------------- the run
void Game::orcStart() {
	const OrchardLevel& lv = orchardLevel(gSave.difficulty);
	orc.endless = true;
	orc.room = 1;
	orc.maxHp = orc.hp = (float)lv.startHp;
	orc.level = 1;
	orc.rerolls = 1;
	orc.paidRerolls = 0;
	orc.coinsSpent = 0;
	orc.slot[0] = orc.slot[1] = CARD_NONE;
	maxStrikes = 0;  // HP, not misses
	orc.runSeed = gSpawn.s ? gSpawn.s : 1u;
	orchardBuildBossOrder(orc.runSeed, orc.bossOrder);
	gravity = lerpf(300, 430, plan(1).d) * kFieldK * levelGravity(gSave.difficulty) * (mix[MIX_LOW_GRAVITY] ? 0.6f : 1.0f);
	if (mode == MODE_ORCHARD) {
		int savedRoom = 0;
#ifndef TEST_ORCHARD
		if (!passName && orchardHasSave(&savedRoom)) {
			orc.resumePrompt = true;
			orc.resumeFocus = 0;
			orc.savedRoom = (u16)(savedRoom < 1 ? 1 : savedRoom);
			phase = PHASE_PLAY;
			return;
		}
#endif
		orc.resumePrompt = false;
		orc.savedRoom = 0;
#if defined(DEV_TOOLS) && !defined(AUTOTEST)
		if (!passName) {
			orc.devPrompt = true;
			orc.devField = 1;
			orc.devRoomTarget = 1;
			orc.devLevelTarget = 1;
			phase = PHASE_PLAY;
			return;
		}
#endif
		phase = PHASE_READY;
		phaseT = 0;
		setCallout("ROOM 1", rgba(255, 230, 140));
#ifndef AUTOTEST
		if (!passName) orcSaveRun();
#endif
	}
#ifdef TEST_ORCHARD
	orc.resumePrompt = false;
	orc.devPrompt = false;
	orc.level = 2;
	orc.levelsWaiting = 1;
	orc.slot[0] = CARD_BLENDER;
	orc.slot[1] = CARD_GOLDAPPLE;
	orc.room = 4;
	orc.roomT = 9;
	orc.upgrade[CARD_ARMOR] = 2;
#ifdef TEST_ORCHARD_ROOM
	orc.room = TEST_ORCHARD_ROOM;
	orc.maxHp = orc.hp = 400;
#endif
#endif
}

void Game::orcStartBossAfterCutscene() {
	if (!orc.cutscene) return;
	orc.cutscene = false;
	if (orc.tensionStarted) audio::tensionMusic(false);
	orc.tensionStarted = false;
	if (!gSave.bossMusic) audio::music(MUSIC_PLAY);
	if (!gSave.seenRoom50Cutscene) {
		gSave.seenRoom50Cutscene = 1;
		saveWrite();
	}
	const int bid = orc.pendingMilestoneBoss < BOSS_COUNT ? orc.pendingMilestoneBoss : BOSS_DURIAN;
	const bool royal = orc.room >= 100;
	bossStart(bid, royal);
}

void Game::orcApplyDevSkip(int targetRoom, int targetLevel) {
	if (mode != MODE_ORCHARD) return;
	if (targetRoom < 1) targetRoom = 1;
	if (targetRoom > 999) targetRoom = 999;
	if (targetLevel < 1) targetLevel = 1;
	if (targetLevel > 255) targetLevel = 255;
	orc.devPrompt = false;
	orc.room = targetRoom;
	orc.roomT = 0;
	score = (u32)(targetRoom - 1);
	orc.bossesBeaten = (targetRoom - 1) / 5;
	orc.bombSquadUsed = false;
	orc.droppedThisRoom = false;
	orc.coinJar = false;
	orc.peelShield = orc.upgrade[CARD_THICKPEEL] != 0;
	orc.bossRoom = orc.room % 5 == 0;
	orc.boss = {};
	for (auto& s : orc.shots) s.on = false;
	for (auto& o : objs) o = {};
	for (auto& p : pending) p = {};
	// Buffer level-ups from Level 1 -> targetLevel so card picks open sequentially upon entry.
	memset(orc.upgrade, 0, sizeof orc.upgrade);
	memset(orc.passiveOrder, 0, sizeof orc.passiveOrder);
	orc.passiveCount = 0;
	for (int s = 0; s < kSlots; s++) {
		orc.slot[s] = CARD_NONE;
		orc.slotOn[s] = orc.slotCool[s] = 0;
	}
	const OrchardLevel& lv = orchardLevel(gSave.difficulty);
	orc.maxHp = orc.hp = (float)lv.startHp;
	orc.choosing = false;
	orc.placing = false;
	orc.level = targetLevel;
	orc.levelsWaiting = targetLevel > 1 ? targetLevel - 1 : 0;
	orc.xp = 0;
	gravity = lerpf(300, 430, plan(orc.room).d) * kFieldK * levelGravity(gSave.difficulty) * (mix[MIX_LOW_GRAVITY] ? 0.6f : 1.0f);
	phase = PHASE_PLAY;
	phaseT = 0;
	waveTimer = 0.25f;
	if (orc.bossRoom) {
		bool royal = false;
		const int bid = orchardBossForRoom(orc.room, orc.runSeed, orc.bossOrder, &royal);
		if (orc.room == 25 && !gSave.seenRoom50Cutscene && !passName) {
			orcBeginRoom50Cutscene(bid);
		} else {
			bossStart(bid, royal);
		}
	} else {
		char buf[32];
		snprintf(buf, sizeof buf, "ROOM %d", orc.room);
		setCallout(buf, rgba(255, 230, 140));
		audio::play(SFX_GO, 0.8f);
	}
	if (!passName) orcSaveRun();
}

#ifdef BOSS_TEST_APP
void Game::bossTestLaunch(int bossId, bool useRush, bool royal, int diff, int preset) {
	if (bossId < 0 || bossId >= BOSS_COUNT) bossId = BOSS_MELON;
	if (diff < 0 || diff >= DIFF_COUNT) diff = DIFF_NORMAL;
	gSave.difficulty = (u8)diff;
	const OrchardLevel& lv = orchardLevel(gSave.difficulty);

	mode = useRush ? MODE_BOSSRUSH : MODE_ORCHARD;
	score = 0;
	sliced = 0;
	bestCombo = 0;
	coinsCaught = 0;
	coinsThrown = 0;
	journalCoins = 0;
	coinsEarned = 0;
	dailyCoins = 0;
	elapsed = 0;
	waveTimer = 999.0f;
	chain = 0;
	comboCount = 0;
	comboTimer = 0;
	feverT = frenzyT = freezeT = doubleT = magnetT = wideT = chiliT = stunT = stickyT = 0;
	flash = 0;
	shake = 0;
	maxStrikes = 0;
	strikes = 0;
	passName = nullptr;
	for (auto& o : objs) o = {};
	for (auto& p : pending) p = {};
	for (auto& h : halves) h = {};
	for (auto& pt : parts) pt = {};
	for (auto& pop : popups) pop = {};
	for (auto& r : rings) r = {};
	orc = {};
	orc.slot[0] = orc.slot[1] = CARD_NONE;
	m12 = {};

	orc.endless = true;
	orc.bossRoom = true;
	orc.runSeed = 0xB0557E57u;
	orchardBuildBossOrder(orc.runSeed, orc.bossOrder);
	if (useRush) {
		orc.room = 0;
		m12.rushPicking = false;
		m12.rushStage = (u8)(bossId >= kFirstMilestoneBoss ? 4 : bossId);
		m12.rushBuild = (u8)(preset == 1 ? BUILD_TANK : (preset == 2 ? BUILD_GLASS : (preset == 3 ? BUILD_CHARMER : BUILD_TANK)));
	} else {
		if (bossId >= kFirstMilestoneBoss) orc.room = royal ? 50 : 25;
		else orc.room = royal ? 30 + (bossId + 1) * 5 : (bossId + 1) * 5;
	}
	orc.level = preset == 4 ? 10 : 1;

	auto addPassive = [this](int card) {
		orc.upgrade[card] = 1;
		orc.passiveOrder[orc.passiveCount++] = (u8)card;
	};
	switch (preset) {
		case 1: orcApplyRushPreset(BUILD_TANK); break;
		case 2: orcApplyRushPreset(BUILD_GLASS); break;
		case 3: orcApplyRushPreset(BUILD_CHARMER); break;
		case 4:  // Full Charms: endgame Orchard showcase with all 2.0c passives
			orc.maxHp = orc.hp = (float)lv.startHp + 10.0f;
			orc.upgrade[CARD_ARMOR] = 2;
			orc.upgrade[CARD_BLADE] = 3;
			addPassive(CARD_JUICE);
			addPassive(CARD_THICKPEEL);
			addPassive(CARD_CUTBOARD);
			addPassive(CARD_LIFESTEAL);
			addPassive(CARD_BOSSHARVEST);
			orc.slot[0] = CARD_CLEAVER;
			orc.slot[1] = CARD_BLENDER;
			break;
		default:  // Starter
			orc.maxHp = orc.hp = (float)lv.startHp;
			break;
	}
	orc.peelShield = orc.upgrade[CARD_THICKPEEL] != 0;
	gravity = lerpf(300, 430, useRush ? 0.5f : clampf(orc.room / 30.0f, 0.2f, 1.0f)) * kFieldK * levelGravity(gSave.difficulty);
	phase = PHASE_PLAY;
	phaseT = 0;
	gSpawn.s = 0xB0557E57u ^ (u32)(bossId * 97 + royal * 13);
	bossStart(bossId, royal);
}
#endif

void Game::orchardResumeChoice(bool continueRun, u32 seed) {
	if (mode != MODE_ORCHARD) return;
	if (continueRun && orcLoadRun()) return;
	orchardClearSave();
	score = 0;
	sliced = 0;
	bestCombo = 0;
	coinsCaught = 0;
	coinsThrown = 0;
	journalCoins = 0;
	elapsed = 0;
	waveTimer = 0;
	chain = 0;
	comboCount = 0;
	comboTimer = 0;
	orc = {};
	for (auto& o : objs) o = {};
	for (auto& p : pending) p = {};
	for (auto& h : halves) h = {};
	for (auto& pt : parts) pt = {};
	gSpawn.s = seed != 0 ? seed : (gRng.next() | 1);
	orcStart();
}

float Game::orcDifficulty() const { return plan(orc.room).d; }

float Game::orcObjScale() const {
	float k = fmaxf(0.35f, powf(0.95f, (float)orc.upgrade[CARD_STEADY]));
	if (orc.slowmoT > 0) k *= 0.5f;
	if (orc.stillT > 0) k = 0;
	return k;
}

float Game::orcReach() const { return 1.0f + 0.12f * orc.upgrade[CARD_LONGEDGE]; }

int Game::orcXpNeed() const { return (int)(orchardXpFor(orc.level) * orchardLevel(gSave.difficulty).xpNeed + 0.5f); }

bool Game::orcWave() {
	if (mode == MODE_BOSSRUSH || orc.bossRoom) {
		waveTimer = 999.0f;
		return true;
	}
	const RoomPlan& p = plan(orc.room);
	gravity = lerpf(300, 430, p.d) * kFieldK * levelGravity(gSave.difficulty) * (mix[MIX_LOW_GRAVITY] ? 0.6f : 1.0f);
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
			queue(stagger * i, x, vx, vy, kinds[i], isBomb ? 0 : fruit);
		}
	}
	waveTimer = p.interval * levelInterval(gSave.difficulty) + stagger * count;
	maybeCoin(p.d);
	return true;
}

void Game::orcGainXp(float amount) {
	if (amount <= 0 || mode == MODE_BOSSRUSH || (orc.bossRoom && !orc.upgrade[CARD_BOSSHARVEST])) return;  // Boss Rush has no level ups; Boss Harvest unlocks boss-room XP
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
#ifdef BOSS_TEST_APP
	if (orc.boss.beaten) return;
#endif
	if (orc.levelsWaiting > 0 && !orc.choosing && !orcBossExploding() && !orc.cutscene && phase == PHASE_PLAY && (feverT > 0 || frenzyT > 0)) orcOffer();
}

void Game::orcSliced(const Obj& o) {
	float xp = 0;
	switch (o.kind) {
		case OBJ_FRUIT:
			if (o.cutGen == 1) xp = o.fruit >= kSpiky ? 1.5f : 0.5f;
			else if (o.cutGen >= 2) xp = o.fruit >= kSpiky ? 0.75f : 0.25f;
			else if (o.loose) xp = orc.upgrade[CARD_SEEDPOUCH] ? 3 : 1;
			else xp = o.fruit >= kSpiky ? 3 : 1;
			if (orc.upgrade[CARD_JUICE] && o.cutGen == 0) orc.hp = fminf(orc.maxHp, orc.hp + 0.25f);
			break;
		case OBJ_GOLD:
			xp = o.cutGen == 1 ? 1.5f : (o.cutGen >= 2 ? 0.75f : 3.0f);
			if (orc.upgrade[CARD_JUICE] && o.cutGen == 0) orc.hp = fminf(orc.maxHp, orc.hp + 0.25f);
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
	(void)what;
	if (phase != PHASE_PLAY || damage <= 0) return;
	char buf[32];
	if (orc.invulnT > 0) {
		return;
	}
	if (orc.peelShield) {
		orc.peelShield = false;
		audio::play(SFX_KNOCK, 0.9f, fieldPan(x), 0.7f);
		addRing(x, y, rgba(190, 230, 140), 2.2f, 0.3f);
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
	snprintf(buf, sizeof buf, "-%d", d);
	addPopup(buf, x, y - 18, rgba(255, 110, 100), 0.72f);
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
	if (orc.boss.on && !orc.boss.beaten && (orc.boss.id == BOSS_KING || orc.boss.id == BOSS_CHILI) && o.tag == 1) {
		orcGainXp(1);
		if (orc.upgrade[CARD_JUICE]) orc.hp = fminf(orc.maxHp, orc.hp + 0.25f);
		sliced++;
		return;
	}
	if (orc.upgrade[CARD_BOMBSQUAD] && !orc.bombSquadUsed) {
		orc.bombSquadUsed = true;
		addRing(o.x, o.y, rgba(200, 220, 255), 2.4f, 0.35f);
		return;
	}
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
	award(1);
	orc.room++;
	orc.roomT = 0;
	orc.bombSquadUsed = false;
	orc.droppedThisRoom = false;
	orc.coinJar = false;
	orc.peelShield = orc.upgrade[CARD_THICKPEEL] != 0;
	orc.bossRoom = orc.room % 5 == 0;
	char buf[32];
	if (orc.bossRoom) {
		bool royal = false;
		const int bid = orchardBossForRoom(orc.room, orc.runSeed, orc.bossOrder, &royal);
		for (auto& p : pending) p = {};
		if (orc.room == 25 && !gSave.seenRoom50Cutscene && !passName) {
			orcBeginRoom50Cutscene(bid);
		} else {
			bossStart(bid, royal);
		}
	} else {
		// Leaving a boss room stopped waveTimer at 999s, so restore normal wave launches right away.
		waveTimer = 0.25f;
		snprintf(buf, sizeof buf, "ROOM %d", orc.room);
		setCallout(buf, rgba(255, 230, 140));
		audio::play(SFX_GO, 0.8f);
	}
#ifndef AUTOTEST
	if (!passName) orcSaveRun();
#endif
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
		int w = info.rarity == RARE_LEGEND ? 16 : (info.rarity == RARE_RARE ? 42 : 60);
		if (orc.room >= 5 && info.rarity != RARE_COMMON) w += 10;
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
	audio::play(SFX_LEVELUP, 0.85f);
	showCaption("Level up! Pick a card.");
}

void Game::orcReroll() {
	if (!orcCanReroll()) return;
	if (orc.rerolls > 0) {
		orc.rerolls--;
	} else if (mode == MODE_ORCHARD) {
		const u32 cost = (u32)orcRerollPrice();
		if (runCoins() < cost) return;
		orc.coinsSpent += cost;
		orc.paidRerolls++;
	} else {
		return;
	}
	orc.luckyPit = orc.offerCount > 3;  // a Lucky Pit's five stay five
	orcOffer();
}

void Game::orcClose() {
	orc.choosing = false;
	orc.placing = false;
	if (orc.levelsWaiting > 0) orc.levelsWaiting--;
	if (orc.levelsWaiting > 0) orcOffer();
	else orc.resumeT = 0;
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
	// A level up opens the cards straight away, even in the middle of a swipe or a Fruit Frenzy.
	if (orc.levelsWaiting > 0 && !orc.choosing && !orcBossExploding() && !orc.cutscene && phase == PHASE_PLAY) {
		blade.reset();
		orcOffer();
	}
}

float Game::orcStat(int card) const {
	switch (card) {
		case CARD_RING: return 100 + 20.0f * orc.upgrade[CARD_RING] * (orc.upgrade[CARD_GLASS] ? 2 : 1);
		case CARD_HEALTH: return orc.maxHp;
		case CARD_ARMOR: return orc.upgrade[CARD_ARMOR];
		case CARD_BLADE: return orchardLevel(gSave.difficulty).openTime * (orc.upgrade[CARD_GLASS] ? 2 : 1) + 0.25f * orc.upgrade[CARD_BLADE];
		case CARD_REGROWTH: return 0.2f * (1 + 0.2f * orc.upgrade[CARD_REGROWTH]);
		case CARD_LONGEDGE: return orcReach() * 100;
	case CARD_STEADY: return 100.0f * fmaxf(0.35f, powf(0.95f, (float)orc.upgrade[CARD_STEADY]));
	case CARD_LUCKY: return 100 + 25.0f * orc.upgrade[CARD_LUCKY];
	case CARD_PARRY: return 100.0f * (1.0f - powf(0.90f, (float)orc.upgrade[CARD_PARRY]));
	default: return 0;
	}
}

// ---------------------------------------------------------------- on screen
void Game::orcSlotRect(int slot, float* x, float* y, float* w, float* h) {
	const float s = PORT_3DS ? 54.0f : 62.0f * uiScale();
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
	const float titleScale = (PORT_3DS ? 0.36f : 0.30f) * u;
	const float hpScale = (PORT_3DS ? 0.30f : 0.24f) * u;
	const float lvScale = (PORT_3DS ? 0.30f : 0.24f) * u;
	const float yOff = PORT_3DS ? y : (y + 2.5f * u);
	float titleW = 0, pipsW = 0;
	// The room, or the boss and the slices it still needs.
	if (b.on && !b.beaten) {
		static const char* const kShortName[BOSS_COUNT] = {
		    "BIG MELON", "BOMB KING", "PINEAPPLE GUARD", "INFERNO PEPPER", "DUSTY DURIAN", "LORD LEMON", "AGGRO APPLE"};
		static const char* const kRoyalName[BOSS_COUNT] = {
		    "ROYAL MELON", "ROYAL KING", "ROYAL PINEAPPLE", "ROYAL PEPPER", "ROYAL DURIAN", "ROYAL LEMON", "ROYAL APPLE"};
		const int bi = b.id % BOSS_COUNT;
		snprintf(buf, sizeof buf, "%s", b.royal ? kRoyalName[bi] : kShortName[bi]);
		titleW = gfx::textWidth(buf, titleScale);
		gfx::text(buf, x, yOff + (PORT_3DS ? 2.0f : 3.0f) * u, titleScale, rgba(255, 200, 140), ALIGN_RIGHT);
		const int all = b.total;
		const float pw = (PORT_3DS ? 9.0f : 8.0f) * u, ph = (PORT_3DS ? 7.0f : 6.0f) * u, gap = (PORT_3DS ? 3.0f : 3.5f) * u;
		pipsW = all > 0 ? all * pw + (all - 1) * gap : 0;
		for (int i = 0; i < all; i++) {
			const bool cut = i < all - b.left;
			// One mark per slice it needs: bright while still to do, dim once landed.
			const float px = x - pw - (all - 1 - i) * (pw + gap);
			gfx::rect(px, yOff + (PORT_3DS ? 19.0f : 18.0f) * u, pw, ph, cut ? rgba(90, 70, 60, 200) : rgba(255, 110, 90));
		}
	} else {
		if (mode == MODE_BOSSRUSH) snprintf(buf, sizeof buf, "BOSS %d/%d", m12.rushStage + 1 > kRushStages ? kRushStages : m12.rushStage + 1, kRushStages);
		else snprintf(buf, sizeof buf, "ROOM %d", orc.room);
		titleW = gfx::textWidth(buf, titleScale);
		gfx::text(buf, x, yOff + (PORT_3DS ? 2.0f : 3.0f) * u, titleScale, COL_CREAM, ALIGN_RIGHT);
		if (!orc.bossRoom) {
			const float k = clampf(1 - orc.roomT / kRoomSeconds, 0, 1);
			const float rw = 56 * u, rh = (PORT_3DS ? 6.0f : 4.5f) * u;
			pipsW = rw;
			gfx::rect(x - rw, yOff + (PORT_3DS ? 20.0f : 18.0f) * u, rw, rh, rgba(0, 0, 0, 110));
			gfx::rect(x - rw, yOff + (PORT_3DS ? 20.0f : 18.0f) * u, rw * k, rh, COL_DIM);
		}
	}
	const float lvReserve = (PORT_3DS || w >= 190.0f * u) ? 0.0f : (mode == MODE_BOSSRUSH ? 0.0f : 34.0f * u);
	const float roomW = fmaxf((PORT_3DS ? 84.0f : 66.0f) * u, fmaxf(titleW, pipsW) + 8.0f * u);
	float bw = w - roomW - lvReserve - 8 * u;
	if (bw > (PORT_3DS ? 118.0f : 124.0f) * u) bw = (PORT_3DS ? 118.0f : 124.0f) * u;
	if (bw < 38 * u) bw = 38 * u;
	const float bx = x - roomW - bw, bh = (PORT_3DS ? 16.0f : 13.0f) * u;
	// HP: red, flashing when hit; blue while a Rind Wall or Last Stand holds.
	const float hpK = clampf(orc.hp / (orc.maxHp > 0 ? orc.maxHp : 1), 0, 1);
	const bool guarded = orc.invulnT > 0 || orc.lastStandT > 0;
	u32 hpCol = guarded ? rgba(130, 200, 255) : (hpK < 0.3f ? rgba(255, 90, 80) : rgba(240, 70, 80));
	if (orc.hurtFlash > 0 && !gSave.calmFx) hpCol = rgba(255, 230, 230);
	const float yHp = yOff + (mode == MODE_BOSSRUSH ? 6.0f : (PORT_3DS ? 4.0f : 4.0f)) * u;
	gfx::rect(bx - 1, yHp - 1, bw + 2, bh + 2, gfx::themeBar());
	gfx::rect(bx, yHp, bw * hpK, bh, hpCol);
	if (orc.peelShield) gfx::rect(bx, yHp, 3 * u, bh, rgba(190, 240, 150));
	snprintf(buf, sizeof buf, "HP %d/%d", (int)ceilf(orc.hp), (int)orc.maxHp);
	gfx::text(buf, bx + 5 * u, yHp + (bh - 40 * hpScale) * 0.5f, hpScale, COL_CREAM);
	// XP and the level. Boss Rush has neither.
	if (mode == MODE_BOSSRUSH) return;
	const float xpK = clampf(orc.xp / (float)orcXpNeed(), 0, 1);
	const float xy = yHp + bh + 2.5f * u;
	const float xh = (PORT_3DS ? 7.0f : 5.0f) * u;
	gfx::rect(bx - 1, xy - 1, bw + 2, xh + 2, gfx::themeBar());
	gfx::rect(bx, xy, bw * xpK, xh, orc.xpPulse > 0 ? rgba(200, 240, 255) : rgba(110, 190, 255));
	snprintf(buf, sizeof buf, "LV %d", orc.level);
	gfx::text(buf, bx - 8.0f * u, yHp + (bh - 40 * lvScale) * 0.5f, lvScale, rgba(170, 220, 255), ALIGN_RIGHT);
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
			const float is = (PORT_3DS ? 0.64f : 0.80f) * (w / 40.0f);
			gfx::sprite(SHEET_SPRITES, cardIcon(card), x + w * 0.5f, y + h * 0.5f + 2, is, is, 0, ready ? WHITE : rgba(150, 150, 160, 200));
			if (orc.slotCool[s] > 0 && info.cooldown > 0) {
				const float k = clampf(orc.slotCool[s] / info.cooldown, 0, 1);
				gfx::rect(x, y + h * (1 - k), w, h * k, rgba(0, 0, 0, 120));
				snprintf(buf, sizeof buf, "%d", (int)ceilf(orc.slotCool[s]));
				gfx::text(buf, x + w * 0.5f, y + h - 18, 0.32f, COL_DIM, ALIGN_CENTER);
			} else if (orc.slotOn[s] > 0) {
				gfx::rect(x, y + h - 3, w * clampf(orc.slotOn[s] / (info.lasts > 0 ? info.lasts : 1), 0, 1), 3, rgba(150, 230, 255));
			}
		}
		const char* key = PORT_3DS ? (s == 0 ? "L" : "R") : (gStickInUse ? (s == 0 ? "LB" : "RB") : (s == 0 ? "Q" : "E"));
		gfx::text(key, x + 4, y + 3, 0.28f, COL_DIM);
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
	const u32 foundWas = gSave.bossTracksFound;
	u16 winsWas[sizeof gSave.bossWins / sizeof gSave.bossWins[0]];
	memcpy(winsWas, gSave.bossWins, sizeof winsWas);
	gSave.difficulty = DIFF_NORMAL;
	const auto setup = [this]() {
		orchardClearSave();
		gSpawn.s = 12345u;
		start(MODE_ORCHARD);
		orc.bossOrder[0] = BOSS_MELON;
		orc.bossOrder[1] = BOSS_KING;
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

	// The boss, charm and level-up sounds made for 1.2.
	if (audio::available()) {
		const Sfx made[] = {SFX_CLANG, SFX_OPEN, SFX_ROAR, SFX_BOSSIN, SFX_VICTORY, SFX_POP, SFX_LAUGH, SFX_CHUCKLE, SFX_WHOOSH, SFX_LAUNCH, SFX_LEVELUP};
		int missing = 0;
		for (Sfx s : made) missing += !audio::loaded(s);
		snprintf(d, sizeof d, "%d missing", missing);
		check(missing == 0, "Orchard Blast's own sounds load: Deflected!, Open!, roar, intro, victory, pop, laugh, chuckle, whoosh, launch, levelup", d);

		// Boss music: the room's track hands over to a boss track, then comes back.
		const u8 musicWas = gSave.bossMusic, lastWas = gSave.bossTrackLast[BOSS_MELON], lastExWas = gSave.bossTrackLastEx[0];
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
		check(bossTracks == 13 && filesThere == bossTracks, "Every boss track's file is in the game, 13 in all", d);
		check(audio::loaded(SFX_BOSS_BLIP), "Room 25 retro boss blip SFX loads", "");
		check(audio::loaded(SFX_TRAIN_WHISTLE) && audio::loaded(SFX_BUBBLES), "Phase 2 train whistle and Lord Lemon bubbles SFX load", "");
		const int mTrack0 = bossTrackPick(BOSS_DURIAN, 3);
		const int mTrack1 = bossTrackPick(BOSS_LEMON, 3);
		check(bossTrackCount(4) == 4 && mTrack0 >= 0 && mTrack1 >= 0 && mTrack0 != mTrack1, "Milestone Boss tracks unlock from a 4-track pool without repeating back-to-back", "");
		audio::bossMusicEnd();
		for (int i = 0; i < 90; i++) audio::update(kDt);
		snprintf(d, sizeof d, "room %d, now %d", room, audio::currentTrack());
		check(audio::currentTrack() == room, "After the boss, the room's music comes back", d);
		// Real time passes too: the 3DS reads music on a background thread.
		for (int i = 0; i < 300 && (audio::bossMusicOn() || audio::musicTime() < 0); i++) {
			audio::update(kDt);
			audioport::settle();
		}
		snprintf(d, sizeof d, "music at %.1f s", audio::musicTime());
		check(!audio::bossMusicOn() && audio::musicTime() >= 0, "and it fades back up to full volume, playing", d);
		gSave.bossMusic = musicWas;
		gSave.bossTrackLast[BOSS_MELON] = lastWas;
		gSave.bossTrackLastEx[0] = lastExWas;

		// Auto music stays continuous between menus and gameplay (without restarting), unless boss music overrides it.
		const u8 menuTrackWas = gSave.menuTrack, gameTrackWas = gSave.gameTrack;
		gSave.menuTrack = gSave.gameTrack = 0;
		audio::music(MUSIC_MENU);
		for (int i = 0; i < 60; i++) audio::update(kDt);
		const int autoMenuTrack = audio::currentTrack();
		audio::music(MUSIC_PLAY);
		const int autoPlayTrack = audio::currentTrack();
		audio::music(MUSIC_MENU);
		const int autoBackTrack = audio::currentTrack();
		snprintf(d, sizeof d, "menu %d -> play %d -> menu %d", autoMenuTrack, autoPlayTrack, autoBackTrack);
		check(autoMenuTrack >= 0 && autoPlayTrack == autoMenuTrack && autoBackTrack == autoMenuTrack,
		      "Auto music keeps playing without resetting between menu and play", d);
		gSave.menuTrack = menuTrackWas;
		gSave.gameTrack = gameTrackWas;

		// The Shop keeps each boss track locked until defeated, and Auto follows discovery + owned boss track rules.
		const u32 beatenNow = gSave.bossesBeaten, seenNow = gSave.bossTracksSeen, ownedNow = gSave.ownedTracks, coinsNow = gSave.coins;
		const u32 foundNow = gSave.bossTracksFound;
		const u8 bmNow = gSave.bossMusic;
		const int item = fight - kBaseTracks;
		gSave.bossesBeaten = gSave.bossTracksSeen = 0;
		gSave.ownedTracks = 0;
		gSave.bossTracksFound = 0;
		gSave.bossMusic = 1;
		gSave.coins = 5000;
		const int undiscAuto = bossMusicResolve(BOSS_MELON, 3);
		check(undiscAuto >= 0 && trackBoss(undiscAuto) == BOSS_MELON && shopLocked(SHOP_MUSIC, item) && shopBuy(SHOP_MUSIC, item) != BUY_DONE && !shopOwned(SHOP_MUSIC, item) && !shopHasNew(),
		      "Undefeated boss plays undiscovered track in Auto and keeps boss tracks locked until beaten", "");
		u8 unlockedCnt = 0;
		bossUnlockAllTracks(BOSS_MELON, &unlockedCnt);
		gSave.bossesBeaten |= 1u << BOSS_MELON;
		const int noPurchaseFallback = bossMusicResolve(BOSS_MELON, 3);
		check(unlockedCnt == bossTrackCount(BOSS_MELON) && !shopLocked(SHOP_MUSIC, item) && shopHasNew() && noPurchaseFallback < 0,
		      "Defeating boss unlocks all its Shop tracks, and Auto keeps Game Music when zero boss tracks are bought", "");
		const int anyBossItem = bossTrackPick(BOSS_KING, 1) - kBaseTracks;
		gSave.bossTracksFound |= 1u << anyBossItem;
		const bool boughtOk = shopBuy(SHOP_MUSIC, anyBossItem) == BUY_DONE;
		const int ownedAuto = bossMusicResolve(BOSS_MELON, 3);
		check(boughtOk && ownedAuto >= 0 && trackBoss(ownedAuto) >= 0 && shopOwned(SHOP_MUSIC, ownedAuto - kBaseTracks),
		      "Once any boss track is purchased, Auto shuffles purchased boss tracks across all bosses", "");
		shopEquip(SHOP_MUSIC, anyBossItem);
		const u8 bossMusEquipped = gSave.bossMusic;
		check(bossMusEquipped == (u8)(2 + kBaseTracks + anyBossItem), "Equipping a Boss Track in Shop sets Boss Music without overwriting Menu/Game Music", "");
		shopSeenNew();
		check(!shopHasNew(), "Opening the Music tab clears the new mark", "");
		gSave.bossesBeaten = beatenNow;
		gSave.bossTracksSeen = seenNow;
		gSave.bossTracksFound = foundNow;
		gSave.ownedTracks = ownedNow;
		gSave.bossMusic = bmNow;
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
	check(orc.hp == 16 && phase == PHASE_PLAY, "Orchard Blast: a bomb costs 4 HP on Normal and the run goes on", d);
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
	// Coin-purchased rerolls: price is difficulty-independent, starts at 15c and scales +15c per buy.
	const u8 diffForReroll = gSave.difficulty;
	gSave.difficulty = DIFF_INSANE;
	const int pInsane = orcRerollPrice();
	gSave.difficulty = DIFF_EASY;
	const int pEasy = orcRerollPrice();
	check(pInsane == 15 && pEasy == 15 && !orcCanReroll(), "Paid Reroll costs 15 coins regardless of difficulty and requires run coins", "");
	coinsCaught = 3;  // 3 * 5 = 15 run coins
	check(orcCanReroll() && runCoins() == 15, "Run coins unlock coin-purchased Rerolls once free rerolls run out", "");
	orcReroll();
	check(orc.paidRerolls == 1 && orc.coinsSpent == 15 && runCoins() == 0 && orcRerollPrice() == 30 && orc.choosing && orc.offerCount == 3,
	      "Buying a Reroll spends run coins, immediately redeals cards, and steps next price by +15c", "");
	gSave.difficulty = diffForReroll;
	coinsCaught = 0;
	orc.coinsSpent = 0;
	orc.paidRerolls = 0;
	orc.offer[0] = CARD_HEALTH;
	orcPick(0);
	check(orc.maxHp == 25 && orc.upgrade[CARD_HEALTH] == 1 && !orc.choosing && orc.resumeT == 0, "Health adds 5 Max HP, and play resumes right away", "");
	feverT = 4.0f;
	orcGainXp((float)orcXpNeed());
	check(orc.choosing && feverT > 0, "Leveling up during a Fruit Frenzy opens the upgrade screen right away", "");
	orc.offer[0] = CARD_HEALTH;
	orcPick(0);
	feverT = 0;
	for (int i = 0; i < 10; i++) update(idle);
	check(!orchardHeld(), "Play goes on right after picking an upgrade", "");

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
	float sx, sy, sw, sh;
	orcSlotRect(0, &sx, &sy, &sw, &sh);
	snprintf(d, sizeof d, "%.0fx%.0f", sw, sh);
	check(sw == sh && sw >= (PORT_3DS ? 50.0f : 56.0f * uiScale()) && sx >= 0 && sy + sh <= kFieldH,
	      "Active charm slots are enlarged and fit cleanly inside the field", d);

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
	check(kCards[CARD_ARMOR].maxLevel == 255 && kCards[CARD_STEADY].maxLevel == 255, "Upgrades have no level-5 cap (u8 255 ceiling)", "");
	orc.upgrade[CARD_ARMOR] = 7;
	orc.upgrade[CARD_STEADY] = 40;
	const float steadyScale = orcObjScale();
	orc.choosing = true;
	orc.placing = false;
	orc.offerCount = 1;
	orc.offer[0] = CARD_ARMOR;
	orcPick(0);
	check(orc.upgrade[CARD_ARMOR] == 8 && steadyScale >= 0.35f, "Upgrades stack past level 5 while Steady Hands size multiplier soft-caps safely", "");
	orc.luckyPit = true;
	orcOffer();
	check(orc.offerCount == 5, "Lucky Pit shows five cards", "");

	// Rooms: 15 seconds each, scoring 1 point per room finished, then a boss in room 5.
	setup();
	Blade none;
	for (int i = 0; i < (int)(kRoomSeconds * 60) + 5; i++) orcUpdate(none);
	check(orc.room == 2 && !orc.bossRoom && score == 1, "A room lasts 15 seconds and scores 1 point when finished", "");
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
	check(!b.on && orc.room == 6 && strstr(callout, "TRICK") == nullptr, "After the boss, the run moves on to plain ROOM 6 without Trick Fruit text", "");
	// Fruits must resume spawning cleanly in non-boss rooms after a boss defeat (waveTimer was 999 during the boss).
	{
		orc.choosing = false;
		orc.levelsWaiting = 0;
		const float postBossTimer = waveTimer;
		for (auto& o : objs) o = {};
		for (auto& p : pending) p = {};
		for (int i = 0; i < 90; i++) {
			if (orc.choosing) orcPick(0);
			update(none);
		}
		int spawned = 0;
		for (const auto& o : objs)
			if (o.on) spawned++;
		for (const auto& p : pending)
			if (p.on) spawned++;
		snprintf(d, sizeof d, "timer %.2f, spawned %d", (double)postBossTimer, spawned);
		check(!orc.bossRoom && orc.room == 6 && postBossTimer < 2.0f && spawned > 0,
		      "After defeating a boss, non-boss rooms reset waveTimer and spawn fruits again", d);
	}

	// Dev skip helper jumps cleanly to any room (including Room 25 Milestone Boss + buffered level-ups) and non-boss rooms.
	{
		const u8 seenWas = gSave.seenRoom50Cutscene;
		setup();
		gSave.seenRoom50Cutscene = 1;
		orcApplyDevSkip(6, 4);
		const bool room6Ok = !orc.bossRoom && orc.room == 6 && waveTimer < 2.0f && orc.level == 4 && orc.levelsWaiting == 3;
		setup();
		gSave.seenRoom50Cutscene = 0;
		orcApplyDevSkip(25, 3);
		const bool room25Ok = orc.bossRoom && orc.room == 25 && orc.cutscene && orc.level == 3 && orc.levelsWaiting == 2;
		orchardClearSave();
		gSave.seenRoom50Cutscene = seenWas;
		check(room6Ok && room25Ok, "Orchard dev skip cleanly enters non-boss rooms (with waves) and Milestone Boss Room 25 with buffered levels", "");
	}

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
	check(!shot.on, "A single slice defuses a cannonball", "");
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
	check(orchardLevel(DIFF_EASY).startHp == 25 && orchardLevel(DIFF_INSANE).startHp == 12 && orchardLevel(DIFF_HARD).bombDamage == 6, "Difficulty sets HP and bomb damage", "");

	// Seeded boss order, Endless rooms past 10, Room 50 Milestone Bosses, Royal laps, room-start Save/Continue, and boss mechanics.
	{
		u8 ordA[kRegularBossCount], ordB[kRegularBossCount];
		orchardBuildBossOrder(0x12345678u, ordA);
		orchardBuildBossOrder(0x12345678u, ordB);
		bool same = true;
		int mask = 0;
		for (int i = 0; i < kRegularBossCount; i++) {
			same = same && (ordA[i] == ordB[i]);
			mask |= 1 << ordA[i];
		}
		check(same && mask == ((1 << kRegularBossCount) - 1), "Seeded boss order deterministically permutes every regular boss once", "");
		bool royalFirst = false, royalLap2 = false, royal25 = false, royal50 = false;
		const int b0 = orchardBossForRoom(5, 0x12345678u, ordA, &royalFirst);
		const int b4 = orchardBossForRoom(5 + kRegularBossCount * 5 + 5, 0x12345678u, ordA, &royalLap2);
		const int b25 = orchardBossForRoom(25, 0x12345678u, ordA, &royal25);
		const int b50 = orchardBossForRoom(50, 0x12345678u, ordA, &royal50);
		check(!royalFirst && royalLap2, "Endless lap 2 switches to Royal variants on regular boss rooms (Room 30)", "");
		check(b25 >= BOSS_DURIAN && b25 < BOSS_COUNT && !royal25 && b50 >= BOSS_DURIAN && b50 < BOSS_COUNT && royal50,
		      "Room 25 picks a Milestone Boss and Room 50+ brings its Royal variant", "");
		(void)b0; (void)b4;

		const u8 seenCutsceneWas = gSave.seenRoom50Cutscene;
		gSave.seenRoom50Cutscene = 1;
		orchardClearSave();
		gSpawn.s = 999u;
		start(MODE_ORCHARD);
		check(orc.endless && !orc.resumePrompt && orc.room == 1, "Orchard Blast starts in Endless Room 1 by default without variant picker", "");
		phase = PHASE_PLAY;
		phaseT = 0;
		waveTimer = 999;
		orc.room = 10;
		orc.bossRoom = false;
		orc.roomT = kRoomSeconds;
		orcUpdate(none);
		check(orc.room == 11 && !orc.won && phase == PHASE_PLAY, "Orchard Blast continues past Room 10 into Room 11", "");
		orc.room = 14;
		orc.roomT = kRoomSeconds;
		orcUpdate(none);
		check(orc.room == 15 && orc.bossRoom && orc.boss.on, "Room 15 brings in the next seeded boss", "");
		// Boss rooms (both Orchard Blast and Boss Rush) must never spawn natural wave objects.
		for (auto& o : objs) o = {};
		for (auto& p : pending) p = {};
		waveTimer = 0;
		orcWave();
		bool anyNatural = false;
		for (const auto& o : objs) anyNatural |= o.on;
		for (const auto& p : pending) anyNatural |= p.on;
		check(!anyNatural, "Orchard Blast boss rooms spawn zero natural fruit/coins/bombs", "");
		start(MODE_BOSSRUSH);
		phase = PHASE_PLAY;
		m12.rushPicking = false;
		for (auto& o : objs) o = {};
		for (auto& p : pending) p = {};
		waveTimer = 0;
		orcWave();
		for (const auto& o : objs) anyNatural |= o.on;
		for (const auto& p : pending) anyNatural |= p.on;
		check(!anyNatural, "Boss Rush spawns zero natural fruit/coins/bombs outside boss attacks", "");

		start(MODE_ORCHARD);
		phase = PHASE_PLAY;
		orc.room = 29;
		orc.bossRoom = false;
		orc.roomT = kRoomSeconds;
		orcUpdate(none);
		check(orc.room == 30 && orc.bossRoom && orc.boss.on && orc.boss.royal, "Lap 2 brings Royal regular boss variants (Room 30)", "");
		start(MODE_ORCHARD);
		phase = PHASE_PLAY;
		orc.room = 24;
		orc.bossRoom = false;
		queue(0, 160, 0, -280, OBJ_FRUIT, 0);
		launch(pending[0]);
		pending[0].on = false;
		orc.roomT = kRoomSeconds;
		orcUpdate(none);
		bool keptLiveFruit = false;
		for (const auto& o2 : objs)
			if (o2.on && o2.kind == OBJ_FRUIT) keptLiveFruit = true;
		check(orc.room == 25 && orc.bossRoom && keptLiveFruit, "Entering a boss room keeps existing in-flight fruits instead of vanishing them", "");
		const u16 bestEndWas = gSave.endlessBestRoom;
		const u32 endRunsWas = gSave.endlessRuns;
		gSave.endlessBestRoom = 0;
		orc.room = 17;
		commitScore();
		check(gSave.endlessBestRoom == 17 && gSave.endlessRuns == endRunsWas + 1, "Finishing an Orchard Blast run records best room", "");
		gSave.endlessBestRoom = bestEndWas;
		gSave.endlessRuns = endRunsWas;

		// Room-start Save, Continue (resets mid-room state & starts boss music on boss rooms), and Start Over flow.
		orchardClearSave();
		start(MODE_ORCHARD);
		orc.runSeed = gSpawn.s = 0x77771111u;
		orchardBuildBossOrder(orc.runSeed, orc.bossOrder);
		orc.room = 7;
		orc.roomT = 0;
		orc.level = 5;
		orc.xp = 19.0f;
		orc.hp = 18.0f;
		orc.upgrade[CARD_BLADE] = 3;
		orc.slot[0] = CARD_SMOOTHIE;
		score = 6;
		const bool savedOk = orcSaveRun();
		int savedRoom = 0;
		check(savedOk && orchardHasSave(&savedRoom) && savedRoom == 7, "orcSaveRun saves current seed, room, level, XP, and upgrades", "");
		// Mutate mid-room gains before reloading to verify Continue restores the room-start snapshot cleanly.
		orc.roomT = 8.5f;
		orc.level = 9;
		orc.xp = 99.0f;
		orc.hp = 4.0f;
		start(MODE_ORCHARD);
		check(orc.resumePrompt && orc.savedRoom == 7, "Re-entering Orchard Blast with a saved run prompts Continue vs Start Over with exact room", "");
		orchardResumeChoice(true);
		check(!orc.resumePrompt && orc.room == 7 && orc.roomT == 0 && orc.level == 5 && (int)orc.xp == 19 && (int)orc.hp == 18 &&
		      orc.upgrade[CARD_BLADE] == 3 && orc.slot[0] == CARD_SMOOTHIE && orc.runSeed == 0x77771111u && score == 6,
		      "Continue restores room-start seed, score, HP, level/XP, and inventory without keeping mid-room gains", "");
		if (audio::available()) {
			const u8 bossMusWas = gSave.bossMusic;
			const u32 beatTmp = gSave.bossesBeaten, foundTmp = gSave.bossTracksFound;
			gSave.bossesBeaten = 0;
			gSave.bossTracksFound = 0;
			gSave.bossMusic = 1;
			audio::bossMusicEnd();
			for (int i = 0; i < 90; i++) audio::update(kDt);
			orc.room = 5;
			orc.bossRoom = true;
			orc.boss = {};
			orcSaveRun();
			start(MODE_ORCHARD);
			orchardResumeChoice(true);
			for (int i = 0; i < 10; i++) audio::update(kDt);
			check(orc.bossRoom && orc.boss.on && audio::bossMusicOn(), "Continuing onto a boss room spawns the boss and starts boss music", "");
			audio::bossMusicEnd();
			for (int i = 0; i < 90; i++) audio::update(kDt);
			gSave.bossesBeaten = beatTmp;
			gSave.bossTracksFound = foundTmp;
			gSave.bossMusic = bossMusWas;
		}
		start(MODE_ORCHARD);
		orchardResumeChoice(false, 0x22223333u);
		check(!orc.resumePrompt && !orchardHasSave() && orc.room == 1 && orc.level == 1 && orc.xp == 0 &&
		      orc.upgrade[CARD_BLADE] == 0 && orc.slot[0] == CARD_NONE && score == 0,
		      "Start Over clears saved run, resets inventory/XP/level, and restarts at Room 1", "");
		orchardClearSave();

		// Pineapple Guard: keeps guard permanently, lobs tag=2 bomb that slices flip to tag=4, and missed bombs hurt player.
		setup();
		bossStart(BOSS_PINEAPPLE);
		Boss& pb = orc.boss;
		pb.enterT = 0;
		pb.y = pb.baseY;
		check(pb.id == BOSS_PINEAPPLE && pb.left == kBosses[BOSS_PINEAPPLE].slices && pb.guard, "The Pineapple Guard enters with its guard up", "");
		bossFire(SHOT_SPIKE, 120, 200, 0, 0);
		Blade spHit = swipe(100, 200, 140, 200, 0x53000001u);
		bossTest(spHit);
		check(!orc.shots[0].on, "A clean slice clears a pineapple spike", "");
		for (auto& o : objs) o = {};
		for (auto& p : pending) p = {};
		pb.attack = PINE_BOMB;
		pb.attackT = 0;
		pb.volley = 1;
		bossUpdate(kDt);
		bool queuedLob = false;
		for (const auto& p : pending) queuedLob |= (p.on && p.kind == OBJ_BOMB && p.fruit == 2);
		for (auto& p : pending) {
			if (p.on) {
				launch(p);
				p.on = false;
			}
		}
		Obj* lob = nullptr;
		for (auto& o : objs)
			if (o.on && o.kind == OBJ_BOMB && o.tag == 2) lob = &o;
		check(queuedLob && lob != nullptr, "PINE_BOMB queues a reflectable tag=2 lobbed bomb", "");
		addPopup("COMBO 15 +5 XP", 0.0f, kFieldTop, rgba(255, 190, 80), 0.55f);
		const Popup& lp = popups[(popupCursor + kMaxPopups - 1) % kMaxPopups];
		const float lhw = gfx::textWidth("COMBO 15 +5 XP", 0.55f) * 0.5f;
		check(lp.on && lp.x - lhw >= 7.5f && lp.y >= kFieldTop + 24.0f, "Edge popups clamp cleanly inside the field and below the HUD", "");
		if (lob) {
			hitBomb(*lob);
			check(lob->tag == 4 && lob->vy < 0 && pb.guard, "Slicing a Pineapple lob bomb sends it upward (tag=4) toward the guarded boss", "");
			// Shield must not intercept or consume itself on reflectable/returned Pineapple Guard bombs.
			lob->tag = 2;
			lob->vy = 120;
			shield = true;
			hitBomb(*lob);
			check(shield && lob->on && lob->tag == 4, "Shield stays intact while reflecting a Pineapple tag=2 bomb", "");
			hitBomb(*lob);
			check(shield && lob->on && lob->tag == 4, "Shield stays intact when blade grazes a returned tag=4 bomb", "");
			shield = false;
			// Verify missing an unreturned lobbed bomb past killY hurts the player.
			lob->tag = 2;
			lob->x = 160;
			lob->y = killY() + 10;
			lob->vy = 90;
			const float hpBefore = orc.hp;
			updateObjects(kDt);
			check(orc.hp < hpBefore, "Missing a Pineapple lobbed bomb off-screen costs HP", "");
		}

		// Inferno Pepper: embers + Heat Surge speed-up and recovery.
		setup();
		bossStart(BOSS_CHILI);
		Boss& cb = orc.boss;
		cb.enterT = 0;
		cb.y = cb.baseY;
		check(cb.id == BOSS_CHILI && cb.left == kBosses[BOSS_CHILI].slices, "The Inferno Pepper enters with its full slice count", "");
		bossFire(SHOT_EMBER, 150, 190, 0, 0);
		Blade emHit = swipe(130, 190, 170, 190, 0x54000001u);
		bossTest(emHit);
		check(!orc.shots[0].on, "A clean slice clears an ember shot", "");
		cb.attackCycle = 3;
		cb.surgeT = 0;
		bossNextAttack();
		check(cb.surgeT > 0 && (!audio::available() || audio::currentMusicSpeed() > 1.05f), "Inferno Pepper triggers Heat Surge speed-up", "");
		for (int i = 0; i < 400 && cb.surgeT > 0; i++) bossUpdate(kDt);
		check(cb.surgeT == 0 && (!audio::available() || fabsf(audio::currentMusicSpeed() - 1.0f) < 0.05f), "Inferno Pepper returns speed to normal after Heat Surge", "");

		// Milestone Bosses: Dusty Durian (thorns), Lord Lemon (acid), Aggro Apple (swattable & lunging worms).
		setup();
		bossStart(BOSS_DURIAN);
		orc.boss.enterT = 0;
		bossFire(SHOT_THORN, 140, 190, 0, 0);
		Blade thHit = swipe(120, 190, 160, 190, 0x55000001u);
		bossTest(thHit);
		check(orc.boss.id == BOSS_DURIAN && !orc.shots[0].on, "Dusty Durian spawns and its thorns can be sliced cleanly", "");

		setup();
		bossStart(BOSS_LEMON);
		orc.boss.enterT = 0;
		bossFire(SHOT_ACID, 140, 190, 0, 0);
		Blade acHit = swipe(120, 190, 160, 190, 0x56000001u);
		bossTest(acHit);
		check(orc.boss.id == BOSS_LEMON && !orc.shots[0].on, "Lord Lemon spawns and its acid shots can be neutralized", "");

		setup();
		bossStart(BOSS_APPLE);
		Boss& ab = orc.boss;
		ab.enterT = 0;
		ab.y = ab.baseY;
		ab.worms[0].on = true;
		ab.worms[0].ox = ab.radius * 0.54f;
		ab.worms[0].oy = 0;
		ab.worms[0].emergeT = 1.0f;
		ab.worms[0].timer = 2.0f;
		const float hpW0 = orc.hp, xpW0 = orc.xp;
		const float wx = ab.x + ab.worms[0].ox, wy = ab.y + ab.worms[0].oy;
		Blade wSwat = swipe(wx - 10, wy, wx + 10, wy, 0x57000001u);
		bossTest(wSwat);
		check(!ab.worms[0].on && orc.hp == hpW0 && orc.xp == xpW0, "Boss room slices grant 0 XP without Boss Harvest passive", "");
		orc.upgrade[CARD_BOSSHARVEST] = 1;
		ab.worms[2].on = true;
		ab.worms[2].ox = ab.radius * 0.54f;
		ab.worms[2].oy = 0;
		ab.worms[2].emergeT = 1.0f;
		ab.worms[2].timer = 2.0f;
		Blade wSwat2 = swipe(ab.x + ab.worms[2].ox - 10, ab.y, ab.x + ab.worms[2].ox + 10, ab.y, 0x57000002u);
		bossTest(wSwat2);
		check(!ab.worms[2].on && orc.hp == hpW0 && orc.xp > xpW0, "Boss Harvest enables XP gain in boss rooms and worm swat stays Deflect-free", "");
		ab.worms[1].on = true;
		ab.worms[1].emergeT = 1.0f;
		ab.worms[1].timer = kDt * 0.5f;
		bossUpdate(kDt);
		check(!ab.worms[1].on && orc.hp < hpW0, "An unswatted Aggro Apple worm lunges and costs HP", "");

		// New §2c mechanics: Cutting Board diminishing halves->quarters, Lifesteal Tome Blade-scaled heal, Phase 2 Enrage, and Lord Lemon Lemonade+Drain.
		{
			setup();
			orc.bossRoom = false;
			orc.upgrade[CARD_CUTBOARD] = 1;
			queue(0, 160, 0, -260, OBJ_FRUIT, 0);
			launch(pending[0]);
			pending[0].on = false;
			Obj* whole = nullptr;
			for (auto& o2 : objs)
				if (o2.on) { whole = &o2; break; }
			whole->hitCooldown = 0;
			const float x0 = orc.xp;
			sliceFruit(*whole, swipe(whole->x - 18, whole->y, whole->x + 18, whole->y, 0x58000001u));
			const float dx0 = orc.xp - x0;
			int gen1Count = 0;
			Obj* firstHalf = nullptr;
			for (auto& o2 : objs) {
				if (o2.on && o2.cutGen == 1) {
					gen1Count++;
					if (!firstHalf) firstHalf = &o2;
				}
			}
			const float x1 = orc.xp;
			if (firstHalf) sliceFruit(*firstHalf, swipe(firstHalf->x - 14, firstHalf->y, firstHalf->x + 14, firstHalf->y, 0x58000002u));
			const float dx1 = orc.xp - x1;
			int gen2Count = 0;
			Obj* firstQuarter = nullptr;
			for (auto& o2 : objs) {
				if (o2.on && o2.cutGen == 2) {
					gen2Count++;
					if (!firstQuarter) firstQuarter = &o2;
				}
			}
			const float x2 = orc.xp;
			if (firstQuarter) sliceFruit(*firstQuarter, swipe(firstQuarter->x - 12, firstQuarter->y, firstQuarter->x + 12, firstQuarter->y, 0x58000003u));
			const float dx2 = orc.xp - x2;
			check(gen1Count == 2 && gen2Count == 2 && dx0 > dx1 && dx1 > dx2 && dx2 > 0,
			      "Cutting Board splits whole -> halves -> quarters with strictly diminishing extra XP", "");

			setup();
			orc.maxHp = 40;
			orc.hp = 10;
			orc.upgrade[CARD_LIFESTEAL] = 1;
			orc.upgrade[CARD_BLADE] = 0;
			bossStart(BOSS_DURIAN);
			orc.boss.enterT = 0;
			orc.boss.guard = false;
			const float hpBefore0 = orc.hp;
			bossHitSlice(orc.boss.x, orc.boss.y, 1, 0);
			const float heal0 = orc.hp - hpBefore0;
			orc.upgrade[CARD_BLADE] = 4;
			const float hpBefore4 = orc.hp;
			bossHitSlice(orc.boss.x, orc.boss.y, 1, 0);
			const float heal4 = orc.hp - hpBefore4;
			check(heal0 > 0 && heal4 > heal0, "Lifesteal Tome heals HP on clean boss slices and scales up with Blade level", "");

			// Milestone Boss Phase 2 enrage (<=50% HP) + shorter chain + no open laugh + Lord Lemon Lemonade/Drain lifecycle.
			setup();
			orc.hp = orc.maxHp = 50;
			bossStart(BOSS_LEMON);
			Boss& lb = orc.boss;
			lb.enterT = 0;
			lb.y = lb.baseY;
			lb.guard = false;
			lb.dazeT = 0;
			lb.left = (u8)(lb.total / 2 + 1);
			bossHitSlice(lb.x, lb.y, 1, 0);
			const bool enragedAtHalf = lb.enraged && lb.left * 2 <= lb.total;
			lb.dazeT = 0;
			lb.roarT = 0;
			lb.guard = false;
			lb.chain = 0;
			bossNextAttack();
			const bool fasterChain = lb.chain <= 2;
			lb.attack = LEM_LEMONADE;
			lb.attackT = 0;
			for (int i = 0; i < 150; i++) bossUpdate(kDt);
			const bool lemFilled = lb.lemonadeOn && lb.lemStage == 2 && lb.lemLevel >= 0.99f;
			const float hpPreSour = orc.hp;
			Blade sourBlade = swipe(60, kFieldH - 18.0f, 90, kFieldH - 18.0f, 0x59000001u);
			bossTest(sourBlade);
			const bool sourHurt = orc.hp < hpPreSour;
			for (int i = 0; i < 240 && !lb.drainOn; i++) bossUpdate(kDt);
			for (int i = 0; i < 80; i++) bossUpdate(kDt);
			const bool drainAppeared = lb.drainOn && lb.drainRise >= 0.9f;
			Blade cutDrain = swipe(lb.drainX - 10, lb.drainY, lb.drainX + 10, lb.drainY, 0x59000002u);
			bossTest(cutDrain);
			for (int i = 0; i < 120 && lb.lemonadeOn; i++) bossUpdate(kDt);
			check(enragedAtHalf && fasterChain && lemFilled && sourHurt && drainAppeared && !lb.lemonadeOn,
			      "Milestone Phase 2 enrages at <=50% HP and Lord Lemon Lemonade fills, hurts below surface, surfaces Drain, and drains", "");

			// Verify Lord Lemon cooldown balance, floating fruit 3s countdown -> lemon juice explosion, and reduced boss popup spam.
			const float cdEnraged = lb.lemCooldown;
			lb.enraged = false;
			lb.lemonadeOn = true;
			lb.lemStage = 3;
			lb.lemLevel = 0.05f;
			for (int i = 0; i < 20 && lb.lemonadeOn; i++) bossUpdate(kDt);
			const float cdPhase1 = lb.lemCooldown;
			check(cdPhase1 > cdEnraged && cdPhase1 >= 10.0f, "Lord Lemon Lemonade cooldown is longer in Phase 1 than enraged Phase 2", "");

			lb.lemonadeOn = true;
			lb.lemStage = 2;
			lb.lemLevel = 1.0f;
			for (auto& o2 : objs) o2 = {};
			objs[0].on = true;
			objs[0].kind = OBJ_FRUIT;
			objs[0].fruit = 0;
			objs[0].radius = 22.0f;
			objs[0].x = kFieldW * 0.5f;
			objs[0].y = lemonadeSurfaceY(lb) + 2.0f;
			objs[0].vy = 90.0f;
			updateObjects(kDt);
			const bool floated = objs[0].on && objs[0].lemFloat && objs[0].lemTimer <= 3.0f && objs[0].lemTimer >= 2.8f;
			const float hpPreExplode = orc.hp;
			for (int i = 0; i < 190 && objs[0].on; i++) updateObjects(kDt);
			const bool explodedHurt = !objs[0].on && orc.hp < hpPreExplode;
			check(floated && explodedHurt, "Fruit touching active Lemonade floats for 3s then explodes in lemon juice dealing damage", "");

			// Dusty Durian periodic extending/retracting thorns + hazard collision.
			setup();
			orc.hp = orc.maxHp = 50;
			bossStart(BOSS_DURIAN);
			Boss& db = orc.boss;
			db.enterT = 0;
			db.y = db.baseY;
			db.thornCd = 0.02f;
			db.nextAttack = 9.0f;
			for (int i = 0; i < 90 && db.thornExt < 0.99f; i++) bossUpdate(kDt);
			const bool thornsExtended = db.thornStage == 2 && db.thornExt >= 0.99f;
			const float hpPreThorn = orc.hp;
			const float edgeX = db.x + db.radius * 1.25f;
			Blade thornBlade = swipe(edgeX - 8.0f, db.y, edgeX + 8.0f, db.y, 0x5A000001u);
			bossTest(thornBlade);
			const bool thornsHurt = orc.hp < hpPreThorn;
			for (int i = 0; i < 150 && db.thornExt > 0.0f; i++) bossUpdate(kDt);
			check(thornsExtended && thornsHurt && db.thornExt == 0.0f, "Dusty Durian extends thorns, deals damage on contact, and retracts cleanly", "");

			// Distracting boss popups removed while numeric damage (-N) stays visible.
			bool hasSpamPopup = false, hasDamagePopup = false;
			for (const auto& pp : popups) {
				if (!pp.on) continue;
				if (strstr(pp.text, "OPEN") || strstr(pp.text, "SLICED") || strstr(pp.text, "DEFUSED") ||
				    strstr(pp.text, "THORN") || strstr(pp.text, "SPIKE")) hasSpamPopup = true;
				if (pp.text[0] == '-' && pp.text[1] >= '1' && pp.text[1] <= '9') hasDamagePopup = true;
			}
			check(!hasSpamPopup && hasDamagePopup, "Boss clutter popups are removed while concise numeric -HP damage popups stay visible", "");

			// Aggro Apple worms emerge radially outward from burrow holes and swat cleanly.
			setup();
			bossStart(BOSS_APPLE);
			Boss& ab = orc.boss;
			ab.enterT = 0;
			ab.y = ab.baseY;
			ab.attack = APP_WORMS;
			ab.volley = 1;
			for (int i = 0; i < 30; i++) bossUpdate(kDt);
			const float holeDist = hypotf(ab.worms[0].ox, ab.worms[0].oy);
			const float headExt = ab.radius * (0.06f + 0.40f * ab.worms[0].emergeT);
			const bool emergedOut = ab.worms[0].on && ab.worms[0].emergeT > 0.5f && (holeDist + headExt) > holeDist + 4.0f;
			check(emergedOut, "Aggro Apple worms emerge radially outward from body burrow holes", "");
		}

		// Room 25 cutscene gate, save separation (Reset Scores keeps seenRoom50Cutscene; Reset Everything clears it), and Dev Skip.
		const Settings saveSnapshot = gSave;
		gSave.seenRoom50Cutscene = 0;
		orchardClearSave();
		if (audio::available()) audio::music(MUSIC_PLAY);
		orcApplyDevSkip(25, 1);
		if (audio::available()) {
			audio::update(kDt);
			check(orc.cutscene && audio::currentTrack() < 0, "Entering the Room 25 dialogue box stops playing music", "");
		}
		check(orc.cutscene && orc.pendingMilestoneBoss >= BOSS_DURIAN, "Entering Room 25 for the first time triggers the Milestone cutscene", "");
		port::AssetFile* tensionFile = port::assetOpen("audio/music_tension.pcm");
		if (!tensionFile) tensionFile = port::assetOpen("audio/music_tension.ogg");
		check(tensionFile != nullptr, "music_tension audio asset opens cleanly", "");
		if (tensionFile) port::assetClose(tensionFile);
		gSave.bossMusic = 0;
		orc.tensionStarted = true;
		orcStartBossAfterCutscene();
		check(!orc.cutscene && gSave.seenRoom50Cutscene == 1 && orc.boss.on && orc.boss.id >= BOSS_DURIAN && (!audio::available() || audio::currentTrack() >= 0),
		      "Finishing the Room 25 cutscene marks it seen, restores music when Boss Music is off, and starts the Milestone Boss", "");
		saveResetScores();
		const bool keptAfterResetScores = gSave.seenRoom50Cutscene == 1;
		saveDefaults(false, SEC_ALL);
		const bool clearedAfterResetAll = gSave.seenRoom50Cutscene == 0;
		gSave = saveSnapshot;
		check(keptAfterResetScores, "Reset Scores preserves seenRoom50Cutscene", "");
		check(clearedAfterResetAll, "Reset Everything clears seenRoom50Cutscene", "");

		// Normal boss defeat returns natural fruit waves on the following room.
		setup();
		bossStart(BOSS_MELON);
		orc.room = 5;
		orc.bossRoom = true;
		orc.boss.enterT = 0;
		orc.boss.y = orc.boss.baseY;
		orc.boss.guard = false;
		orc.boss.dazeT = 0;
		orc.boss.left = 1;
		bossHitSlice(orc.boss.x, orc.boss.y, 1, 0);
		for (int i = 0; i < 110 && orc.boss.on; i++) bossUpdate(kDt);
		orc.resumeT = 0;
		bool sawWave5 = false;
		for (int i = 0; i < 60; i++) {
			if (orc.choosing) {
				if (orc.placing) orcPlace(0);
				else orcPick(0);
			}
			update(none);
			for (const auto& o2 : objs)
				if (o2.on) sawWave5 = true;
			for (const auto& p2 : pending)
				if (p2.on) sawWave5 = true;
		}
		check(orc.room == 6 && !orc.bossRoom && sawWave5, "Defeating a boss restores natural fruit waves on the next non-boss room", "");

		// Milestone boss final hit: moves to center with X eyes, explodes screen-wide for 5s, defers level-up until finished, then bursts into fruit pieces.
		gSave.seenRoom50Cutscene = 1;
		setup();
		for (auto& h : halves) h = {};
		boom::clear();
		orcApplyDevSkip(25, 1);
		orc.upgrade[CARD_BOSSHARVEST] = 1;  // Boss Harvest enables XP gain in boss room so final hit triggers deferred level-up
		orc.choosing = false;
		orc.levelsWaiting = 0;
		orc.boss.enterT = 0;
		orc.boss.guard = false;
		orc.boss.dazeT = 0;
		orc.boss.hidden = false;
		orc.boss.left = 1;
		orc.boss.x = 60.0f;
		orc.boss.y = kFieldTop + 26.0f;
		orc.xp = (float)(orcXpNeed() - 1);
		bossHitSlice(orc.boss.x, orc.boss.y, 1, 0);
		const bool deferredStart = orc.boss.beaten && orc.boss.on && orc.levelsWaiting >= 1 && !orc.choosing;
		for (int i = 0; i < 180; i++) {
			orcUpdate(none);
			boom::update(kDt);
		}
		const float cx = kFieldW * 0.5f;
		const bool midExploding = orc.boss.beaten && orc.boss.on && fabsf(orc.boss.x - cx) < 12.0f && boom::active() && !orc.choosing;
		for (int i = 0; i < 126 && !orc.choosing; i++) {
			orcUpdate(none);
			boom::update(kDt);
		}
		const bool afterBoom = !orc.boss.on && activeHalves() >= 8 && orc.room == 26 && !orc.bossRoom && orc.choosing && orc.offerCount >= 3;
		check(deferredStart && midExploding && afterBoom,
		      "Milestone boss explodes at center for 5s, holds level-up until explosion ends, then bursts into pieces and advances room", "");
		while (orc.choosing) {
			if (orc.placing) orcPlace(0);
			else orcPick(0);
		}
		orc.resumeT = 0;
		bool sawWave26 = false;
		for (int i = 0; i < 60; i++) {
			update(none);
			for (const auto& o2 : objs)
				if (o2.on) sawWave26 = true;
			for (const auto& p2 : pending)
				if (p2.on) sawWave26 = true;
		}
		check(!orc.choosing && orc.room == 26 && sawWave26, "Fruit spawns normally in Room 26 after Milestone boss finale and deferred level-up", "");
		addPopup("COMBO 10 +5", kFieldW * 0.5f, kFieldTop, rgba(255, 230, 120), 0.7f);
		for (int i = 0; i < 40; i++) updateEffects(kDt);
		bool popupSafe = true;
		for (const auto& pp : popups)
			if (pp.on && pp.y - 12.0f < kFieldTop + 2.0f) popupSafe = false;
		check(popupSafe, "Floating popups stay clear below the HUD bar while rising", "");
		boom::clear();

		orcApplyDevSkip(48, 4);
		check(orc.room == 48 && !orc.bossRoom && orc.level == 4 && orc.levelsWaiting == 3,
		      "orcApplyDevSkip jumps to target room and buffers Level 1->N upgrades", "");
		for (int l = 0; l < 3; l++) {
			orcOffer();
			orc.offer[0] = CARD_HEALTH;
			orcPick(0);
		}
		check(orc.levelsWaiting == 0 && orc.upgrade[CARD_HEALTH] == 3, "Buffered Dev Skip level-ups step sequentially from Level 1 to target", "");

		gSave.seenRoom50Cutscene = seenCutsceneWas;
		orchardClearSave();
	}


	// Timed fights: a bot that plays like a good player beats each boss on each difficulty, three times. It slices
	// shots after a short reaction, reflects Pineapple Guard bombs, swats Aggro Apple worms, blocks charges/spins,
	// cuts fuses, scrubs the Giant Bomb, and slices open bosses 0.4 s after guard drop.
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
						for (auto& p : pending) {
							if (!p.on) continue;
							p.delay -= kDt;
							if (p.delay <= 0) {
								launch(p);
								p.on = false;
							}
						}
						updateObjects(kDt);
						bossUpdate(kDt);
						t += kDt;
						for (auto& o : objs) {
							if (o.on && o.kind == OBJ_BOMB && (o.tag >= 1 && o.tag <= 3) && o.vy > -20 && o.y > kFieldTop + 30 && o.hitCooldown <= 0) {
								Blade hitB = swipe(o.x - 16, o.y, o.x + 16, o.y, ++stroke);
								testSlices(hitB);
							}
						}
						for (auto& s : orc.shots)
							if (s.on && s.age > (s.kind == SHOT_CROWN ? 1.8f : 0.45f)) {
								if (f.lemonadeOn && s.y > kFieldTop + (kFieldH - kFieldTop) * 0.50f) continue;
								const float ddx = s.x - f.x, ddy = s.y - f.y;
								if (ddx * ddx + ddy * ddy < f.radius * f.radius * 2.2f) continue;
								Blade cut = swipe(s.x - 4, s.y - 4, s.x + 4, s.y + 4, ++stroke);
								bossTest(cut);
							}
						if (f.lemonadeOn && f.drainOn && f.lemStage == 2 && f.drainRise >= 0.9f) {
							Blade dc = swipe(f.drainX - 8, f.drainY, f.drainX + 8, f.drainY, ++stroke);
							bossTest(dc);
						}
						if (f.id == BOSS_APPLE && f.dazeT <= 0) {
							for (auto& w : f.worms) {
								if (!w.on || w.emergeT < 0.4f) continue;
								const float len = sqrtf(w.ox * w.ox + w.oy * w.oy) + 0.001f;
								const float ext = f.radius * (0.06f + 0.40f * w.emergeT);
								const float wx = f.x + w.ox + (w.ox / len) * ext, wy = f.y + w.oy + (w.oy / len) * ext;
								Blade sw = swipe(wx - 8, wy, wx + 8, wy, ++stroke);
								bossTest(sw);
							}
						}
						if (f.attack != KING_GIANT) inBomb = false;
						if (f.attack == KING_GIANT && f.giant.on && !f.giant.launched && f.volley == 1) {
							if (!inBomb) sawGiant++;
							inBomb = true;
							scrub += 1;
							const float side = ((int)scrub & 1) ? 1.0f : -1.0f;
							Blade sc = swipe(f.giant.x - side * 3, f.giant.y, f.giant.x + side * 3, f.giant.y, 0x5F000000u);
							bossTest(sc);
						} else if ((f.attack == MEL_CHARGE || f.attack == DUR_SPIN) && f.attackT > 0.8f) {
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
					audio::musicSpeed(1.0f);
				}
				snprintf(d, sizeof d, "%s %s: %.0f s on average (longest %.0f), %.0f damage taken, %d Giant Bombs", kBosses[boss].name, kDiffName[dif], total / 3, longest, hurt / 3, giants);
				check(total > 0 && longest < 400, "Timed fight", d);
				if (dif == DIFF_NORMAL) normalTime[boss] = total / 3;
			}
		}
		check(allBeaten, "A good player can beat every boss on every difficulty", "");
		snprintf(d, sizeof d, "%.0f s", normalTime[BOSS_MELON]);
		check(normalTime[BOSS_MELON] >= 40 && normalTime[BOSS_MELON] <= 75, "The Big Melon takes a good player about 45 to 60 seconds on Normal", d);
		snprintf(d, sizeof d, "%.0f s", normalTime[BOSS_KING]);
		check(normalTime[BOSS_KING] >= 75 && normalTime[BOSS_KING] <= 135, "The Bomb King takes a good player about 90 to 120 seconds on Normal", d);

		// Verify difficulty scaling fairness: no double-scaled Deflected! damage, no Insane 1-shot baseFuse/Giant hits, and capped Endless boost.
		bool scaleOk = true;
		for (int dif = 0; dif < DIFF_COUNT; dif++) {
			gSave.difficulty = (u8)dif;
			setup();
			orc.room = 5;
			bossStart(BOSS_MELON);
			orc.boss.enterT = 0;
			orc.boss.y = orc.boss.baseY;
			orc.hp = orc.maxHp = 50;
			Blade defl = swipe(orc.boss.x - 60, orc.boss.y, orc.boss.x + 60, orc.boss.y, 100u + (u32)dif * 7u);
			bossTest(defl);
			const int gotDefl = (int)(50.0f - orc.hp + 0.5f);
			if (gotDefl != orchardLevel(dif).deflectDamage) scaleOk = false;
		}
		gSave.difficulty = DIFF_INSANE;
		orc.room = 10;
		const int insaneStart = orchardLevel(DIFF_INSANE).startHp;
		const int insaneFuse = bossDamageBase(5);
		const int insaneGiant = bossDamageBase(7);  // kGiantDamage
		check(scaleOk && insaneFuse < insaneStart && insaneGiant < insaneStart,
		      "All 4 difficulties keep Deflect damage un-doubled and prevent full-HP Insane one-shots", "");

		// Finite-HP check: a skilled run with realistic stats survives & beats Big Melon, Bomb King, and Milestone Boss on Normal & Insane.
		bool finiteOk = true;
		static const int kFiniteDiffs[] = {DIFF_NORMAL, DIFF_INSANE};
		static const int kFiniteBosses[] = {BOSS_MELON, BOSS_KING, BOSS_LEMON};
		for (int dif : kFiniteDiffs) {
			for (int bossId : kFiniteBosses) {
				gSave.difficulty = (u8)dif;
				setup();
				const OrchardLevel& lv = orchardLevel(dif);
				orc.room = bossId == BOSS_LEMON ? 25 : (bossId == BOSS_KING ? 10 : 5);
				orc.maxHp = orc.hp = (float)lv.startHp + (bossId == BOSS_LEMON ? 15.0f : (dif == DIFF_INSANE ? 10.0f : 5.0f));
				orc.upgrade[CARD_ARMOR] = (u8)(bossId == BOSS_LEMON ? 2 : (dif == DIFF_INSANE ? 1 : 0));
				orc.upgrade[CARD_BLADE] = (u8)(bossId == BOSS_LEMON ? 2 : 1);
				bossStart(bossId);
				orc.boss.enterT = 0;
				u32 st = 0x64000000u + (u32)(dif * 10000 + bossId * 1000);
				float openFor = 0, fuseT = 0, t = 0;
				while (t < 240 && !orc.boss.beaten && orc.hp > 0) {
					for (auto& p : pending) {
						if (p.on && (p.delay -= kDt) <= 0) {
							launch(p);
							p.on = false;
						}
					}
					updateObjects(kDt);
					bossUpdate(kDt);
					t += kDt;
					for (auto& o2 : objs) {
						if (o2.on && o2.kind == OBJ_BOMB && o2.tag == 1 && o2.vy > -20 && o2.y > kFieldTop + 30 && o2.hitCooldown <= 0) {
							Blade hb = swipe(o2.x - 16, o2.y, o2.x + 16, o2.y, ++st);
							testSlices(hb);
						}
					}
					for (auto& s : orc.shots) {
						if (s.on && s.age > (s.kind == SHOT_CROWN ? 1.8f : 0.45f)) {
							if (orc.boss.lemonadeOn && s.y > kFieldTop + (kFieldH - kFieldTop) * 0.50f) continue;
							const float ddx = s.x - orc.boss.x, ddy = s.y - orc.boss.y;
							if (ddx * ddx + ddy * ddy >= orc.boss.radius * orc.boss.radius * 2.2f) {
								Blade c = swipe(s.x - 4, s.y - 4, s.x + 4, s.y + 4, ++st);
								bossTest(c);
							}
						}
					}
					if (orc.boss.lemonadeOn && orc.boss.drainOn && orc.boss.lemStage == 2 && orc.boss.drainRise >= 0.9f) {
						Blade dc = swipe(orc.boss.drainX - 8, orc.boss.drainY, orc.boss.drainX + 8, orc.boss.drainY, ++st);
						bossTest(dc);
					}
					if (orc.boss.attack == KING_GIANT && orc.boss.giant.on && !orc.boss.giant.launched && orc.boss.volley == 1) {
						static float gs = 0;
						gs += 1;
						const float side = ((int)gs & 1) ? 1.0f : -1.0f;
						Blade sc = swipe(orc.boss.giant.x - side * 3, orc.boss.giant.y, orc.boss.giant.x + side * 3, orc.boss.giant.y, 0x5F000001u);
						bossTest(sc);
					} else if ((orc.boss.attack == MEL_CHARGE || orc.boss.attack == DUR_SPIN) && orc.boss.attackT > 0.8f) {
						Blade bl = swipe(orc.boss.x - 30, orc.boss.y, orc.boss.x + 30, orc.boss.y, ++st);
						bossTest(bl);
					} else if (orc.boss.attack == KING_FUSE) {
						if ((fuseT += kDt) > 0.35f) {
							fuseT = 0;
							Blade fz = swipe(orc.boss.x - 10, orc.boss.y - orc.boss.radius - 8, orc.boss.x + 10, orc.boss.y - orc.boss.radius - 8, ++st);
							bossTest(fz);
						}
					}
					if (!orc.boss.guard && orc.boss.openT > 0 && orc.boss.dazeT <= 0 && !orc.boss.hidden) {
						if ((openFor += kDt) > 0.35f) {
							Blade hit = swipe(orc.boss.x - 30, orc.boss.y, orc.boss.x + 30, orc.boss.y, ++st);
							bossTest(hit);
							openFor = 0;
						}
					} else {
						openFor = 0;
					}
				}
				if (!orc.boss.beaten || orc.hp <= 0) finiteOk = false;
				audio::fuse(false);
			}
		}
		check(finiteOk, "Finite-HP skilled runs survive and beat Big Melon, Bomb King, and Milestone Boss on Normal & Insane", "");
	}

	gSave.difficulty = diffWas;
	gSave.bossesBeaten = beatenWas;
	gSave.bossTracksFound = foundWas;
	memcpy(gSave.bossWins, winsWas, sizeof winsWas);
	audio::fuse(false);
}
#endif
