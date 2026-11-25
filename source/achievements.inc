// 32 Achievements, Wide-Banner Grid, 3DS Top-Screen Zoom Showcase,
// Silent 2.0.0 Retroactive Unlock, and Top-of-Screen Wide-Banner Toast (Milestone C).
// Included from source/main.cpp after topPanel / drawPlank / drawHeader / fitText / shopOwned.

enum AchId {
	ACH_OOH_SHINY = 0,
	ACH_TRANSACTION_COMPLETE,
	ACH_FRUIT_JUICE,
	ACH_WHAT_A_MESS,
	ACH_NEW_COAT_OF_PAINT,
	ACH_A_NEW_FOE,
	ACH_LETS_PARTY,
	ACH_QUARTER_MILESTONE,
	ACH_INSANE_JUGGLER,
	ACH_BIG_SPENDER,
	ACH_SELL_OUT,
	ACH_CENTURY_MILESTONE,
	ACH_COMPLETIONIST,
	ACH_MIX_IT_UP,
	ACH_PINATA_PARTY,
	ACH_REROLL_ADDICT,
	ACH_CLOSE_CALL,
	ACH_COMBO_MASTER,
	ACH_CLASSIC_LEGEND,
	ACH_ARCADE_ACE,
	ACH_RHYTHM_MASTER,
	ACH_ZEN_SERENITY,
	ACH_MASTER_CHEF,
	ACH_DEADEYE,
	ACH_ORCHARD_SURVIVOR,
	ACH_MIXOLOGIST,
	ACH_DEDICATED_SLICER,
	ACH_FEELIN_LUCKY,
	ACH_MILESTONE_BREAKER,
	ACH_BOSS_RUSH_CHAMPION,
	ACH_UNTOUCHABLE,
	ACH_FULL_ARSENAL,
	ACH_COUNT
};

enum AchScope : u8 { ACH_ALL = 0, ACH_3DS_ONLY = 1, ACH_UNIV_ONLY = 2 };

struct AchDef {
	const char* title;
	const char* desc;
	u16 coins;
	AchScope scope;
	int sprIdx;
};

static const AchDef kAchievements[ACH_COUNT] = {
	{"Ooh, Shiny!", "Collect your first coin.", 15, ACH_ALL, sprites_ach_0_idx},
	{"Transaction Complete!", "Buy an item from the Shop.", 15, ACH_ALL, sprites_ach_1_idx},
	{"Fruit Juice", "Slice 100 fruits in total.", 20, ACH_ALL, sprites_ach_2_idx},
	{"What a Mess!", "Slice 1,000 fruits in total.", 40, ACH_ALL, sprites_ach_3_idx},
	{"A New Coat of Paint", "Enable your first mod.", 20, ACH_ALL, sprites_ach_4_idx},
	{"A New Foe!", "Challenge a StreetPass player.", 30, ACH_3DS_ONLY, sprites_ach_5_idx},
	{"Let's Party!", "Click the main menu logo.", 15, ACH_UNIV_ONLY, sprites_ach_6_idx},
	{"Quarter of a Milestone", "Reach Room 25 in Orchard Blast.", 50, ACH_ALL, sprites_ach_7_idx},
	{"Insane Juggler", "Keep 10 fruits airborne in Juggle.", 50, ACH_ALL, sprites_ach_8_idx},
	{"Big Spender", "Spend 10,000 coins in the Shop.", 75, ACH_ALL, sprites_ach_9_idx},
	{"Sell Out", "Buy everything in the Shop.", 100, ACH_ALL, sprites_ach_10_idx},
	{"A Century Milestone!", "Reach Room 100 in Orchard Blast.", 100, ACH_ALL, sprites_ach_11_idx},
	{"Completionist", "Unlock every other achievement.", 100, ACH_ALL, sprites_ach_12_idx},
	{"Mix It Up", "Play a run with the Mixer on.", 15, ACH_ALL, sprites_ach_13_idx},
	{"Pinata Party", "Burst a rare Pinata fruit.", 20, ACH_ALL, sprites_ach_14_idx},
	{"Reroll Addict", "Reroll 5 times in one Orchard run.", 25, ACH_ALL, sprites_ach_15_idx},
	{"Close Call", "Survive via Second Wind once.", 25, ACH_ALL, sprites_ach_16_idx},
	{"Combo Master", "Land a 5+ fruit combo.", 25, ACH_ALL, sprites_ach_17_idx},
	{"Classic Legend", "Score 250 in Classic.", 40, ACH_ALL, sprites_ach_18_idx},
	{"Arcade Ace", "Score 200 in Arcade.", 40, ACH_ALL, sprites_ach_19_idx},
	{"Rhythm Master", "Hit 95%+ in Beat Slice.", 50, ACH_ALL, sprites_ach_20_idx},
	{"Zen Serenity", "Score 100 in Zen.", 30, ACH_ALL, sprites_ach_21_idx},
	{"Master Chef", "Complete 10 orders in Recipe.", 45, ACH_ALL, sprites_ach_22_idx},
	{"Deadeye", "Score 50 in Sharpshooter.", 35, ACH_ALL, sprites_ach_23_idx},
	{"Orchard Survivor", "Defeat your first Orchard boss.", 35, ACH_ALL, sprites_ach_24_idx},
	{"Mixologist", "Win a run with 3+ Mixer rules.", 50, ACH_ALL, sprites_ach_25_idx},
	{"Dedicated Slicer", "Reach a 7-day Daily Fruit streak.", 50, ACH_ALL, sprites_ach_26_idx},
	{"Feelin' Lucky", "Win a Mythic charm from a boss.", 75, ACH_ALL, sprites_ach_27_idx},
	{"Milestone Breaker", "Beat a Room 25+ Milestone Boss.", 75, ACH_ALL, sprites_ach_28_idx},
	{"Boss Rush Champion", "Clear all 5 bosses in Boss Rush.", 75, ACH_ALL, sprites_ach_29_idx},
	{"Untouchable", "Beat any boss without taking damage.", 85, ACH_ALL, sprites_ach_30_idx},
	{"Full Arsenal", "Stack 6+ upgrades in one Orchard run.", 60, ACH_ALL, sprites_ach_31_idx},
};

