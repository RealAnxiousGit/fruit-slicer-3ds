// Test-only bot, compiled with `make AUTOTEST=1`. It presses what a player presses: all of its
// input goes through the same Input struct as the hardware. It walks the menus, plays every mode, then quits.
//   BOT_ONLY=n   measure one mode with hands off
//   BOT_TOUR     stop on each menu for a few seconds, for screenshots, and don't play
//   TEST_ART=n   start with this art style
//   TEST_DIFF=n  start on this difficulty
//   TEST_SPEED   run the Speed Test once, then quit
//   BOT_SHOTS    skip the menu walk and go straight to playing, for screenshots
//   BOT_PLAY     with BOT_ONLY, play that mode normally instead of keeping hands off
//   TEST_SEEKBOMB  with BOT_ONLY, aim for bombs
//   TEST_LATEBOMB  aim for bombs in a timed mode's last 5 seconds
//   TEST_BALANCE play every mode at high speed with nothing ending a game early, and count what is thrown
//   TEST_SPAWN   print what every mode and difficulty throws, from thousands of waves, then quit
//   TEST_LOUD    in audio.cpp: fire big combos and full swipes of cuts on a timer, to measure the mix
#ifdef BOT_ONLY
static int sBotMode = BOT_ONLY;
#else
static int sBotMode = 0;
#endif
static int sBotPhaseFrames = 0;
static int sBotStroke = 0;
static float sBotX, sBotY, sBotDX, sBotDY;
static int sBotTapFrames = 0;
static bool sBotDone = false;
static int sBotTapX, sBotTapY;
static bool sBotSwipe = false;  // menu signs need a swipe across them; settings rows take a tap
static u32 sBotKeys = 0;        // buttons to press for one frame
static u32 sBotModeFrames = 0;
#if defined(BOT_ONLY) || defined(TEST_SPEED) || defined(BOT_SHOTS) || defined(TEST_BALANCE)
static int sBotMenuStep = 100;
#else
static int sBotMenuStep = 0;
#endif

static bool autotestDone() { return sBotDone; }

static void botTap(const Button& b) {
	sBotTapX = (int)(b.x + b.w / 2);
	sBotTapY = (int)(b.y + b.h / 2);
	sBotTapFrames = 8;
	sBotSwipe = true;
}

static void botTapXY(int x, int y) {
	sBotTapX = x;
	sBotTapY = y;
	sBotTapFrames = 8;
	sBotSwipe = false;
}

#ifdef LAB
#include "lab.inc"
static bool sLabFinishing = false;
#endif

#ifdef BOT_TOUR
static constexpr int kBotDwell = 200;
#elif defined(TEST_RECORD)
static constexpr int kBotDwell = 110;  // trailer capture: long enough on each menu to see it
#elif defined(BOT_MENUS) && PORT_3DS
static constexpr int kBotDwell = 150;  // long enough for the emulator's capture to land on each page
#else
static constexpr int kBotDwell = 30;
#endif

