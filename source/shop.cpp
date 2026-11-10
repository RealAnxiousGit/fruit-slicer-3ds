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
	if (gSave.skin > countOf(kSkins) || (gSave.skin > 0 && !shopOwned(SHOP_SKINS, gSave.skin - 1))) gSave.skin = 0;
}

const m3d::Skin& shopSkinLook(int item) { return kSkinLooks[item % countOf(kSkins)]; }

u32 shopSkinTint(int item) {
	const u32 c = kSkinColours[item % countOf(kSkins)];
	return rgba((u8)(c >> 16), (u8)(c >> 8), (u8)c, (u8)(shopSkinLook(item).alpha * 255));
}

const m3d::Skin* shopWornSkin() { return gSave.skin == 0 ? nullptr : &shopSkinLook(gSave.skin - 1); }
u32 shopWornSkinTint() { return gSave.skin == 0 ? WHITE : shopSkinTint(gSave.skin - 1); }

u32 shopSplatPreview(int item) {
	const u32 c = kSplatColours[item % countOf(kSplats)];
	return rgba((u8)(c >> 16), (u8)(c >> 8), (u8)c);
}

u32 shopSplatColour(u32 juice) { return gSave.splat == 0 ? juice : shopSplatPreview(gSave.splat - 1); }
