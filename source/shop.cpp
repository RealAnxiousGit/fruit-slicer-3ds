#include <math.h>

#include "shop.h"

#include "render.h"

// Prices rise gently: a run pays about 30-60 coins (5 per coin caught, 1 per 100 points), so the first item is a couple of runs away.
static const ShopItem kThemes[] = {
#define SHOP_THEME(name, price, line, ...) {name, price, line},
#include "shop_items.inc"
};
static const ShopItem kTrails[] = {
#define SHOP_TRAIL(name, price, line, ...) {name, price, line},
#include "shop_items.inc"
};
static const ShopItem kTracks[] = {
#define SHOP_TRACK(name, price, line, ...) {name, price, line},
#include "shop_items.inc"
};
static const ShopItem kSplats[] = {
#define SHOP_SPLAT(name, price, line, ...) {name, price, line},
#include "shop_items.inc"
};
static const u32 kSplatColours[] = {
#define SHOP_SPLAT(name, price, line, colour) colour,
#include "shop_items.inc"
};
static const ShopItem kSkins[] = {
#define SHOP_SKIN(name, price, line, ...) {name, price, line},
#include "shop_items.inc"
};
static const m3d::Skin kSkinLooks[] = {
#define SHOP_SKIN(name, price, line, recolour, colour, bright, opacity, shine)                                         \
	{recolour / 100.0f, ((colour >> 16) & 255) * bright / 25500.0f, ((colour >> 8) & 255) * bright / 25500.0f,         \
	 (colour & 255) * bright / 25500.0f, opacity / 100.0f, shine / 100.0f},
#include "shop_items.inc"
};
static const u32 kSkinColours[] = {
#define SHOP_SKIN(name, price, line, recolour, colour, bright, opacity, shine) colour,
#include "shop_items.inc"
};

template <typename T, int N>
static constexpr int countOf(const T (&)[N]) {
	return N;
}

static constexpr int kFirstSkinBit = 17;

struct ShopTabInfo {
	const char* name;
	const ShopItem* items;
	int count;
	u32* owned;       // the save's bits for this tab
	int firstBit;     // the bit of item 0
	u8* slot;         // the saved choice this tab sets
	int firstValue;   // what that choice holds for item 0
	bool canTakeOff;  // picking the item in use again leaves nothing in use
};

static const ShopTabInfo kTabs[SHOP_TABS] = {
	{"Themes", kThemes, countOf(kThemes), &gSave.ownedThemes, 1, &gSave.board, kBaseBoards, false},
	{"Trails", kTrails, countOf(kTrails), &gSave.ownedTrails, 1, &gSave.trail, 1, true},
	{"Music", kTracks, countOf(kTracks), &gSave.ownedTracks, 1, &gSave.menuTrack, kBaseTracks + 1, false},
	{"Splats", kSplats, countOf(kSplats), &gSave.ownedExtras, 1, &gSave.splat, 1, true},
	{"Skins", kSkins, countOf(kSkins), &gSave.ownedExtras, kFirstSkinBit, &gSave.skin, 1, true},
};
static_assert(countOf(kSplats) < kFirstSkinBit && countOf(kSkins) + kFirstSkinBit <= 32, "splats and skins share one word of the save");
static_assert(countOf(kThemes) <= kMaxShopItems && countOf(kTrails) <= kMaxShopItems && countOf(kTracks) <= kMaxShopItems && countOf(kSplats) <= kMaxShopItems, "a save has one bit per item on a tab");

int shopCount(int tab) { return tab >= 0 && tab < SHOP_TABS ? kTabs[tab].count : 0; }
const ShopItem& shopItem(int tab, int i) { return kTabs[tab].items[i]; }
const char* shopTabName(int tab) { return tab >= 0 && tab < SHOP_TABS ? kTabs[tab].name : ""; }
bool shopOwned(int tab, int i) { return (*kTabs[tab].owned >> (kTabs[tab].firstBit + i)) & 1; }
u8* shopSlot(int tab) { return kTabs[tab].slot; }
int shopValue(int tab, int i) { return kTabs[tab].firstValue + i; }
bool shopCanTakeOff(int tab) { return kTabs[tab].canTakeOff; }

BuyResult shopBuy(int tab, int i) {
	if (shopOwned(tab, i)) return BUY_OWNED;
	if (shopLocked(tab, i)) return BUY_SHORT;
	u32 price = shopItem(tab, i).price;
	if (gSave.coins < price) return BUY_SHORT;
	gSave.coins -= price;
	*kTabs[tab].owned |= 1u << (kTabs[tab].firstBit + i);
	return BUY_DONE;
}