static bool achInPlatform(int id) {
	if (id < 0 || id >= ACH_COUNT) return false;
	const AchScope sc = kAchievements[id].scope;
#if PORT_3DS
	return sc != ACH_UNIV_ONLY;
#else
	return sc != ACH_3DS_ONLY;
#endif
}

static bool achHas(int id) {
	if (id < 0 || id >= ACH_COUNT) return false;
	return (gSave.achievements[id / 64] & (1ULL << (id % 64))) != 0;
}

static bool achShopAnyOwned() {
	for (int tab = 0; tab < SHOP_TABS; tab++) {
		const int cnt = shopCount(tab);
		for (int i = 0; i < cnt; i++) {
			if (shopOwned(tab, i)) return true;
		}
	}
	return false;
}

static bool achShopAllOwned() {
	for (int tab = 0; tab < SHOP_TABS; tab++) {
		const int cnt = shopCount(tab);
		for (int i = 0; i < cnt; i++) {
			if (!shopOwned(tab, i)) return false;
		}
	}
	return true;
}

static int achCountPlatform(int* totalOut = nullptr) {
	int unlocked = 0, total = 0;
	for (int i = 0; i < ACH_COUNT; i++) {
		if (!achInPlatform(i)) continue;
		total++;
		if (achHas(i)) unlocked++;
	}
	if (totalOut) *totalOut = total;
	return unlocked;
}

// Toast queue & state
static constexpr int kAchQueueCap = 16;
static int sAchQueue[kAchQueueCap];
static int sAchQHead = 0, sAchQTail = 0;
static int sAchToastId = -1;
static int sAchToastPhase = 0;  // 0 idle, 1 slide-in, 2 hold, 3 slide-out
static float sAchToastT = 0.0f;
static constexpr float kAchSlideIn = 0.55f;
static constexpr float kAchHold = 4.50f;
static constexpr float kAchSlideOut = 0.55f;

static bool sAchAllowInLab = false;  // explicitly enabled only inside Lab's achievement unit tests

static void achPushToast(int id) {
	const int nextTail = (sAchQTail + 1) % kAchQueueCap;
	if (nextTail == sAchQHead) return;
	sAchQueue[sAchQTail] = id;
	sAchQTail = nextTail;
}

static bool achCheckAllowed(int id) {
#if defined(LAB)
	if (!sAchAllowInLab) return false;
#endif
	if (sScreen == SCREEN_BOSS_TEST) return false;
	// Mixer runs only allow Mix It Up and Mixologist
	if (sScreen == SCREEN_GAME && sGame.mode != MODE_DAILY && sGame.mixed) {
		if (id != ACH_MIX_IT_UP && id != ACH_MIXOLOGIST) return false;
	}
	return true;
}

