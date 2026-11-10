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
#if PORT_ULTRA
namespace gfx {
bool ultraLoaded();  // port/render.cpp: the 4K sheets are what's loaded
}
#endif

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
// The Shop's list, driven through the real input code: wheel, drag and tap on the same tiles.
static void labShopScroll() {
	Input in{};
	const auto settle = [&]() {
		for (int i = 0; i < 40; i++) updateShop(in, false);
	};
	shopSetTab(SHOP_MUSIC);
	const float tabY = shopButton(kShopTab0).y, buyY = shopButton(kShopAct).y;
	const int picked = sShopItem;
	in.wheel = -1;
	updateShop(in, false);
	in.wheel = 0;
	settle();
	labCheck(sShopList.pos == kShopStep, "Shop wheel scrolls one row");
	labCheck(shopButton(kShopTab0).y == tabY && shopButton(kShopAct).y == buyY, "Shop tabs and Buy sign stay put when the list scrolls");

	int shown = 0;
	for (int k = 0; k < shopCount(SHOP_MUSIC); k++) {
		const Button b = shopButton(kShopTile0 + k);
		shown += b.y >= kShopWindowTop && b.y + b.h <= kShopWindowTop + kShopWindowH;
	}
	char d[32];
	snprintf(d, sizeof d, "%d on show", shown);
	labCheck(shown == kShopCols * kShopRowsShown, "Shop shows six items at a time", d);

	const auto press = [&](int y0, int y1) {
		in.touching = true;
		in.tx = 60;
		in.ty = y0;
		updateShop(in, false);
		in.ty = y1;
		updateShop(in, true);
		updateShop(in, true);
		in.touching = false;
		updateShop(in, true);
		settle();
	};
	press(150, 90);
	labCheck(sShopItem == picked, "A drag across the Shop's tiles picks nothing");
	labCheck(sShopList.pos == 2 * kShopStep, "A Shop drag scrolls, and comes to rest on a whole row");
	const int target = 2 * kShopCols;
	const Button b = shopButton(kShopTile0 + target);
	press((int)(b.y + 20), (int)(b.y + 20));
	labCheck(sShopItem == target, "A tap still picks a scrolled tile");

	for (int i = 0; i < 80; i++) {
		in.wheel = -1;
		updateShop(in, false);
	}
	in.wheel = 0;
	settle();
	labCheck(sShopList.pos == shopHiddenRows(SHOP_MUSIC) * kShopStep, "Shop list stops at its last row");
}

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
			labCheck(sKeep[SHOP_THEMES] == kBaseBoards + 1, "Shop puts the bought board in use");
			botTap(shopButton(kShopTab0 + SHOP_MUSIC));
			return;
		case 14: labCheck(sShopTab == SHOP_MUSIC, "Shop music tab opens"); botTap(shopButton(kShopTile0)); return;
		case 15: labCheck(audio::musicTime() >= 0 || audio::musicPending(), "Shop music preview plays"); labCheck(audio::currentTrack() == kBaseTracks, "Shop preview switches to the track on the tile");
			labShopScroll();
			// The last song on sale, the longest kind of file the game loads: give it time to arrive.
			sShopItem = shopCount(SHOP_MUSIC) - 1;
			shopTryOn();
			sBotPhaseFrames = -420;
			return;
		case 16: {
			labCheck(audio::currentTrack() == kTrackCount - 1 && audio::musicTime() >= 0 && !audio::musicPending(), "A full-length Shop song loads and plays");
			labCheck(strchr(shopItem(SHOP_MUSIC, shopCount(SHOP_MUSIC) - 1).name, '.') == nullptr, "Shop song names carry no file extension");
			sBotMenuStep = 55;
			botTap(kBack);
			return;
		}
		case 55: sBotMenuStep = 17; labExpect(SCREEN_TITLE, "Shop back arrow cuts its berry and goes back"); botTap(kTitleButtons[1]); return;
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
		labCheck(gfx::fontImageCount() == gfx::fontLetterCount() * 2, "the font table matches the font sheet");
		{
			const u32 extras = gSave.ownedExtras, coins = gSave.coins;
			const u8 skin = gSave.skin;
			gSave.ownedExtras = 0;
			gSave.coins = 5000;
			gSave.skin = 0;
			labCheck(shopWornSkin() == nullptr && shopWornSkinTint() == WHITE, "no skin leaves fruit as they are");
			shopBuy(SHOP_SKINS, 0);
			labCheck(shopOwned(SHOP_SKINS, 0) && !shopOwned(SHOP_SKINS, 1) && !shopOwned(SHOP_SPLATS, 0), "buying a skin doesn't unlock a splat");
			shopEquip(SHOP_SKINS, 0);
			labCheck(gSave.skin == 1 && shopWornSkin() == &shopSkinLook(0), "an equipped skin is the one worn");
			gSave.ownedExtras = 0;
			shopCheckSave();
			labCheck(gSave.skin == 0, "a skin not owned is taken off after a load");
			gSave.ownedExtras = extras;
			gSave.coins = coins;
			gSave.skin = skin;
		}
		{
			const u8 board = gSave.board, off = gSave.seasonalOff;
			gSave.board = 0;
			gSave.seasonalOff = 0;
			labCheck(!strcmp(kBoardNames[gfx::shownBoardOn(10, 31)], "Pumpkin Patch"), "late October shows the Pumpkin Patch");
			labCheck(!strcmp(kBoardNames[gfx::shownBoardOn(12, 1)], "Snowfall"), "December shows Snowfall");
			labCheck(!strcmp(kBoardNames[gfx::shownBoardOn(4, 10)], "Spring Blossom"), "spring shows Spring Blossom");
			labCheck(gfx::shownBoardOn(7, 1) == 0, "summer keeps the chosen board");
			gSave.board = kBaseBoards;
			labCheck(gfx::shownBoardOn(12, 1) == kBaseBoards, "a bought board is never replaced by the season");
			gSave.board = 0;
			gSave.seasonalOff = 1;
			labCheck(gfx::shownBoardOn(12, 1) == 0, "the setting turns seasonal boards off");
			gSave.board = board;
			gSave.seasonalOff = off;
			bool allBeat = true;
			for (int t = 0; t < kTrackCount; t++) allBeat = allBeat && audio::trackHasBeat(t);
			labCheck(allBeat, "every Shop song can be played in Beat Slice");
		}
		{
			// Pass and Play: names are cleaned, the winner is right, and the bests stay untouched.
			passSetName(0, " J@ne 2");
			labCheck(strcmp(sPassNames[0], "Jne 2") == 0, "a typed name keeps only letters, numbers and spaces");
			passSetName(0, "@@@");
			labCheck(strcmp(sPassNames[0], "Player 1") == 0, "an empty name goes back to the player's number");
			sPassPlayers = 1;
			sPassScores[0] = 5;
			sPassScores[1] = 9;
			sPassScores[2] = 3;
			labCheck(passWinner() == 1, "Pass and Play finds the winner");
			sPassScores[2] = 9;
			labCheck(passWinner() == -1, "a tie at the top has no winner");
			const u32 bestBefore = gSave.best[MODE_CLASSIC];
			sGame.start(MODE_CLASSIC);
			sGame.passName = sPassNames[0];
			sGame.score = bestBefore + 500;
			sGame.commitScore();
			labCheck(gSave.best[MODE_CLASSIC] == bestBefore && !sGame.newBest, "a Pass and Play run leaves the bests alone");
			sPassPlayers = 0;
			// The Mixer: its rules reach the run, its score stays apart, and the fixed-sequence modes ignore it.
			const u32 savedBest = gSave.best[MODE_CLASSIC], savedMixed = gSave.bestMixer[MODE_CLASSIC];
			gSave.best[MODE_CLASSIC] = 50;
			gSave.bestMixer[MODE_CLASSIC] = 0;
			memset(gSave.mixer, 0, sizeof gSave.mixer);
			gSave.mixer[MIX_LOW_GRAVITY] = 1;
			sGame.start(MODE_CLASSIC);
			labCheck(sGame.mixed && sGame.best == 0 && sGame.gravity < 300, "a Mixer run starts from its own best, with its rules on");
			sGame.score = 10;
			sGame.keepBestSoFar();
			labCheck(gSave.best[MODE_CLASSIC] == 50 && gSave.bestMixer[MODE_CLASSIC] == 10, "a Mixer score is kept apart from the normal best");
			sGame.start(MODE_DAILY);
			labCheck(!sGame.mixed, "Daily Fruit ignores the Mixer");
			gSave.mixer[MIX_LOW_GRAVITY] = 0;
			gSave.mixer[MIX_SIDES] = 1;
			sGame.start(MODE_CLASSIC);
			float tx = kFieldW * 0.5f, tvx = 0, tvy = 0;
			sGame.labThrow(tx, tvx, tvy);
			labCheck((tx < 0 && tvx > 0) || (tx > kFieldW && tvx < 0), "Sides Only throws in from the edges");
			gSave.mixer[MIX_SIDES] = 0;
			gSave.mixer[MIX_GIANT] = 1;
			sGame.start(MODE_CLASSIC);
			labCheck(sGame.fruitSize() > 1, "Giant Fruit makes the fruit bigger");
			adjustRow(kMixerRows[MIX_TINY], 1);
			labCheck(gSave.mixer[MIX_TINY] && !gSave.mixer[MIX_GIANT], "Tiny Fruit turns Giant Fruit off");
			memset(gSave.mixer, 0, sizeof gSave.mixer);
			gSave.mixer[MIX_MIRROR] = 1;
			sGame.start(MODE_CLASSIC);
			Input mirrorTest{};
			mirrorTest.touching = true;
			mirrorTest.tx = 40;
			labCheck(sGame.bladeInput(mirrorTest).tx == kFieldW - 40, "Mirror flips the blade left and right");
			memset(gSave.mixer, 0, sizeof gSave.mixer);
			sGame.start(MODE_CLASSIC);
			labCheck(!sGame.mixed && sGame.best == 50, "with every rule off, a run uses the normal best");
			gSave.best[MODE_CLASSIC] = savedBest;
			gSave.bestMixer[MODE_CLASSIC] = savedMixed;
			// Latency Test: the middle slice wins, Beat Timing is stored and used, and older saves start at 0 ms.
			float latSet[] = {0.05f, 0.04f, 0.3f, 0.06f, -0.25f, 0.045f};
			labCheck(fabsf(latencyMedian(latSet, 6) - 0.0475f) < 0.0001f, "the Latency Test takes the middle slice, not a stray one");
			labCheck(beatLatencyStore(0.038f) == 119 && beatLatencyStore(-1) == 0 && beatLatencyStore(1) == 200, "Beat Timing stores in 2 ms steps and stays in range");
			const u8 latBefore = gSave.beatLatency, rulesBefore = gSave.scoreRules;
			const u32 zenBefore = gSave.best[MODE_ZEN];
			gSave.beatLatency = 0;
			gSave.scoreRules = 1;
			saveRetireOldBests();
			labCheck(gSave.beatLatency == 100 && gSave.scoreRules == 2 && gSave.best[MODE_ZEN] == zenBefore, "a save from before the Latency Test starts at 0 ms and keeps its bests");
			gSave.scoreRules = rulesBefore > 2 ? rulesBefore : 2;
			sGame.start(MODE_CLASSIC);
			sGame.beatPerLoop = 0;
			sGame.beatFirst = 0;
			sGame.beatStepLen = 0.5f;
			sGame.songT = 1.0f;
			gSave.beatLatency = beatLatencyStore(0.04f);
			labCheck(fabsf(sGame.beatOffset() + 0.04f) < 0.003f, "Beat Timing moves when Beat Slice hears the beat");
			{
				// A whole test, pressing A three frames after every tick.
				const Screen wasScreen = sScreen;
				startLatencyTest();
				int seen = 0, wait = -1;
				for (int i = 0; i < 60 * 40 && !sLatDone; i++) {
					Input k{};
					if (wait == 0) k.down = KEY_A;
					if (wait >= 0) wait--;
					updateLatency(k, false, false);
					if (sLatTicks != seen) {
						seen = sLatTicks;
						wait = 2;
					}
				}
				labCheck(sLatDone && fabsf(sLatResult - 3 * kDt) < 0.002f, "the Latency Test measures slices three frames late");
				sScreen = wasScreen;
				audio::music(MUSIC_MENU);
			}
			gSave.beatLatency = latBefore;
#if PORT_ULTRA
			{
				// Textures: Ultra loads the 4K art, the other choices never do, and a saved Ultra comes back.
				const u8 texBefore = gSave.texQuality;
				gSave.texQuality = 3;
				port::applyGraphics();
				labCheck(gfx::ultraLoaded(), "Textures: Ultra loads the 4K art");
				for (int q = 0; q < 3; q++) {
					gSave.texQuality = (u8)q;
					port::applyGraphics();
					if (gfx::ultraLoaded()) break;
				}
				labCheck(!gfx::ultraLoaded(), "Low, Medium and High never load the 4K art");
				gSave.texQuality = 3;
				saveWrite();
				saveLoad();
				labCheck(gSave.texQuality == 3, "a saved Ultra is still Ultra after a restart");
				gSave.texQuality = texBefore;
				port::applyGraphics();
				saveWrite();
			}
#endif
#if PORT_3DS
			// The 3DSX update's file swap, on stand-in files beside the save.
			static const char* const kLabOld = "sdmc:/3ds/FruitSlicer/lab_swap.bin";
			static const char* const kLabNew = "sdmc:/3ds/FruitSlicer/lab_swap.bin.new";
			const auto write = [](const char* path, const char* text) {
				FILE* f = fopen(path, "wb");
				if (f) {
					fputs(text, f);
					fclose(f);
				}
			};
			const auto holds = [](const char* path, const char* text) {
				char got[8] = {};
				FILE* f = fopen(path, "rb");
				if (!f) return false;
				fread(got, 1, sizeof got - 1, f);
				fclose(f);
				return strcmp(got, text) == 0;
			};
			write(kLabOld, "old");
			remove(kLabNew);
			labCheck(!port::replaceFile(kLabOld, kLabNew) && holds(kLabOld, "old"), "a 3DSX update with no download leaves the game alone");
			write(kLabNew, "new");
			labCheck(port::replaceFile(kLabOld, kLabNew) && holds(kLabOld, "new"), "a 3DSX update replaces the game file");
			FILE* leftover = fopen("sdmc:/3ds/FruitSlicer/lab_swap.bin.new", "rb");
			FILE* backup = fopen("sdmc:/3ds/FruitSlicer/lab_swap.bin.old", "rb");
			labCheck(!leftover && !backup, "a 3DSX update leaves no extra files behind");
			if (leftover) fclose(leftover);
			if (backup) fclose(backup);
			remove(kLabOld);
#endif
			const u32 zen = gSave.best[MODE_ZEN], combo = gSave.best[MODE_COMBO], classic = gSave.best[MODE_CLASSIC];
			const u8 rules = gSave.scoreRules;
			gSave.scoreRules = 0;
			gSave.best[MODE_ZEN] = gSave.best[MODE_COMBO] = gSave.best[MODE_CLASSIC] = 77;
			saveRetireOldBests();
			labCheck(gSave.best[MODE_ZEN] == 0 && gSave.best[MODE_COMBO] == 0 && gSave.best[MODE_CLASSIC] == 77, "a 1.0 save loses only its Zen and Combo Rush bests");
			gSave.best[MODE_ZEN] = 77;
			saveRetireOldBests();
			labCheck(gSave.best[MODE_ZEN] == 77, "old bests are cleared only once");
			gSave.best[MODE_ZEN] = zen;
			gSave.best[MODE_COMBO] = combo;
			gSave.best[MODE_CLASSIC] = classic;
			gSave.scoreRules = rules;
		}
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
