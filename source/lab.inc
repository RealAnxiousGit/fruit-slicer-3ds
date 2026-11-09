// Lab build: LAB, together with AUTOTEST and DEV_TOOLS (CMake -DLAB=ON, 3DS `make LAB=1`). Developer builds
// only. One quick run through everything the game does, each check printed as PASS or FAIL on the console
// and written to lab.txt beside the save, then a summary line. It plays like a person: every press goes
// through the same Input as the hardware.
//
// The run: every menu screen and its back arrow, the Shop (buy, use, music preview), the Options sections,
// the Practice lessons (meter stays empty until the Frenzy lesson, a Frenzy must happen), every mode cut
// down to about 12 seconds (half of them in 2D), pause and resume, bombs and their blasts, music in menus
// and games, then on Windows, Linux and Android a short hold of Escape that must not quit and a 3-second
// hold that must.
//
// Keep it current: a new feature gets a labCheck here, in the same commit.

static int sLabPass, sLabFail;
static FILE* sLabFile;

static void labCheck(bool ok, const char* name, const char* detail = "") {
	if (!sLabFile) {
		char path[256];
		port::dataPath(path, sizeof path, "lab.txt");
		sLabFile = fopen(path, "w");
	}
	(ok ? sLabPass : sLabFail)++;
	fprintf(stderr, "[lab] %s %s%s%s\n", ok ? "PASS" : "FAIL", name, *detail ? ": " : "", detail);
	if (sLabFile) {
		fprintf(sLabFile, "%s %s%s%s\n", ok ? "PASS" : "FAIL", name, *detail ? ": " : "", detail);
		fflush(sLabFile);
	}
}

static void labSummary() {
	char line[96];
	snprintf(line, sizeof line, "%d passed, %d failed", sLabPass, sLabFail);
	fprintf(stderr, "[lab] done: %s\n", line);
	if (sLabFile) {
		fprintf(sLabFile, "DONE %s\n", line);
		fclose(sLabFile);
		sLabFile = nullptr;
	}
}

static void labExpect(Screen want, const char* name) {
	char d[48];
	snprintf(d, sizeof d, "on screen %d, wanted %d", (int)sScreen, (int)want);
	labCheck(sScreen == want, name, sScreen == want ? "" : d);
}

static u32 sLabCoins;
static u8 sLabArt;

// Daily Fruit's calendar, on a scratch copy of the save.
static void labDaily() {
	static Settings keep;
	memcpy((void*)&keep, (const void*)&gSave, sizeof keep);
	gSave.dailyDay = gSave.dailyFirstDay = 0;
	gSave.dailyStreak = gSave.dailyBestStreak = 0;
	memset(gSave.dailyScore, 0, sizeof gSave.dailyScore);
	labCheck(progress::dailyNumber(9000) == 1 && progress::dailyNumber(9040) == 1, "a fresh save's Daily Fruit is day 1 until it is played");
	progress::dailyRecord(9000, 10);
	labCheck(progress::dailyNumber(9000) == 1, "the first day played is day 1");
	labCheck(progress::dailyNumber(9001) == 2 && progress::dailyNumber(9006) == 7, "the day number goes up each day");
	labCheck(progress::dailyNumber(9007) == 8 && progress::dailyNumber(9020) == 21, "the day number carries on past 7");
	progress::dailyRecord(9001, 20);
	labCheck(progress::dailyStreak(9001) == 2 && progress::dailyPlayed(9000) && progress::dailyPlayed(9001), "two days in a row make a streak and both keep their scores");
	progress::dailyRecord(9004, 30);
	labCheck(progress::dailyNumber(9004) == 5 && progress::dailyStreak(9004) == 1 && !progress::dailyPlayed(9003), "a skipped day still counts and ends the streak");
	labCheck(progress::dailyNumber(8990) == 1, "a clock set back doesn't go below day 1");
	memcpy((void*)&gSave, (const void*)&keep, sizeof keep);
}

