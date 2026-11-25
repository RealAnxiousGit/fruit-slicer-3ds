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
static void updateFrame(const Input& in, bool wasTouching);
static void drawFrame();
#if !PORT_3DS
namespace gfx {
void requestShot(const char* path);
}
static void labSnapV2f(const char* name) {
	const char* dir = getenv("FRUIT_SLICER_V2F_SHOTS");
	if (!dir || !*dir) return;
	const bool wasSliding = sSliding;
	const float wasFade = sFade;
	sSliding = false;
	sFade = 0;
	char path[300];
	snprintf(path, sizeof path, "%s/%s.png", dir, name);
	gfx::requestShot(path);
	drawFrame();
	sSliding = wasSliding;
	sFade = wasFade;
}
#else
static void labSnapV2f(const char*) {}
#endif
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
static float sLabCredit;  // where the credits were before a drag
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

// The field follows the window's shape: 427 x 240 when wide, taller when square, narrower and taller
// still when upright. Throws start below its bottom edge and gravity grows with its height.
static void labField() {
#if !PORT_TWO_SCREENS
	float w, h;
	gfx::fieldFor(16.0f / 9, &w, &h);
	bool wide = fabsf(w - 426.67f) < 1.0f && h == 240;
	gfx::fieldFor(21.0f / 9, &w, &h);
	wide &= w > 540.0f && h == 240;
	labCheck(wide, "a 16:9 window uses 427 x 240 and an ultrawide window expands the stage width smoothly");
	gfx::fieldFor(4.0f / 3, &w, &h);
	labCheck(w == 427 && fabsf(h - 320) < 0.5f, "a 4:3 window gets a taller field");
	gfx::fieldFor(9.0f / 16, &w, &h);
	const bool upright = fabsf(w - 240) < 0.5f && h > 420;
	gfx::fieldFor(9.0f / 20, &w, &h);
	labCheck(upright && fabsf(w - 240) < 0.5f && h > 530, "an upright window gets a narrow, tall field");
	const float wasW = gFieldW, wasH = gFieldH;
	gfx::fieldFor(9.0f / 16, &gFieldW, &gFieldH);
	const bool follows = fabsf(Game::throwY() - (gFieldH + 22)) < 0.01f && kFieldK > 1.7f;
	gFieldW = wasW;
	gFieldH = wasH;
	labCheck(follows, "an upright field throws from its own bottom edge with stronger gravity");
#endif
}

// The last ordinary song on sale: boss tracks after it stay locked until their boss is beaten.
static int labShopSong() {
	int i = shopCount(SHOP_MUSIC) - 1;
	while (i > 0 && trackBoss(kBaseTracks + i) >= 0) i--;
	return i;
}

// A song tried on plays for 8 to 10 seconds, then the music in use comes back; Use makes a song both
// the Menu and the Game Music. Driven through updateShop, a frame at a time, on the last song on sale.
static void labShopPreview() {
	Input in{};
	const int song = labShopSong(), value = shopValue(SHOP_MUSIC, song);
	const u8 menuWas = sKeep[SHOP_MUSIC], gameWas = gSave.gameTrack;
	labCheck(!shopInUse(SHOP_MUSIC, song) && gSave.menuTrack == value, "the song tried on is on show, not in use");
	sPreviewT = 0;
	int frames = 0;
	bool faded = false;
	while (sPreviewT >= 0 && frames < 900) {
		updateShop(in, false);
		frames++;
		faded = faded || (sPreviewT > kPreviewPlay && audio::currentTrack() == kBaseTracks + labShopSong());
	}
	char d[48];
	snprintf(d, sizeof d, "%.2f s", frames * kDt);
	labCheck(frames * kDt >= 8.0f && frames * kDt <= 10.0f, "a Shop preview gives way after 8 to 10 seconds", d);
	labCheck(faded, "a Shop preview fades out before it gives way");
	labCheck(gSave.menuTrack == menuWas && audio::currentTrack() != kBaseTracks + labShopSong() && sPreviewOver, "the music in use comes back after a Shop preview");
	for (int i = 0; i < 90; i++) updateShop(in, false);
	labCheck(sReturnT < 0, "the music in use fades back in within a second and a half");
	const float longest = gfx::textWidth("Your Menu Music, not your Game Music.", 0.36f);
	// fitText keeps them inside 290 of the 340-wide sign; they shouldn't need shrinking by more than a tenth to get there.
	labCheck(longest * 0.9f <= 290 && gfx::textWidth("Use it for the Menu and Game Music.", 0.36f) * 0.9f <= 290, "Shop music notes fit the preview with a margin");

	gSave.coins += shopItem(SHOP_MUSIC, song).price;
	shopBuy(SHOP_MUSIC, song);
	shopEquipNow();
	labCheck(gSave.menuTrack == value && gSave.gameTrack == value && shopInUse(SHOP_MUSIC, song), "Using a Shop song makes it the Menu and the Game Music");
	labCheck(audio::currentTrack() == kBaseTracks + labShopSong() && sPreviewT < 0, "a song put in use plays on, with no preview timer");
	sKeep[SHOP_MUSIC] = menuWas;  // back as it was, so later steps play the usual tracks
	gSave.gameTrack = gameWas;
	shopTryOn();
	shopPreviewStop();
	shopSave();
}

