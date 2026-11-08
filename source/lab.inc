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

// The menu part, one step each time the screen has settled. A step checks what the last one did, then acts.
static void labMenus() {
	const float firstRowY = kListTop + kRowH * 0.5f;
	switch (sBotMenuStep++) {
		case 0:
			labExpect(SCREEN_TITLE, "boots to the title");
			labCheck(audio::musicTime() >= 0, "menu music plays");
			botTap(kTitleButtons[4]);
			return;
		case 1: labExpect(SCREEN_HOWTO, "How to Play opens"); botTap(kBack); return;
		case 2: labExpect(SCREEN_TITLE, "How to Play back arrow"); botTap(kTitleButtons[2]); return;
		case 3: labExpect(SCREEN_JOURNAL, "Journal opens"); botTap(kBack); return;
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
		case 15: labCheck(audio::musicTime() >= 0, "Shop music preview plays"); botTap(kBack); return;
		case 16: labExpect(SCREEN_TITLE, "Shop back arrow cuts its berry and goes back"); botTap(kTitleButtons[1]); return;
		case 17: labExpect(SCREEN_OPTIONS, "Options opens"); botTap(optionButtonFor(SID_AUDIO)); return;
		case 18: labExpect(SCREEN_SECTION, "Audio section opens"); botTapXY((int)((kTrackX0 + kTrackX1) / 2), (int)firstRowY); return;
		case 19: botTap(kBack); return;
		case 20: labExpect(SCREEN_OPTIONS, "section back arrow cuts its berry and goes back"); botTap(optionButtonFor(SID_VISUALS)); return;
		case 21:
			labExpect(SCREEN_SECTION, "Visuals section opens");
			sLabArt = gSave.artStyle;
			botTapXY(231, (int)firstRowY);  // Art Style: next
			return;
		case 22: labCheck(gSave.artStyle != sLabArt, "Art Style changes with a tap"); botTapXY(231, (int)firstRowY); return;
		case 23: labCheck(gSave.artStyle == sLabArt, "Art Style changes back"); botTap(kBack); return;
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

// Checks made every frame of a game.
static bool sLabMeterEarly, sLabBlastSeen, sLabBlastMissed;
static u32 sLabBombHits;
static void labWatchGame() {
	if (sBotModeFrames == 1) {
		// Every mode cut short, and every other one played with the 2D stickers.
		if (sGame.mode != MODE_PRACTICE) {
			if (sGame.timeLeft > 12) sGame.timeLeft = 12;
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