#if defined(BOT_MENUS)
// A picture of Pick a Game and every Options page, for checking layouts. Needs FRUIT_SLICER_SHOTS.
// The 3DS can't save its own screen, so there the walk just pauses on each page for the emulator's capture.
#if PORT_3DS
static void botMenuPicture(const char*) {}
#else
namespace gfx {
void requestShot(const char* path);
}
static void botMenuPicture(const char* name) {
	char path[300];
	snprintf(path, sizeof path, "%s/%s.png", getenv("FRUIT_SLICER_SHOTS"), name);
	gfx::requestShot(path);
}
#endif
static void botMenuWalk() {
	const auto open = [](SectionId id) { botTap(optionButtonFor(id)); };
#if !PORT_3DS
	// FRUIT_SLICER_WALK_FROM=n starts the walk at step n, to photograph the newest pages without the wait.
	static bool jumped = false;
	if (!jumped && getenv("FRUIT_SLICER_WALK_FROM")) sBotMenuStep = atoi(getenv("FRUIT_SLICER_WALK_FROM"));
	jumped = true;
#endif
	switch (sBotMenuStep++) {
		case 0: gSave.practiceDone = 1; botTap(kTitleButtons[0]); return;
		case 1: botMenuPicture("a1_modes1"); sFocus[SCREEN_MODES] = 2; sBotKeys = KEY_RIGHT; return;
		case 2: botMenuPicture("a2_modes2"); sBotKeys = KEY_LEFT; sBotPhaseFrames = kBotDwell - 4; return;
		case 3: botMenuPicture("a3_mid_slide"); return;
		case 4: botTap(kBack); return;
		case 5: botTap(kTitleButtons[1]); return;
		case 6: botMenuPicture("a5_options"); open(SID_VISUALS); return;
		case 7: botMenuPicture("a6_visuals"); sFocus[SCREEN_SECTION] = kSections[sSection].count - 1; sScroll = maxScroll(); return;
		case 8: botMenuPicture("a7_visuals_end"); sBotKeys = KEY_A; return;
		case 9: botMenuPicture("a8_advanced"); sScroll = maxScroll(); return;
		case 10: botMenuPicture("a9_advanced_end"); botTap(kBack); return;
		case 11: botTap(kBack); return;
		case 12: open(SID_GAMEPLAY); return;
		case 13: botMenuPicture("b1_gameplay"); sFocus[SCREEN_SECTION] = kSections[sSection].count - 1; sBotKeys = KEY_A; return;
		case 14: botMenuPicture("b2_gameplay_more"); botTap(kBack); return;
		case 15: botTap(kBack); return;
		case 16: open(SID_ACCESS); return;
		case 17: botMenuPicture("b3_access"); sFocus[SCREEN_SECTION] = kSections[sSection].count - 1; sBotKeys = KEY_A; return;
		case 18: botMenuPicture("b4_access_more"); botTap(kBack); return;
		case 19: botTap(kBack); return;
		case 20: open(SID_AUDIO); return;
		case 21: botMenuPicture("b5_audio"); openShop(); return;
		case 22: botMenuPicture("c1_shop_themes"); shopSetTab(SHOP_MUSIC); sShopItem = 6; sShopList.pos = sShopList.aim = kShopStep; return;
		case 23: botMenuPicture("c2_shop_music"); sShopItem = 7; sShopList.pos = sShopList.aim = kShopStep + 30; return;
		case 24: botMenuPicture("c3_shop_midway"); shopSetTab(SHOP_SPLATS); return;
		case 25: botMenuPicture("c4_shop_splats"); shopSetTab(SHOP_TRAILS); return;
		case 26: botMenuPicture("c5_shop_trails"); leaveShop(); return;
		case 27: sFocus[SCREEN_JOURNAL] = 1; goTo(SCREEN_JOURNAL, 1); return;
		case 28: botMenuPicture("d1_journal"); sJournalList.pos = sJournalList.aim = kJournalStep; return;
		case 29: botMenuPicture("d2_journal_scrolled"); sGame.labShowcase(); sScreen = SCREEN_GAME; botMenuPicture("e1_fruit"); return;
		case 30: sGame.labShowcase(); sScreen = SCREEN_PAUSE; botMenuPicture("e2_pause"); return;
		case 31: gSave.ownedExtras |= 3u << 17; sScreen = SCREEN_SHOP; shopSetTab(SHOP_SKINS); sShopItem = 0; botMenuPicture("e5_shop_skins"); return;
		case 32: sSection = sectionIndex(SID_MIXER); sFocus[SCREEN_SECTION] = 1; sScroll = 0; sScreen = SCREEN_SECTION; botMenuPicture("e6_mixer"); return;
		case 33: gSave.skin = 1; sGame.labShowcase(); sScreen = PORT_3DS ? SCREEN_PAUSE : SCREEN_GAME; botMenuPicture("e3_gold"); return;
		case 34: gSave.skin = 2; sGame.labShowcase(); sScreen = PORT_3DS ? SCREEN_PAUSE : SCREEN_GAME; botMenuPicture("e4_glass"); return;
		case 35: gSave.skin = 0; gSave.mixer[MIX_GIANT] = 1; sGame.labShowcase(); sScreen = PORT_3DS ? SCREEN_PAUSE : SCREEN_GAME; botMenuPicture("e7_giant"); return;
		case 36: gSave.mixer[MIX_GIANT] = 0; gSave.mixer[MIX_TINY] = 1; sGame.labShowcase(); sScreen = PORT_3DS ? SCREEN_PAUSE : SCREEN_GAME; botMenuPicture("e8_tiny"); return;
		case 37: {
			gSave.mixer[MIX_TINY] = 0;
			sPassPlayers = 2;
			sPassTurn = 1;
			sStartMode = MODE_CLASSIC;
			for (int i = 0; i < kPassMax; i++) passDefaultName(i);
			passSetName(1, "Sam");
			sSection = sectionIndex(SID_PASS);
			sFocus[SCREEN_SECTION] = 2;
			sScroll = 0;
			sScreen = SCREEN_SECTION;
			botMenuPicture("f1_pass_setup");
			return;
		}
		case 38: passScreen(SCREEN_PASS_TURN); botMenuPicture("f2_pass_turn"); return;
		case 39: {
			sPassScores[0] = 340;
			sPassScores[1] = 512;
			sPassScores[2] = 95;
			passScreen(SCREEN_PASS_BOARD);
			botMenuPicture("f3_pass_board");
			return;
		}
		case 40: {
			startLatencyTest();
			static const float kMarks[] = {0.034f, -0.018f, 0.052f, 0.012f, 0.041f};
			memcpy(sLatMarks, kMarks, sizeof kMarks);
			sLatMarkCount = 5;
			sLatSlices = 9;
			botMenuPicture("g1_latency");
			return;
		}
		case 41: sLatDone = true; sLatResult = 0.036f; botMenuPicture("g2_latency_done"); return;
		case 42: {
			leaveLatencyTest();
			sGame.start(MODE_CLASSIC);
			sGame.phase = PHASE_OVER;
			sGame.phaseT = 5;
			sGame.score = 1234;
			sGame.sliced = 88;
			sGame.bestCombo = 7;
			sScreen = SCREEN_RESULT;
			sFocus[SCREEN_RESULT] = kResultCount - 1;
			botMenuPicture("h1_result_share");
			return;
		}
#if !PORT_3DS
		case 43: sCardFrame = true; port::shareCard(30, 12, 340, 212); return;  // the card lands in Pictures/Fruit Slicer
		case 44: botMenuPicture("h2_result_after"); return;
#endif
#if PORT_ULTRA
		// The same field on High and on Ultra, for comparing sharpness (run with FRUIT_SLICER_WINDOW=3840x2160).
		// Paused, so the fruit hold still between the two pictures.
		case 45: gSave.texQuality = 2; port::applyGraphics(); sGame.labShowcase(); sScreen = SCREEN_PAUSE; return;
		case 46: botMenuPicture("i1_high"); return;
		case 47: gSave.texQuality = 3; port::applyGraphics(); return;
		case 48: botMenuPicture("i2_ultra"); return;
		case 49: goTo(SCREEN_TITLE, -1); return;
		case 50: botMenuPicture("i3_ultra_title"); return;
#endif
#if !PORT_3DS
		case 51: goTo(SCREEN_TITLE, -1); return;
		case 52:  // the controller's menu blade point, resting on Options
			sMenuPointShow = 1.5f;
			sMenuPointX = kTitleButtons[1].x + kTitleButtons[1].w * 0.5f + 14;
			sMenuPointY = kTitleButtons[1].y + 12;
			botMenuPicture("j1_menu_point");
			return;
#endif
		default: return;
	}
}
#endif

