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

template <typename T, int N>
static constexpr int countOf(const T (&)[N]) {
	return N;
}

struct ShopTabInfo {
	const char* name;
	const ShopItem* items;
	int count;
	u32* owned;       // bit 0 is the free set; item i is bit i + 1
	u8* slot;         // the saved choice this tab sets
	int firstValue;   // what that choice holds for item 0
	bool canTakeOff;  // picking the item in use again leaves nothing in use
};

static const ShopTabInfo kTabs[SHOP_TABS] = {
	{"Themes", kThemes, countOf(kThemes), &gSave.ownedThemes, &gSave.board, kBaseBoards, false},
	{"Trails", kTrails, countOf(kTrails), &gSave.ownedTrails, &gSave.trail, 1, true},
	{"Music", kTracks, countOf(kTracks), &gSave.ownedTracks, &gSave.menuTrack, kBaseTracks + 1, false},
	{"Splats", kSplats, countOf(kSplats), &gSave.ownedExtras, &gSave.splat, 1, true},
};
static_assert(countOf(kThemes) <= kMaxShopItems && countOf(kTrails) <= kMaxShopItems && countOf(kTracks) <= kMaxShopItems && countOf(kSplats) <= kMaxShopItems, "a save has one bit per item on a tab");

int shopCount(int tab) { return tab >= 0 && tab < SHOP_TABS ? kTabs[tab].count : 0; }
const ShopItem& shopItem(int tab, int i) { return kTabs[tab].items[i]; }
const char* shopTabName(int tab) { return tab >= 0 && tab < SHOP_TABS ? kTabs[tab].name : ""; }
bool shopOwned(int tab, int i) { return (*kTabs[tab].owned >> (i + 1)) & 1; }
u8* shopSlot(int tab) { return kTabs[tab].slot; }
int shopValue(int tab, int i) { return kTabs[tab].firstValue + i; }
bool shopCanTakeOff(int tab) { return kTabs[tab].canTakeOff; }

BuyResult shopBuy(int tab, int i) {
	if (shopOwned(tab, i)) return BUY_OWNED;
	u32 price = shopItem(tab, i).price;
	if (gSave.coins < price) return BUY_SHORT;
	gSave.coins -= price;
	*kTabs[tab].owned |= 1u << (i + 1);
	return BUY_DONE;
}

void shopEquip(int tab, int i) {
	if (!shopOwned(tab, i)) return;
	const ShopTabInfo& t = kTabs[tab];
	const u8 value = (u8)shopValue(tab, i);
	*t.slot = t.canTakeOff && *t.slot == value ? 0 : value;
}

bool boardUnlocked(int board) { return board < kBaseBoards || (board < kBoardCount && shopOwned(SHOP_THEMES, board - kBaseBoards)); }
bool trackUnlocked(int track) { return track < kBaseTracks || (track < kTrackCount && shopOwned(SHOP_MUSIC, track - kBaseTracks)); }
bool trailUnlocked(int trail) { return trail == 0 || (trail < kTrailCount && shopOwned(SHOP_TRAILS, trail - 1)); }

void shopCheckSave() {
	if (!boardUnlocked(gSave.board)) gSave.board = 0;
	if (!trailUnlocked(gSave.trail)) gSave.trail = 0;
	if (gSave.menuTrack > 0 && !trackUnlocked(gSave.menuTrack - 1)) gSave.menuTrack = 0;
	if (gSave.gameTrack > 0 && !trackUnlocked(gSave.gameTrack - 1)) gSave.gameTrack = 0;
	if (gSave.splat > countOf(kSplats) || (gSave.splat > 0 && !shopOwned(SHOP_SPLATS, gSave.splat - 1))) gSave.splat = 0;
}

u32 shopSplatPreview(int item) {
	const u32 c = kSplatColours[item % countOf(kSplats)];
	return rgba((u8)(c >> 16), (u8)(c >> 8), (u8)c);
}

u32 shopSplatColour(u32 juice) { return gSave.splat == 0 ? juice : shopSplatPreview(gSave.splat - 1); }