static bool achUnlock(int id, bool silent = false) {
	if (!achInPlatform(id) || achHas(id)) return false;
	if (!silent && !achCheckAllowed(id)) return false;

	gSave.achievements[id / 64] |= (1ULL << (id % 64));
	const u32 reward = kAchievements[id].coins;
	gSave.coins += reward;
	gSave.coinsLifetime += reward;
	if (!silent) achPushToast(id);

	// Auto-unlock Completionist once every other platform-applicable achievement is unlocked.
	if (id != ACH_COMPLETIONIST && !achHas(ACH_COMPLETIONIST)) {
		bool allDone = true;
		for (int i = 0; i < ACH_COUNT; i++) {
			if (i == ACH_COMPLETIONIST || !achInPlatform(i)) continue;
			if (!achHas(i)) { allDone = false; break; }
		}
		if (allDone) {
			gSave.achievements[ACH_COMPLETIONIST / 64] |= (1ULL << (ACH_COMPLETIONIST % 64));
			gSave.coins += kAchievements[ACH_COMPLETIONIST].coins;
			gSave.coinsLifetime += kAchievements[ACH_COMPLETIONIST].coins;
			if (!silent) achPushToast(ACH_COMPLETIONIST);
		}
	}
	saveWrite();
	return true;
}

static void achRetroactiveCheck() {
	if (gSave.achFlags2 & 0x80) return;
	if (gSave.coinsLifetime >= 1) achUnlock(ACH_OOH_SHINY, true);
	if (gSave.coinsSpentShop > 0 || achShopAnyOwned())
		achUnlock(ACH_TRANSACTION_COMPLETE, true);
	if (gSave.totalFruit >= 100) achUnlock(ACH_FRUIT_JUICE, true);
	if (gSave.totalFruit >= 1000) achUnlock(ACH_WHAT_A_MESS, true);
	if (gSave.achFlags & (1 << 1)) achUnlock(ACH_NEW_COAT_OF_PAINT, true);
	if (gSave.spChallengesWon >= 1) achUnlock(ACH_A_NEW_FOE, true);
	if (gSave.achFlags & (1 << 0)) achUnlock(ACH_LETS_PARTY, true);
	if (gSave.orchardBestRoom >= 25 || gSave.endlessBestRoom >= 25) achUnlock(ACH_QUARTER_MILESTONE, true);
	if (gSave.orchardBestRoom >= 100 || gSave.endlessBestRoom >= 100) achUnlock(ACH_CENTURY_MILESTONE, true);
	if (gSave.maxJuggleAir >= 10) achUnlock(ACH_INSANE_JUGGLER, true);
	if (gSave.coinsSpentShop >= 10000) achUnlock(ACH_BIG_SPENDER, true);
	if (achShopAllOwned()) achUnlock(ACH_SELL_OUT, true);
	if (gSave.achFlags2 & (1 << 5)) achUnlock(ACH_MIX_IT_UP, true);
	if (gSave.achFlags & (1 << 2)) achUnlock(ACH_PINATA_PARTY, true);
	if (gSave.achFlags & (1 << 3)) achUnlock(ACH_REROLL_ADDICT, true);
	if (gSave.achFlags & (1 << 4)) achUnlock(ACH_CLOSE_CALL, true);
	if (gSave.bestCombo >= 5 || (gSave.achFlags2 & (1 << 0))) achUnlock(ACH_COMBO_MASTER, true);
	if (gSave.best[MODE_CLASSIC] >= 250 || gSave.bestAssisted[MODE_CLASSIC] >= 250 || (gSave.achFlags & (1 << 5))) achUnlock(ACH_CLASSIC_LEGEND, true);
	if (gSave.best[MODE_ARCADE] >= 200 || gSave.bestAssisted[MODE_ARCADE] >= 200 || (gSave.achFlags & (1 << 6))) achUnlock(ACH_ARCADE_ACE, true);
	if (gSave.achFlags & (1 << 7)) achUnlock(ACH_RHYTHM_MASTER, true);
	if (gSave.best[MODE_ZEN] >= 100 || gSave.bestAssisted[MODE_ZEN] >= 100) achUnlock(ACH_ZEN_SERENITY, true);
	if (gSave.best[MODE_RECIPE] >= 10 || gSave.bestAssisted[MODE_RECIPE] >= 10) achUnlock(ACH_MASTER_CHEF, true);
	if (gSave.best[MODE_SHARP] >= 50 || gSave.bestAssisted[MODE_SHARP] >= 50) achUnlock(ACH_DEADEYE, true);
	if (gSave.bossesBeaten != 0) achUnlock(ACH_ORCHARD_SURVIVOR, true);
	if (gSave.achFlags2 & (1 << 6)) achUnlock(ACH_MIXOLOGIST, true);
	if (gSave.dailyStreak >= 7) achUnlock(ACH_DEDICATED_SLICER, true);
	if (gSave.mythicsUnlocked != 0 || (gSave.achFlags2 & (1 << 4))) achUnlock(ACH_FEELIN_LUCKY, true);
	if (gSave.achFlags2 & (1 << 3)) achUnlock(ACH_MILESTONE_BREAKER, true);
	if (gSave.rushBestSecs > 0 || (gSave.achFlags2 & (1 << 2))) achUnlock(ACH_BOSS_RUSH_CHAMPION, true);
	if (gSave.achFlags2 & (1 << 1)) achUnlock(ACH_UNTOUCHABLE, true);
	gSave.achFlags2 |= 0x80;
	saveWrite();
}