static void autotestMenus(Input& in) {
	// A scripted walk: title, each top-level screen, each settings section and its controls.
	if (sBotPhaseFrames < kBotDwell || sSliding || sFade > 0) return;
#if defined(BOT_MENUS)
	sBotPhaseFrames = 0;
	botMenuWalk();
	return;
#endif
#ifdef TEST_RECORD
	if (sBotMenuStep == 0 && sBotPhaseFrames < 360) return;  // trailer capture: a long look at the title first
#endif
	sBotPhaseFrames = 0;
#ifdef LAB
	labMenus();
	return;
#endif
	const float firstRowY = kListTop + kRowH * 0.5f;
	switch (sBotMenuStep++) {
		case 0: botTap(kTitleButtons[4]); return;   // How to Play
		case 1: botTap(kBack); return;
		case 2: botTap(kTitleButtons[2]); return;   // Fruit Journal
		case 3: botTap(kBack); return;
		case 4: botTap(kTitleButtons[1]); return;   // Options
		case 5: botTap(optionButtonFor(SID_AUDIO)); return;  // Audio
		case 6: botTapXY((int)kTrackX1, (int)firstRowY); return;  // drag-free slider tap
		case 7: botTapXY((int)((kTrackX0 + kTrackX1) / 2), (int)firstRowY); return;
		case 8: botTap(kBack); return;
		case 9: botTap(optionButtonFor(SID_VISUALS)); return;  // Visuals
		case 10: botTapXY(231, (int)(firstRowY + kRowH)); return;  // Board: next
		case 11: botTapXY(231, (int)(firstRowY + kRowH * 2)); return;  // Scenery: next
		case 12: botTapXY(231, (int)(firstRowY + kRowH * 3)); return;  // Blade: next
		case 13: botTapXY(231, (int)(firstRowY + kRowH * 4)); return;  // Effects: wraps round
		case 14: botTap(kBack); return;
		case 15: botTap(optionButtonFor(SID_GAMEPLAY)); return;  // Gameplay
		case 16: botTapXY(231, (int)(firstRowY + kRowH)); return;  // Difficulty
		case 17: botTap(kBack); return;
		case 18: botTap(optionButtonFor(SID_DATA)); return;  // Scores and Data
		case 19: botTap(kBack); return;
		case 20: botTap(kBack); return;              // back to the title
#ifdef BOT_TOUR
		case 21: botTap(kTitleButtons[0]); return;   // Play
		case 22: {
			sBotPhaseFrames = -60;
			sBotDone = sBotMenuStep > 40;
			return;
		}
		default:
			if (sBotMenuStep > 26) sBotDone = true;
			return;
#else
		default:
			sBotMenuStep = 100;
			return;
#endif
	}
	(void)in;
}

