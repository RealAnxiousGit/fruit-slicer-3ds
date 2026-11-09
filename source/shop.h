// The Shop: cosmetics and music, bought with coins earned by playing.
// What is on sale lives in shop_items.inc: a new item is one line there. A new tab is one line in kTabs.
#pragma once
#include "common.h"

enum ShopTab { SHOP_THEMES, SHOP_TRAILS, SHOP_MUSIC, SHOP_SPLATS, SHOP_TABS };
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
void shopEquip(int tab, int i);     // puts the item in use; on a tab that allows it, a second go takes it off

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
