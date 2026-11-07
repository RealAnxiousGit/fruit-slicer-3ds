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

#ifdef BOT_TOUR
static constexpr int kBotDwell = 200;
#else
static constexpr int kBotDwell = 30;
#endif

static void autotestMenus(Input& in) {
	// A scripted walk: title, each top-level screen, each settings section and its controls.
	if (sBotPhaseFrames < kBotDwell || sSliding || sFade > 0) return;
	sBotPhaseFrames = 0;
	const float firstRowY = kListTop + kRowH * 0.5f;
	switch (sBotMenuStep++) {
		case 0: botTap(kTitleButtons[3]); return;   // How to Play
		case 1: botTap(kBack); return;
		case 2: botTap(kTitleButtons[2]); return;   // Credits
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
#ifdef TEST_SPAWN
	sGame.spawnReport();
	sBotDone = true;
	return;
#endif
	sBotPhaseFrames++;
	static Screen lastScreen = SCREEN_LOADING;
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
				sBotDone = true;
				fprintf(stderr, "[bot] all modes finished\n");
				return;
			}
			fprintf(stderr, "[bot] starting %s\n", kModes[sBotMode].name);
			sBotModeFrames = 0;
			botTap(kModeButtons[sBotMode + 1]);
			return;

		case SCREEN_RESULT:
			if (sBotPhaseFrames < 80) return;
			sBotPhaseFrames = 0;
			fprintf(stderr, "[bot] result mode=%s score=%lu sliced=%d combo=%d best=%lu newBest=%d strikes=%d waves=%lu bombFrames=%lu overlapFrames=%lu finaleHits=%d frenzies=%lu\n", kModes[sGame.mode].name,
			        (unsigned long)sGame.score, sGame.sliced, sGame.bestCombo, (unsigned long)sGame.best, sGame.newBest, sGame.strikes,
			        (unsigned long)sGame.statWaves, (unsigned long)sGame.statBombFrames, (unsigned long)sGame.statOverlapFrames, sGame.finaleHits, (unsigned long)sGame.statFrenzies);
			{
				// Emulators buffer their logs, so measurement runs read the result from a file.
				char botPath[256];
				port::dataPath(botPath, sizeof botPath, "bot.txt");
				FILE* f = fopen(botPath, "a");
				if (f) {
					fprintf(f, "%s score=%lu sliced=%d combo=%d waves=%lu bombFrames=%lu overlapFrames=%lu frenzies=%lu resets=%lu finale=%d\n", kModes[sGame.mode].name, (unsigned long)sGame.score, sGame.sliced, sGame.bestCombo, (unsigned long)sGame.statWaves,
					        (unsigned long)sGame.statBombFrames, (unsigned long)sGame.statOverlapFrames, (unsigned long)sGame.statFrenzies, (unsigned long)sGame.statResets, sGame.finaleHits);
					fprintf(f, "  elapsed=%.1f throws=%lu feverThrows=%lu feverTime=%.1f bombHits=%lu dropped=%lu peakObjs=%lu\n", sGame.elapsed, (unsigned long)sGame.statThrows, (unsigned long)sGame.statFeverThrows, sGame.statFeverTime,
					        (unsigned long)sGame.statBombHits, (unsigned long)sGame.statDropped, (unsigned long)sGame.statPeakObjs);
#ifdef TEST_BALANCE
					fprintf(f, "BAL %s diff=%d t=%.0f fruit=%lu,%lu,%lu bombs=%lu,%lu,%lu fever=%lu sliced=%d hits=%lu score=%lu combo=%d frenzies=%lu resets=%lu gravity=%.0f\n", kModes[sGame.mode].name, gSave.difficulty, sGame.elapsed,
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
	if (sBotModeFrames == 600) {
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
	bool suicide = sBotModeFrames > 1500 && sGame.timeLeft <= 0;
#endif
#endif
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
	if (inRow >= 3 && !bombInRow) {
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
