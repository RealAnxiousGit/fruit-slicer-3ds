// The Shop: cosmetic themes, blade trails and music, bought with coins earned by playing.
#pragma once
#include "common.h"

enum ShopTab { SHOP_THEMES, SHOP_TRAILS, SHOP_MUSIC, SHOP_TABS };
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
bool shopEquipped(int tab, int i);
BuyResult shopBuy(int tab, int i);  // takes the coins and marks it owned; the caller saves
void shopEquip(int tab, int i);     // themes set the board, trails the trail, music the menu track

// Options lists skip what hasn't been bought yet.
bool boardUnlocked(int board);
bool trackUnlocked(int track);  // 0-based track
bool trailUnlocked(int trail);  // 0 is always unlocked
void shopCheckSave();           // puts locked picks back to free ones after a load