static void achPollProgress() {
	if (gSave.coinsLifetime >= 1) achUnlock(ACH_OOH_SHINY);
	if (gSave.coinsSpentShop > 0) achUnlock(ACH_TRANSACTION_COMPLETE);
	if (gSave.coinsSpentShop >= 10000) achUnlock(ACH_BIG_SPENDER);
	if (achShopAllOwned()) achUnlock(ACH_SELL_OUT);
	if (gSave.totalFruit >= 100) achUnlock(ACH_FRUIT_JUICE);
	if (gSave.totalFruit >= 1000) achUnlock(ACH_WHAT_A_MESS);
	if (gSave.achFlags & (1 << 1)) achUnlock(ACH_NEW_COAT_OF_PAINT);
	if (gSave.spChallengesWon >= 1) achUnlock(ACH_A_NEW_FOE);
	if (gSave.achFlags & (1 << 0)) achUnlock(ACH_LETS_PARTY);
	if (gSave.orchardBestRoom >= 25 || gSave.endlessBestRoom >= 25) achUnlock(ACH_QUARTER_MILESTONE);
	if (gSave.orchardBestRoom >= 100 || gSave.endlessBestRoom >= 100) achUnlock(ACH_CENTURY_MILESTONE);
	if (gSave.maxJuggleAir >= 10) achUnlock(ACH_INSANE_JUGGLER);
	if (gSave.bestCombo >= 5) achUnlock(ACH_COMBO_MASTER);
	if (gSave.best[MODE_CLASSIC] >= 250 || gSave.bestAssisted[MODE_CLASSIC] >= 250) achUnlock(ACH_CLASSIC_LEGEND);
	if (gSave.best[MODE_ARCADE] >= 200 || gSave.bestAssisted[MODE_ARCADE] >= 200) achUnlock(ACH_ARCADE_ACE);
	if (gSave.best[MODE_ZEN] >= 100 || gSave.bestAssisted[MODE_ZEN] >= 100) achUnlock(ACH_ZEN_SERENITY);
	if (gSave.best[MODE_RECIPE] >= 10 || gSave.bestAssisted[MODE_RECIPE] >= 10) achUnlock(ACH_MASTER_CHEF);
	if (gSave.best[MODE_SHARP] >= 50 || gSave.bestAssisted[MODE_SHARP] >= 50) achUnlock(ACH_DEADEYE);
	if (gSave.bossesBeaten != 0) achUnlock(ACH_ORCHARD_SURVIVOR);
	if (gSave.rushBestSecs > 0) achUnlock(ACH_BOSS_RUSH_CHAMPION);
	if (gSave.dailyStreak >= 7) achUnlock(ACH_DEDICATED_SLICER);
	if (gSave.mythicsUnlocked != 0 || (gSave.achFlags2 & (1 << 4))) achUnlock(ACH_FEELIN_LUCKY);
	if (gSave.achFlags & (1 << 2)) achUnlock(ACH_PINATA_PARTY);
	if (gSave.achFlags & (1 << 3)) achUnlock(ACH_REROLL_ADDICT);
	if (gSave.achFlags & (1 << 4)) achUnlock(ACH_CLOSE_CALL);
	if (gSave.achFlags & (1 << 7)) achUnlock(ACH_RHYTHM_MASTER);
	if (gSave.achFlags2 & (1 << 1)) achUnlock(ACH_UNTOUCHABLE);
	if (gSave.achFlags2 & (1 << 2)) achUnlock(ACH_BOSS_RUSH_CHAMPION);
	if (gSave.achFlags2 & (1 << 3)) achUnlock(ACH_MILESTONE_BREAKER);
	if (gSave.achFlags2 & (1 << 5)) achUnlock(ACH_MIX_IT_UP);
	if (gSave.achFlags2 & (1 << 6)) achUnlock(ACH_MIXOLOGIST);

	if (sScreen == SCREEN_GAME || sScreen == SCREEN_RESULT) {
		if (sGame.mode != MODE_DAILY && sGame.mixed) {
			gSave.achFlags2 |= (1 << 5);
			achUnlock(ACH_MIX_IT_UP);
			int rules = 0;
			for (int i = 0; i < MIX_COUNT; i++) if (sGame.mix[i]) rules++;
			if (rules >= 3 && sGame.phase == PHASE_OVER && sGame.score > 0) {
				gSave.achFlags2 |= (1 << 6);
				achUnlock(ACH_MIXOLOGIST);
			}
		}
		if (sGame.orchardLike()) {
			if (sGame.orc.room >= 25) achUnlock(ACH_QUARTER_MILESTONE);
			if (sGame.orc.room >= 100) achUnlock(ACH_CENTURY_MILESTONE);
			if (sGame.orc.usedSecondWind) {
				gSave.achFlags |= (1 << 4);
				achUnlock(ACH_CLOSE_CALL);
			}
			int upg = 0;
			for (int c = 0; c < CARD_COUNT; c++) upg += sGame.orc.upgrade[c];
			if (upg >= 6) achUnlock(ACH_FULL_ARSENAL);
		}
		if (sGame.mode == MODE_JUGGLE && sGame.m12.bestAir >= 10) {
			if (sGame.m12.bestAir > gSave.maxJuggleAir) gSave.maxJuggleAir = (u16)sGame.m12.bestAir;
			achUnlock(ACH_INSANE_JUGGLER);
		}
		if (sGame.mode == MODE_BEAT && sGame.phase == PHASE_OVER && sGame.sliced >= 10 &&
		    sGame.beatHits * 100 >= sGame.sliced * 95) {
			gSave.achFlags |= (1 << 7);
			achUnlock(ACH_RHYTHM_MASTER);
		}
		if (sGame.mode == MODE_CLASSIC && sGame.score >= 250) {
			gSave.achFlags |= (1 << 5);
			achUnlock(ACH_CLASSIC_LEGEND);
		}
		if (sGame.mode == MODE_ARCADE && sGame.score >= 200) {
			gSave.achFlags |= (1 << 6);
			achUnlock(ACH_ARCADE_ACE);
		}
	}
}