void shopEquip(int tab, int i) {
	if (!shopOwned(tab, i)) return;
	const ShopTabInfo& t = kTabs[tab];
	const u8 value = (u8)shopValue(tab, i);
	if (tab == SHOP_MUSIC && trackBoss(kBaseTracks + i) >= 0) {
		// Boss tracks equip to Boss Music instead of Menu/Game Music.
		gSave.bossMusic = (u8)(2 + kBaseTracks + i);
		return;
	}
	*t.slot = t.canTakeOff && *t.slot == value ? 0 : value;
	if (tab == SHOP_MUSIC) gSave.gameTrack = *t.slot;  // a song from the Shop plays in the menus and in games
}

bool boardUnlocked(int board) { return board < kBaseBoards || (board < kBoardCount && shopOwned(SHOP_THEMES, board - kBaseBoards)); }
bool trackUnlocked(int track) { return track < kBaseTracks || (track < kTrackCount && shopOwned(SHOP_MUSIC, track - kBaseTracks)); }
bool trailUnlocked(int trail) { return trail == 0 || (trail < kTrailCount && shopOwned(SHOP_TRAILS, trail - 1)); }

void shopCheckSave() {
	if (!boardUnlocked(gSave.board)) gSave.board = 0;
	if (!trailUnlocked(gSave.trail)) gSave.trail = 0;
	// Built-in Boss Tracks only belong to Boss Music, never Menu/Game Music.
	if (gSave.menuTrack > 0 && gSave.menuTrack <= kTrackCount && (!trackUnlocked(gSave.menuTrack - 1) || trackBoss(gSave.menuTrack - 1) >= 0)) gSave.menuTrack = 0;
	if (gSave.gameTrack > 0 && gSave.gameTrack <= kTrackCount && (!trackUnlocked(gSave.gameTrack - 1) || trackBoss(gSave.gameTrack - 1) >= 0)) gSave.gameTrack = 0;
	if (gSave.bossMusic >= 2 && gSave.bossMusic < 2 + kTrackCount) {
		const int t = gSave.bossMusic - 2;
		if (trackBoss(t) < 0 || !trackUnlocked(t)) gSave.bossMusic = 1;
	}
	if (gSave.splat > countOf(kSplats) || (gSave.splat > 0 && !shopOwned(SHOP_SPLATS, gSave.splat - 1))) gSave.splat = 0;
	if (gSave.skin > countOf(kSkins) || (gSave.skin > 0 && !shopOwned(SHOP_SKINS, gSave.skin - 1))) gSave.skin = 0;
}

static float sHoloPhase = 0.0f;
void shopTick(float dt) { sHoloPhase += dt * 2.8f; }
float shopHoloPhase() { return sHoloPhase; }

static void holoRgb(float* r, float* g, float* b) {
	*r = 0.58f + 0.42f * sinf(sHoloPhase);
	*g = 0.58f + 0.42f * sinf(sHoloPhase + 2.0944f);
	*b = 0.58f + 0.42f * sinf(sHoloPhase + 4.1888f);
}

const m3d::Skin& shopSkinLook(int item) {
	static m3d::Skin sHoloSkin;
	const int idx = item % countOf(kSkins);
	if (idx == 2) {
		sHoloSkin = kSkinLooks[idx];
		float r, g, b;
		holoRgb(&r, &g, &b);
		sHoloSkin.r = r * 1.25f;
		sHoloSkin.g = g * 1.25f;
		sHoloSkin.b = b * 1.25f;
		return sHoloSkin;
	}
	return kSkinLooks[idx];
}

u32 shopSkinTint(int item) {
	const int idx = item % countOf(kSkins);
	if (idx == 2) {
		float r, g, b;
		holoRgb(&r, &g, &b);
		return rgba((u8)(r * 255), (u8)(g * 255), (u8)(b * 255), 255);
	}
	const u32 c = kSkinColours[idx];
	return rgba((u8)(c >> 16), (u8)(c >> 8), (u8)c, (u8)(shopSkinLook(item).alpha * 255));
}

const m3d::Skin* shopWornSkin() { return gSave.skin == 0 ? nullptr : &shopSkinLook(gSave.skin - 1); }
u32 shopWornSkinTint() { return gSave.skin == 0 ? WHITE : shopSkinTint(gSave.skin - 1); }

u32 shopSplatPreview(int item) {
	const u32 c = kSplatColours[item % countOf(kSplats)];
	return rgba((u8)(c >> 16), (u8)(c >> 8), (u8)c);
}

u32 shopSplatColour(u32 juice) { return gSave.splat == 0 ? juice : shopSplatPreview(gSave.splat - 1); }

static const s8 kTrackBossOf[] = {
#define SHOP_TRACK(...) -1,
#define BOSS_TRACK(name, price, line, file, bpm, boss) boss,
#include "shop_items.inc"
};
static_assert(countOf(kTrackBossOf) == countOf(kTracks), "a boss (or -1) for every Shop song");

