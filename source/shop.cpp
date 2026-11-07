#include "shop.h"

// Prices rise gently: a run pays about 30-60 coins (5 per coin caught, 1 per 100 points), so the first item is a couple of runs away.
static const ShopItem kThemes[] = {
	{"Night Picnic", 120, "Gingham under the fireflies."},
	{"Beach Towel", 200, "Stripes, sun and a little sand."},
	{"Kitchen Counter", 300, "Terrazzo by the window."},
	{"Snowfall", 400, "Fresh snow and tiny footprints."},
	{"Autumn Leaves", 500, "Planks under falling maple."},
};
static const ShopItem kTrails[] = {
	{"Ink", 80, "A brush stroke that dries as it goes."},
	{"Flame", 150, "Hot at the tip, embers behind."},
	{"Rainbow", 250, "Every colour, all at once."},
	{"Ribbon", 250, "A ribbon that twists as it flies."},
	{"Sparkle", 350, "Leaves a trail of twinkles."},
	{"Comet", 450, "A bright head and a long tail."},
};
static const ShopItem kTracks[] = {
	{"Sunny Side", 200, "A bouncy plucked tune."},
	{"Ember Glow", 300, "Warm and slow, for late games."},
	{"Juice Bar", 250, "Drums, bass and a bouncy hook."},
	{"Pip Pop", 300, "A playful marimba skip."},
	{"Peel Out", 400, "Fast and loud, for a high score."},
};
static_assert(sizeof kThemes / sizeof kThemes[0] == kBoardCount - kBaseBoards, "one theme per Shop board");
static_assert(sizeof kTrails / sizeof kTrails[0] == kTrailCount - 1, "one item per Shop trail");
static_assert(sizeof kTracks / sizeof kTracks[0] == kTrackCount - kBaseTracks, "one item per Shop track");

int shopCount(int tab) {
	switch (tab) {
		case SHOP_THEMES: return kBoardCount - kBaseBoards;
		case SHOP_TRAILS: return kTrailCount - 1;
		case SHOP_MUSIC: return kTrackCount - kBaseTracks;
		default: return 0;
	}
}

const ShopItem& shopItem(int tab, int i) {
	if (tab == SHOP_TRAILS) return kTrails[i];
	if (tab == SHOP_MUSIC) return kTracks[i];
	return kThemes[i];
}

const char* shopTabName(int tab) {
	static const char* const k[SHOP_TABS] = {"Themes", "Trails", "Music"};
	return tab >= 0 && tab < SHOP_TABS ? k[tab] : "";
}

// Bit 0 of each mask is the free set; Shop item i is bit i + 1.
static u32* maskFor(int tab) {
	if (tab == SHOP_TRAILS) return &gSave.ownedTrails;
	if (tab == SHOP_MUSIC) return &gSave.ownedTracks;
	return &gSave.ownedThemes;
}

bool shopOwned(int tab, int i) { return (*maskFor(tab) >> (i + 1)) & 1; }

bool shopEquipped(int tab, int i) {
	switch (tab) {
		case SHOP_THEMES: return gSave.board == kBaseBoards + i;
		case SHOP_TRAILS: return gSave.trail == i + 1;
		case SHOP_MUSIC: return gSave.menuTrack == kBaseTracks + i + 1;
		default: return false;
	}
}

BuyResult shopBuy(int tab, int i) {
	if (shopOwned(tab, i)) return BUY_OWNED;
	u32 price = shopItem(tab, i).price;
	if (gSave.coins < price) return BUY_SHORT;
	gSave.coins -= price;
	*maskFor(tab) |= 1u << (i + 1);
	return BUY_DONE;
}

void shopEquip(int tab, int i) {
	if (!shopOwned(tab, i)) return;
	switch (tab) {
		case SHOP_THEMES: gSave.board = (u8)(kBaseBoards + i); break;
		// Picking the trail you already wear takes it off again.
		case SHOP_TRAILS: gSave.trail = gSave.trail == i + 1 ? 0 : (u8)(i + 1); break;
		case SHOP_MUSIC: gSave.menuTrack = (u8)(kBaseTracks + i + 1); break;
		default: break;
	}
}

bool boardUnlocked(int board) { return board < kBaseBoards || (board < kBoardCount && shopOwned(SHOP_THEMES, board - kBaseBoards)); }
bool trackUnlocked(int track) { return track < kBaseTracks || (track < kTrackCount && shopOwned(SHOP_MUSIC, track - kBaseTracks)); }
bool trailUnlocked(int trail) { return trail == 0 || (trail < kTrailCount && shopOwned(SHOP_TRAILS, trail - 1)); }

void shopCheckSave() {
	if (!boardUnlocked(gSave.board)) gSave.board = 0;
	if (!trailUnlocked(gSave.trail)) gSave.trail = 0;
	if (gSave.menuTrack > 0 && !trackUnlocked(gSave.menuTrack - 1)) gSave.menuTrack = 0;
	if (gSave.gameTrack > 0 && !trackUnlocked(gSave.gameTrack - 1)) gSave.gameTrack = 0;
}