static void updateAchToast(float dt) {
	if (sAchToastPhase == 0) {
		if (sAchQHead != sAchQTail) {
			sAchToastId = sAchQueue[sAchQHead];
			sAchQHead = (sAchQHead + 1) % kAchQueueCap;
			sAchToastPhase = 1;
			sAchToastT = 0.0f;
			audio::play(SFX_ACH_IN, 0.72f);
			audio::play(SFX_ACH_JINGLE, 0.78f);
		}
		return;
	}
	sAchToastT += dt;
	if (sAchToastPhase == 1 && sAchToastT >= kAchSlideIn) {
		sAchToastPhase = 2;
		sAchToastT = 0.0f;
	} else if (sAchToastPhase == 2 && sAchToastT >= kAchHold) {
		sAchToastPhase = 3;
		sAchToastT = 0.0f;
		audio::play(SFX_ACH_OUT, 0.70f);
	} else if (sAchToastPhase == 3 && sAchToastT >= kAchSlideOut) {
		sAchToastPhase = 0;
		sAchToastId = -1;
		sAchToastT = 0.0f;
	}
}

// Shared wide-banner renderer used identically by SCREEN_ACHIEVEMENTS and the top-of-screen Toast.
static void drawAchBanner(float x, float y, float w, float h, int id, bool unlocked, bool selected) {
	const AchDef& a = kAchievements[id];
	drawPlank(x, y, w, h, selected, false);
	if (unlocked) {
		gfx::rect(x + 1, y + 1, w - 2, 1.5f, COL_ACCENT);
		gfx::rect(x + 1, y + h - 2.5f, w - 2, 1.5f, COL_ACCENT);
	}
	const float iconSz = h - 10.0f;
	const float iconX = x + 8.0f + iconSz * 0.5f;
	const float iconY = y + h * 0.5f;
	const float iconScale = iconSz / 32.0f;
	gfx::sprite(SHEET_SPRITES, a.sprIdx, iconX, iconY, iconScale, iconScale, 0.0f,
	            unlocked ? WHITE : rgba(0, 0, 0, 255));

	const float tx = x + 14.0f + iconSz;
	const float tw = w - (14.0f + iconSz + 10.0f);
	const u32 titleCol = unlocked ? COL_CREAM : rgba(10, 10, 12, 255);
	const u32 descCol = unlocked ? rgba(236, 232, 218, 255) : rgba(12, 12, 14, 255);
	fitText(a.title, tx, y + 5.0f, 0.35f, tw, titleCol, ALIGN_LEFT);
	fitText(a.desc, tx, y + 19.0f, PORT_3DS ? 0.30f : 0.28f, tw, descCol, ALIGN_LEFT);

	// Bottom universal coin reward strip
	char cbuf[32];
	snprintf(cbuf, sizeof cbuf, "+%u COINS", (unsigned)a.coins);
	gfx::rect(tx, y + h - 13.0f, tw, 10.0f, unlocked ? rgba(36, 24, 12, 165) : rgba(10, 10, 14, 150));
	fitText(cbuf, tx + tw * 0.5f, y + h - 12.5f, 0.26f, tw - 4.0f, unlocked ? COL_ACCENT : rgba(12, 12, 14, 255), ALIGN_CENTER);
}

