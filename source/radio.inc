// Radio Music Gallery & Live Oscilloscope Visualizer (Milestone B).
// Included from source/main.cpp after topPanel / drawPlank / drawHeader / fitText / shop helpers.

struct RadioEntry {
	int track;      // -1 for Auto (Shuffle), else 0-based track index
	bool isAuto;
	bool isBoss;
	const char* title;
	const char* tags;
};

static bool sRadioGrid = false;
static bool sRadioDropOpen = false;
static bool sRadioOsc = false;
static bool sRadioTyping = false;
static char sRadioQuery[28] = "";
static int sRadioSel = 0;
static float sRadioScroll = 0.0f;
static float sRadioScrollDraw = 0.0f;
static int sRadioPreviewTrack = -2;
static bool sRadioDrag = false;
static int sRadioDragY = 0;
static bool sRadioMoved = false;

static const char* radioTrackTags(int track, bool isBoss) {
	if (isBoss) return "boss fight theme intense shop";
	if (track >= kTrackCount) return "custom user music";
	switch (track) {
		case 0: return "default chill morning orchard";
		case 1: return "default upbeat picnic stroll";
		case 2: return "default calm meadow zen";
		case 3: return "default retro lantern walk";
		case 4: return "default calm water chill";
		case 5: return "default zen garden moss";
		case 6: return "default upbeat breeze bamboo";
		case 7: return "default chill clouds drift";
		default: return "shop regular upbeat arcade";
	}
}

static bool ciContains(const char* hay, const char* needle) {
	if (!needle || !needle[0]) return true;
	if (!hay) return false;
	for (const char* p = hay; *p; p++) {
		const char* a = p;
		const char* b = needle;
		while (*a && *b) {
			char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
			char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
			if (ca != cb) break;
			a++;
			b++;
		}
		if (!*b) return true;
	}
	return false;
}

static int radioBuildList(RadioEntry* out, int cap) {
	int n = 0;
	auto push = [&](const RadioEntry& e) {
		if (n >= cap) return;
		if (sRadioQuery[0]) {
			const char* kindTag = e.isAuto ? "auto shuffle" : (e.isBoss ? "boss" : "regular");
			if (!ciContains(e.title, sRadioQuery) && !ciContains(e.tags, sRadioQuery) && !ciContains(kindTag, sRadioQuery))
				return;
		}
		out[n++] = e;
	};
	push({-1, true, false, "Auto (Shuffle)", "auto shuffle random all"});
	for (int t = 0; t < kTrackCount; t++) {
		if (!trackUnlocked(t)) continue;
		const bool boss = trackBoss(t) >= 0;
		push({t, false, boss, kTrackRowNames[t + 1], radioTrackTags(t, boss)});
	}
#if PORT_CUSTOM_MUSIC
	const int total = audio::trackCount();
	for (int t = kTrackCount; t < total; t++) {
		push({t, false, false, custom::name(t - kTrackCount), radioTrackTags(t, false)});
	}
#endif
	return n;
}

static bool radioIsEquipped(const RadioEntry& e) {
	if (e.isAuto) return gSave.menuTrack == 0 && gSave.gameTrack == 0;
	if (e.isBoss) return gSave.bossMusic == (u8)(e.track + 2);
	return gSave.menuTrack == (u8)(e.track + 1) && gSave.gameTrack == (u8)(e.track + 1);
}

static void radioPreviewEntry(const RadioEntry& e) {
	if (e.isAuto) {
		if (sRadioPreviewTrack != -1) {
			sRadioPreviewTrack = -1;
			audio::reapply();
		}
		return;
	}
	if (sRadioPreviewTrack != e.track) {
		sRadioPreviewTrack = e.track;
		audio::playRadioTrack(e.track);
	}
}

static void radioEquipEntry(const RadioEntry& e) {
	if (e.isAuto) {
		gSave.menuTrack = 0;
		gSave.gameTrack = 0;
		gSave.bossMusic = 1;
		sRadioPreviewTrack = -1;
		audio::reapply();
	} else if (e.isBoss) {
		gSave.bossMusic = (u8)(e.track + 2);
		sRadioPreviewTrack = e.track;
		audio::playRadioTrack(e.track);
	} else {
		gSave.menuTrack = (u8)(e.track + 1);
		gSave.gameTrack = (u8)(e.track + 1);
		sRadioPreviewTrack = e.track;
		audio::playRadioTrack(e.track);
	}
	audio::play(SFX_GOLD, 0.55f);
	saveWrite();
}