static void autotestInput(Input& in) {
	in = Input{};
	in.down = in.repeat = sBotKeys;
	sBotKeys = 0;
#ifdef TEST_SPAWN
	sGame.spawnReport();
	sBotDone = true;
	return;
#endif
	sBotPhaseFrames++;
	static Screen lastScreen = SCREEN_LOADING;
#ifdef TEST_RECORD
	static u32 recFrame = 0;
	recFrame++;
	static int lastMode = -2;
	const int modeNow = sScreen == SCREEN_GAME ? (int)sGame.mode : -1;
	if (sScreen != lastScreen || modeNow != lastMode) {
		// Where each screen starts in the recording, for cutting it into clips.
		static FILE* marks = nullptr;
		if (!marks) {
			const char* dir = getenv("FRUIT_SLICER_RECORD");
			char path[400];
			snprintf(path, sizeof path, "%s/markers.txt", dir ? dir : ".");
			marks = fopen(path, "w");
		}
		if (marks) {
			fprintf(marks, "%u %d %s %s\n", recFrame, (int)sScreen, modeNow >= 0 ? modeName(sGame.mode) : "-", gSave.artStyle == ART_2D ? "2D" : "3D");
			fflush(marks);
		}
		lastMode = modeNow;
	}
#endif
	if (sScreen != lastScreen) {
		lastScreen = sScreen;
		sBotPhaseFrames = 0;
	}

	if (sBotTapFrames > 0) {
		sBotTapFrames--;
		if (sBotTapFrames > 2) {
			in.touching = true;
			in.tx = sBotSwipe ? sBotTapX - 18 + (7 - sBotTapFrames) * 9 : sBotTapX;
			in.ty = sBotTapY;
		}
		return;
	}
#ifdef LAB
	if (sLabFinishing) {
		labFinish(in);
		return;
	}
#endif

#ifdef TEST_SPEED
	static bool started = false;
	if (!started && sScreen == SCREEN_TITLE && sBotPhaseFrames > 60) {
		started = true;
		startSpeedTest();
		return;
	}
	if (sScreen == SCREEN_BENCH && sBenchDone) sBotDone = true;
	return;
#endif

	if (sBotMenuStep < 100 && sScreen != SCREEN_GAME) {
		autotestMenus(in);
		return;
	}

	switch (sScreen) {
		case SCREEN_TITLE:
			if (sBotPhaseFrames < 30 || sSliding || sFade > 0) return;
			sBotPhaseFrames = 0;
			botTap(kTitleButtons[0]);
			return;

		case SCREEN_MODES:
			if (sBotPhaseFrames < 45 || sSliding || sFade > 0) return;
			sBotPhaseFrames = 0;
#ifdef BOT_ONLY
			if (sBotMode != BOT_ONLY) {
#else
			if (sBotMode >= MODE_COUNT) {
#endif
#ifdef LAB
				sLabFinishing = true;  // then the quit checks (lab.inc)
				fprintf(stderr, "[bot] all modes finished\n");
				return;
#endif
				sBotDone = true;
				fprintf(stderr, "[bot] all modes finished\n");
				return;
			}
#ifdef LAB
			if (labModePages()) return;
#endif
			if (modePageOf(sBotMode) != sModePage) {
				const Button& next = kPageArrows[1];
				botTapXY((int)(next.x + next.w / 2), (int)(next.y + next.h / 2));
				return;
			}
			fprintf(stderr, "[bot] starting %s\n", kModes[sBotMode].name);
			sBotModeFrames = 0;
#if defined(LAB) && PORT_3DS
			{
				// The stylus: a plain tap on a mode sign starts it, with no swipe. Three tries, then a swipe.
				static int tries = 0, forMode = -1;
				if (forMode != sBotMode) {
					forMode = sBotMode;
					tries = 0;
				}
				const Button b = modeSignOnPage(sBotMode);
				if (tries++ < 3) {
					sLabTapped = true;
					botTapXY((int)(b.x + b.w / 2), (int)(b.y + b.h / 2));
					return;
				}
				labCheck(false, "a stylus tap on a mode sign starts it", kModes[sBotMode].name);
				sLabTapped = false;
			}
#endif
			botTap(modeSignOnPage(sBotMode));
			return;

		case SCREEN_BEAT:
#ifdef BOT_CUSTOM
			if (sBotPhaseFrames < 240 || sSliding || sFade > 0) return;  // long enough for a picture of the list
#else
			if (sBotPhaseFrames < 45 || sSliding || sFade > 0) return;
#endif
			sBotPhaseFrames = 0;
#if defined(BOT_CUSTOM) && PORT_CUSTOM_MUSIC
			// Test builds with songs in Custom Music: pick song BOT_CUSTOM of your own, then Play, which
			// opens the Beat Tool first when its beat hasn't been found.
			if (beatSong() != kTrackCount + BOT_CUSTOM && kTrackCount + BOT_CUSTOM < audio::trackCount()) {
				beatSelect(kTrackCount + BOT_CUSTOM);
				fprintf(stderr, "[bot] picked your song %s\n", custom::name(BOT_CUSTOM));
				return;
			}
			if (sListenSong >= 0) return;
			if (audio::trackHasBeat(beatSong())) fprintf(stderr, "[beat] list: %s, %.2f BPM, first beat %.3f s\n", custom::name(BOT_CUSTOM), 60 / audio::trackBeat(beatSong()), audio::trackFirst(beatSong()));
#endif
			botTap(kBeatButtons[1]);
			return;
#if PORT_CUSTOM_MUSIC
		case SCREEN_BEATTOOL:
			if (sBotPhaseFrames < 300 || sSliding || sFade > 0 || sTool.listening) return;  // long enough for a picture
			sBotPhaseFrames = 0;
			fprintf(stderr, "[beat] tool: %s, %.2f BPM, first beat %.3f s%s\n", custom::name(sTool.song), sTool.has ? sTool.bpm : 0.0f, sTool.first, sTool.failed ? ", none found" : "");
			botTap(kToolButtons[1]);
			return;
#endif

		case SCREEN_RESULT:
			if (sBotPhaseFrames < 80) return;
			sBotPhaseFrames = 0;
#ifdef LAB
			labResult();
#endif
			if (sGame.mode == MODE_PRACTICE) {
				fprintf(stderr, "[bot] practice finished, slicing Let's Play\n");
				botTap(kPracticeEndButtons[1]);
				return;
			}
			fprintf(stderr, "[bot] result mode=%s score=%lu sliced=%d combo=%d best=%lu newBest=%d strikes=%d waves=%lu bombFrames=%lu overlapFrames=%lu finaleHits=%d frenzies=%lu\n", modeName(sGame.mode),
			        (unsigned long)sGame.score, sGame.sliced, sGame.bestCombo, (unsigned long)sGame.best, sGame.newBest, sGame.strikes,
			        (unsigned long)sGame.statWaves, (unsigned long)sGame.statBombFrames, (unsigned long)sGame.statOverlapFrames, sGame.finaleHits, (unsigned long)sGame.statFrenzies);
			{
				// Emulators buffer their logs, so measurement runs read the result from a file.
				char botPath[256];
				port::dataPath(botPath, sizeof botPath, "bot.txt");
				FILE* f = fopen(botPath, "a");
				if (f) {
					fprintf(f, "%s score=%lu sliced=%d combo=%d waves=%lu bombFrames=%lu overlapFrames=%lu frenzies=%lu resets=%lu finale=%d\n", modeName(sGame.mode), (unsigned long)sGame.score, sGame.sliced, sGame.bestCombo, (unsigned long)sGame.statWaves,
					        (unsigned long)sGame.statBombFrames, (unsigned long)sGame.statOverlapFrames, (unsigned long)sGame.statFrenzies, (unsigned long)sGame.statResets, sGame.finaleHits);
					fprintf(f, "  elapsed=%.1f throws=%lu feverThrows=%lu feverTime=%.1f bombHits=%lu dropped=%lu peakObjs=%lu\n", sGame.elapsed, (unsigned long)sGame.statThrows, (unsigned long)sGame.statFeverThrows, sGame.statFeverTime,
					        (unsigned long)sGame.statBombHits, (unsigned long)sGame.statDropped, (unsigned long)sGame.statPeakObjs);
#ifdef TEST_BALANCE
					fprintf(f, "BAL %s diff=%d t=%.0f fruit=%lu,%lu,%lu bombs=%lu,%lu,%lu fever=%lu sliced=%d hits=%lu score=%lu combo=%d frenzies=%lu resets=%lu gravity=%.0f\n", modeName(sGame.mode), gSave.difficulty, sGame.elapsed,
					        (unsigned long)sGame.statFruitAt[0], (unsigned long)sGame.statFruitAt[1], (unsigned long)sGame.statFruitAt[2], (unsigned long)sGame.statBombsAt[0], (unsigned long)sGame.statBombsAt[1], (unsigned long)sGame.statBombsAt[2],
					        (unsigned long)sGame.statFeverThrows, sGame.sliced, (unsigned long)sGame.statBombHits, (unsigned long)sGame.score, sGame.bestCombo, (unsigned long)sGame.statFrenzies, (unsigned long)sGame.statResets, sGame.gravity);
#endif
					fclose(f);
				}
			}
			sBotMode++;
			botTap(kResultButtons[2]);
			return;

		case SCREEN_PAUSE:
			if (sBotPhaseFrames < 40) return;
#ifdef LAB
			if (!sLabCalloutChecked) {
				sLabCalloutChecked = true;
				const float keep = sGame.calloutT;
				sGame.calloutT = 1;
				labCheck(!sGame.calloutShown(), "no callout shows behind the PAUSED sign");
				sGame.calloutT = keep;
			}
#endif
			sBotPhaseFrames = 0;
			fprintf(stderr, "[bot] resume from pause\n");
			botTap(kPauseButtons[1]);
			return;

		case SCREEN_GAME:
			break;

		default:
			return;
	}

	sBotModeFrames++;
#ifdef LAB
	labWatchGame();
	const u32 kPauseAt = 240;  // the lab plays short games
#else
	const u32 kPauseAt = 600;
#endif
	if (sBotModeFrames == kPauseAt) {
		in.down |= KEY_START;  // exercise the pause path once per mode
		sBotPhaseFrames = 0;
		return;
	}
	if (sBotModeFrames % 2400 == 0) fprintf(stderr, "[bot] t=%lus score=%lu\n", (unsigned long)(sBotModeFrames / 60), (unsigned long)sGame.score);

	if (sGame.phase == PHASE_FINALE) {
		// Scrub back and forth over the finale fruit, like a player would.
		in.touching = true;
		in.tx = (int)(160 + sinf(sBotModeFrames * 0.2f) * 75);
		in.ty = 112 + (int)(cosf(sBotModeFrames * 0.31f) * 18);
		return;
	}
#ifdef BOT_STICK
	// Stick only: steer the blade point at the next fruit and hold the trigger near it, never near a bomb.
	{
		const Obj* t = sGame.botTarget(false);
		if (!t) return;
		// Aim where the fruit will be by the time the point gets there, at full push: a player flicks.
		float dx = t->x - sPointX, dy = t->y - sPointY, d = sqrtf(dx * dx + dy * dy) + 0.01f;
		float lead = clampf(d / 600, 0.03f, 0.3f);
		dx += t->vx * lead;
		dy += t->vy * lead;
		d = sqrtf(dx * dx + dy * dy) + 0.01f;
		in.stickX = dx / d;
		in.stickY = dy / d;
		bool bombNear = false;
		for (const auto& o : sGame.objs)
			if (o.on && o.kind == OBJ_BOMB && fabsf(o.x - sPointX) < 50 && fabsf(o.y - sPointY) < 50) bombNear = true;
		in.stickSlice = d < 60 && !bombNear;
		return;
	}
#endif
#if defined(TEST_BALANCE)
	bool suicide = false;
#elif defined(TEST_SEEKBOMB)
	bool suicide = true;  // always goes for bombs, to prove what a bomb costs in this mode
#elif defined(BOT_ONLY) && !defined(BOT_PLAY)
	// Measurement run: keep hands off so every throw flies its whole path, then end the game.
	if (sBotModeFrames < 7200) return;
	bool suicide = true;
#else
	// After 25 seconds, go looking for bombs so the bomb and game-over paths get covered.
	// Timed modes stay alive instead, so that their finale always gets exercised.
#ifdef TEST_LATEBOMB
	bool suicide = sGame.timeLeft > 0 && sGame.timeLeft <= 5;
#else
#ifdef LAB
	const u32 kSeekBombsAt = 480;
#else
	const u32 kSeekBombsAt = 1500;
#endif
	bool suicide = sBotModeFrames > kSeekBombsAt && sGame.timeLeft <= 0 && sGame.mode != MODE_PRACTICE;  // lessons want bombs left alone
#endif
#endif
	if (sGame.mode == MODE_ZEN && sBotModeFrames == 900) sGame.finishRun();  // Zen is endless: stop it as the pause menu would
	if (sBotStroke > 0) {
		sBotStroke--;
		sBotX += sBotDX;
		sBotY += sBotDY;
		in.touching = true;
		in.tx = (int)clampf(sBotX, 1, 318);
		in.ty = (int)clampf(sBotY, 1, 238);
		return;
	}
	if (sBotPhaseFrames % 4 != 0) return;
	const Obj* t = sGame.botTarget(suicide);
	if (!t && suicide && sGame.mode != MODE_ZEN) {
		// Stop slicing entirely so strikes can end strike-based modes too.
		return;
	}
	if (!t) t = sGame.botTarget(false);
	if (!t) return;
	// In the lessons, wait for the fruit to come down within reach (on a 3DS they peak on the top screen).
	if (sGame.mode == MODE_PRACTICE && t->y < 12) return;
	// A short horizontal stroke through the target, staying clear of nearby bombs.
	int inRow = 0;
	bool bombInRow = false;
	if (!suicide) {
		for (const auto& o : sGame.objs) {
			if (!o.on) continue;
			float dx = o.x - t->x, dy = o.y - t->y;
			if (o.kind == OBJ_BOMB) {
				if (dx * dx + dy * dy < 60 * 60) return;
				if (fabsf(dy) < 50) bombInRow = true;
			} else if (fabsf(dy) < 22) {
				inRow++;
			}
		}
	}
	sBotY = t->y + t->vy * kDt * 3;
	sBotDY = 0;
	if (PORT_3DS && inRow >= 3 && !bombInRow && sGame.mode == MODE_PRACTICE) {
		// The three-fruit lesson: a steadier sweep near the top of the throw, which the 3DS touch filter
		// follows (it takes very long jumps between frames for noise).
		if (fabsf(t->vy) > 160) return;
		sBotX = 6;
		sBotDX = 13;
		sBotStroke = 24;
	} else if (inRow >= 3 && !bombInRow) {
		// Several fruit level with each other: one long sweep to earn a combo.
		sBotX = 6;
		sBotDX = 26;
		sBotStroke = 12;
	} else {
		sBotX = t->x - 30;
		sBotDX = 15;
		sBotStroke = 4;
	}
}