static void drawAchToast(float sx, float sy, float sw) {
	if (sAchToastPhase == 0 || sAchToastId < 0 || sAchToastId >= ACH_COUNT) return;
	float slide = 1.0f;
	if (sAchToastPhase == 1) {
		float u = clampf(sAchToastT / kAchSlideIn, 0.0f, 1.0f);
		slide = 1.0f - (1.0f - u) * (1.0f - u);
	} else if (sAchToastPhase == 3) {
		float u = clampf(sAchToastT / kAchSlideOut, 0.0f, 1.0f);
		slide = 1.0f - u * u;
	}
	const float w = sw < 284.0f ? sw - 12.0f : 276.0f, h = 44.0f;
	const float x = sx + (sw - w) * 0.5f;
	const float y = sy - h - 6.0f + (h + 14.0f) * slide;
	drawAchBanner(x, y, w, h, sAchToastId, true, true);
}

// ---------------------------------------------------------------- SCREEN_ACHIEVEMENTS
static int sAchIds[ACH_COUNT];
static int sAchCount = 0;
static int sAchSel = 0;
static float sAchScroll = 0.0f;
static float sAchScrollDraw = 0.0f;
static bool sAchDrag = false;
static int sAchDragY = 0;
static bool sAchMoved = false;

static void openAchievements() {
	sAchCount = 0;
	for (int i = 0; i < ACH_COUNT; i++) {
		if (achInPlatform(i)) sAchIds[sAchCount++] = i;
	}
	if (sAchSel >= sAchCount) sAchSel = 0;
	sAchScroll = 0.0f;
	sAchScrollDraw = 0.0f;
	sAchDrag = false;
	goTo(SCREEN_ACHIEVEMENTS, 1);
}