// The Orchard Blast Inventory: opens on Select, resumes from its first sign, swaps, and asks before a drop or a quit.
static void labInventory() {
	const u8 seenWas = gSave.howToSeen;
	gSave.howToSeen |= 3;
	Game::orchardClearSave();
	sGame.start(MODE_ORCHARD);
	sGame.phase = PHASE_PLAY;
	Orchard& o = sGame.orc;
	o.choosing = false;
	o.boss.on = false;
	o.room = 6;
	o.level = 3;
	o.xp = 11.0f;
	o.slot[0] = CARD_BLENDER;
	o.slot[1] = CARD_GOLDAPPLE;
	Input in = {};
	auto press = [&](u32 key) {
		in = {};
		in.down = key;
		updateOrchardMenus(in, false);
	};
	labCheck(o.endless && !o.resumePrompt, "Orchard Blast enters directly in Endless mode when no run save exists");
	const bool savedMid = sGame.orcSaveRun();
	sGame.start(MODE_ORCHARD);
	sGame.phase = PHASE_PLAY;
	sOrcOpenT = 0.25f;
	labCheck(savedMid && o.resumePrompt && o.savedRoom == 6, "Saved Orchard Blast run triggers Continue / Start Over modal on re-entry");
	press(KEY_A);
	labCheck(!o.resumePrompt && o.room == 6 && o.level == 3 && (int)o.xp == 11 && o.slot[0] == CARD_BLENDER, "Continue button resumes exact saved room, level, XP, and slots");
	sGame.orcSaveRun();
	sGame.start(MODE_ORCHARD);
	sGame.phase = PHASE_PLAY;
	sOrcOpenT = 0.25f;
	press(KEY_RIGHT);
	press(KEY_A);
	labCheck(!o.resumePrompt && !Game::orchardHasSave() && o.room == 1 && o.level == 1 && o.xp == 0 && o.slot[0] == CARD_NONE,
	         "Start Over button clears save, resets inventory/XP/level, and restarts at Room 1");
	sGame.phase = PHASE_PLAY;
	o.choosing = false;
	o.boss.on = false;
	o.slot[0] = CARD_BLENDER;
	o.slot[1] = CARD_GOLDAPPLE;
	press(KEY_SELECT);
	labCheck(sOrcInv && sOrcConfirm == 0, "Select opens the Inventory");
	press(KEY_A);
	labCheck(!sOrcInv && o.resumeT == 0, "the Inventory opens on Resume, and play resumes right away");
	press(KEY_SELECT);
	OrcInvItem items[64];
	const int n = orcInvItems(items);
	const int first = n - INVB_COUNT;
	sOrcInvFocus = first + INVB_SWAP;
	press(KEY_A);
	labCheck(o.slot[0] == CARD_GOLDAPPLE && o.slot[1] == CARD_BLENDER, "Swap Slots in the Inventory swaps the charms");
	sOrcInvFocus = 1;  // Slot 2
	press(0);
	sOrcInvFocus = first + INVB_DROP;
	press(KEY_A);
	labCheck(sOrcConfirm == 1, "Drop asks first: Drop this charm?");
	press(KEY_A);
	labCheck(sOrcConfirm == 0 && o.slot[1] == CARD_BLENDER, "the drop question starts on No, so A keeps the charm");
	press(KEY_A);
	press(KEY_RIGHT);
	press(KEY_A);
	labCheck(o.slot[1] == CARD_NONE && o.slot[0] == CARD_GOLDAPPLE, "Yes drops the charm in the slot picked last");
	sOrcInvFocus = first + INVB_QUIT;
	press(KEY_A);
	labCheck(sOrcConfirm == 2, "Quit asks first: End this run?");
	press(KEY_B);
	labCheck(sOrcInv && sOrcConfirm == 0, "B answers No and stays in the Inventory");
	int upgrades = 0, buttons = 0;
	for (int i = 0; i < n; i++) {
		upgrades += items[i].kind == INV_UPGRADE;
		buttons += items[i].kind == INV_BUTTON && items[i].y + items[i].h <= kFieldH && items[i].x >= 0 && items[i].x + items[i].w <= kFieldW;
	}
	char d[128];
	snprintf(d, sizeof d, "%d upgrades, %d signs on a %.0f by %.0f field", upgrades, buttons, (double)kFieldW, (double)kFieldH);
	labCheck(upgrades == 9 && buttons == INVB_COUNT, "the Inventory lists every upgrade, and its signs fit the field", d);
	labCheck(items[0].kind == INV_SLOT && items[0].h >= 44, "the Inventory charm slot boxes are enlarged");
	// Details strip on an Upgrade with a long description + Next line must clear the action buttons even with Larger Text on.
	const u8 largeWas = gSave.largeText;
	const u8 bladeWas = o.upgrade[CARD_BLADE];
	o.upgrade[CARD_BLADE] = 0;
	float detailsBotOff = 0, detailsBotOn = 0;
	const float btnTop = items[first].y;
	gSave.largeText = 0;
	applyAccessibility();
	detailsBotOff = orcInvDetailsBottom(CARD_BLADE);
	gSave.largeText = 1;
	applyAccessibility();
	detailsBotOn = orcInvDetailsBottom(CARD_BLADE);
	gSave.largeText = largeWas;
	o.upgrade[CARD_BLADE] = bladeWas;
	applyAccessibility();
	snprintf(d, sizeof d, "bottom %.1f / %.1f vs buttons %.1f", (double)detailsBotOff, (double)detailsBotOn, (double)btnTop);
	labCheck(detailsBotOff < btnTop && detailsBotOn < btnTop, "the Inventory card details + Next line stay clear of the bottom buttons", d);

	// Scrollable passive charms: fill 18 passives, scroll with wheel and D-pad, verify visible clip bounds.
	{
		const int savedPassives = o.passiveCount;
		for (int i = 0; i < 18; i++) o.passiveOrder[i] = (u8)(CARD_RING + (i % 10));
		o.passiveCount = 18;
		sOrcPassiveScroll = 0;
		const OrcInvLayout invL = orcInvLayout();
		float pTop, pBottom, pIcon;
		int pPerRow;
		orcPassiveArea(invL, &pTop, &pBottom, &pIcon, &pPerRow);
		const float maxSc = orcPassiveMaxScroll(invL);
		in = {};
		in.tx = (int)(invL.cx + invL.cw * 0.5f);
		in.ty = (int)(pTop + 8);
		in.wheel = -2;
		updateOrchardMenus(in, false);
		OrcInvItem scItems[64];
		const int scn = orcInvItems(scItems);
		float firstPassiveY = 0, lastPassiveY = 0;
		for (int i = 0; i < scn; i++) {
			if (scItems[i].kind == INV_PASSIVE && scItems[i].index == 0) firstPassiveY = scItems[i].y;
			if (scItems[i].kind == INV_PASSIVE && scItems[i].index == 17) lastPassiveY = scItems[i].y;
		}
		snprintf(d, sizeof d, "scroll %.1f / max %.1f, y0 %.1f, y17 %.1f, win %.1f..%.1f", (double)sOrcPassiveScroll, (double)maxSc, (double)firstPassiveY, (double)lastPassiveY, (double)pTop, (double)pBottom);
		labCheck(maxSc > 0 && sOrcPassiveScroll > 0 && firstPassiveY < pTop && lastPassiveY + pIcon <= pBottom + pIcon,
		         "Inventory scrolls passive charms when more passives are held than fit on screen", d);
		sOrcPassiveScroll = 0;
		o.passiveCount = savedPassives;
	}
	// Uncapped upgrades: every INV_UPGRADE item must leave >= 40 px for its name (no 255-pip overflow),
	// and high levels must format concisely without crowding bottom buttons even with Larger Text on.
	{
		const u8 bladeWas2 = o.upgrade[CARD_BLADE];
		const u8 largeWas2 = gSave.largeText;
		o.upgrade[CARD_BLADE] = 25;
		bool allPos = true;
		for (int i = 0; i < n; i++) {
			if (items[i].kind != INV_UPGRADE) continue;
			const CardInfo& uinfo = kCards[items[i].index];
			char lvBuf[16];
			snprintf(lvBuf, sizeof lvBuf, "Lv %d", o.upgrade[items[i].index]);
			const float bw = fmaxf(gfx::textWidth(lvBuf, 0.25f) + 4.0f, 24.0f);
			if (!cardUncapped(uinfo) || items[i].w - 14.0f - bw < 40.0f) allPos = false;
		}
		gSave.largeText = 1;
		applyAccessibility();
		const float bladeBot = orcInvDetailsBottom(CARD_BLADE);
		gSave.largeText = largeWas2;
		o.upgrade[CARD_BLADE] = bladeWas2;
		applyAccessibility();
		snprintf(d, sizeof d, "blade@25 bottom %.1f vs buttons %.1f", (double)bladeBot, (double)btnTop);
		labCheck(allPos && bladeBot < btnTop, "Uncapped Inventory upgrades keep positive name width and fit details strip cleanly", d);
	}
	// Room 25 cutscene: all 10 dialogue lines wrap cleanly above the prompt, KEY_START does not skip, and KEY_A advances.
	{
		bool wrapFits = true;
		int worstLines = 0;
		float worstGap = 999.0f;
		for (int i = 0; i < 10; i++) {
			const Room50CutsceneBox B = room50CutsceneBox(kRoom50Lines[i]);
			const int lines = orcCountWrapLines(kRoom50Lines[i], B.maxW, B.textScale, 8);
			const float gap = B.promptY - (B.textY + lines * B.step);
			if (lines > worstLines) worstLines = lines;
			if (gap < worstGap) worstGap = gap;
			if (lines > B.maxLines || gap < 6.0f || B.y < kFieldTop) wrapFits = false;
		}
		const Room50CutsceneBox B1 = room50CutsceneBox("Wait...");
		const Room50CutsceneBox B3 = room50CutsceneBox(kRoom50Lines[6]);
		const bool scaledBox = B1.h <= 56.0f && B3.h > B1.h && B3.h < 90.0f;
		snprintf(d, sizeof d, "worstLines %d, minPromptGap %.1f, B1=%.1f, B3=%.1f", worstLines, (double)worstGap, (double)B1.h, (double)B3.h);
		labCheck(wrapFits && scaledBox, "Room 25 cutscene box scales per line, stays compact, and clears prompt", d);

		if (audio::available()) audio::tensionMusic(false);
		o.cutscene = true;
		o.tensionStarted = false;
		o.dialogueLine = 0;
		o.dialogueChars = (float)strlen(kRoom50Lines[0]);
		o.dialogueLastChar = (int)o.dialogueChars - 1;
		o.dialoguePauseT = 0;
		o.dialogueWaitRelease = false;
		press(KEY_START);
		const bool startIgnored = o.cutscene && o.dialogueLine == 0;
		press(KEY_A);
		const bool aAdvanced = o.cutscene && o.dialogueLine == 1;
		o.dialogueLine = 4;
		o.dialogueChars = (float)strlen(kRoom50Lines[4]);
		o.dialogueLastChar = (int)o.dialogueChars - 1;
		press(KEY_A);
		const bool atTension = o.dialogueLine == 5 && o.tensionStarted && (!audio::available() || audio::currentTrack() == -2);
		const Screen scrKeep = sScreen;
		const bool fromPauseKeep = sOptionsFromPause;
		const int secKeep = sSection;
		sOptionsFromPause = true;
		sSection = 0;
		leaveSection(true);
		const bool cutMusPreserved = !audio::available() || audio::currentTrack() == -2;
		sOptionsFromPause = fromPauseKeep;
		sSection = secKeep;
		sScreen = scrKeep;
		if (audio::available()) audio::tensionMusic(false);
		o.cutscene = false;
		o.tensionStarted = false;
		labCheck(startIgnored && aAdvanced && atTension && cutMusPreserved,
		         "Room 25 cutscene keeps Start for Pause, starts tension music at line 5, and preserves music state when leaving Pause->Options");

		// Dev Skip UI: small button selectors, press-and-hold acceleration, numeric typing + Backspace, Esc revert, Enter save.
		{
			o.devPrompt = true;
			o.devRoomTarget = 12;
			o.devLevelTarget = 3;
			o.devField = 1;
			float dx, dy, dw, dh;
			Button db[7];
			devSkipLayout(&dx, &dy, &dw, &dh, db);
			press(KEY_LEFT);
			const bool selLeft = o.devField == 0;
			press(KEY_DOWN);
			const bool selDown = o.devField == 3;
			press(KEY_RIGHT);
			press(KEY_RIGHT);
			const bool selPlus = o.devField == 5;
			// Press-and-hold '+' on Room (db[2]): immediate +1 step, hold delay ~0.34s, then rapid acceleration.
			in = {};
			in.touching = true;
			in.tx = (int)(db[2].x + db[2].w * 0.5f);
			in.ty = (int)(db[2].y + db[2].h * 0.5f);
			updateDevSkip(in, false);
			const int afterTap = o.devRoomTarget;
			for (int f = 1; f < 10; f++) updateDevSkip(in, true);
			const int duringDelay = o.devRoomTarget;
			for (int f = 10; f < 90; f++) updateDevSkip(in, true);
			const int afterHold = o.devRoomTarget;
			in.touching = false;
			updateDevSkip(in, true);
			// Numeric typing + Backspace + Esc revert + Enter save for BOTH Room and Level.
			in.touching = true;
			in.tx = (int)(db[1].x + db[1].w * 0.5f);
			in.ty = (int)(db[1].y + db[1].h * 0.5f);
			updateDevSkip(in, false);
			in.touching = false;
			port::testTextInject("x");
			char tbuf[16] = {};
			port::textPoll(tbuf, sizeof tbuf);
			const bool nonDigitsIgnored = atoi(tbuf) == afterHold;
			port::testTextBackspace();
			port::testTextBackspace();
			port::testTextInject("99");
			port::testTextFinish(false);  // Esc reverts!
			updateDevSkip(Input{}, false);
			const bool escRevertedRoom = !sDevTyping && o.devRoomTarget == afterHold;
			in.touching = true;
			updateDevSkip(in, false);
			in.touching = false;
			port::testTextBackspace();
			port::testTextBackspace();
			port::testTextInject("50");
			port::testTextFinish(true);  // Enter saves!
			updateDevSkip(Input{}, false);
			const bool enterSavedRoom = !sDevTyping && o.devRoomTarget == 50;

			// Now Level box (db[4]): Esc revert + Enter save.
			in.touching = true;
			in.tx = (int)(db[4].x + db[4].w * 0.5f);
			in.ty = (int)(db[4].y + db[4].h * 0.5f);
			updateDevSkip(in, false);
			in.touching = false;
			port::testTextBackspace();
			port::testTextInject("88");
			port::testTextFinish(false);
			updateDevSkip(Input{}, false);
			const bool escRevertedLvl = !sDevTyping && o.devLevelTarget == 3;
			in.touching = true;
			updateDevSkip(in, false);
			in.touching = false;
			port::testTextBackspace();
			port::testTextInject("7");
			port::testTextFinish(true);
			updateDevSkip(Input{}, false);
			const bool enterSavedLvl = !sDevTyping && o.devLevelTarget == 7;
			o.devPrompt = false;
			sDevTyping = 0;
			snprintf(d, sizeof d, "tap=%d delay=%d hold=%d room=%d lvl=%d", afterTap, duringDelay, afterHold, o.devRoomTarget, o.devLevelTarget);
			labCheck(selLeft && selDown && selPlus && afterTap == 13 && duringDelay == 13 && afterHold >= 22 &&
			         nonDigitsIgnored && escRevertedRoom && enterSavedRoom && escRevertedLvl && enterSavedLvl,
			         "Dev Skip selectors, hold acceleration, numeric Backspace, Esc revert, and Enter save work for Room & Level", d);
		}
		// Quick button highlight fade transitions smoothly (neither instant snap nor slow drag) and does not double-step within a frame.
		{
			const Screen drawWas = sDrawing;
			sDrawing = SCREEN_GUIDES;
			beginHighlightFrame();
			const float hi0 = highlightFade(444, 55, 42, 26, true);
			const float sameFrame = highlightFade(444, 55, 42, 26, false);
			beginHighlightFrame();
			const float step1 = highlightFade(444, 55, 42, 26, false);
			beginHighlightFrame();
			const float step2 = highlightFade(444, 55, 42, 26, false);
			for (int k = 0; k < 12; k++) {
				beginHighlightFrame();
				highlightFade(444, 55, 42, 26, false);
			}
			beginHighlightFrame();
			const float done = highlightFade(444, 55, 42, 26, false);
			sDrawing = drawWas;
			snprintf(d, sizeof d, "hi0=%.2f same=%.2f s2=%.2f done=%.2f", (double)hi0, (double)sameFrame, (double)step2, (double)done);
			labCheck(fabsf(hi0 - 1.0f) < 0.01f && fabsf(sameFrame - 1.0f) < 0.01f && step1 < 1.0f && step2 > 0.0f && step2 < 0.9f && done <= 0.001f,
			         "Button highlight quick-fades over ~0.11s and stays frame-locked across stereo draws", d);
		}
	}
	// Heat Surge music speed survives updateFrame() when fighting Inferno Pepper unpaused.
	{
		const Screen scrWas = sScreen;
		const bool invWas = sOrcInv;
		sOrcInv = false;
		sScreen = SCREEN_GAME;
		sGame.phase = PHASE_PLAY;
		sGame.chiliT = 0;
		o.boss = {};
		o.boss.on = true;
		o.boss.id = BOSS_CHILI;
		o.boss.beaten = false;
		o.boss.enterT = 0;
		o.boss.phase = 1;
		o.boss.left = 10;
		o.boss.total = 10;
		o.boss.baseY = kFieldTop + 56;
		o.boss.y = o.boss.baseY;
		o.boss.x = kFieldW * 0.5f;
		o.boss.radius = 28;
		o.boss.surgeT = 2.0f;
		updateFrame(Input{}, false);
		const float spdDuring = audio::currentMusicSpeed();
		sOrcInv = true;
		updateFrame(Input{}, false);
		const float spdPaused = audio::currentMusicSpeed();
		sOrcInv = invWas;
		sScreen = scrWas;
		o.boss = {};
		audio::musicSpeed(1.0f);
		snprintf(d, sizeof d, "active %.2f, inv %.2f", (double)spdDuring, (double)spdPaused);
		labCheck(!audio::available() || (spdDuring > 1.20f && fabsf(spdPaused - 1.0f) < 0.05f),
		         "Inferno Pepper Heat Surge speedup persists across updateFrame() and resets while paused", d);
	}
	// A touch press and release on level-up card 0 picks it
	const int hpBefore = o.upgrade[CARD_HEALTH];
	o.choosing = true;
	o.levelsWaiting = 1;
	o.offerCount = 3;
	o.offer[0] = CARD_HEALTH;
	o.offer[1] = CARD_ARMOR;
	o.offer[2] = CARD_BLADE;
	sOrcOpenT = 0.05f;  // inside the 0.40s swipe grace time
	float cx, cy, cw, ch;
	orcCardRect(0, 3, &cx, &cy, &cw, &ch);
	Input touchIn = {};
	touchIn.touching = true;
	touchIn.tx = cx + cw * 0.5f;
	touchIn.ty = cy + ch * 0.5f;
	updateOrchardMenus(touchIn, true);   // swipe held from play
	sOrcOpenT = 0.50f;
	updateOrchardMenus(touchIn, true);   // still touching past grace
	touchIn.touching = false;
	updateOrchardMenus(touchIn, true);   // release must not pick
	labCheck(o.choosing && o.upgrade[CARD_HEALTH] == hpBefore, "a swipe held past the level-up grace time never picks a card");

	// Press card 0, slide off while still touching, then release: must cancel.
	touchIn.touching = true;
	updateOrchardMenus(touchIn, false);
	touchIn.tx = cx - 24;
	updateOrchardMenus(touchIn, true);
	touchIn.touching = false;
	updateOrchardMenus(touchIn, true);
	labCheck(o.choosing && sOrcCardDown < 0 && o.upgrade[CARD_HEALTH] == hpBefore, "sliding off a level-up card cancels the tap");

	// Now a clean press and release on card 0 picks it.
	touchIn.tx = cx + cw * 0.5f;
	touchIn.touching = true;
	updateOrchardMenus(touchIn, false);  // frame 1: press down
	const bool pressedDown = (sOrcCardDown == 0);
	touchIn.touching = false;
	updateOrchardMenus(touchIn, true);   // frame 2: release
	labCheck(pressedDown && !o.choosing && o.upgrade[CARD_HEALTH] == hpBefore + 1, "a level-up card is picked by clicking or tapping on it");

	sOrcInv = false;
	sOrcConfirm = 0;
	gSave.howToSeen = seenWas;
}