static void openRadio() {
#if PORT_CUSTOM_MUSIC
	custom::scan();
#endif
	sRadioDropOpen = false;
	sRadioOsc = false;
	sRadioTyping = false;
	sRadioDrag = false;
	sRadioSel = 0;
	sRadioScroll = 0.0f;
	sRadioScrollDraw = 0.0f;
	sRadioPreviewTrack = -2;
	RadioEntry list[64];
	const int n = radioBuildList(list, 64);
	for (int i = 0; i < n; i++) {
		if (radioIsEquipped(list[i]) && !list[i].isAuto) {
			sRadioSel = i;
			break;
		}
	}
	goTo(SCREEN_RADIO, 1);
}

static void leaveRadio() {
	if (sRadioTyping) {
		port::textStop();
		sRadioTyping = false;
	}
	sRadioDropOpen = false;
	sRadioOsc = false;
	sRadioPreviewTrack = -2;
	audio::reapply();
	goTo(SCREEN_RECORDS, -1);
}

static int radioClampOsc(int v) { return v < 0 ? 0 : (v > 3 ? 3 : v); }

static void drawOscLive(float x0, float y0, float w, float h, int style, bool fullMode) {
	gfx::rect(x0, y0, w, h, fullMode ? rgba(8, 12, 20, 245) : rgba(12, 16, 26, 220));
	const u32 gridCol = rgba(255, 255, 255, fullMode ? 22 : 16);
	gfx::rect(x0, y0 + h * 0.5f, w, 1.0f, gridCol);
	for (int gx = 1; gx < 4; gx++) gfx::rect(x0 + w * (gx * 0.25f), y0, 1.0f, h, gridCol);

	static constexpr int kPts = 96;
	float samples[kPts];
	const bool live = audio::oscSamples(samples, kPts);
	const float t = sTime;
	const float cy = y0 + h * 0.5f;
	const float amp = h * (fullMode ? 0.40f : 0.36f);
	float px = x0, py = cy;
	if (style == 3) gfx::setAdditive(true);
	for (int i = 0; i < kPts; i++) {
		const float u = (float)i / (kPts - 1);
		const float sx = x0 + 4.0f + (w - 8.0f) * u;
		float v = live ? samples[i] : 0.0f;
		if (!live) v = 0.06f * sinf(u * 12.56f + t * 3.0f);
		const float sy = clampf(cy - v * amp, y0 + 4.0f, y0 + h - 4.0f);
		u32 col = COL_CREAM;
		if (style == 0) {
			float hue = fmodf(u + t * 0.35f, 1.0f);
			if (hue < 0) hue += 1.0f;
			const float r = clampf(fabsf(hue * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
			const float g = clampf(2.0f - fabsf(hue * 6.0f - 2.0f), 0.0f, 1.0f);
			const float b = clampf(2.0f - fabsf(hue * 6.0f - 4.0f), 0.0f, 1.0f);
			col = rgba((u8)(r * 255), (u8)(g * 255), (u8)(b * 255), 255);
		} else if (style == 1) {
			col = rgba(248, 250, 255, 255);
		} else if (style == 2) {
			col = rgba(96, 245, 132, 255);
		} else {
			col = rgba(92, 188, 255, 255);
		}
		if (i > 0) {
			if (style == 3) appSeg(px, py, sx, sy, fullMode ? 7.5f : 5.5f, rgba(64, 150, 255, 90));
			appSeg(px, py, sx, sy, fullMode ? 2.6f : 2.0f, col);
		}
		px = sx;
		py = sy;
	}
	if (style == 3) gfx::setAdditive(false);
}

static void drawRadioTop() {
	const int cur = audio::currentTrack();
	const char* nowName = (cur >= 0 && cur < kTrackCount) ? kTrackRowNames[cur + 1]
#if PORT_CUSTOM_MUSIC
		: (cur >= kTrackCount && cur < audio::trackCount()) ? custom::name(cur - kTrackCount)
#endif
		: "Auto (Shuffle)";

	if (sRadioOsc) {
		drawOscLive(16, 12, 368, 216, gSave.oscStyle, true);
		gfx::rect(16, 12, 368, 26, rgba(10, 14, 22, 190));
		fitText(nowName, 26, 17, 0.42f, 230, COL_ACCENT, ALIGN_LEFT);
		fitText(kOscNames[radioClampOsc(gSave.oscStyle)], 374, 18, 0.34f, 118, COL_DIM, ALIGN_RIGHT);
		gfx::rect(16, 206, 368, 22, rgba(10, 14, 22, 180));
		gfx::text("PRESS TAB / SELECT / TAP TO RETURN", 200, 211, 0.34f, COL_CREAM, ALIGN_CENTER);
		return;
	}

	topPanel(24, 16, 352, 208);
	gfx::text("RADIO", 200, 26, 0.56f, COL_ACCENT, ALIGN_CENTER);
	gfx::text("NOW PLAYING:", 200, 52, 0.34f, COL_DIM, ALIGN_CENTER);
	fitText(nowName, 200, 68, 0.44f, 320, COL_CREAM, ALIGN_CENTER);
	drawOscLive(48, 92, 304, 68, gSave.oscStyle, false);
	char buf[96];
	const char* menuStr = gSave.menuTrack == 0 ? "Auto" : (gSave.menuTrack <= kTrackCount ? kTrackRowNames[gSave.menuTrack] : "Custom");
	const char* bossStr = gSave.bossMusic == 0 ? "Off" : (gSave.bossMusic == 1 ? "Auto" : (gSave.bossMusic <= kTrackCount + 1 ? kTrackRowNames[gSave.bossMusic - 1] : "Custom"));
	snprintf(buf, sizeof buf, "MENU/GAME: %s", menuStr);
	fitText(buf, 200, 170, 0.34f, 320, COL_CREAM, ALIGN_CENTER);
	snprintf(buf, sizeof buf, "BOSS: %s", bossStr);
	fitText(buf, 200, 186, 0.34f, 320, COL_DIM, ALIGN_CENTER);
	gfx::text("TAB / SELECT: OSCILLOSCOPE", 200, 203, 0.32f, COL_ACCENT, ALIGN_CENTER);
}

static bool radioHit(const Button& b, int x, int y) {
	return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static void updateRadio(const Input& in, bool wasTouching) {
	u32 scopeKey = KEY_SELECT;
#ifdef KEY_TAB
	scopeKey |= KEY_TAB;
#endif
	if (sRadioOsc) {
		if ((in.down & (scopeKey | KEY_B)) || (in.touching && !wasTouching)) {
			audio::click();
			sRadioOsc = false;
		}
		return;
	}

	if (sRadioTyping) {
		const int st = port::textPoll(sRadioQuery, sizeof sRadioQuery);
		if (st != 0) sRadioTyping = false;
		sRadioScroll = 0.0f;
	}

	RadioEntry list[64];
	const int n = radioBuildList(list, 64);
	if (sRadioSel >= n) sRadioSel = n > 0 ? n - 1 : 0;

	if ((in.down & scopeKey) && !sRadioTyping) {
		audio::click();
		sRadioDropOpen = false;
		sRadioOsc = true;
		return;
	}
	if ((in.down & KEY_Y) && !sRadioTyping) {
		audio::click();
		sRadioDropOpen = false;
		sRadioTyping = true;
		port::textStart(sRadioQuery, 24);
		return;
	}
	if (in.down & KEY_B) {
		if (sRadioTyping) {
			port::textStop();
			sRadioTyping = false;
			return;
		}
		if (sRadioDropOpen) {
			sRadioDropOpen = false;
			return;
		}
		audio::click();
		leaveRadio();
		return;
	}

	const float viewH = 180.0f;
	const float rowH = sRadioGrid ? 44.0f : 29.0f;
	const int cols = sRadioGrid ? 2 : 1;
	const int rows = (n + cols - 1) / cols;
	const float maxScroll = clampf(rows * rowH - viewH, 0.0f, 9999.0f);

	if (in.wheel != 0) sRadioScroll = clampf(sRadioScroll - in.wheel * 28.0f, 0.0f, maxScroll);

	const Button scopeBtn = {180, 6, 56, 20, "SCOPE"};
	const Button dropBtn = {240, 6, 68, 20, ""};
	const Button searchBtn = {14, 32, 266, 18, ""};
	const Button clearBtn = {284, 32, 24, 18, "X"};

	if (in.touching && !wasTouching) {
		if (radioHit(kBack, in.tx, in.ty)) {
			audio::click();
			leaveRadio();
			return;
		}
		if (radioHit(scopeBtn, in.tx, in.ty)) {
			audio::click();
			sRadioDropOpen = false;
			sRadioOsc = true;
			return;
		}
		if (radioHit(dropBtn, in.tx, in.ty)) {
			audio::click();
			sRadioDropOpen = !sRadioDropOpen;
			return;
		}
		if (sRadioDropOpen) {
			const Button optList = {240, 28, 68, 19, "LIST"};
			const Button optGrid = {240, 47, 68, 19, "GRID"};
			if (radioHit(optList, in.tx, in.ty)) {
				audio::click();
				sRadioGrid = false;
				sRadioDropOpen = false;
				sRadioScroll = 0.0f;
				return;
			}
			if (radioHit(optGrid, in.tx, in.ty)) {
				audio::click();
				sRadioGrid = true;
				sRadioDropOpen = false;
				sRadioScroll = 0.0f;
				return;
			}
			sRadioDropOpen = false;
		}
		if (radioHit(clearBtn, in.tx, in.ty)) {
			audio::click();
			sRadioQuery[0] = 0;
			if (sRadioTyping) { port::textStop(); sRadioTyping = false; }
			sRadioScroll = 0.0f;
			sRadioSel = 0;
			return;
		}
		if (radioHit(searchBtn, in.tx, in.ty)) {
			audio::click();
			sRadioTyping = true;
			port::textStart(sRadioQuery, 24);
			return;
		}
		if (in.ty >= 54 && in.ty <= 234) {
			sRadioDrag = true;
			sRadioDragY = in.ty;
			sRadioMoved = false;
		}
	}

	if (sRadioDrag && in.touching) {
		const int dy = in.ty - sRadioDragY;
		if (dy > 4 || dy < -4) sRadioMoved = true;
		if (sRadioMoved) {
			sRadioScroll = clampf(sRadioScroll - dy, 0.0f, maxScroll);
			sRadioDragY = in.ty;
		}
	}

	if (!in.touching && !sRadioDropOpen && in.tx >= 12 && in.tx <= 308 && in.ty >= 54 && in.ty <= 234) {
		const float relY = (in.ty - 54.0f) + sRadioScrollDraw;
		const int r = (int)(relY / rowH);
		const int c = sRadioGrid ? (in.tx >= 160 ? 1 : 0) : 0;
		const int idx = r * cols + c;
		if (idx >= 0 && idx < n && idx != sRadioSel) {
			sRadioSel = idx;
			radioPreviewEntry(list[idx]);
		}
	}

	if (sRadioDrag && !in.touching) {
		sRadioDrag = false;
		if (!sRadioMoved && in.ty >= 54 && in.ty <= 234) {
			const float relY = (in.ty - 54.0f) + sRadioScrollDraw;
			const int r = (int)(relY / rowH);
			const int c = sRadioGrid ? (in.tx >= 160 ? 1 : 0) : 0;
			const int idx = r * cols + c;
			if (idx >= 0 && idx < n) {
				sRadioSel = idx;
				radioEquipEntry(list[idx]);
			}
		}
	}

	if (!sRadioTyping && n > 0) {
		int next = sRadioSel;
		if (in.down & KEY_UP) next -= cols;
		if (in.down & KEY_DOWN) next += cols;
		if (sRadioGrid && (in.down & KEY_LEFT) && (sRadioSel % cols) > 0) next--;
		if (sRadioGrid && (in.down & KEY_RIGHT) && (sRadioSel % cols) + 1 < cols && sRadioSel + 1 < n) next++;
		if (next < 0) next = 0;
		if (next >= n) next = n - 1;
		if (next != sRadioSel) {
			sRadioSel = next;
			audio::click(0.45f);
			radioPreviewEntry(list[sRadioSel]);
			const int row = sRadioSel / cols;
			const float topY = row * rowH;
			const float botY = topY + rowH;
			if (topY < sRadioScroll) sRadioScroll = topY;
			if (botY > sRadioScroll + viewH) sRadioScroll = botY - viewH;
		}
		if (in.down & KEY_A) radioEquipEntry(list[sRadioSel]);
	}

	sRadioScroll = clampf(sRadioScroll, 0.0f, maxScroll);
	sRadioScrollDraw += (sRadioScroll - sRadioScrollDraw) * 0.28f;
}

static void drawRadio() {
	if (sRadioOsc) {
		const int cur = audio::currentTrack();
		const char* nowName = (cur >= 0 && cur < kTrackCount) ? kTrackRowNames[cur + 1]
#if PORT_CUSTOM_MUSIC
			: (cur >= kTrackCount && cur < audio::trackCount()) ? custom::name(cur - kTrackCount)
#endif
			: "Auto (Shuffle)";
		drawPlank(14, 16, 292, 208, false, false);
		gfx::text("OSCILLOSCOPE", 160, 28, 0.50f, COL_ACCENT, ALIGN_CENTER);
		fitText(nowName, 160, 50, 0.38f, 260, COL_CREAM, ALIGN_CENTER);
		drawOscLive(28, 72, 264, 96, gSave.oscStyle, true);
		gfx::text(kOscNames[radioClampOsc(gSave.oscStyle)], 160, 176, 0.36f, COL_ACCENT, ALIGN_CENTER);
		drawPlank(90, 194, 140, 22, true, false);
		gfx::text("EXIT SCOPE", 160, 198, 0.36f, COL_CREAM, ALIGN_CENTER);
		return;
	}

	drawBackArrow(false, false);
	gfx::text("RADIO", 56, 8, 0.50f, COL_CREAM, ALIGN_LEFT);

	drawPlank(180, 6, 56, 20, false, false);
	gfx::text("SCOPE", 208, 10, 0.32f, COL_CREAM, ALIGN_CENTER);
	drawPlank(240, 6, 68, 20, sRadioDropOpen, false);
	gfx::text(sRadioGrid ? "GRID v" : "LIST v", 274, 10, 0.32f, COL_ACCENT, ALIGN_CENTER);

	// Search Bar
	drawPlank(14, 32, 266, 18, sRadioTyping, false);
	if (sRadioQuery[0]) {
		char qbuf[32];
		snprintf(qbuf, sizeof qbuf, "%s%s", sRadioQuery, sRadioTyping ? "_" : "");
		fitText(qbuf, 24, 35, 0.34f, 242, COL_CREAM, ALIGN_LEFT);
	} else {
		gfx::text(sRadioTyping ? "_" : "Search title or tag...", 24, 35, 0.34f, sRadioTyping ? COL_ACCENT : COL_DIM);
	}
	drawPlank(284, 32, 24, 18, false, false);
	gfx::text("X", 296, 35, 0.34f, COL_CREAM, ALIGN_CENTER);

	RadioEntry list[64];
	const int n = radioBuildList(list, 64);

	gfx::setClip(10, 54, 300, 180);
	if (n == 0) {
		gfx::text("No matching tracks.", 160, 130, 0.38f, COL_DIM, ALIGN_CENTER);
	} else {
		const float rowH = sRadioGrid ? 44.0f : 29.0f;
		const int cols = sRadioGrid ? 2 : 1;
		for (int i = 0; i < n; i++) {
			const int r = i / cols;
			const int c = i % cols;
			const float x = sRadioGrid ? (14.0f + c * 148.0f) : 14.0f;
			const float y = 55.0f + r * rowH - sRadioScrollDraw;
			const float w = sRadioGrid ? 144.0f : 292.0f;
			const float h = sRadioGrid ? 40.0f : 26.0f;
			if (y + h < 52.0f || y > 236.0f) continue;

			const bool sel = (i == sRadioSel);
			const bool eq = radioIsEquipped(list[i]);
			drawPlank(x, y, w, h, sel, false);
			if (eq) gfx::rect(x, y, 3, h, COL_ACCENT);

			const char* kindLbl = list[i].isAuto ? "AUTO" : (list[i].isBoss ? "BOSS" : "REG");
			const u32 kindCol = list[i].isAuto ? COL_ACCENT : (list[i].isBoss ? rgba(255, 110, 110, 255) : rgba(130, 210, 255, 255));
			if (sRadioGrid) {
				fitText(list[i].title, x + 11, y + 6, 0.34f, w - 22, sel ? COL_ACCENT : COL_CREAM, ALIGN_LEFT);
				gfx::text(kindLbl, x + 11, y + 23, 0.28f, kindCol);
				if (eq) gfx::text("IN USE", x + w - 11, y + 23, 0.28f, COL_ACCENT, ALIGN_RIGHT);
			} else {
				fitText(list[i].title, x + 12, y + 7, 0.35f, w - 102, sel ? COL_ACCENT : COL_CREAM, ALIGN_LEFT);
				if (eq) gfx::text("IN USE", x + w - 52, y + 8, 0.28f, COL_ACCENT, ALIGN_RIGHT);
				gfx::text(kindLbl, x + w - 12, y + 8, 0.28f, kindCol, ALIGN_RIGHT);
			}
		}
	}
	gfx::clearClip();

	if (sRadioDropOpen) {
		drawPlank(240, 28, 68, 19, !sRadioGrid, false);
		gfx::text("LIST", 274, 32, 0.32f, !sRadioGrid ? COL_ACCENT : COL_CREAM, ALIGN_CENTER);
		drawPlank(240, 47, 68, 19, sRadioGrid, false);
		gfx::text("GRID", 274, 51, 0.32f, sRadioGrid ? COL_ACCENT : COL_CREAM, ALIGN_CENTER);
	}
}