static void drawAchievementsTop() {
	if (sAchCount <= 0) return;
	const int id = sAchIds[sAchSel < sAchCount ? sAchSel : 0];
	const AchDef& a = kAchievements[id];
	const bool unl = achHas(id);

	topPanel(24, 16, 352, 208);
	gfx::text("ACHIEVEMENTS", 200, 26, 0.52f, COL_ACCENT, ALIGN_CENTER);

	const float iconScale = 2.1f;
	gfx::sprite(SHEET_SPRITES, a.sprIdx, 82, 114, iconScale, iconScale, 0.0f,
	            unl ? WHITE : rgba(0, 0, 0, 255));

	const u32 titleCol = unl ? COL_CREAM : rgba(12, 12, 16, 255);
	const u32 descCol = unl ? rgba(236, 232, 218, 255) : rgba(14, 14, 18, 255);
	fitText(a.title, 132, 68, 0.44f, 226, titleCol, ALIGN_LEFT);
	fitText(a.desc, 132, 98, 0.34f, 226, descCol, ALIGN_LEFT);

	char cbuf[48];
	snprintf(cbuf, sizeof cbuf, "%s  |  +%u COINS", unl ? "UNLOCKED" : "LOCKED", (unsigned)a.coins);
	gfx::rect(132, 142, 226, 22, unl ? rgba(36, 24, 12, 180) : rgba(12, 12, 16, 160));
	fitText(cbuf, 245, 146, 0.34f, 216, unl ? COL_ACCENT : rgba(14, 14, 18, 255), ALIGN_CENTER);

	int tot = 0;
	const int got = achCountPlatform(&tot);
	snprintf(cbuf, sizeof cbuf, "%d / %d DISCOVERED", got, tot);
	gfx::text(cbuf, 200, 194, 0.34f, COL_DIM, ALIGN_CENTER);
}

static void updateAchievements(const Input& in, bool wasTouching) {
	if (in.down & KEY_B) {
		audio::click();
		goTo(SCREEN_RECORDS, -1);
		return;
	}
	const float viewH = 192.0f;
	const float rowH = 48.0f;
	const float maxScroll = clampf(sAchCount * rowH - viewH, 0.0f, 9999.0f);

	if (in.wheel != 0) sAchScroll = clampf(sAchScroll - in.wheel * 30.0f, 0.0f, maxScroll);

	if (in.touching && !wasTouching) {
		if (radioHit(kBack, in.tx, in.ty)) {
			audio::click();
			goTo(SCREEN_RECORDS, -1);
			return;
		}
		if (in.ty >= 40 && in.ty <= 234) {
			sAchDrag = true;
			sAchDragY = in.ty;
			sAchMoved = false;
		}
	}

	if (sAchDrag && in.touching) {
		const int dy = in.ty - sAchDragY;
		if (dy > 4 || dy < -4) sAchMoved = true;
		if (sAchMoved) {
			sAchScroll = clampf(sAchScroll - dy, 0.0f, maxScroll);
			sAchDragY = in.ty;
		}
	}

	if (sAchDrag && !in.touching) {
		sAchDrag = false;
		if (!sAchMoved && in.ty >= 40 && in.ty <= 234) {
			const int idx = (int)(((in.ty - 42.0f) + sAchScrollDraw) / rowH);
			if (idx >= 0 && idx < sAchCount) {
				sAchSel = idx;
				audio::click(0.55f);
			}
		}
	}

	if (sAchCount > 0) {
		int next = sAchSel;
		if (in.down & KEY_UP) next--;
		if (in.down & KEY_DOWN) next++;
		if (next < 0) next = 0;
		if (next >= sAchCount) next = sAchCount - 1;
		if (next != sAchSel) {
			sAchSel = next;
			audio::click(0.45f);
			const float topY = sAchSel * rowH;
			const float botY = topY + rowH;
			if (topY < sAchScroll) sAchScroll = topY;
			if (botY > sAchScroll + viewH) sAchScroll = botY - viewH;
		}
	}
	sAchScroll = clampf(sAchScroll, 0.0f, maxScroll);
	sAchScrollDraw += (sAchScroll - sAchScrollDraw) * 0.28f;
}

static void drawAchievements() {
	drawBackArrow(false, false);
	int tot = 0;
	const int got = achCountPlatform(&tot);
	char hdr[32];
	snprintf(hdr, sizeof hdr, "ACHIEVEMENTS (%d/%d)", got, tot);
	fitText(hdr, 56, 10, 0.44f, 250, COL_CREAM, ALIGN_LEFT);

	gfx::setClip(10, 40, 300, 194);
	const float rowH = 48.0f;
	for (int i = 0; i < sAchCount; i++) {
		const float y = 42.0f + i * rowH - sAchScrollDraw;
		if (y + 44.0f < 38.0f || y > 236.0f) continue;
		const int id = sAchIds[i];
		drawAchBanner(14.0f, y, 292.0f, 44.0f, id, achHas(id), i == sAchSel);
	}
	gfx::clearClip();
}
