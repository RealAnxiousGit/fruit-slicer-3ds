// The Shop: cosmetics and music, bought with coins earned by playing.
// What is on sale lives in shop_items.inc: a new item is one line there. A new tab is one line in kTabs.
#pragma once
#include "common.h"
#include "model3d.h"

enum ShopTab { SHOP_THEMES, SHOP_TRAILS, SHOP_MUSIC, SHOP_SPLATS, SHOP_SKINS, SHOP_TABS };
enum BuyResult { BUY_DONE, BUY_OWNED, BUY_SHORT };

struct ShopItem {
	const char* name;
	u16 price;
	const char* line;  // one short line for the preview
};

int shopCount(int tab);
const ShopItem& shopItem(int tab, int i);
const char* shopTabName(int tab);
bool shopOwned(int tab, int i);
BuyResult shopBuy(int tab, int i);  // takes the coins and marks it owned; the caller saves
void shopEquip(int tab, int i);     // puts the item in use (a song: as Menu and Game Music); on a tab that allows it, a second go takes it off

// The saved choice a tab controls, what that choice is for item i, and whether the tab can have nothing in use.
u8* shopSlot(int tab);
int shopValue(int tab, int i);
bool shopCanTakeOff(int tab);

// Options lists skip what hasn't been bought yet.
bool boardUnlocked(int board);
bool trackUnlocked(int track);  // 0-based track
bool trailUnlocked(int trail);  // 0 is always unlocked
void shopCheckSave();           // puts locked picks back to free ones after a load

u32 shopSplatColour(u32 juice);  // the colour a fruit's splat lands in, with the Shop's choice applied
u32 shopSplatPreview(int item);  // the colour of a splat on sale

// Fruit skins: how a 3D fruit is recoloured, and the tint for a 2D sticker. nullptr and WHITE mean no skin.
const m3d::Skin& shopSkinLook(int item);
u32 shopSkinTint(int item);
const m3d::Skin* shopWornSkin();
u32 shopWornSkinTint();
void shopTick(float dt);
float shopHoloPhase();

// Boss tracks: Shop songs tied to a boss. Each stays locked, with no price and no preview, until its boss is beaten.
int trackBoss(int track);              // the boss a 0-based track belongs to, or -1
bool shopLocked(int tab, int i);       // a boss track whose boss hasn't been beaten yet
int bossTrackCount(int boss);          // how many tracks that boss has
int bossTrackPick(int boss, u32 roll); // one of the boss's tracks at random, never the one heard last time; -1 if it has none
int bossMusicResolve(int boss, u32 roll); // effective track for Boss Music setting (0 Off, 1 Auto, >=2 chosen), -1 for Game Music
void bossUnlockAllTracks(int boss, u8* newCountOut = nullptr);  // defeating a boss unlocks all of its tracks in Shop
bool shopHasNew();                     // a beaten boss's tracks haven't been looked at in the Shop yet
void shopSeenNew();                    // the Music tab was opened: they have been now