int trackBoss(int track) {
	const int i = track - kBaseTracks;
	return i >= 0 && i < countOf(kTrackBossOf) ? kTrackBossOf[i] : -1;
}

bool shopLocked(int tab, int i) {
	if (tab != SHOP_MUSIC) return false;
	const int boss = trackBoss(kBaseTracks + i);
	return boss >= 0 && !(gSave.bossTracksFound >> i & 1) && !shopOwned(SHOP_MUSIC, i);
}

int bossTrackCount(int boss) {
	int n = 0;
	for (int t = 0; t < kTrackCount; t++) n += trackBoss(t) == boss;
	return n;
}

int bossTrackPick(int boss, u32 roll) {
	const int eff = boss >= 4 ? 4 : boss;
	if (eff < 0 || eff >= 5) return -1;
	int pool[kTrackCount], n = 0;
	u8& lastSlot = eff < 4 ? gSave.bossTrackLast[eff] : gSave.bossTrackLastEx[eff - 4];
	const int last = lastSlot - 1;
	for (int t = 0; t < kTrackCount; t++)
		if (trackBoss(t) == eff && (t != last || bossTrackCount(eff) == 1)) pool[n++] = t;
	if (n == 0) return -1;
	const int t = pool[roll % n];
	lastSlot = (u8)(t + 1);
	return t;
}

static int bossTrackGroup(int boss) {
	const int grp = boss >= 4 ? 4 : boss;
	if (grp < 0 || grp >= 5) return -1;
	return bossTrackCount(grp) > 0 ? grp : (boss % 2);
}

void bossUnlockAllTracks(int boss, u8* newCountOut) {
	const int grp = bossTrackGroup(boss);
	if (grp < 0) return;
	for (int t = kBaseTracks; t < kTrackCount; t++) {
		if (trackBoss(t) != grp) continue;
		const int item = t - kBaseTracks;
		if (item >= 0 && item < kMaxShopItems) {
			const u32 bit = 1u << item;
			if (!(gSave.bossTracksFound & bit)) {
				gSave.bossTracksFound |= bit;
				if (newCountOut) (*newCountOut)++;
			}
		}
	}
}

int bossMusicResolve(int boss, u32 roll) {
	if (gSave.bossMusic == 0) return -1;
	if (gSave.bossMusic >= 2) {
		const int chosen = gSave.bossMusic - 2;
		if (chosen >= kTrackCount) return chosen;  // custom song: checked by audio::bossMusic / audio::trackCount
		if (trackBoss(chosen) >= 0 && trackUnlocked(chosen)) return chosen;
	}
	const int grp = bossTrackGroup(boss);
	if (grp < 0) return -1;
	// Undiscovered boss track for this boss (until beaten & all its tracks unlocked in the Shop).
	int undisc[kTrackCount], nu = 0;
	for (int t = kBaseTracks; t < kTrackCount; t++) {
		if (trackBoss(t) != grp) continue;
		const int item = t - kBaseTracks;
		if (!(gSave.bossTracksFound >> item & 1) && !trackUnlocked(t)) undisc[nu++] = t;
	}
	u8& lastSlot = grp < 4 ? gSave.bossTrackLast[grp] : gSave.bossTrackLastEx[grp - 4];
	const int last = lastSlot - 1;
	if (nu > 0) {
		int avail[kTrackCount], na = 0;
		for (int i = 0; i < nu; i++)
			if (undisc[i] != last || nu == 1) avail[na++] = undisc[i];
		const int t = avail[roll % na];
		lastSlot = (u8)(t + 1);
		return t;
	}
	// If this boss still hasn't been beaten, let an unlocked/already-found track for that boss group play if needed;
	// once defeated, Auto only shuffles purchased boss tracks across all bosses (else keeps Game Music).
	const bool bossBeatenBefore = boss >= 0 && boss < 16 && ((gSave.bossesBeaten >> boss) & 1);
	if (!bossBeatenBefore) {
		const int t = bossTrackPick(grp, roll);
		if (t >= 0) return t;
	}
	int owned[kTrackCount], no = 0;
	for (int t = kBaseTracks; t < kTrackCount; t++) {
		if (trackBoss(t) >= 0 && shopOwned(SHOP_MUSIC, t - kBaseTracks)) owned[no++] = t;
	}
	if (no == 0) return -1;  // no purchased boss tracks yet: keep playing Game Music
	int avail[kTrackCount], na = 0;
	for (int i = 0; i < no; i++)
		if (owned[i] != last || no == 1) avail[na++] = owned[i];
	const int t = avail[roll % na];
	lastSlot = (u8)(t + 1);
	return t;
}

bool shopHasNew() {
	return (gSave.bossTracksFound & ~gSave.bossTracksSeen) != 0;
}

void shopSeenNew() { gSave.bossTracksSeen |= gSave.bossTracksFound; }