// The menu part, one step each time the screen has settled. A step checks what the last one did, then acts.
static void labMenus() {
	const float firstRowY = kListTop + kRowH * 0.5f;
	switch (sBotMenuStep++) {
		case 0:
			labExpect(SCREEN_TITLE, "boots to the title");
			labDaily();
			labCheck(audio::musicTime() >= 0 || audio::musicPending(), "menu music plays");
			botTap(kTitleButtons[4]);
			return;
		case 1: labExpect(SCREEN_HOWTO, "How to Play opens"); botTap(kBack); return;
		case 2: labExpect(SCREEN_TITLE, "How to Play back arrow"); botTap(kTitleButtons[2]); return;
		case 3:
			labExpect(SCREEN_JOURNAL, "Journal opens");
			// The Journal draws 2D pictures whichever art style is chosen; the 3DS used to drop them with 3D art.
			labCheck(gfx::stickersLoaded(), gSave.artStyle == ART_3D ? "Journal has its fruit pictures with 3D models" : "Journal has its fruit pictures with 2D stickers");
			botTap(kBack);
			return;
		case 4: labExpect(SCREEN_TITLE, "Journal back arrow cuts its berry and goes back"); botTap(kTitleButtons[3]); return;
		case 5: labExpect(SCREEN_STATS, "Stats opens"); botTap(kBack); return;
		case 6: labExpect(SCREEN_TITLE, "Stats back arrow"); botTap(kTitleButtons[6]); return;
		case 7: labExpect(SCREEN_CREDITS, "Credits opens"); botTap(kBack); return;
		case 8:
			labExpect(SCREEN_TITLE, "Credits back arrow");
			gSave.coins = 5000;  // the developer tools' Add Coins
			botTap(kTitleButtons[5]);
			return;
		case 9: labExpect(SCREEN_SHOP, "Shop opens"); botTap(shopButton(kShopTile0 + 1)); return;
		case 10:
			labCheck(sShopItem == 1, "Shop tile is picked by a tap");
			sLabCoins = gSave.coins;
			botTap(shopButton(kShopAct));
			return;
		case 11: botTap(shopButton(kShopAct)); return;  // the second press buys
		case 12: {
			const bool owned = shopOwned(SHOP_THEMES, 1);
			char d[48];
			snprintf(d, sizeof d, "coins %lu -> %lu", (unsigned long)sLabCoins, (unsigned long)gSave.coins);
			labCheck(owned && gSave.coins == sLabCoins - shopItem(SHOP_THEMES, 1).price, "Shop buys a board for its price", d);
			botTap(shopButton(kShopAct));
			return;
		}
		case 13:
			labCheck(sKeepBoard == kBaseBoards + 1, "Shop puts the bought board in use");
			botTap(shopButton(kShopTab0 + SHOP_MUSIC));
			return;
		case 14: labCheck(sShopTab == SHOP_MUSIC, "Shop music tab opens"); botTap(shopButton(kShopTile0)); return;
		case 15: labCheck(audio::musicTime() >= 0 || audio::musicPending(), "Shop music preview plays"); labCheck(audio::currentTrack() == kBaseTracks, "Shop preview switches to the track on the tile"); botTap(kBack); return;
		case 16: labExpect(SCREEN_TITLE, "Shop back arrow cuts its berry and goes back"); botTap(kTitleButtons[1]); return;
		case 17: labExpect(SCREEN_OPTIONS, "Options opens"); botTap(optionButtonFor(SID_AUDIO)); return;
		case 18: labExpect(SCREEN_SECTION, "Audio section opens"); botTapXY((int)((kTrackX0 + kTrackX1) / 2), (int)firstRowY); return;
		case 19: botTap(kBack); return;
		case 20:
			labExpect(SCREEN_OPTIONS, "section back arrow cuts its berry and goes back");
			labCheck(optionButtons() == 7, "Options shows five signs, plus Developer in a test build");
			botTap(optionButtonFor(SID_VISUALS));
			return;
		case 21:
			labExpect(SCREEN_SECTION, "Visuals section opens");
			sLabArt = gSave.artStyle;
			botTapXY(231, (int)firstRowY);  // Art Style: next
			return;
		case 22: labCheck(gSave.artStyle != sLabArt, "Art Style changes with a tap"); botTapXY(231, (int)firstRowY); return;
		case 23:
			labCheck(gSave.artStyle == sLabArt, "Art Style changes back");
			sScroll = maxScroll();
			botTapXY(60, (int)(kListTop + kRowH * (kSections[sSection].count - 0.5f) - sScroll));  // Advanced, the last row
			sBotMenuStep = 40;
			return;
		case 40:
			labCheck(sScreen == SCREEN_SECTION && kSections[sSection].id == SID_VISUALS_MORE, "the Advanced row opens its page");
			botTap(kBack);
			return;
		case 41:
			labCheck(sScreen == SCREEN_SECTION && kSections[sSection].id == SID_VISUALS && sFocus[SCREEN_SECTION] == kSections[sSection].count - 1, "the back arrow goes up one page, to the row that opened it");
			botTap(kBack);
			sBotMenuStep = 24;
			return;
		case 24: botTap(optionButtonFor(SID_GAMEPLAY)); return;
		case 25: labExpect(SCREEN_SECTION, "Gameplay section opens"); botTap(kBack); return;
		case 26: botTap(optionButtonFor(SID_ACCESS)); return;
		case 27: labExpect(SCREEN_SECTION, "Accessibility section opens"); botTap(kBack); return;
		case 28: botTap(optionButtonFor(SID_DATA)); return;
		case 29: labExpect(SCREEN_SECTION, "Scores and Data section opens"); botTap(kBack); return;
		case 30: botTap(kBack); return;
		case 31: labExpect(SCREEN_TITLE, "Options back arrow"); sBotMenuStep = 100; return;
		default: sBotMenuStep = 100; return;
	}
}