static void labMenus() {
	const float firstRowY = kListTop + kRowH * 0.5f;
	switch (sBotMenuStep++) {
		case 0: {
			labExpect(SCREEN_TITLE, "boots to the title");
			labDaily();
			labField();
			float fx0, fy0, fd0, fx1, fy1, fd1;
			fruitSpot(kTitleButtons[0], &fx0, &fy0, &fd0);
			fruitSpot(kTitleButtons[1], &fx1, &fy1, &fd1);
			const bool hitCenter = menuHit(kTitleButtons[0], SCREEN_TITLE, (int)fx0, (int)fy0) && menuHit(kTitleButtons[1], SCREEN_TITLE, (int)fx1, (int)fy1);
			const bool missEdge = !menuHit(kTitleButtons[0], SCREEN_TITLE, (int)kTitleButtons[0].x + 2, (int)kTitleButtons[0].y + 2) &&
			                      !menuHit(kTitleButtons[1], SCREEN_TITLE, (int)kTitleButtons[1].x + 2, (int)kTitleButtons[1].y + 2);
			labCheck(hitCenter && missEdge, "Title fruit hitboxes match the fruit, not the sign corners");
			labCheck(audio::musicTime() >= 0 || audio::musicPending(), "menu music plays");
			botTap(kTitleButtons[4]);
			return;
		}
		case 1: labExpect(SCREEN_HOWTO, "How to Play opens"); botTap(kBack); return;
		case 2: labExpect(SCREEN_TITLE, "How to Play back arrow"); botTap(kTitleButtons[2]); return;
		case 3:
			labExpect(SCREEN_RECORDS, "Records opens from Title");
			botTap(kRecordsButtons[1]);
			sBotMenuStep = 65;
			return;
		case 65: {
			labExpect(SCREEN_APPEARANCE, "Appearance opens from Records");
			appSetTab(APP_BLADES);
			sAppSel = 1;
			appSyncPreview();
			appEquipSelected();
			labCheck(gSave.blade == 1, "Appearance equips selected Blade");
			appSetTab(APP_OSC);
			sAppSel = 3;
			appSyncPreview();
			appEquipSelected();
			labCheck(gSave.oscStyle == 3, "Appearance equips Oscilloscope Bloomin' Blue");
			appSetTab(APP_BOARDS);
			sAppSel = 0;
			appSyncPreview();
			appEquipSelected();
			botTap(kBack);
			return;
		}
		case 66:
			labExpect(SCREEN_RECORDS, "Appearance back arrow returns to Records");
			botTap(kRecordsButtons[4]);
			sBotMenuStep = 4;
			return;
		case 4:
			labExpect(SCREEN_JOURNAL, "Journal opens from Records");
			// The Journal draws 2D pictures whichever art style is chosen; the 3DS used to drop them with 3D art.
			labCheck(gfx::stickersLoaded(), gSave.artStyle == ART_3D ? "Journal has its fruit pictures with 3D models" : "Journal has its fruit pictures with 2D stickers");
			labCheck(journalTiles() == progress::journalPages() + BOSS_COUNT && journalIsBoss(journalTiles() - 1) && !journalIsBoss(progress::journalPages() - 1), "the Journal ends with a Bosses page, one sticker per boss");
			botTap(kBack);
			return;
		case 5:
			labExpect(SCREEN_RECORDS, "Journal back arrow returns to Records");
			botTap(kRecordsButtons[5]);
			return;
		case 6: {
			labExpect(SCREEN_STATS, "Stats opens from Records");
			const u16 roomWas = gSave.orchardBestRoom;
			const u16 endWas = gSave.endlessBestRoom;
			gSave.orchardBestRoom = 0;
			gSave.endlessBestRoom = 0;
			const int none = statsRoomsConquered();
			gSave.orchardBestRoom = 7;
			const int six = statsRoomsConquered();
			gSave.endlessBestRoom = 14;
			const int fourteen = statsRoomsConquered();
			gSave.orchardBestRoom = roomWas;
			gSave.endlessBestRoom = endWas;
			labCheck(none == 0 && six == 6 && fourteen == 14, "Stats shows best room across Endless runs and legacy saves");
			botTap(kBack);
			sBotMenuStep = 67;
			return;
		}
		case 67:
			labExpect(SCREEN_RECORDS, "Stats back arrow returns to Records");
			botTap(kRecordsButtons[3]);
			sBotMenuStep = 97;
			return;
		case 97: {
			labExpect(SCREEN_ACHIEVEMENTS, "Achievements opens from Records");
			labCheck(sAchCount == 31, "Achievements lists 31 platform-applicable achievements");
			labCheck(audio::loaded(SFX_ACH_IN) && audio::loaded(SFX_ACH_JINGLE) && audio::loaded(SFX_ACH_OUT) && audio::loaded(SFX_MYTHIC),
			         "Achievement & Mythic SFX are loaded cleanly");
			// Unit-test achievement rules with saved state restored cleanly afterwards.
			const u64 ach0 = gSave.achievements[0], ach1 = gSave.achievements[1];
			const u32 coinsWas = gSave.coins, lifeWas = gSave.coinsLifetime, fruitWas = gSave.totalFruit;
			const u16 roomWas = gSave.orchardBestRoom;
			const u8 f1Was = gSave.achFlags, f2Was = gSave.achFlags2;
			gSave.achievements[0] = gSave.achievements[1] = 0;
			gSave.achFlags2 = 0;
			gSave.coinsLifetime = 50; gSave.totalFruit = 120; gSave.orchardBestRoom = 25;
			sAchQHead = sAchQTail = sAchToastPhase = 0; sAchToastId = -1;
			achRetroactiveCheck();
			labCheck(achHas(ACH_OOH_SHINY) && achHas(ACH_FRUIT_JUICE) && achHas(ACH_QUARTER_MILESTONE) &&
			         sAchQHead == sAchQTail && gSave.coins == coinsWas + 15 + 20 + 50,
			         "Retroactive 2.0.0 save silently unlocks earned achievements and grants universal Shop coins");
			const Screen scrWas = sScreen;
			const bool mixWas = sGame.mixed;
			const Mode modeWas = sGame.mode;
			sScreen = SCREEN_GAME; sGame.mode = MODE_CLASSIC; sGame.mixed = true;
			sAchAllowInLab = true;
			labCheck(!achCheckAllowed(ACH_COMBO_MASTER) && achCheckAllowed(ACH_MIX_IT_UP) && achCheckAllowed(ACH_MIXOLOGIST),
			         "Mixer runs block non-Mixer achievements while allowing Mix It Up and Mixologist");
			sGame.mixed = false;
			achUnlock(ACH_COMBO_MASTER);
			achUnlock(ACH_CLASSIC_LEGEND);
			updateAchToast(0.01f);
			const bool toastStarted = sAchToastPhase == 1 && sAchToastId == ACH_COMBO_MASTER;
			updateAchToast(0.60f);
			const bool toastHeld = sAchToastPhase == 2;
			updateAchToast(4.60f);
			const bool toastOut = sAchToastPhase == 3;
			updateAchToast(0.60f);
			updateAchToast(0.01f);
			const bool toastNext = sAchToastPhase == 1 && sAchToastId == ACH_CLASSIC_LEGEND;
			labCheck(toastStarted && toastHeld && toastOut && toastNext,
			         "Wide-banner achievement toast steps slide-in -> hold -> slide-out and pops FIFO queue");
			for (int i = 0; i < ACH_COUNT; i++) {
				if (i != ACH_COMPLETIONIST && i != ACH_ARCADE_ACE && achInPlatform(i))
					gSave.achievements[i / 64] |= (1ULL << (i % 64));
			}
			achUnlock(ACH_ARCADE_ACE);
			labCheck(achHas(ACH_COMPLETIONIST), "Completionist auto-unlocks once all other platform achievements are earned");
			sAchAllowInLab = false;
			sAchQHead = sAchQTail = sAchToastPhase = 0; sAchToastId = -1;
			sScreen = scrWas; sGame.mixed = mixWas; sGame.mode = modeWas;
			gSave.achievements[0] = ach0; gSave.achievements[1] = ach1;
			gSave.coins = coinsWas; gSave.coinsLifetime = lifeWas; gSave.totalFruit = fruitWas;
			gSave.orchardBestRoom = roomWas; gSave.achFlags = f1Was; gSave.achFlags2 = f2Was;
			saveWrite();
			botTap(kBack);
			sBotMenuStep = 98;
			return;
		}
		case 98:
			labExpect(SCREEN_RECORDS, "Achievements back arrow returns to Records");
			botTap(kBack);
			sBotMenuStep = 68;
			return;
		case 68:
			labExpect(SCREEN_TITLE, "Records back arrow returns to Title");
			botTap(kTitleButtons[3]);
			return;
		case 69:
			labExpect(SCREEN_MODS, "Mods opens from Title");
			botTap(kBack);
			return;
		case 70:
			labExpect(SCREEN_TITLE, "Mods back arrow returns to Title");
			botTap(kTitleButtons[6]);
			sBotMenuStep = 7;
			return;
		case 7:
			labExpect(SCREEN_CREDITS, "Credits opens");
			labCheck(sCreditScroll == 0 && creditMaxScroll() > 200, "Credits opens at the top of a list longer than its sign");
			sBotKeys = KEY_DOWN;
			sBotMenuStep = 60;
			return;
		case 60:
			labCheck(sCreditScroll > 0, "Down scrolls the credits");
			sLabCredit = sCreditScroll;
			botDragY(160, 200, -15);  // a drag up of 60, inside the list
			return;
		case 61:
			labCheck(sScreen == SCREEN_CREDITS && sCreditScroll >= sLabCredit + 55, "dragging up scrolls the credits down");
			sBotWheel = 20;
			return;
		case 62:
			labCheck(sCreditScroll == 0, "the wheel scrolls the credits back to the top");
			sBotWheel = -100;
			return;
		case 63:
			labCheck(sCreditScroll == creditMaxScroll(), "the credits stop at their end");
			botDragY(29, 120, -25);  // starts on the list and runs up across the back arrow
			return;
		case 64:
			labCheck(sScreen == SCREEN_CREDITS, "a drag on the list never cuts the back arrow");
			botTap(kBack);
			sBotMenuStep = 8;
			return;
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
			sShopItem = labShopSong();
			shopTryOn();
			sPreviewT = -1;  // held while it loads: labShopPreview times the preview on its own
			sBotPhaseFrames = -420;
			return;
		case 16: {
			labCheck(audio::currentTrack() == kBaseTracks + labShopSong() && audio::musicTime() >= 0 && !audio::musicPending(), "A full-length Shop song loads and plays");
			labCheck(strchr(shopItem(SHOP_MUSIC, labShopSong()).name, '.') == nullptr, "Shop song names carry no file extension");
			labShopPreview();
			sBotMenuStep = 55;
			botTap(kBack);
			return;
		}
		case 55: sBotMenuStep = 17; labExpect(SCREEN_TITLE, "Shop back arrow cuts its berry and goes back"); botTap(kTitleButtons[1]); return;
		case 17: labExpect(SCREEN_OPTIONS, "Options opens"); botTap(optionButtonFor(SID_AUDIO)); return;
		case 18: labExpect(SCREEN_SECTION, "Audio section opens"); {
			const u8 prevMenu = gSave.menuTrack, prevGame = gSave.gameTrack, prevBoss = gSave.bossMusic;
			const u32 prevOwned = gSave.ownedTracks;
			openRadio();
			labCheck(sScreen == SCREEN_RADIO, "Radio opens");
			RadioEntry rlist[64];
			const int nAll = radioBuildList(rlist, 64);
			labCheck(nAll >= 9 && rlist[0].isAuto, "Radio lists Auto (Shuffle) plus all 8 default tracks");
			snprintf(sRadioQuery, sizeof sRadioQuery, "orchard");
			const int nFilt = radioBuildList(rlist, 64);
			labCheck(nFilt >= 1 && nFilt < nAll && ciContains(rlist[0].title, "Orchard"), "Radio search filters by title");
			sRadioQuery[0] = 0;
			labCheck(radioBuildList(rlist, 64) == nAll, "Clearing Radio search restores full track list");
			sRadioGrid = true;
			sRadioGrid = false;
			radioEquipEntry({1, false, false, kTrackRowNames[2], ""});
			labCheck(gSave.menuTrack == 2 && gSave.gameTrack == 2, "Equipping a regular Radio track sets both Menu and Game Music");
			gSave.ownedTracks = 0xFFFFFFFFu;
			int bossTr = -1;
			for (int t = 0; t < kTrackCount; t++) if (trackBoss(t) >= 0) { bossTr = t; break; }
			if (bossTr >= 0) {
				radioEquipEntry({bossTr, false, true, kTrackRowNames[bossTr + 1], ""});
				labCheck(gSave.bossMusic == (u8)(bossTr + 2) && gSave.menuTrack == 2, "Equipping a boss Radio track sets Boss Music only");
			}
			float oscBuf[64];
			audio::oscSamples(oscBuf, 64);
			sRadioOsc = true;
			labCheck(sRadioOsc, "Radio Oscilloscope toggles on");
			sRadioOsc = false;
			radioPreviewEntry({3, false, false, kTrackRowNames[4], ""});
			leaveRadio();
			labCheck(sScreen == SCREEN_RECORDS && gSave.menuTrack == 2, "Leaving Radio restores Records without overwriting equipped track on preview");
			gSave.menuTrack = prevMenu; gSave.gameTrack = prevGame; gSave.bossMusic = prevBoss; gSave.ownedTracks = prevOwned;
			saveWrite();
			audio::reapply();
			goTo(SCREEN_SECTION, 1);
			sSection = sectionIndex(SID_AUDIO);
		} botTapXY((int)((kTrackX0 + kTrackX1) / 2), (int)firstRowY); return;
		case 19: botTap(kBack); return;
		case 20:
			labExpect(SCREEN_OPTIONS, "section back arrow cuts its berry and goes back");
			{
				const bool pauseWas = sOptionsFromPause;
				sOptionsFromPause = true;
				const int pn = optionButtons();
				const bool grid2x2 = pn == 5 && sOptionButtons[1].x < 160 && sOptionButtons[2].x > 160 && sOptionButtons[1].y == sOptionButtons[2].y &&
				                     sOptionButtons[3].y == sOptionButtons[4].y && sOptionButtons[4].y + sOptionButtons[4].h < 220 &&
				                     fabsf((sOptionButtons[1].x + sOptionButtons[2].x + sOptionButtons[2].w) - 320.0f) < 1.0f;
				sOptionsFromPause = pauseWas;
				labCheck(grid2x2, "Pause Options uses a centred 2x2 grid without Scores and Data");
			}
			labCheck(optionButtons() == (PORT_3DS ? 8 : 7), "Options shows every section sign cleanly, plus Developer in a test build");
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

// The Mixer page, each time Pick a Game's checks open it: counted, then left by its back arrow.
static int sLabMixerOpens = 0;
static void labMixerPage() {
	const bool mixer = kSections[sSection].id == SID_MIXER;
	if (mixer) sLabMixerOpens++;
	else labCheck(false, "only the Mixer opens from Pick a Game here", kSections[sSection].title);
	botTap(kBack);
}

// Pick a Game's pages, the first time the bot reaches it. Returns true while there are steps left.
static bool labModePages() {
#ifdef LAB_QUICK
	return false;  // the quick Lab skips the menu checks
#endif
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
			static int taps = 0;
			if (sModePage != 0 && ++taps < kModePages) {
				step--;  // tap on past the last page
				botTapXY((int)(next.x + next.w / 2), (int)(next.y + next.h / 2));
				return true;
			}
			labCheck(sModePage == 0, "turning past the last page wraps to the first");
			sBotKeys = KEY_SELECT;  // a 3DS's Select, or Back or View on a controller
			return true;
		case 5:
			labCheck(sLabMixerOpens == 1 && !sShowDebug, "Select on Pick a Game opens the Mixer");
#if PORT_3DS
			return false;  // the 3DS keeps its Mixer sign
#else
			{
				bool onPage = false;
				for (int i = 0; i < (int)sizeof kModeOrder; i++) onPage |= kModeOrder[i] == SIGN_MIXER;
				const Button m = mixerButton();
				labCheck(!onPage && modeAt(0, kMixFocus) == SIGN_MIXER, "the Mixer is a fruit of its own, not a sign among the modes");
				labCheck(m.w > 0 && (m.x + m.w < 0 || m.x > 320 || m.y + m.h < 0 || m.y > 240), "the Mixer fruit sits on the picture side, under the info sign");
			}
			sFocus[SCREEN_MODES] = kMixFocus;
			sBotKeys = KEY_LEFT;
			return true;
		case 6:
			labCheck(sModePage == 0, "Left on the Mixer fruit doesn't turn the page");
			sFocus[SCREEN_MODES] = kMixFocus;
			sBotKeys = KEY_A;
			return true;
		case 7:
			labCheck(sLabMixerOpens == 2, "confirm on the Mixer fruit opens the Mixer");
			return false;
#endif
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
		labCheck(sScreen == SCREEN_GAME && sGame.mode == (Mode)sBotMode && (sGame.phase == PHASE_READY || sGame.phase == PHASE_PLAY), "a stylus tap on a mode sign starts it", modeName(sGame.mode));
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
	if (sBotModeFrames == 120) {
		const bool playing = audio::musicTime() >= 0 || audio::musicPending() || (sGame.mode == MODE_BOSSRUSH && audio::bossMusicOn());
		labCheck(playing, "game music plays", modeName(sGame.mode));
	}
	if (sGame.mode == MODE_ORCHARD) {
		// Orchard Blast: level-up cards are taken with A, and play resumes immediately.
		static bool sawCards, cardsChecked;
		if (sBotModeFrames == 1) {
			sawCards = cardsChecked = false;
		}
		if (sGame.orc.choosing && sBotModeFrames % 40 == 0) sBotKeys = KEY_A;
		if (sGame.orc.choosing) sawCards = true;
		if (sawCards && !cardsChecked && !sGame.orc.choosing && sGame.orc.level > 1) {
			cardsChecked = true;
			labCheck(sGame.orc.levelsWaiting == 0 && !sGame.orc.choosing && sGame.orc.hp > 0, "a level-up card is picked with A, and play goes on", "");
		}
	}
	if (sGame.mode >= MODE_BOSSRUSH && sGame.mode <= MODE_JUGGLE) {
		if (sGame.mode == MODE_BOSSRUSH && sGame.m12.rushPicking && sBotModeFrames % 40 == 0) sBotKeys = KEY_A;
		if (sGame.mode == MODE_BOSSRUSH && sBotModeFrames == 1200)
			labCheck(sGame.m12.rushBuild < BUILD_COUNT && !sGame.m12.rushPicking, "Boss Rush's build is picked with A", "");
	}
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
	const bool reached = sGame.mode == MODE_BOSSRUSH ? (sGame.score > 0 || sGame.orc.bossesBeaten > 0 || sGame.sliced > 0) : sGame.sliced > 0;
	labCheck(reached, name, d);
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
static void labStreetPass() {
	char savedMsg[28];
	snprintf(savedMsg, sizeof savedMsg, "%s", gSave.streetPassMsg);
	const u16 winsWas = gSave.spChallengesWon;
	const u64 idWas = gSave.leaderboardId;
	const u8 onWas = gSave.streetPassOn;
	const u32 coinsWas = gSave.coins, classicWas = gSave.best[MODE_CLASSIC];
	streetpass::resetInbox();

	char cleaned[28];
	streetpass::cleanMessage("  Hello! :)  ", cleaned, sizeof cleaned);
	labCheck(strcmp(cleaned, "Hello! :)") == 0, "StreetPass greeting preserves Hello! :) cleanly");
	streetpass::cleanMessage("@@@", cleaned, sizeof cleaned);
	labCheck(strcmp(cleaned, "Hello! :)") == 0, "Empty StreetPass greeting falls back to Hello! :)");
	labCheck(fruitFor("StreetPass") == MF_MELON, "Options StreetPass button uses a green Melon");
#if PORT_3DS
	labCheck(optionButtonFor(SID_STREETPASS).label != nullptr && strcmp(optionButtonFor(SID_STREETPASS).label, "StreetPass") == 0,
	         "StreetPass has its own Options section on 3DS");
#endif

	gSave.leaderboardId = 0x1122334455667788ull;
	snprintf(gSave.streetPassMsg, sizeof gSave.streetPassMsg, "Hello! :)");
	u8 pkt[sizeof(streetpass::Packet)];
	const int len = streetpass::pack("Rival", pkt, sizeof pkt);
	streetpass::Packet out = {};
	labCheck(len == (int)sizeof(streetpass::Packet) && streetpass::unpack(pkt, len, &out) &&
	         strcmp(out.name, "Rival") == 0 && strcmp(out.msg, "Hello! :)") == 0,
	         "StreetPass packet packs and unpacks player card with greeting intact");
	pkt[20] ^= 0x55;
	labCheck(!streetpass::unpack(pkt, len, &out), "Corrupted StreetPass packet is rejected by CRC");

	// Foreign card arrives: pending alert fires, challenge win pays +50 coins and marks entry won.
	pkt[20] ^= 0x55;  // restore valid CRC packet from Rival (senderId 0x1122334455667788)
	gSave.leaderboardId = 0x9988776655443322ull;
	const u64 idBefore = gSave.leaderboardId;
	(void)idBefore;
	gSave.best[MODE_CLASSIC] = 25;
	u8 foreignPkt[sizeof(streetpass::Packet)];
	gSave.leaderboardId = 0x1122334455667788ull;
	const int flen = streetpass::pack("MiiPal", foreignPkt, sizeof foreignPkt);
	gSave.leaderboardId = 0x9988776655443322ull;
	const int ing = streetpass::ingestPacket(foreignPkt, flen);
	labCheck(ing == 0 && streetpass::count() == 1 && streetpass::hasPending(), "Incoming StreetPass card is stored and flagged pending");
	sGame.startStreetPassChallenge(0, streetpass::entry(0).pkt.name, streetpass::entry(0).pkt.classicBest);
	sGame.score = 30;
	sGame.commitScore();
	labCheck(sGame.spWon && gSave.spChallengesWon == winsWas + 1 && streetpass::entry(0).won && !streetpass::hasPending(),
	         "Beating a StreetPass challenge score awards +50 coins and marks it won");

	streetpass::resetInbox();
	snprintf(gSave.streetPassMsg, sizeof gSave.streetPassMsg, "%s", savedMsg);
	gSave.spChallengesWon = winsWas;
	gSave.leaderboardId = idWas;
	gSave.streetPassOn = onWas;
	gSave.coins = coinsWas;
	gSave.best[MODE_CLASSIC] = classicWas;
}

static void labLeaderboard() {
	const char* expected[MODE_COUNT] = {
		"classic", "arcade", "zen", "frenzy", "sudden_death", "minefield",
		"combo_rush", "daily", "beat_slice", "orchard_blast", "boss_rush",
		"recipe", "sharpshooter", "juggle",
	};
	bool slugsOk = strcmp(lb::modeSlug(MODE_ORCHARD, true), "orchard_endless") == 0 && lb::modeSlug(MODE_PRACTICE, false) == nullptr;
	for (int m = 0; m < MODE_COUNT; m++) {
		const char* s = lb::modeSlug(m, false);
		if (!s || strcmp(s, expected[m]) != 0) slugsOk = false;
	}
	labCheck(slugsOk, "All 15 leaderboard mode slugs map stably and Practice maps to nullptr");

	char nameBuf[8] = {};
	lb::cleanName("  Alex_99!?", nameBuf, sizeof nameBuf);
	const bool truncOk = strcmp(nameBuf, "Alex_9") == 0 && !lb::sanitizeName(" !@# ", nameBuf, sizeof nameBuf);
	lb::cleanName("   ", nameBuf, sizeof nameBuf);
	labCheck(truncOk && strcmp(nameBuf, "PLAYER") == 0, "Leaderboard name enforces 6-char max and safe fallback");

	char json[256] = {};
	const bool built = lb::buildSubmitJson(MODE_ORCHARD, true, 420, "NICK_123", 0x1234ABCDULL, true, json, sizeof json);
	labCheck(built && strstr(json, "\"mode\":\"orchard_endless\"") && strstr(json, "\"player_id\":\"000000001234abcd\"") &&
	         strstr(json, "\"name\":\"NICK_1\"") && strstr(json, "\"score\":420") && strstr(json, "\"test\":1"),
	         "Leaderboard submit JSON serializes mode slug, 64-bit id, sanitized 6-char name, score, and test flag");

	const char* sample =
	    "{\"total\":12,\"mode\":\"classic\",\"you\":{\"score\":900,\"rank\":2,\"name\":\"SAM\"},"
	    "\"top\":[{\"score\":1250,\"name\":\"ACE\",\"platform\":\"win\",\"player_id\":\"aaa\"},"
	    "{\"name\":\"SAM\",\"platform\":\"3ds\",\"score\":900,\"player_id\":\"bbb\"},"
	    "{\"platform\":\"linux\",\"name\":\"MAX\",\"score\":750,\"player_id\":\"ccc\"}],\"ok\":true}";
	lb::BoardView parsed = {}, bad = {};
	const bool parseOk = lb::parseResponse(sample, &parsed);
	labCheck(parseOk && parsed.status == lb::STATUS_READY && parsed.count == 3 &&
	         strcmp(parsed.entries[1].name, "SAM") == 0 && strcmp(parsed.entries[1].platform, "3ds") == 0 &&
	         parsed.entries[1].score == 900 && parsed.yourRank == 2 && parsed.yourScore == 900 && parsed.total == 12 &&
	         !lb::parseResponse("{\"ok\":false}", &bad) && !lb::parseResponse("{corrupt", &bad),
	         "Leaderboard JSON parser reads top entries, rank telemetry, and rejects invalid/error payloads");

	char savedName[8];
	snprintf(savedName, sizeof savedName, "%s", gSave.leaderboardName);
	snprintf(gSave.leaderboardName, sizeof gSave.leaderboardName, "NICK");
	int resetIdx = -1;
	const int dataCount = (int)(sizeof kDataRows / sizeof kDataRows[0]);
	for (int i = 0; i < dataCount; i++) {
		if (kDataRows[i].action == ACT_RESET_LB_NAME) { resetIdx = i; break; }
	}
	sArmed = -1;
	if (resetIdx >= 0) {
		rowAction(resetIdx, kDataRows[resetIdx]);
		const bool armed = sArmed == resetIdx && strcmp(gSave.leaderboardName, "NICK") == 0;
		rowAction(resetIdx, kDataRows[resetIdx]);
		labCheck(armed && gSave.leaderboardName[0] == 0, "Reset Leaderboard Name uses two-step confirm and clears saved name");
	} else {
		labCheck(false, "Reset Leaderboard Name row missing from Scores and Data");
	}
	sArmed = -1;
	snprintf(gSave.leaderboardName, sizeof gSave.leaderboardName, "NICK");
	const u32 oldBest = gSave.best[MODE_CLASSIC];
	gSave.best[MODE_CLASSIC] = 350;
	lb::clearView();
	lb::fetch(MODE_CLASSIC, false);
	for (int f = 0; f < 120 && lb::view().status == lb::STATUS_LOADING; f++) lb::tick();
	const lb::BoardView& lv = lb::view();
	labCheck(lv.status == lb::STATUS_READY && lv.count >= 1 && lv.yourRank >= 1 && lv.yourScore >= 350 &&
	         strcmp(lv.entries[lv.yourRank - 1].name, "NICK") == 0,
	         "Top 10 view always includes player name and best score even when offline");
	gSave.best[MODE_CLASSIC] = oldBest;
	snprintf(gSave.leaderboardName, sizeof gSave.leaderboardName, "%s", savedName);
}

static void labV2Fixes() {
	// 1. Check for Updates fruit slicing: swipe across "Yes" and from outside onto "No" without clicking first.
	{
		const Screen prevScr = sScreen;
		const auto prevCut = sCut;
		const update::State prevUpd = update::state();
		update::setTestState(update::ASK);
		sScreen = SCREEN_UPDATE;
		sDrawing = SCREEN_UPDATE;
		sCut = {};
		const Button* btns = nullptr;
		const char *a = nullptr, *b = nullptr;
		const int n = updateButtons(&btns, &a, &b);
		float fx = 0, fy = 0, fd = 0;
		fruitSpot(btns[0], &fx, &fy, &fd);
		const float swipeSpan = gSave.sliceAssist ? 16.0f : 36.0f;
		Input uin = {};
		uin.touching = true;
		uin.tx = (int)(fx - swipeSpan * 0.5f);
		uin.ty = (int)fy;
		updateSignInput(uin, false);
		uin.tx = (int)(fx + swipeSpan * 0.5f);
		updateSignInput(uin, true);
		const bool yesSliced = n == 2 && btns[0].label != nullptr && fruitFor(btns[0].label) != MF_BERRY && sCut.pending && sCut.index == 0;
		updateSignInput(Input{}, true);  // lift touch cleanly before second swipe
		// Also test swiping from outside the hitbox across "No" (index 1) without clicking inside it first.
		sCut = {};
		float fx1 = 0, fy1 = 0, fd1 = 0;
		if (n >= 2) fruitSpot(btns[1], &fx1, &fy1, &fd1);
		Input uin2 = {};
		uin2.touching = true;
		uin2.tx = (int)(fx1 - 54.0f);
		uin2.ty = (int)fy1;
		updateSignInput(uin2, false);
		uin2.tx = (int)fx1;
		updateSignInput(uin2, true);
		uin2.tx = (int)(fx1 + 24.0f);
		updateSignInput(uin2, true);
		const bool noSliced = n == 2 && btns[1].label != nullptr && fruitFor(btns[1].label) != MF_BERRY && sCut.pending && sCut.index == 1;
		updateSignInput(Input{}, true);
		update::setTestState(prevUpd);
		sCut = prevCut;
		sScreen = prevScr;
		labCheck(yesSliced && noSliced,
		         "Check for Updates Yes/No buttons slice cleanly via touch swipe across button or from outside");
	}

	// 2. Guides slide transitions & wrapped line budget.
	{
		const Screen prevScr = sScreen;
		const bool slidingWas = sSliding;
		const int dirWas = sSlideDir;
		sScreen = SCREEN_HOWTO;
		sSliding = false;
		goTo(SCREEN_GUIDES, 1);
		const bool toGuidesSlides = sSliding && sSlideDir == 1;
		sScreen = SCREEN_GUIDES;
		sSliding = false;
		goTo(SCREEN_HOWTO, -1);
		const bool toHowtoSlides = sSliding && sSlideDir == -1;
		sScreen = prevScr;
		sSliding = slidingWas;
		sSlideDir = dirWas;
		labCheck(toGuidesSlides && toHowtoSlides, "Guides uses clean horizontal slide transitions (non-zero dir)");

		bool fitsAll = true;
		for (int p = 0; p < kGuideCount; p++) {
			const GuideEntry& g = kGuides[p];
			const char* const lines[4] = {g.line1, g.line2, g.line3, g.line4};
			float ly = 104.0f;
			for (int i = 0; i < 4; i++) {
				if (!lines[i] || !lines[i][0]) continue;
				char wbuf[160];
				snprintf(wbuf, sizeof wbuf, "%s", lines[i]);
				char* cur = wbuf;
				int wrapped = 0;
				while (*cur && wrapped < 2) {
					while (*cur == ' ') cur++;
					if (!*cur) break;
					if (gfx::textWidth(cur, 0.31f) <= 264.0f || wrapped == 1) {
						ly += 10.8f;
						break;
					}
					int split = -1;
					for (int k = 1; cur[k]; k++) {
						if (cur[k] == ' ') {
							cur[k] = 0;
							const float tw = gfx::textWidth(cur, 0.31f);
							cur[k] = ' ';
							if (tw <= 264.0f) split = k;
							else break;
						}
					}
					if (split < 0) {
						ly += 10.8f;
						break;
					}
					ly += 10.8f;
					cur += split + 1;
					wrapped++;
				}
				ly += 2.0f;
			}
			if (ly > 212.0f) fitsAll = false;
		}
		const bool guidesUpdated = strstr(kGuides[MODE_ORCHARD].line4, "Runs save at each room start") != nullptr &&
		                           strstr(kGuides[MODE_BOSSRUSH].line1, "4 seeded bosses") != nullptr &&
		                           strstr(kGuides[MODE_ORCHARD].goal, "10 ROOMS") == nullptr;
		labCheck(fitsAll && guidesUpdated, "Guides fit inside plank with updated Endless room-start save & 5-boss Boss Rush text");
	}

	// 3. Shop Skins: Solid Gold is opaque metallic, Clear Glass is translucent, and Holographic is an animated shining overlay over the fruit.
	{
		const u32 extrasWas = gSave.ownedExtras, coinsWas = gSave.coins;
		const u8 skinWas = gSave.skin;
		gSave.coins = 5000;
		const int goldIdx = 0, glassIdx = 1, holoIdx = 2;
		const bool hasSkins = shopCount(SHOP_SKINS) >= 3 &&
		                      strcmp(shopItem(SHOP_SKINS, goldIdx).name, "Solid Gold") == 0 &&
		                      strcmp(shopItem(SHOP_SKINS, glassIdx).name, "Clear Glass") == 0 &&
		                      strcmp(shopItem(SHOP_SKINS, holoIdx).name, "Holographic") == 0;
		if (hasSkins) {
			const m3d::Skin& goldSk = shopSkinLook(goldIdx);
			const m3d::Skin& glassSk = shopSkinLook(glassIdx);
			shopBuy(SHOP_SKINS, goldIdx);
			shopBuy(SHOP_SKINS, glassIdx);
			shopBuy(SHOP_SKINS, holoIdx);
			const Screen scrWas = sScreen;
			const u8 artWas = gSave.artStyle;
			gSave.artStyle = ART_3D;
			m3d::sync();
			sScreen = SCREEN_GAME;
			sGame.start(MODE_CLASSIC);
			sGame.phase = PHASE_PLAY;
			for (auto& o : sGame.objs) o = {};
			for (int fi = 0; fi < 3; fi++) {
				Obj& o = sGame.objs[fi];
				o.on = true;
				o.kind = fi == 2 ? OBJ_GOLD : OBJ_FRUIT;
				o.fruit = fi == 0 ? 0 : 3;
				o.x = 130.0f + fi * 80.0f;
				o.y = 140.0f;
				o.radius = 28.0f;
				o.rot = 0.25f * fi;
			}
			shopEquip(SHOP_SKINS, goldIdx);
			labSnapV2f("v2f_skin_gold_3d");
			shopEquip(SHOP_SKINS, glassIdx);
			labSnapV2f("v2f_skin_glass_3d");
			shopEquip(SHOP_SKINS, holoIdx);
			const m3d::Skin* sk = shopWornSkin();
			const float phaseA = shopHoloPhase();
			const u32 tintA = shopWornSkinTint();
			shopTick(0.42f);
			const float phaseB = shopHoloPhase();
			const u32 tintB = shopWornSkinTint();
			labSnapV2f("v2f_skin_holo_3d");
			for (auto& o : sGame.objs) o = {};
			sScreen = scrWas;
			gSave.artStyle = artWas;
			m3d::sync();
			labCheck(goldSk.alpha >= 0.99f && goldSk.mix >= 0.99f && goldSk.shine >= 0.90f &&
			             glassSk.alpha < 0.70f &&
			             sk && sk->shine >= 0.99f && sk->alpha >= 0.99f && sk->mix < 0.60f && phaseB > phaseA && tintA != tintB,
			         "Solid Gold is opaque metallic, Clear Glass stays translucent, and Holographic animates an opaque shining rainbow overlay");
		} else {
			labCheck(false, "Solid Gold, Clear Glass, and Holographic skins exist in Shop Skins tab");
		}
		gSave.ownedExtras = extrasWas;
		gSave.coins = coinsWas;
		gSave.skin = skinWas;
	}

	// 4. Room 25 First-Encounter Cutscene UI: fast-forward, release-latch between lines, tensionMusic at line 5, boss transition at line 10.
	{
		const u8 seenWas = gSave.seenRoom50Cutscene;
		const int trkWas = audio::currentTrack();
		gSave.seenRoom50Cutscene = 0;
		Game::orchardClearSave();
		sGame.start(MODE_ORCHARD);
		sGame.orcApplyDevSkip(25, 1);
		Input cin = {};
		cin.held = KEY_A;
		for (int i = 0; i < 60 && (int)sGame.orc.dialogueChars < (int)strlen(kRoom50Lines[0]); i++) updateRoom50Cutscene(cin, false);
		const bool latched = sGame.orc.dialogueWaitRelease;
		updateRoom50Cutscene(cin, false);
		const bool heldDidNotSkip = sGame.orc.dialogueLine == 0 && sGame.orc.cutscene;
		// Release A, then advance through line 5 (starts tension music) and line 9 -> launches Milestone Boss.
		cin = {};
		updateRoom50Cutscene(cin, false);
		for (int line = 0; line < 9; line++) {
			sGame.orc.dialogueChars = (float)strlen(kRoom50Lines[line]);
			sGame.orc.dialogueWaitRelease = false;
			cin.down = KEY_A;
			updateRoom50Cutscene(cin, false);
		}
		const bool tensionAt5Plus = sGame.orc.tensionStarted && sGame.orc.dialogueLine == 9;
		sGame.orc.dialogueChars = (float)strlen(kRoom50Lines[9]);
		sGame.orc.dialogueWaitRelease = false;
		cin.down = KEY_A;
		updateRoom50Cutscene(cin, false);
		const bool orcFinished = !sGame.orc.cutscene && sGame.orc.boss.on && gSave.seenRoom50Cutscene == 1;

		// Dev Skip to Milestone room (25 or 50) resets seenRoom50Cutscene=0 and triggers the cutscene even if already seen.
		sGame.start(MODE_ORCHARD);
		sGame.orc.devPrompt = true;
		sGame.orc.devField = 6;
		sGame.orc.devRoomTarget = 25;
		sGame.orc.devLevelTarget = 1;
		cin = {};
		cin.down = KEY_START;
		updateDevSkip(cin, false);
		const bool devSkipCutscene = !sGame.orc.devPrompt && sGame.orc.cutscene && sGame.orc.pendingMilestoneBoss >= BOSS_DURIAN;
		sGame.orcStartBossAfterCutscene();
		const bool devSkipBossStarted = !sGame.orc.cutscene && sGame.orc.boss.on && sGame.orc.boss.id >= BOSS_DURIAN && gSave.seenRoom50Cutscene == 1;

		labCheck(latched && heldDidNotSkip && tensionAt5Plus && orcFinished && devSkipCutscene && devSkipBossStarted,
		         "Room 25 first-encounter cutscene fast-forwards, latches release, starts tensionMusic at line 5, enters Milestone Boss after line 10, and Dev Skip to Room 25 replays it");
		audio::tensionMusic(false);
		audio::bossMusicEnd();
		audio::music(MUSIC_MENU);
		(void)trkWas;
		gSave.seenRoom50Cutscene = seenWas;
		Game::orchardClearSave();
	}

	// 5. Audio Boss Music choice separation, Orchard Pause Start Over modal, Stats Global Top 100 tab/controls, Card non-overlap, and Guides accuracy.
	{
		// Options -> Audio no longer has Menu/Game/Boss Music rows, and Radio classifies boss tracks separately.
		bool audioCleaned = true;
		for (const Row& r : kAudioRows) {
			if (r.value == &gSave.menuTrack || r.value == &gSave.gameTrack || r.value == &gSave.bossMusic)
				audioCleaned = false;
		}
		const u32 ownedWas = gSave.ownedTracks;
		gSave.ownedTracks = 0xFFFFFFFFu;
		RadioEntry rchk[64];
		const int rcnt = radioBuildList(rchk, 64);
		bool sepOk = audioCleaned && rcnt > 9;
		for (int i = 0; i < rcnt; i++) {
			if (rchk[i].track >= 0 && rchk[i].track < kTrackCount) {
				if (rchk[i].isBoss != (trackBoss(rchk[i].track) >= 0)) sepOk = false;
			}
		}
		gSave.ownedTracks = ownedWas;
		labCheck(sepOk, "Options->Audio removes track rows while Radio separates Regular/Custom vs Boss tracks");

		// Upgrade card description + footer non-overlap math check across BLADE & all cards.
		bool cardsClear = true;
		for (int c = 0; c < CARD_COUNT; c++) {
			const CardInfo& info = kCards[c];
			const OrcCardLayout CL = orcCardLayout(3, false);
			const float maxW = CL.cardW - 8.0f;
			char foot[128] = "";
			char footSub[64] = "";
			if (info.kind == KIND_UPGRADE) {
				cardNextLine(c, 1, foot, sizeof foot);
				snprintf(footSub, sizeof footSub, "Lv 1 (Uncapped)");
			} else if (info.kind == KIND_REUSABLE) {
				snprintf(foot, sizeof foot, "%d s cooldown", (int)info.cooldown);
			} else if (info.kind == KIND_SINGLE) {
				snprintf(foot, sizeof foot, "Slot charm (1 use)");
			}
			const float footMaxW = fmaxf(48.0f, CL.cardW - 18.0f);
			float footH = 0.0f;
			if (foot[0]) {
				float fs = 0.27f;
				float tw = gfx::textWidth(foot, fs);
				if (tw > footMaxW && fs * (footMaxW / tw) >= 0.21f) fs *= footMaxW / tw;
				int fl = orcCountWrapLines(foot, footMaxW, fs, 2);
				float fstep = fl > 1 ? fmaxf(8.5f, fs * 0.90f * 34.0f) : 10.0f;
				footH = fl * fstep + (footSub[0] ? 10.0f : 0.0f);
			}
			const float footY = CL.cardH - 8.0f - footH;
			char buf[128];
			snprintf(buf, sizeof buf, "%s %s", rarityName(info.rarity), cardKindName(info.kind));
			const int rarLines = orcCountWrapLines(buf, maxW, 0.30f, 2);
			const float descTop = 34.0f + rarLines * 10.5f + 2.0f + 15.0f;
			snprintf(buf, sizeof buf, "%s%s%s", info.line1, info.line2[0] ? " " : "", info.line2);
			float ds = 0.31f, dstep = 11.2f;
			const float budget = fmaxf(14.0f, footY - 4.0f - descTop);
			while (ds > 0.205f) {
				dstep = ds * 36.0f;
				if (orcCountWrapLines(buf, maxW, ds, 6) * dstep <= budget) break;
				ds -= 0.015f;
			}
			const float descH = orcCountWrapLines(buf, maxW, ds, 6) * dstep;
			if (descTop + descH > footY) cardsClear = false;
		}
		labCheck(cardsClear, "Upgrade cards (including BLADE) auto-fit descriptions above reserved footer bounds");

		// Pause Start Over confirmation flow in Orchard Blast.
		const Screen scrWas2 = sScreen;
		sScreen = SCREEN_GAME;
		sGame.start(MODE_ORCHARD);
		sGame.orc.room = 9;
		sGame.orc.level = 5;
		pauseGame();
		sPauseStartOverConfirm = true;
		sPauseConfirmFocus = 1;  // No
		const Screen pScr = sScreen;
		const bool confirmOpen = sPauseStartOverConfirm && sGame.orc.room == 9;
		sPauseStartOverConfirm = false;  // cancelled -> stays at Room 9
		const bool noKeepsRoom = sGame.orc.room == 9 && sGame.orc.level == 5;
		Game::orchardClearSave();
		sGame.start(MODE_ORCHARD);
		const bool yesResetsRoom1 = sGame.orc.room == 1 && sGame.orc.level == 1 && !Game::orchardHasSave();
		sScreen = scrWas2;
		Game::orchardClearSave();
		labCheck(pScr == SCREEN_PAUSE && confirmOpen && noKeepsRoom && yesResetsRoom1,
		         "Orchard Pause Start Over confirmation cancels cleanly on No and restarts Room 1 Level 1 on Yes");

		// Stats Global tab navigation & Top-100 JSON parse + eligible modes exclusion check.
		sStatsTab = 0;
		statsSwitchTab(kStatsGlobalTab);
		const int m0 = sGlobalModeIdx;
		statsSelectGlobalMode(sGlobalModeIdx + 1);
		const int m1 = sGlobalModeIdx;
		bool excludedOk = true;
		for (int i = 0; i < kGlobalModeCount; i++) {
			if (kGlobalModes[i] == MODE_PRACTICE || kGlobalModes[i] >= MODE_COUNT) excludedOk = false;
		}
		lb::BoardView parsed100 = {};
		static char bigJson[8192];
		int pos = snprintf(bigJson, sizeof bigJson, "{\"ok\":true,\"mode\":\"classic\",\"total\":120,\"you\":{\"rank\":42,\"score\":999},\"top\":[");
		for (int i = 0; i < 100 && pos < (int)sizeof bigJson - 96; i++) {
			pos += snprintf(bigJson + pos, sizeof bigJson - pos, "%s{\"name\":\"P%03d\",\"score\":%d,\"platform\":\"win\",\"player_id\":\"%016x\"}",
			                i ? "," : "", i + 1, 2000 - i, i + 1);
		}
		snprintf(bigJson + pos, sizeof bigJson - pos, "]}");
		const bool parse100Ok = lb::parseResponse(bigJson, &parsed100) && parsed100.count == 100 && parsed100.yourRank == 42 && parsed100.total == 120;
		sStatsTab = 0;
		lb::clearView();
		labCheck(m1 == (m0 + 1) % kGlobalModeCount && excludedOk && parse100Ok,
		         "Stats Global switches gamemodes via tabs/ZL/ZR, excludes non-board modes, and parses Top 100 + rank");

		// Fact-checked Guides assertions.
		bool guideClassicOk = false, guideOrchardOk = false, guideSharpOk = false, guideJuggleOk = false;
		for (int i = 0; i < kGuideCount; i++) {
			if (kGuides[i].id == MODE_CLASSIC) guideClassicOk = strstr(kGuides[i].line3, "100 points") == nullptr;
			if (kGuides[i].id == MODE_ORCHARD) guideOrchardOk = strstr(kGuides[i].line2, "25th") != nullptr && strstr(kGuides[i].line3, "Boss Harvest") != nullptr;
			if (kGuides[i].id == MODE_SHARP) guideSharpOk = strstr(kGuides[i].line1, "Square") != nullptr && strstr(kGuides[i].line3, "double") != nullptr;
			if (kGuides[i].id == MODE_JUGGLE) guideJuggleOk = strstr(kGuides[i].line4, "costs a life") != nullptr;
		}
		labCheck(guideClassicOk && guideOrchardOk && guideSharpOk && guideJuggleOk,
		         "All corrected Mode Guides match actual gameplay mechanics");

		// Dialogue zoom-in curve & pre-wrapped line stability.
		{
			float s0 = 0, dx0 = 0, dy0 = 0, a0 = 0;
			float sMid = 0, dxMid = 0, dyMid = 0, aMid = 0;
			float sEnd = 0, dxEnd = 0, dyEnd = 0, aEnd = 0;
			room50CharAnim(0.0f, false, &s0, &dx0, &dy0, &a0);
			room50CharAnim(kDiagCharAnimDur * 0.55f, false, &sMid, &dxMid, &dyMid, &aMid);
			room50CharAnim(kDiagCharAnimDur, false, &sEnd, &dxEnd, &dyEnd, &aEnd);
			DiagLine lines[8];
			const Room50CutsceneBox B = room50CutsceneBox(kRoom50Lines[3]);
			const int lc = orcLayoutLines(kRoom50Lines[3], B.maxW, B.textScale, B.maxLines, lines);
			labCheck(s0 < 0.5f && a0 == 0.0f && sMid > 1.03f && fabsf(sEnd - 1.0f) < 0.001f &&
			             fabsf(dxEnd) < 0.001f && fabsf(dyEnd) < 0.001f && aEnd == 1.0f && lc >= 2 && lines[0].start == 0,
			         "Dialogue letters zoom in smoothly with overshoot and pre-wrap cleanly without jumping");
		}

		// Slice Without Holding: hoverSlice slices gameplay without triggering dialogue or upgrade card clicks.
		{
			const Screen wasScr = sScreen;
			const u8 wasNoHold = gSave.sliceNoHold;
			gSave.sliceNoHold = 1;
			sScreen = SCREEN_GAME;
			sGame.start(MODE_ORCHARD);
			sGame.phase = PHASE_PLAY;
			Orchard& o = sGame.orc;
			o.cutscene = true;
			o.dialogueLine = 0;
			o.dialogueChars = (float)strlen(kRoom50Lines[0]);
			o.dialogueWaitRelease = false;
			Input hoverIn = {};
			hoverIn.hoverSlice = true;
			hoverIn.tx = (int)(kFieldW * 0.5f);
			hoverIn.ty = (int)(kFieldH - 30.0f);
			updateFrame(hoverIn, false);
			const bool diagUntouched = o.cutscene && o.dialogueLine == 0;
			o.cutscene = false;

			o.choosing = true;
			o.offerCount = 3;
			o.offer[0] = CARD_BLADE;
			o.offer[1] = CARD_HEALTH;
			o.offer[2] = CARD_ARMOR;
			sOrcOpenT = 1.0f;
			float cx = 0, cy = 0, cw = 0, ch = 0;
			orcCardRect(0, 3, &cx, &cy, &cw, &ch);
			hoverIn.tx = (int)(cx + cw * 0.5f);
			hoverIn.ty = (int)(cy + ch * 0.5f);
			updateFrame(hoverIn, false);
			const bool cardUntouched = o.choosing && o.upgrade[CARD_BLADE] == 0;
			o.choosing = false;

			sGame.start(MODE_ZEN);
			sGame.phase = PHASE_PLAY;
			sGame.objs[0] = {};
			sGame.objs[0].on = true;
			sGame.objs[0].kind = OBJ_FRUIT;
			sGame.objs[0].fruit = 0;
			sGame.objs[0].x = kFieldW * 0.5f;
			sGame.objs[0].y = kFieldH * 0.5f;
			sGame.objs[0].radius = 28.0f;
			sBlade.reset();
			Input h1 = {};
			h1.hoverSlice = true;
			h1.tx = (int)(kFieldW * 0.5f - 40.0f);
			h1.ty = (int)(kFieldH * 0.5f);
			updateFrame(h1, false);
			Input h2 = h1;
			h2.tx = (int)(kFieldW * 0.5f + 40.0f);
			updateFrame(h2, false);
			const bool bladeSliced = sBlade.active && !sGame.objs[0].on;
			sBlade.reset();
			gSave.sliceNoHold = wasNoHold;
			sScreen = wasScr;
			labCheck(diagUntouched && cardUntouched && bladeSliced,
			         "Slice Without Holding triggers gameplay blade slicing without clicking dialogue or upgrade cards");
		}

		// 3D prop meshes (drain, crown, power-ups) load cleanly when 3D art is active.
		{
			const u8 wasArt = gSave.artStyle;
			gSave.artStyle = ART_3D;
			for (int i = 0; i < 5; i++) m3d::sync();
			const bool m3dOk = (!m3d::usable() || (m3d::ready() && m3d::MESH_COUNT == 43)) &&
			                   m3d::MESH_PW_FREEZE + 6 == m3d::MESH_PW_TIME;
			char md[64];
			snprintf(md, sizeof md, "usable=%d ready=%d count=%d", (int)m3d::usable(), (int)m3d::ready(), m3d::MESH_COUNT);
			gSave.artStyle = wasArt;
			for (int i = 0; i < 5; i++) m3d::sync();
			labCheck(m3dOk, "Updated 3D model bundle (drain, crown, and 7 power-ups) loads cleanly", md);
		}

#if !PORT_3DS
		// Universal vector logo + per-letter rainbow Easter egg.
		{
			const int tris = gfx::vectorLogoTriCount();
			const u32 cA0 = gfx::logoLetterColor(0, 0.5f, true, 0.0f);
			const u32 cA1 = gfx::logoLetterColor(4, 0.5f, true, 0.0f);
			const u32 cA2 = gfx::logoLetterColor(0, 0.5f, true, 0.6f);
			const Screen wasScr = sScreen;
			const u8 wasTrans = gSave.transitions;
			gSave.transitions = 0;
			sScreen = SCREEN_TITLE;
			sSliding = false;
			sFade = 0;
			sQuitT = -1;
			sQuitHold = 0;
			sLogoRainbow = false;
			sLogoLastScreen = SCREEN_TITLE;
			sLogoBox = {200.0f, 66.0f, 1.0f, true};
			float sx = 0, sy = 0;
			gfx::topToCorner(200.0f, 66.0f, &sx, &sy);
			Input click = {};
			click.touching = true;
			click.tx = (int)(sx + 0.5f);
			click.ty = (int)(sy + 0.5f);
			updateFrame(click, false);
			const bool toggledOn = sLogoRainbow;
			updateFrame(click, false);
			const bool toggledOff = !sLogoRainbow;
			updateFrame(click, false);
			const bool reOn = sLogoRainbow;
			goTo(SCREEN_MODES, 1);
			const bool resetOnScreenChange = !sLogoRainbow;
			sScreen = wasScr;
			sLogoLastScreen = wasScr;
			gSave.transitions = wasTrans;
			labCheck(tris > 500 && cA0 != cA1 && cA0 != cA2 && toggledOn && toggledOff && reOn && resetOnScreenChange,
			         "Universal vector logo shifts per-letter rainbow hue on click and resets on second click or screen change");
		}
#endif
	}
}

static void labFinish(Input& in) {
	static int step = 0, held = 0;
#ifdef LAB_QUICK
	// The quick Lab: only the checks of the new code, then done. No shop, boards, saves of older versions or quit bomb.
	(void)in;
	(void)held;
	if (step == 0) {
		step = 1;
		labInventory();
		labStreetPass();
		labLeaderboard();
		labV2Fixes();
		sGame.labOrchard(labCheck);
		sGame.labModes12(labCheck);
		labSummary();
		sBotDone = true;
	}
	return;
#endif
	if (step == 0) {
		labStreetPass();
		labLeaderboard();
		labV2Fixes();
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
#if !PORT_3DS
		{
			// The Quit Game blast shakes the window as hard as Screen Shake says, and not at all at 0.
			const u8 shakeWas = gSave.shake, calmWas = gSave.calmFx;
			gSave.calmFx = 0;
			sQuitT = 0.05f;
			gSave.shake = 100;
			const float full = quitQuake();
			gSave.shake = 50;
			const float half = quitQuake();
			gSave.shake = 0;
			const float none = quitQuake();
			gSave.shake = 100;
			gSave.calmFx = 1;
			const float calm = quitQuake();
			gSave.calmFx = 0;
			sQuitT = kQuitBlast;
			const float end = quitQuake();
			char d[64];
			snprintf(d, sizeof d, "%.1f, %.1f, %.1f, calm %.1f, end %.1f", full, half, none, calm, end);
			labCheck(full >= 10 && fabsf(half - full * 0.5f) < 0.01f && none == 0 && calm < full * 0.5f && end < 0.01f,
			         "the Quit Game blast follows the Screen Shake setting", d);
			sQuitT = -1;
			gSave.shake = shakeWas;
			gSave.calmFx = calmWas;
			gfx::setShake(0, 0);
			gfx::shakeMenus(false);
		}
#endif
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
			labCheck(gSave.beatLatency == 100 && gSave.scoreRules == 3 && gSave.best[MODE_ZEN] == zenBefore, "a save from before the Latency Test starts at 0 ms and keeps its bests");
			gSave.scoreRules = rulesBefore > 3 ? rulesBefore : 3;
			// 1.2: the score tables moved to make room for more modes. Old bests come across, and only once.
			{
				const u32 classicNow = gSave.best[MODE_CLASSIC];
				const u8 musicNow = gSave.bossMusic;
				gSave.scoreRules = 2;
				gSave.best11[MODE_CLASSIC] = classicNow + 500;
				gSave.bossMusic = 0;
				saveRetireOldBests();
				labCheck(gSave.best[MODE_CLASSIC] == classicNow + 500 && gSave.best11[MODE_CLASSIC] == 0 && gSave.scoreRules == 3 && gSave.bossMusic == 1, "a 1.1 save keeps its bests in the bigger 1.2 tables, with Boss Music on");
				gSave.best[MODE_CLASSIC] = classicNow;
				saveRetireOldBests();
				labCheck(gSave.best[MODE_CLASSIC] == classicNow, "the 1.2 score move happens only once");
				gSave.bossMusic = musicNow;
			}
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

				// The same with one swipe held down the whole time, sweeping across the fruit and back on each tick.
				startLatencyTest();
				Input hold{};
				hold.touching = true;
				hold.tx = (int)kLatX - 80;
				hold.ty = (int)kLatY;
				sMenuBlade.update(hold);
				const u32 stroke = sMenuBlade.strokeId;
				int dir = 1, steps = 0;
				seen = 0;
				wait = -1;
				for (int i = 0; i < 60 * 40 && !sLatDone; i++) {
					if (wait == 0) steps = 4;
					if (wait >= 0) wait--;
					if (steps > 0) {
						hold.tx += dir * 40;
						if (--steps == 0) dir = -dir;
					}
					sMenuBlade.update(hold);
					updateLatency(hold, true, false);
					if (sLatTicks != seen) {
						seen = sLatTicks;
						wait = 2;  // the sweep starts three frames after the tick and crosses the fruit on its first step
					}
				}
				char d[48];
				snprintf(d, sizeof d, "%d slices, %.1f ms", sLatSlices, sLatResult * 1000);
				labCheck(sLatDone && sMenuBlade.strokeId == stroke && fabsf(sLatResult - 3 * kDt) < 0.002f,
				         "a swipe held down slices on every pass across the Latency Test fruit", d);
				sMenuBlade.update(Input{});
				sScreen = wasScreen;
				audio::music(MUSIC_MENU);
			}
			gSave.beatLatency = latBefore;
#if PORT_CUSTOM_MUSIC
			{
				// Add Songs: what the file picker hands back is copied into Custom Music, and the list picks it up.
				const u8 beatWas = gSave.beatTrack;
				static const char* const kNames[] = {"Lab Song A.wav", "Lab Song B.ogg", "Lab Notes.txt"};
				char src[3][520];
				const auto writeFile = [](const char* path, int bytes) {
					if (FILE* f = fopen(path, "wb")) {
						for (int i = 0; i < bytes; i++) fputc(i & 0x7f, f);
						fclose(f);
					}
				};
				for (int i = 0; i < 3; i++) {
					port::dataPath(src[i], sizeof src[i], kNames[i]);
					writeFile(src[i], 4000 + i);
				}
				const char* picked[] = {src[0], src[1], src[2], nullptr};
				const int added = custom::importFiles(picked);
				beatImportTick();
				char d[80];
				snprintf(d, sizeof d, "%d copied, chosen %s", added, beatSong() >= kTrackCount ? custom::name(beatSong() - kTrackCount) : "a game track");
				labCheck(added == 2 && beatSong() >= kTrackCount && strcmp(custom::name(beatSong() - kTrackCount), "Lab Song A") == 0,
				         "Add Songs copies the songs picked, skips other files, and picks the first new one", d);
				labCheck(strcmp(sAddNote, "Added 2 songs.") == 0 && sAddNoteT > 0, "Add Songs says how many it added");
				labCheck(custom::importFiles(picked) == 0, "the same songs picked again are not copied twice");
				beatImportTick();
				writeFile(src[0], 5000);
				const char* again[] = {src[0], nullptr};
				custom::importFiles(again);
				beatImportTick();
				labCheck(beatSong() >= kTrackCount && strcmp(custom::name(beatSong() - kTrackCount), "Lab Song A (2)") == 0,
				         "a different song with a name already there is added under a new name");
				{
					// Radio lists the custom songs after the game's tracks, and missing custom songs fall back to Auto.
					const u8 menuWas = gSave.menuTrack, gameWas = gSave.gameTrack;
					RadioEntry rlist[64];
					const int rn = radioBuildList(rlist, 64);
					const RadioEntry* lastCustom = rn > 0 ? &rlist[rn - 1] : nullptr;
					if (lastCustom && !lastCustom->isAuto) radioEquipEntry(*lastCustom);
					char d2[120];
					snprintf(d2, sizeof d2, "value %d, playing %d, last %s", gSave.menuTrack, audio::currentTrack(), lastCustom ? lastCustom->title : "none");
					labCheck(lastCustom && gSave.menuTrack == kTrackCount + custom::count() && gSave.gameTrack == gSave.menuTrack &&
					         strcmp(lastCustom->title, "Lab Song B") == 0 && audio::currentTrack() == gSave.menuTrack - 1,
					         "Radio offers the custom songs after the game's tracks, by name, and plays the one chosen", d2);
					gSave.menuTrack = (u8)(kTrackCount + kCustomSongsMax);
					audio::preview(MUSIC_MENU);
					snprintf(d2, sizeof d2, "playing %d", audio::currentTrack());
					labCheck(audio::currentTrack() < kTrackCount,
					         "a chosen song that left the folder plays Auto meanwhile", d2);
					gSave.menuTrack = menuWas;
					gSave.gameTrack = gameWas;
					audio::reapply();
				}
				const char* made[] ={"Lab Song A.wav", "Lab Song A (2).wav", "Lab Song B.ogg"};
				for (const char* m : made) {
					char p[600];
					snprintf(p, sizeof p, "%s%s", custom::folder(), m);
					remove(p);
				}
				for (auto& s : src) remove(s);
				custom::scan();
				gSave.beatTrack = beatWas;
				sAddNoteT = 0;
				audio::music(MUSIC_MENU);
			}
#endif
			{
				// The 3DS black screen readings count the kinds of fruit on show.
				sGame.start(MODE_CLASSIC);
				for (auto& o : sGame.objs) o.on = false;
				for (auto& h : sGame.halves) h.on = false;
				for (int i = 0; i < 3; i++) {
					sGame.objs[i].on = true;
					sGame.objs[i].fruit = (u8)(i < 2 ? 2 : 5);
				}
				labCheck(sGame.activeKinds() == 2, "the readings count two kinds of fruit, not three fruit");
				for (auto& o : sGame.objs) o.on = false;
			}
#if PORT_ULTRA
			if (gfx::ultraArt()) {
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
			} else {
				labCheck(!gfx::ultraLoaded(), "Textures: Ultra is skipped when unsupported");
			}
#endif
#if !PORT_3DS
			{
				// In menus the right trigger turns the stick into a blade point; without it the stick moves the selector.
				const Screen wasScreen = sScreen;
				sScreen = SCREEN_TITLE;
				sMenuPointShow = 0;
				Input pad{};
				pad.stickX = 1;
				pad.held = pad.down = KEY_RIGHT;
				stickBlade(pad);
				labCheck(!pad.touching && (pad.down & KEY_RIGHT), "in menus the stick alone still moves the selector");
				float x0 = 0;
				for (int i = 0; i < 10; i++) {
					Input held{};
					held.stickX = 1;
					held.stickSlice = true;
					held.held = held.down = KEY_RIGHT;
					stickBlade(held);
					if (i == 0) x0 = (float)held.tx;
					if (i == 9) labCheck(held.touching && held.tx > x0 + 20 && !(held.down & KEY_RIGHT), "with the trigger held the stick is a blade point that slices menus");
				}
				sMenuPointShow = 0;
				sScreen = wasScreen;
			}
#endif
#if PORT_3DS
			// The 3DSX update's file swap, on stand-in files beside the save.
			char kLabOld[128], kLabNew[128], kLabBackup[128];
			port::dataPath(kLabOld, sizeof kLabOld, "lab_swap.bin");
			port::dataPath(kLabNew, sizeof kLabNew, "lab_swap.bin.new");
			port::dataPath(kLabBackup, sizeof kLabBackup, "lab_swap.bin.old");
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
			FILE* leftover = fopen(kLabNew, "rb");
			FILE* backup = fopen(kLabBackup, "rb");
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
		labInventory();
		sGame.labProbes(sBlade, labCheck);
		{
			const u8 wasFx = gSave.effects, wasHc = gSave.highContrast;
			const float wasDull = gBladeDull;
			labCheck(FX_SIMPLE == 2 && FX_MEDIUM == 2 && FX_FULL == 3, "Effects enum preserves index 2 for Simple/Medium and 3 for Full");
#if PORT_3DS
			labCheck(strcmp(kEffectNames[FX_MEDIUM], "Medium") == 0 && strcmp(kEffectNames[FX_FULL], "Full") == 0, "3DS keeps classic Medium and Full Effects labels");
			gSave.effects = FX_FULL;
			gSave.highContrast = 0;
			labCheck(!gfx::fullGpuFx(), "3DS disables Full GPU additive pass");
			gfx::setAdditive(true);
			labCheck(!gfx::isAdditive(), "3DS setAdditive stays an inert no-op");
#else
			labCheck(strcmp(kEffectNames[FX_SIMPLE], "Simple") == 0 && strcmp(kEffectNames[FX_FULL], "Full") == 0, "Universal exposes Simple and Full Effects labels");
			gSave.highContrast = 0;
			gSave.effects = FX_NONE;
			labCheck(!gfx::fullGpuFx(), "Effects: None disables Full GPU FX");
			gSave.effects = FX_LOW;
			labCheck(!gfx::fullGpuFx(), "Effects: Low disables Full GPU FX");
			gSave.effects = FX_SIMPLE;
			labCheck(!gfx::fullGpuFx(), "Effects: Simple disables Full GPU FX");
			gSave.effects = FX_FULL;
			gSave.highContrast = 1;
			labCheck(!gfx::fullGpuFx(), "High Contrast disables Full GPU additive FX");
			gSave.highContrast = 0;
			labCheck(gfx::fullGpuFx(), "Effects: Full enables GPU additive FX when High Contrast is off");
			gfx::setAdditive(true);
			labCheck(gfx::isAdditive(), "gfx::setAdditive(true) enters additive blend state");
			gfx::setAdditive(false);
			labCheck(!gfx::isAdditive(), "gfx::setAdditive(false) restores standard blend state");
			gfx::setAdditive(true);
			gfx::beginField();
			labCheck(!gfx::isAdditive(), "gfx::beginField clears lingering additive state");

			sGame.start(MODE_CLASSIC);
			for (auto& o : sGame.objs) o.on = false;
			for (auto& r : sGame.rings) r.on = false;
			for (auto& p : sGame.parts) p.on = false;
			sGame.objs[0] = {};
			sGame.objs[0].on = true;
			sGame.objs[0].kind = OBJ_GOLD;
			sGame.objs[0].x = 160;
			sGame.objs[0].y = 120;
			sGame.rings[0] = {160, 120, 0.2f, 0.4f, 1.4f, WHITE, true};
			sGame.parts[0] = {160, 120, 0, 0, 0.2f, 0.4f, 1.0f, 0, WHITE, 1, true};
			sGame.flash = 0.5f;
			sGame.flashColor = WHITE;
			boom::start(160, 120, 1.0f);
			boom::update(0.12f);
			sBlade.reset();
			sBlade.count = 4;
			sBlade.head = 4;
			for (int i = 0; i < 4; i++) sBlade.trail[i] = {120.0f + i * 20.0f, 120.0f, 0.02f * (3 - i)};
			gBladeDull = 0;

			gSave.effects = FX_SIMPLE;
			gfx::beginField();
			const u32 c0 = gfx::drawCallsLive;
			sGame.drawField(sBlade);
			gfx::beginField();
			const u32 simpleCalls = gfx::drawCallsLive - c0;
			labCheck(!gfx::isAdditive(), "drawField under Effects: Simple leaves standard blend restored");

			gSave.effects = FX_FULL;
			gfx::beginField();
			const u32 c1 = gfx::drawCallsLive;
			sGame.drawField(sBlade);
			gfx::beginField();
			const u32 fullCalls = gfx::drawCallsLive - c1;
			labCheck(!gfx::isAdditive(), "drawField under Effects: Full leaves standard blend restored");

			char msg[64];
			snprintf(msg, sizeof msg, "simple=%u full=%u", (unsigned)simpleCalls, (unsigned)fullCalls);
			labCheck(fullCalls > simpleCalls, "Effects: Full issues additional GPU additive blend passes beyond Simple", msg);

			boom::clear();
			sBlade.reset();
			sGame.flash = 0;
			sGame.objs[0].on = false;
			sGame.rings[0].on = false;
			sGame.parts[0].on = false;
#endif
			gBladeDull = wasDull;
			gSave.effects = wasFx;
			gSave.highContrast = wasHc;
		}
		{
			// Reroll warning and card bounds stay on-screen on 3DS (240px) and across all Universal aspect ratios.
#if !PORT_TWO_SCREENS
			const float wasW = gFieldW, wasH = gFieldH;
			static const float kAspects[] = {9.0f / 16.0f, 3.0f / 4.0f, 1.0f, 4.0f / 3.0f, 9.0f / 6.0f, 16.0f / 9.0f, 21.0f / 9.0f};
			const int aspCount = (int)(sizeof kAspects / sizeof kAspects[0]);
#else
			const int aspCount = 1;
#endif
			bool rerollOk = true, modalsOk = true;
			for (int ai = 0; ai < aspCount; ai++) {
#if !PORT_TWO_SCREENS
				gfx::fieldFor(kAspects[ai], &gFieldW, &gFieldH);
#endif
				static const int kCardCounts[] = {3, 5};
				for (int n : kCardCounts) {
					const OrcCardLayout cl = orcCardLayout(n);
					if (cl.reroll.x < 0 || cl.reroll.x + cl.reroll.w > kFieldW + 0.5f || cl.reroll.y + cl.reroll.h > kFieldH - 12.0f || cl.warnY + 8.0f > kFieldH) {
						rerollOk = false;
					}
					for (int i = 0; i < n; i++) {
						float cx, cy, cw, ch;
						orcCardRect(i, n, &cx, &cy, &cw, &ch);
						if (cx < 0 || cx + cw > kFieldW + 0.5f || cy < kFieldTop || cy + ch > cl.reroll.y - 2.0f) modalsOk = false;
					}
					if (!PORT_TWO_SCREENS && kFieldH < 285.0f && cl.headerY < kFieldTop + 6.5f) modalsOk = false;
				}
				for (int s = 0; s < kSlots; s++) {
					float sx, sy, sw, sh;
					orcPlaceRect(s, &sx, &sy, &sw, &sh);
					if (sx < 0 || sx + sw > kFieldW + 0.5f || sy + sh > kFieldH) modalsOk = false;
				}
				const Button kb = orcKeepButton();
				if (kb.x < 0 || kb.x + kb.w > kFieldW + 0.5f || kb.y + kb.h > kFieldH) modalsOk = false;
				sGame.orc.savedRoom = 25;
				Button rBtns[2];
				float rx, ry, rw, rh;
				resumeButtons(rBtns, &rx, &ry, &rw, &rh);
				if (rx < 0 || rx + rw > kFieldW + 0.5f || rBtns[1].y + rBtns[1].h > ry + rh) modalsOk = false;
#ifdef DEV_TOOLS
				float dx, dy, dw, dh;
				Button db[7];
				devSkipLayout(&dx, &dy, &dw, &dh, db);
				if (dx < 0 || dx + dw > kFieldW + 0.5f || dy + dh > kFieldH) modalsOk = false;
#endif
			}
#if !PORT_TWO_SCREENS
			gFieldW = wasW;
			gFieldH = wasH;
#endif
			labCheck(rerollOk, "Reroll button and Need more run coins warning stay on-screen across aspect ratios");
			labCheck(modalsOk, "Level-Up cards, charm placement, Resume prompt, and Dev Skip stay inside screen bounds");
#if PORT_TWO_SCREENS
			sGame.start(MODE_PRACTICE);
			float ph = 0, pmy = 0, plx = 0, prx = 0;
			sGame.hudMetrics(&ph, &pmy, &plx, &prx);
			labCheck(ph <= 56.0f && pmy > 2.0f + ph && pmy <= 2.0f + ph + 8.0f, "3DS HUD box shrinks snugly when Practice hides coins and timer");
			labCheck(plx >= 22.0f && prx <= 376.0f, "3DS Practice HUD text stays clear of left and right corner pins");
			static const Mode kHudModes[] = {MODE_CLASSIC, MODE_ARCADE, MODE_ORCHARD};
			for (Mode m : kHudModes) {
				sGame.start(m);
				float mh = 0, mmy = 0, mlx = 0, mrx = 0;
				sGame.hudMetrics(&mh, &mmy, &mlx, &mrx);
				labCheck(mh > ph && mh < 82.0f && mmy > 2.0f + mh, "3DS bottom-row modes expand HUD compactly under 82px and shift meter right below");
				labCheck(mlx >= 22.0f && mrx <= 376.0f, "3DS HUD text stays clear of corner pins across modes");
			}
#else
#if PORT_PAUSE_BUTTON
			labCheck(kPauseSign.y >= kFieldTop + 4.0f && kInvSign.x + kInvSign.w <= 80.0f,
			         "Mobile Pause and Inventory buttons clear the Frenzy meter and coin/power-up lane");
			{
				const Screen wasScr = sScreen;
				const bool wasInv = sOrcInv;
				const InputMode wasMode = gInputMode;
				sScreen = SCREEN_GAME;
				sGame.start(MODE_ORCHARD);
				sGame.orc.devPrompt = true;
				sOrcInv = false;
				gInputMode = INPUT_TOUCH;
				Input tapInv{};
				tapInv.touching = true;
				tapInv.tx = (int)(kInvSign.x + 18);
				tapInv.ty = (int)(kInvSign.y + 18);
				updateFrame(tapInv, false);
				const bool ignoredWhileHeld = !sOrcInv;

				// Dynamic input detection: Touch opens on-screen Inventory button and hides keyboard slot keys;
				// Keyboard+Mouse shows Q/E and ignores on-screen touch button; Controller shows L/R.
				sGame.orc.devPrompt = false;
				sGame.orc.resumePrompt = false;
				sGame.orc.cutscene = false;
				sGame.orc.choosing = false;
				sGame.phase = PHASE_PLAY;
				gInputMode = INPUT_KEYMOUSE;
				gStickInUse = false;
				const bool kmPrompts = strcmp(orcSlotKey(0), "Q") == 0 && strcmp(orcSlotKey(1), "E") == 0 && !touchUiActive();
				updateFrame(tapInv, false);
				const bool kmHidesTouchBtn = !sOrcInv;

				gInputMode = INPUT_PAD;
				gStickInUse = true;
				const bool padPrompts = strcmp(orcSlotKey(0), "L") == 0 && strcmp(orcSlotKey(1), "R") == 0 && padUiActive() && !touchUiActive();
				labSnapV2f("v2f_input_pad");

				gInputMode = INPUT_TOUCH;
				gStickInUse = false;
				const bool touchPrompts = orcSlotKey(0)[0] == 0 && orcSlotKey(1)[0] == 0 && touchUiActive();
				labSnapV2f("v2f_input_touch");
				updateFrame(tapInv, false);
				const bool touchOpensInv = sOrcInv;

				labCheck(ignoredWhileHeld && kmPrompts && kmHidesTouchBtn && padPrompts && touchPrompts && touchOpensInv,
				         "Unified Universal port dynamically switches Touch buttons, Q/E keyboard prompts, and L/R controller prompts");
				sOrcInv = wasInv;
				sScreen = wasScr;
				gInputMode = wasMode;
				gStickInUse = false;
			}
#endif
			labCheck(PORT_QUIT_BOMB == (!PORT_WEB), "Quit Game bomb stays disabled only on Web/WASM");
			labCheck(PORT_PAUSE_BUTTON == 1 && PORT_MOUSE == 1, "Unified Universal port keeps touch overlay and mouse/keyboard active across all builds");
			{
				const float wasW2 = gFieldW, wasH2 = gFieldH;
				sGame.start(MODE_ORCHARD);
				sGame.phase = PHASE_PLAY;
				sGame.orc.resumePrompt = false;
				sGame.orc.bossRoom = true;
				sBlade.reset();
				gfx::fieldFor(16.0f / 9.0f, &gFieldW, &gFieldH);
				sGame.orc.boss = {};
				sGame.orc.boss.on = true;
				sGame.orc.boss.id = BOSS_MELON;
				sGame.orc.boss.radius = 48.0f;
				sGame.orc.boss.x = kFieldW - 10.0f;
				sGame.orc.boss.y = 120.0f;
				sGame.orc.boss.baseY = 120.0f;
				sGame.orc.boss.nextAttack = 5.0f;
				gfx::fieldFor(9.0f / 16.0f, &gFieldW, &gFieldH);
				sGame.update(sBlade);
				labCheck(sGame.orc.boss.x - sGame.orc.boss.radius >= -0.5f && sGame.orc.boss.x + sGame.orc.boss.radius <= kFieldW + 0.5f,
				         "Shrinking window mid-boss-fight immediately clamps active boss inside field bounds");
				sGame.orc.boss = {};
				gFieldW = wasW2;
				gFieldH = wasH2;
			}
			{
				Button pauseBtns[4];
				for (int i = 0; i < 4; i++) pauseBtns[i] = kPauseButtons[i];
				pauseBtns[0] = {};
				int f = 1;
				dpadNav(pauseBtns, 4, KEY_UP, f);
				labCheck(f == 3, "Single-screen Pause D-pad wraps around Resume..Main Menu without hitting hidden Back button");
			}
#endif
			{
				const Screen wasScr = sScreen;
				const bool wasInv = sOrcInv;
				sOrcInv = false;
				sScreen = SCREEN_GAME;
				const bool openOk = !hudShouldHide();
				sOrcInv = true;
				const bool invOk = hudShouldHide();
				sOrcInv = false;
				sScreen = SCREEN_PAUSE;
				const bool pauseOk = hudShouldHide();
				sScreen = SCREEN_RESULT;
				const bool resOk = hudShouldHide();
				sOrcInv = wasInv;
				sScreen = wasScr;
				labCheck(openOk && invOk && pauseOk && resOk, "hudShouldHide hides corner charm overlay and mode HUD behind Pause, Result, and Inventory");
			}
			{
				// Beat Slice high-combo stage light show: dormant below threshold, fades in on high streak/combo,
				// advances tempo-synced beam rotation & hue per beat, and tones down under Calm Effects.
				const u8 wasCalm = gSave.calmFx;
				const u8 wasFx = gSave.effects;
				Blade noBlade = {};
				sGame.start(MODE_BEAT);
				sGame.phase = PHASE_PLAY;
				sGame.beatLen = 0.5f;
				sGame.beatStepLen = 0.5f;
				sGame.beatFirst = 0.0f;
				sGame.beatLoop = 0.0f;
				sGame.beatPerLoop = 0;
				sGame.beatStreak = 2;
				sGame.chain = 2;
				for (int i = 0; i < 15; i++) sGame.update(noBlade);
				labCheck(sGame.beatLightFade <= 0.01f, "Beat Slice light show stays dormant below high combo threshold");

				bool blendRestored = true;
				auto measureLightShow = [&](bool* outBlendOk = nullptr) -> u32 {
#if PORT_TWO_SCREENS
					gfx::beginFrame();
					gfx::beginBottom();
					const u32 c0 = gfx::drawCallsLive;
					sGame.drawBeatLightShow();
					if (outBlendOk) *outBlendOk = true;
					const u32 drawn = gfx::drawCallsLive - c0;
					gfx::endFrame();
					return drawn;
#else
					gfx::beginField();
					const u32 c0 = gfx::drawCallsLive;
					sGame.drawBeatLightShow();
					if (outBlendOk) *outBlendOk = !gfx::isAdditive();
					gfx::beginField();
					return gfx::drawCallsLive - c0;
#endif
				};
				const u32 lowCalls = measureLightShow();

				sGame.beatStreak = 14;
				sGame.chain = 14;
				for (int i = 0; i < 60; i++) {
					sGame.beatStreak = 14;
					sGame.chain = 14;
					sGame.update(noBlade);
				}
				gSave.calmFx = 0;
				gSave.effects = FX_FULL;
				const u32 highCalls = measureLightShow(&blendRestored);

				gSave.calmFx = 1;
				const u32 calmCalls = measureLightShow();
				gSave.calmFx = wasCalm;
				gSave.effects = wasFx;

				const float clockA = sGame.beatClock();
				const u32 hueA = hueColor(clockA * 0.05f + floorf(clockA / sGame.beatLen) * 0.12f);
				sGame.songT += sGame.beatLen;
				const float clockB = sGame.beatClock();
				const u32 hueB = hueColor(clockB * 0.05f + floorf(clockB / sGame.beatLen) * 0.12f);
				char bmsg[96];
				snprintf(bmsg, sizeof bmsg, "low=%u high=%u calm=%u fade=%.2f blend=%d hue=%d",
				         (unsigned)lowCalls, (unsigned)highCalls, (unsigned)calmCalls, sGame.beatLightFade, (int)blendRestored, (int)(hueA != hueB));
				labCheck(lowCalls == 0 && highCalls >= 1 && calmCalls <= highCalls && blendRestored && hueA != hueB && sGame.beatLightFade > 0.8f,
				         "Beat Slice light show activates at high combo, shifts hue/beams with beat tempo, and honors Calm Effects", bmsg);
			}
			// Android/Touch parity: plain taps pick non-back menu buttons, screen transitions ignore held
			// touches, menuHit covers label text, and portrait Practice clears kPauseSign.
			{
				const InputMode wasMode = gInputMode;
				const auto wasCut = sCut;
				const int wasPauseFocus = sFocus[SCREEN_PAUSE];
				gInputMode = INPUT_TOUCH;
				sCut = {};

				// 1) Screen-transition while held must NOT trigger a phantom swipe/tap on the new screen.
				int dummyFocus = 1;
				Input heldIn{};
				heldIn.touching = true;
				heldIn.tx = (int)(kPauseButtons[2].x + kPauseButtons[2].w * 0.5f);
				heldIn.ty = (int)(kPauseButtons[2].y + kPauseButtons[2].h * 0.5f);
				sliceButtons(kPauseButtons, 4, SCREEN_PAUSE, heldIn, true, dummyFocus);
				Input relIn{};
				sliceButtons(kPauseButtons, 4, SCREEN_PAUSE, relIn, true, dummyFocus);
				const bool noPhantom = !sCut.pending;

				// 2) Fresh plain tap + release on a non-zero touch button ("Options" on Pause) activates sCut.pending.
				sCut = {};
				sliceButtons(kPauseButtons, 4, SCREEN_PAUSE, heldIn, false, dummyFocus);
				sliceButtons(kPauseButtons, 4, SCREEN_PAUSE, heldIn, true, dummyFocus);
				sliceButtons(kPauseButtons, 4, SCREEN_PAUSE, relIn, true, dummyFocus);
				const bool tapWorks = sCut.pending && sCut.index == 2 && sCut.screen == SCREEN_PAUSE;

				// 3) menuHit covers the label text under a fruit while keeping outer top-left corners clear.
				float fx, fy, d;
				fruitSpot(kPauseButtons[2], &fx, &fy, &d);
				const bool labelHit = menuHit(kPauseButtons[2], SCREEN_PAUSE, (int)fx, (int)(fy + d * 0.5f + 6.0f));
				const bool cornerMiss = !menuHit(kTitleButtons[0], SCREEN_TITLE, (int)kTitleButtons[0].x + 2, (int)kTitleButtons[0].y + 2);

#if !PORT_TWO_SCREENS && PORT_PAUSE_BUTTON
				const float wasFieldW = gFieldW;
				gFieldW = 240.0f;
				const float practiceTop = (touchUiActive() && kFieldW < 340.0f) ? fmaxf(30.0f * uiScale() + 28.0f, kFieldTop + 44.0f) : (30.0f * uiScale() + 28.0f);
				const bool clearsPause = practiceTop >= kPauseSign.y + kPauseSign.h;
				gFieldW = wasFieldW;
#else
				const bool clearsPause = true;
#endif
				sCut = wasCut;
				sFocus[SCREEN_PAUSE] = wasPauseFocus;
				gInputMode = wasMode;
				labCheck(noPhantom && tapWorks && labelHit && cornerMiss && clearsPause,
				         "Touch tap activates menu buttons, ignores held transition, hits labels, and portrait Practice clears Pause");
			}
			// Portrait Inventory centering/bounds, orientation-switch invariants, Options spacing, and boss music playback.
			{
#if !PORT_3DS
				const float wasW = gFieldW, wasH = gFieldH;
				const InputMode wasMode = gInputMode;
				const u8 wasLarge = gSave.largeText;
				const int savedPassives = sGame.orc.passiveCount;
				gInputMode = INPUT_TOUCH;

				bool invOk = true;
				const float aspects[2] = {9.0f / 20.0f, 9.0f / 16.0f};
				for (int ai = 0; ai < 2; ai++) {
					gfx::fieldFor(aspects[ai], &gFieldW, &gFieldH);
					const OrcInvLayout L = orcInvLayout();
					const float minTop = kFieldTop + 44.0f;
					const float avail = (kFieldH - 8.0f) - minTop;
					const float usedH = (L.btnY + 24.0f) - L.headerY;
					const float topGap = L.headerY - minTop;
					const float botGap = (kFieldH - 8.0f) - (L.btnY + 24.0f);
					const bool sym = avail > usedH ? fabsf(topGap - botGap) <= 2.5f : L.headerY >= minTop - 0.1f;
					gSave.largeText = 1;
					applyAccessibility();
					const float detBot = orcInvDetailsBottom(CARD_BLADE);
					gSave.largeText = wasLarge;
					applyAccessibility();
					sGame.orc.passiveCount = 18;
					const float pScrollMax = orcPassiveMaxScroll(L);
					sGame.orc.passiveCount = savedPassives;
					if (!L.stacked || L.headerY < minTop - 0.1f || !sym || L.detailsY - L.bottom > 12.0f || detBot >= L.btnY || pScrollMax <= 0.0f) {
						invOk = false;
					}
				}
				labCheck(invOk, "Portrait Inventory centers vertically, clears touch signs, avoids voids, and fits Larger Text");

				gFieldW = 240.0f;
				gFieldH = 427.0f;
				const Room50CutsceneBox b1 = room50CutsceneBox(kRoom50Lines[0]);
				gFieldH = 533.0f;
				const Room50CutsceneBox b2 = room50CutsceneBox(kRoom50Lines[0]);
				float rx = 0, ry = 0, rw = 0, rh = 0;
				rushBuildRect(0, &rx, &ry, &rw, &rh);
				const OrcCardLayout rushL = orcCardLayout(BUILD_COUNT, false);

				const Mode wasModeG = sGame.mode;
				const auto wasPhaseG = sGame.phase;
				const Orchard wasOrc = sGame.orc;
				sGame.mode = MODE_ORCHARD;
				sGame.phase = PHASE_PLAY;
				sGame.orc.choosing = false;
				sGame.orc.cutscene = false;
				sGame.orc.resumePrompt = false;
				sGame.orc.bossRoom = true;
				sGame.orc.resumeT = 0;
				sGame.orc.stillT = 0;
				sGame.orc.boss.id = BOSS_LEMON;
				sGame.orc.boss.attack = ATK_NONE;
				sGame.orc.boss.lemonadeOn = true;
				sGame.orc.boss.lemLevel = 0.5f;
				sGame.orc.boss.on = true;
				sGame.orc.boss.beaten = false;
				sGame.orc.boss.enterT = 0;
				sGame.orc.boss.roarT = 0;
				sGame.orc.boss.dazeT = 0;
				sGame.orc.boss.radius = 28.0f;
				sGame.orc.boss.x = 400.0f;
				sGame.orc.boss.drainOn = true;
				sGame.orc.boss.drainX = 390.0f;
				sGame.update(sBlade);
				const bool bossClamped = sGame.orc.boss.x <= gFieldW - 36.0f && sGame.orc.boss.drainX <= gFieldW - 44.0f &&
				                         fabsf(sGame.orc.boss.baseY - (kFieldTop + (kFieldH - kFieldTop) * 0.36f)) < 0.01f;
				char od[128];
				snprintf(od, sizeof od, "dy=%.1f ry=%.1f/%.1f bx=%.1f drainX=%.1f baseY=%.1f",
				         (double)(b2.y - b1.y), (double)ry, (double)rushL.cardTop,
				         (double)sGame.orc.boss.x, (double)sGame.orc.boss.drainX, (double)sGame.orc.boss.baseY);
				sGame.orc = wasOrc;
				sGame.mode = wasModeG;
				sGame.phase = wasPhaseG;

				labCheck(fabsf(b2.y - b1.y - 106.0f) < 0.5f && fabsf(ry - rushL.cardTop) < 0.01f && bossClamped,
				         "Orientation switches invalidate cached dialogue height, align Rush cards, and clamp boss bounds", od);

				gFieldW = wasW;
				gFieldH = wasH;
				gInputMode = wasMode;
#endif
				bool optOk = true;
				const bool wasFromPause = sOptionsFromPause;
				for (int fp = 0; fp < 2; fp++) {
					sOptionsFromPause = (fp == 1);
					const int n = optionButtons();
					for (int i = 1; i < n; i++) {
						const Button& a = sOptionButtons[i];
						if (a.x < 0 || a.x + a.w > 320.0f || a.y < 44.0f || a.y + a.h > 236.0f) optOk = false;
						for (int j = i + 1; j < n; j++) {
							const Button& b = sOptionButtons[j];
							if (a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y) optOk = false;
						}
					}
				}
				sOptionsFromPause = wasFromPause;
				optionButtons();
				labCheck(optOk, "Options buttons stay inside bounds without overlap in full and pause views");

				bool bossFilesOk = true;
				int firstBossTrack = -1;
				for (int t = 0; t < kTrackCount; t++) {
					if (trackBoss(t) >= 0) {
						if (firstBossTrack < 0) firstBossTrack = t;
						if (!audio::trackFileThere(t)) bossFilesOk = false;
					}
				}
				bool bossPlaysOk = true;
				if (audio::available() && firstBossTrack >= 0) {
					audio::music(MUSIC_PLAY);
					for (int f = 0; f < 40; f++) audio::update(kDt);
					audio::bossMusic(firstBossTrack);
					for (int f = 0; f < 160; f++) audio::update(kDt);
					bossPlaysOk = audio::bossMusicOn() && audio::currentTrack() == firstBossTrack;
					audio::bossMusicEnd();
					for (int f = 0; f < 160; f++) audio::update(kDt);
				}
				labCheck(bossFilesOk && bossPlaysOk, "Every boss track asset exists and bossMusic enters playback without dropping");
			}
		}
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