// Pick a Game's pages, the first time the bot reaches it. Returns true while there are steps left.
static bool labModePages() {
	static int step = 0;
	const Button& next = kPageArrows[1];
	switch (step++) {
		case 0:
			labCheck(sModePage == 0 && modeOnSign(0, 1) == MODE_CLASSIC && modeOnSign(0, 2) == MODE_ARCADE && modeOnSign(0, 3) == MODE_FRENZY, "the first page of Pick a Game holds the main modes");
			sFocus[SCREEN_MODES] = 2;
			sBotKeys = KEY_RIGHT;
			return true;
		case 1:
			labCheck(sModePage == 1 && sFocus[SCREEN_MODES] == 1, "Right past the last sign in a row turns the page");
			sBotKeys = KEY_LEFT;
			return true;
		case 2:
			labCheck(sModePage == 0 && sFocus[SCREEN_MODES] == 2, "Left past the first sign in a row turns back");
			botTapXY((int)(next.x + next.w / 2), (int)(next.y + next.h / 2));
			return true;
		case 3:
			labCheck(sModePage == 1, "a tap on the page arrow turns the page");
			botTapXY((int)(next.x + next.w / 2), (int)(next.y + next.h / 2));
			return true;
		case 4:
			labCheck(sModePage == 0, "turning past the last page wraps to the first");
			return false;
		default:
			return false;
	}
}

// Checks made every frame of a game.
static bool sLabMeterEarly, sLabBlastSeen, sLabBlastMissed;
static bool sLabCalloutChecked;  // the PAUSED sign has been checked once
static bool sLabTapped, sLabTapSeen;  // 3DS: a mode was started with a plain stylus tap
static u32 sLabBombHits;
static void labWatchGame() {
	if (sBotModeFrames == 1 && sLabTapped && !sLabTapSeen && sGame.mode != MODE_PRACTICE) {
		sLabTapSeen = true;
		labCheck(true, "a stylus tap on a mode sign starts it", modeName(sGame.mode));
	}
	if (sBotModeFrames == 1) {
		// Every mode cut short, and every other one played with the 2D stickers.
		if (sGame.mode != MODE_PRACTICE) {
#ifdef TEST_RECORD
			if (sGame.timeLeft > 25) sGame.timeLeft = 25;  // trailer capture: a little longer
#else
			if (sGame.timeLeft > 12) sGame.timeLeft = 12;
#endif
			gSave.artStyle = (sBotMode % 2) ? ART_2D : ART_3D;
		}
		sLabBombHits = sGame.statBombHits;
	}
	if (sBotModeFrames == 7200) {
		// Two minutes in one game: something is stuck. Say so and stop rather than run forever.
		labCheck(false, "a game ends within two minutes", modeName(sGame.mode));
		labSummary();
		sBotDone = true;
		return;
	}
	if (sBotModeFrames == 120) labCheck(audio::musicTime() >= 0, "game music plays", modeName(sGame.mode));
	if (sGame.mode == MODE_PRACTICE && sGame.practiceStep < PS_FRENZY && (sGame.feverMeter > 0 || sGame.chain >= 2)) sLabMeterEarly = true;
	if (sGame.statBombHits != sLabBombHits) {
		sLabBombHits = sGame.statBombHits;
		(boom::active() ? sLabBlastSeen : sLabBlastMissed) = true;
	}
}

static void labResult() {
	char d[64];
	snprintf(d, sizeof d, "score %lu, sliced %d, %.0f s", (unsigned long)sGame.score, sGame.sliced, sGame.elapsed);
	char name[48];
	snprintf(name, sizeof name, "%s reaches its result", modeName(sGame.mode));
	labCheck(sGame.sliced > 0, name, d);
	if (sGame.mode == MODE_PRACTICE) {
		labCheck(!sLabMeterEarly, "lessons keep the Frenzy meter and combo off until the Frenzy lesson");
		labCheck(sGame.statFrenzies > 0, "the Frenzy lesson needs a Frenzy before it ends");
	}
}

// Hold the quit control: Escape on a keyboard; on Android, where Back leaves from the title, a finger on the bomb.
static void labHoldQuit(Input& in, bool first) {
#if PORT_ANDROID
	(void)first;
	const Button b = quitButton();
	in.touching = true;
	in.tx = (int)(b.x + b.w * 0.5f);
	in.ty = (int)(b.y + 22);
#elif !PORT_3DS
	in.held = KEY_B | KEY_START;
	if (first) in.down = KEY_B | KEY_START;
#else
	(void)in;
	(void)first;
#endif
}

// After the last mode. Windows, Linux and Android: hold Escape briefly (nothing happens), then for 3 seconds
// (the bomb goes off and the game closes). The 3DS just ends.
static void labFinish(Input& in) {
	static int step = 0, held = 0;
	if (step == 0) {
		labCheck(sLabBlastSeen && !sLabBlastMissed, "every bomb hit sets off a blast");
		sGame.labProbes(sBlade, labCheck);
		step = 1;
#if PORT_3DS
		labSummary();
		sBotDone = true;
		return;
#endif
	}
	if (sScreen != SCREEN_TITLE) {
		if (sBotPhaseFrames > 45 && !sSliding && sFade <= 0) {
			sBotPhaseFrames = 0;
			botTap(kBack);
		}
		return;
	}
	if (sBotPhaseFrames < 45 || sSliding || sFade > 0) return;
	held++;
	if (step == 1) {
		if (held <= 60) {
			labHoldQuit(in, held == 1);
			return;
		}
		if (held < 90) return;
		labCheck(sQuitT < 0 && sScreen == SCREEN_TITLE, "a 1-second hold of the quit bomb does not quit");
		step = 2;
		held = 0;
		return;
	}
	labHoldQuit(in, held == 1);
	if (sQuitT >= 0 && step == 2) {
		char d[32];
		snprintf(d, sizeof d, "went off after %.2f s", held / 60.0f);
		labCheck(held >= 175 && held <= 185, "a 3-second hold of the quit bomb blows up the screen and quits", d);
		labSummary();
		step = 3;  // the game closes once the blast has played
	}
	if (held > 400 && step == 2) {
		labCheck(false, "a 3-second hold of the quit bomb blows up the screen and quits", "nothing happened");
		labSummary();
		sBotDone = true;
	}
}
