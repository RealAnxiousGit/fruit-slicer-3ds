// Records hub, Tabbed Appearance gallery (with live preview + 4 free Oscilloscope styles),
// and Milestone A shells for Radio, Achievements, and Mods. Included by source/main.cpp.

// ---------------------------------------------------------------- Records hub
static const Button kRecordsButtons[] = {
	kBack,
	{24, 54, 88, 74, "Appearance"},
	{116, 54, 88, 74, "Radio"},
	{208, 54, 88, 74, "Achievements"},
	{68, 136, 90, 74, "Journal"},
	{162, 136, 90, 74, "Stats"},
};
static constexpr int kRecordsCount = sizeof kRecordsButtons / sizeof kRecordsButtons[0];

static void drawRecords() {
	const int f = sFocus[SCREEN_RECORDS];
	drawHeader("Records");
	drawBackArrow(f == 0, sPressed == 0);
	for (int i = 1; i < kRecordsCount; i++) {
		const Button& b = kRecordsButtons[i];
		float fx, fy, d;
		fruitSpot(b, &fx, &fy, &d);
		const float dy = (sPressed == i) ? 2.0f : 0.0f;
		const float hi = highlightFade(b.x, b.y, b.w, b.h, f == i);
		const u32 labelCol = lerpColor(COL_CREAM, COL_ACCENT, hi);
		if (hi > 0.01f)
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, fx, fy + dy, d / 34.0f, d / 34.0f, 0, withAlpha(COL_ACCENT, 0.9f * hi));
		const float bob = (hi > 0.25f && !gSave.calmFx) ? sinf(sTime * 4.0f) * 1.5f * hi : 0.0f;
		if (!isCut(b)) drawMenuFruit(fruitFor(b.label), fx, fy + dy + bob, d, sTime * 0.8f + b.x * 0.05f);
		fitText(b.label, fx, fy + d * 0.42f + dy, 0.42f, 100.0f, labelCol, ALIGN_CENTER);
	}
}

static void drawRecordsTop() {
	static const char* const kRecHints[kRecordsCount] = {
		"Back to the title",
		"Boards, skins, trails, blades and oscilloscope.",
		"Listen to unlocked tracks and view waveforms.",
		"Track milestones and earn bonus coins.",
		"Every fruit and boss you have sliced.",
		"Personal records, StreetPass and Global Top 100.",
	};
	const int f = sFocus[SCREEN_RECORDS];
	topPanel(30, 108, 340, 82);
	gfx::setDepth(1.0f);
	drawLogo(200, 54, 0.62f);
	gfx::setDepth(0.7f);
	gfx::text(f >= 1 && f < kRecordsCount ? kRecordsButtons[f].label : "Records", 200, 118, 0.56f, COL_ACCENT, ALIGN_CENTER);
	drawWrappedText(kRecHints[f >= 0 && f < kRecordsCount ? f : 0], 200, 148, 0.35f, 316, 14.0f, COL_CREAM, ALIGN_CENTER);
}

// ---------------------------------------------------------------- Appearance gallery
enum AppTab {
	APP_BOARDS,
#if PORT_TWO_SCREENS
	APP_SCENERY,
#endif
	APP_SKINS,
	APP_SPLATS,
	APP_TRAILS,
	APP_BLADES,
	APP_OSC,
	APP_TABS
};

static const char* appTabName(int tab) {
	switch (tab) {
		case APP_BOARDS: return "Boards";
#if PORT_TWO_SCREENS
		case APP_SCENERY: return "Scenery";
#endif
		case APP_SKINS: return "Skins";
		case APP_SPLATS: return "Splats";
		case APP_TRAILS: return "Trails";
		case APP_BLADES: return "Blades";
		default: return "Oscilloscope";
	}
}

static const char* const kOscNames[] = {"Rainbow", "Simple White", "Simple Green", "Bloomin' Blue"};
static const char* const kOscLines[] = {
	"Full spectrum cycling waveform.",
	"Crisp pure white retro line.",
	"Classic phosphor green monitor.",
	"Glowing blue line with soft bloom.",
};
static constexpr int kOscCount = 4;

struct AppEntry {
	int value;
	const char* name;
	const char* line;
};

static int appEntries(int tab, AppEntry* out, int maxOut) {
	int n = 0;
	auto push = [&](int v, const char* name, const char* line) {
		if (n < maxOut) out[n++] = {v, name, line};
	};
	switch (tab) {
		case APP_BOARDS: {
			static const char* const kBaseLines[kBaseBoards] = {
				"Classic navy picnic blanket.",
				"Warm golden kitchen table.",
				"Pale ceramic dinner plate.",
				"Crisp slate stone slab.",
				"Fresh green orchard turf.",
				"Deep midnight velvet cloth.",
			};
			for (int i = 0; i < kBaseBoards; i++) push(i, kBoardNames[i], kBaseLines[i]);
			const int sc = shopCount(SHOP_THEMES);
			for (int i = 0; i < sc; i++)
				if (shopOwned(SHOP_THEMES, i)) push(shopValue(SHOP_THEMES, i), shopItem(SHOP_THEMES, i).name, shopItem(SHOP_THEMES, i).line);
			break;
		}
#if PORT_TWO_SCREENS
		case APP_SCENERY: {
			static const char* const kSceneLines[kSceneCount] = {
				"Warm dusk over the orchard hills.",
				"Bright morning mist and meadow.",
				"Quiet moonlit ocean bay.",
				"Drifting pink sakura blossoms.",
				"Golden autumn harvest trees.",
				"Starlit mountain peaks.",
			};
			for (int i = 0; i < kSceneCount; i++) push(i, kSceneNames[i], kSceneLines[i]);
			break;
		}
#endif
		case APP_SKINS:
			push(0, "Normal", "Natural fresh fruit colours.");
			for (int i = 0; i < shopCount(SHOP_SKINS); i++)
				if (shopOwned(SHOP_SKINS, i)) push(shopValue(SHOP_SKINS, i), shopItem(SHOP_SKINS, i).name, shopItem(SHOP_SKINS, i).line);
			break;
		case APP_SPLATS:
			push(0, "Fruit's Own", "Each fruit splashes its own juice.");
			for (int i = 0; i < shopCount(SHOP_SPLATS); i++)
				if (shopOwned(SHOP_SPLATS, i)) push(shopValue(SHOP_SPLATS, i), shopItem(SHOP_SPLATS, i).name, shopItem(SHOP_SPLATS, i).line);
			break;
		case APP_TRAILS:
			push(0, "Plain", "Clean standard blade streak.");
			for (int i = 0; i < shopCount(SHOP_TRAILS); i++)
				if (shopOwned(SHOP_TRAILS, i)) push(shopValue(SHOP_TRAILS, i), shopItem(SHOP_TRAILS, i).name, shopItem(SHOP_TRAILS, i).line);
			break;
		case APP_BLADES: {
			static const char* const kBladeLines[kBladeCount] = {
				"Classic bright steel edge.",
				"Fiery orange-gold ember edge.",
				"Cool arctic frost edge.",
				"Vibrant lime-green edge.",
				"Royal violet gleam.",
				"Deep crimson razor edge.",
			};
			for (int i = 0; i < kBladeCount; i++) push(i, kBladeNames[i], kBladeLines[i]);
			break;
		}
		case APP_OSC:
			for (int i = 0; i < kOscCount; i++) push(i, kOscNames[i], kOscLines[i]);
			break;
	}
	return n;
}

static u8* appSlot(int tab) {
	switch (tab) {
		case APP_BOARDS: return &gSave.board;
#if PORT_TWO_SCREENS
		case APP_SCENERY: return &gSave.scene;
#endif
		case APP_SKINS: return &gSave.skin;
		case APP_SPLATS: return &gSave.splat;
		case APP_TRAILS: return &gSave.trail;
		case APP_BLADES: return &gSave.blade;
		default: return &gSave.oscStyle;
	}
}

static constexpr int kAppTab0 = 1, kAppTile0 = kAppTab0 + APP_TABS;
static constexpr int kAppMaxItems = 24;
static constexpr int kAppSigns = kAppTile0 + kAppMaxItems;
static constexpr int kAppCols = 3, kAppRowsShown = 3;
static constexpr float kAppTop = 88, kAppStep = 48;
static constexpr float kAppWindowTop = kAppTop - 2, kAppWindowH = kAppRowsShown * kAppStep + 2;

static int sAppTab = APP_BOARDS;
static int sAppSel = 0;
static RowScroller sAppList;
static u8 sAppKeep[APP_TABS];

static Button appButton(int i) {
	if (i == 0) return kBack;
	if (i < kAppTile0) {
		const int t = i - kAppTab0;
		const int row1Count = APP_TABS - 3;  // 4 on 3DS (with Scenery), 3 on Universal
		if (t < row1Count) {
			const float w = (300 - (row1Count - 1) * 4.0f) / row1Count;
			return {10 + t * (w + 4), 40, w, 20, appTabName(t)};
		}
		const int t2 = t - row1Count;
		const float w = (300 - 2 * 4.0f) / 3.0f;
		return {10 + t2 * (w + 4), 63, w, 20, appTabName(t)};
	}
	const int k = i - kAppTile0;
	return {(float)(10 + (k % kAppCols) * 102), kAppTop + (k / kAppCols) * kAppStep - sAppList.pos, 96, 44, nullptr};
}

static int appHiddenRows(int count) {
	int r = (count + kAppCols - 1) / kAppCols - kAppRowsShown;
	return r > 0 ? r : 0;
}

static void appRemember() {
	for (int t = 0; t < APP_TABS; t++) sAppKeep[t] = *appSlot(t);
}

static void appPutBack() {
	for (int t = 0; t < APP_TABS; t++) *appSlot(t) = sAppKeep[t];
}

static void appSyncPreview() {
	AppEntry list[kAppMaxItems];
	const int n = appEntries(sAppTab, list, kAppMaxItems);
	if (sAppSel >= n) sAppSel = n > 0 ? n - 1 : 0;
	appPutBack();
	if (n > 0) *appSlot(sAppTab) = (u8)list[sAppSel].value;
}

static void openAppearance() {
	appRemember();
	sAppTab = APP_BOARDS;
	rowReset(sAppList);
	AppEntry list[kAppMaxItems];
	const int n = appEntries(sAppTab, list, kAppMaxItems);
	sAppSel = 0;
	for (int i = 0; i < n; i++)
		if (list[i].value == sAppKeep[sAppTab]) { sAppSel = i; break; }
	sFocus[SCREEN_APPEARANCE] = kAppTile0 + sAppSel;
	sPreviewBlade.reset();
	appSyncPreview();
	goTo(SCREEN_APPEARANCE, 1);
}

static void leaveAppearance() {
	appPutBack();
	gfx::sync();
	sMenuBlade.reset();
	goTo(SCREEN_RECORDS, -1);
}

static void appSetTab(int tab) {
	tab = (tab % APP_TABS + APP_TABS) % APP_TABS;
	if (tab == sAppTab) return;
	sAppTab = tab;
	rowReset(sAppList);
	AppEntry list[kAppMaxItems];
	const int n = appEntries(sAppTab, list, kAppMaxItems);
	sAppSel = 0;
	for (int i = 0; i < n; i++)
		if (list[i].value == sAppKeep[sAppTab]) { sAppSel = i; break; }
	rowReveal(sAppList, sAppSel / kAppCols, kAppRowsShown, kAppStep);
	sFocus[SCREEN_APPEARANCE] = kAppTile0 + sAppSel;
	sPreviewBlade.reset();
	appSyncPreview();
}

static void appEquipSelected() {
	AppEntry list[kAppMaxItems];
	const int n = appEntries(sAppTab, list, kAppMaxItems);
	if (sAppSel < 0 || sAppSel >= n) return;
	sAppKeep[sAppTab] = (u8)list[sAppSel].value;
	*appSlot(sAppTab) = sAppKeep[sAppTab];
	audio::play(SFX_GOLD, 0.55f);
	gfx::sync();
	saveWrite();
}

static void appSeg(float x0, float y0, float x1, float y1, float w, u32 col) {
	const float dx = x1 - x0, dy = y1 - y0;
	const float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.01f) return;
	const float nx = (-dy / len) * (w * 0.5f), ny = (dx / len) * (w * 0.5f);
	gfx::tri(x0 - nx, y0 - ny, col, x0 + nx, y0 + ny, col, x1 + nx, y1 + ny, col);
	gfx::tri(x0 - nx, y0 - ny, col, x1 + nx, y1 + ny, col, x1 - nx, y1 - ny, col);
}

static int oscBloomLevel() {
#if PORT_GRAPHICS_MENU
	return gSave.bloom;
#else
	return gSave.calmFx ? 0 : 1;
#endif
}

static void drawOscPreview(float x0, float y0, float w, float h, int style, float t) {
	gfx::rect(x0, y0, w, h, rgba(12, 16, 26, 220));
	gfx::rect(x0, y0, w, 1.5f, rgba(255, 255, 255, 45));
	gfx::rect(x0, y0 + h - 1.5f, w, 1.5f, rgba(255, 255, 255, 45));
	const int segs = 44;
	const float cy = y0 + h * 0.5f;
	const float amp = h * 0.34f;
	const int bl = oscBloomLevel();
	const bool bloomOn = (style == 3 && bl > 0);
	if (bloomOn) gfx::setAdditive(true);
	float px = x0 + 6, py = cy;
	for (int i = 0; i <= segs; i++) {
		const float u = (float)i / segs;
		const float sx = x0 + 6 + u * (w - 12);
		const float env = sinf(u * kPi);
		const float wave = sinf(u * 15.0f - t * 6.5f) * 0.65f + sinf(u * 31.0f + t * 9.0f) * 0.35f;
		const float sy = cy + wave * amp * env;
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
			col = bl >= 2 ? rgba(130, 215, 255, 255) : rgba(92, 188, 255, 255);
		}
		if (i > 0) {
			if (style == 3 && bl >= 2) appSeg(px, py, sx, sy, 10.5f, rgba(48, 130, 255, 52));
			if (style == 3 && bl >= 1) appSeg(px, py, sx, sy, 5.5f, rgba(64, 150, 255, bl >= 2 ? 115 : 75));
			appSeg(px, py, sx, sy, 2.2f, col);
		}
		px = sx;
		py = sy;
	}
	if (bloomOn) gfx::setAdditive(false);
}

static void updateAppearance(const Input& in, bool wasTouching) {
	int& f = sFocus[SCREEN_APPEARANCE];
	AppEntry list[kAppMaxItems];
	const int n = appEntries(sAppTab, list, kAppMaxItems);
	if (in.down & KEY_B) {
		audio::click();
		leaveAppearance();
		return;
	}
	bool leave;
	if (backCut(SCREEN_APPEARANCE, in, wasTouching, &leave)) {
		if (leave) leaveAppearance();
		return;
	}
	if (in.down & KEY_L) { audio::click(0.5f); appSetTab(sAppTab - 1); return; }
	if (in.down & KEY_R) { audio::click(0.5f); appSetTab(sAppTab + 1); return; }
	if (rowScroll(sAppList, in, wasTouching, kAppWindowTop, kAppWindowTop + kAppWindowH, kAppStep, appHiddenRows(n))) sPressed = -1;

	Button btns[kAppSigns];
	for (int i = 0; i < kAppTile0 + n; i++) btns[i] = appButton(i);
	const bool inWindow = in.ty >= kAppWindowTop && in.ty < kAppWindowTop + kAppWindowH;
	if (in.touching || in.hovering) {
		for (int i = kAppTile0; i < kAppTile0 + n; i++)
			if (!inWindow) btns[i] = {-100, -100, 0, 0, nullptr};
	}
	int t = touchButtons(btns, kAppTile0 + n, in, wasTouching, f);
#if PORT_MOUSE
	if (in.hovering && !in.touching)
		for (int i = 0; i < kAppTile0 + n; i++)
			if (hit(btns[i], in.tx, in.ty)) f = i;
#endif
	if (in.repeat & KEY_UP) {
		if (f >= kAppTile0 && f - kAppTile0 < kAppCols) f = kAppTab0;
		else if (f >= kAppTile0) f -= kAppCols;
		else if (f >= kAppTab0 + APP_TABS - 3) f = kAppTab0;
		else if (f >= kAppTab0) f = 0;
	} else if (in.repeat & KEY_DOWN) {
		if (f == 0) f = kAppTab0;
		else if (f < kAppTile0) {
			const int row1Count = APP_TABS - 3;
			if (f - kAppTab0 < row1Count) f = kAppTab0 + row1Count;
			else if (n > 0) f = kAppTile0;
		} else if (f + kAppCols < kAppTile0 + n) {
			f += kAppCols;
		}
	} else if (in.repeat & KEY_LEFT) {
		if (f > kAppTab0 && f < kAppTile0) f--;
		else if (f > kAppTile0) f--;
	} else if (in.repeat & KEY_RIGHT) {
		if (f >= kAppTab0 && f + 1 < kAppTile0) f++;
		else if (f >= kAppTile0 && f + 1 < kAppTile0 + n) f++;
	}
	if (f >= kAppTile0 && f < kAppTile0 + n) {
		const int idx = f - kAppTile0;
		if (idx != sAppSel) {
			sAppSel = idx;
			appSyncPreview();
		}
		rowReveal(sAppList, sAppSel / kAppCols, kAppRowsShown, kAppStep);
	}
	if (t < 0 && (in.down & KEY_A)) t = f;
	if (t == 0) cutBack(SCREEN_APPEARANCE);
	else if (t >= kAppTab0 && t < kAppTile0) {
		audio::click(0.6f);
		appSetTab(t - kAppTab0);
	} else if (t >= kAppTile0 && t < kAppTile0 + n) {
		sAppSel = t - kAppTile0;
		appSyncPreview();
		appEquipSelected();
	}
}

static void drawAppearance() {
	const int f = sFocus[SCREEN_APPEARANCE];
	AppEntry list[kAppMaxItems];
	const int n = appEntries(sAppTab, list, kAppMaxItems);
	drawHeader("Appearance");
	drawBackArrow(f == 0, sPressed == 0);
	for (int i = 0; i < APP_TABS; i++) {
		Button b = appButton(kAppTab0 + i);
		drawPlank(b.x, b.y, b.w, b.h, f == kAppTab0 + i, sPressed == kAppTab0 + i);
		fitText(b.label, b.x + b.w / 2, b.y + 4, 0.34f, b.w - 6, i == sAppTab ? COL_ACCENT : COL_DIM);
	}
	gfx::setClip(0, kAppWindowTop, 320, kAppWindowH);
	for (int k = 0; k < n; k++) {
		Button b = appButton(kAppTile0 + k);
		if (b.y + b.h + 2 < kAppWindowTop || b.y - 2 > kAppWindowTop + kAppWindowH) continue;
		const bool sel = k == sAppSel;
		const bool inUse = list[k].value == sAppKeep[sAppTab];
		const bool pressed = sPressed == kAppTile0 + k;
		drawPlank(b.x, b.y, b.w, b.h, sel, pressed);
		if (!sel && f == kAppTile0 + k)
			gfx::sign(sprites_plank_focus_idx, b.x - 2, b.y - 2 + (pressed ? 2 : 0), b.w + 4, b.h + 4, withAlpha(COL_CREAM, 0.5f));
		fitText(list[k].name, b.x + b.w / 2, b.y + 8, 0.36f, b.w - 10, inUse ? COL_ACCENT : COL_CREAM);
		gfx::text(inUse ? "IN USE" : "EQUIP", b.x + b.w / 2, b.y + 27, 0.29f, inUse ? COL_ACCENT : COL_DIM, ALIGN_CENTER);
	}
	gfx::clearClip();
	drawScrollMark(kAppTop, kAppRowsShown * kAppStep - 6, sAppList.pos, appHiddenRows(n) * kAppStep);
}

static void drawAppearancePreview() {
	AppEntry list[kAppMaxItems];
	const int n = appEntries(sAppTab, list, kAppMaxItems);
	if (sAppSel < 0 || sAppSel >= n) return;
	const AppEntry& e = list[sAppSel];
	const bool inUse = e.value == sAppKeep[sAppTab];
	topPanel(30, 14, 340, 206);
	gfx::setDepth(0.7f);
	fitText(e.name, 200, 22, 0.54f, 316, COL_ACCENT);
	drawWrappedText(e.line, 200, 48, 0.35f, 316, 13.0f, COL_CREAM, ALIGN_CENTER);
	gfx::setDepth(0.5f);
	if (sAppTab == APP_BOARDS) {
		gfx::image(SHEET_BOARD, 0, 126, 76, 148, 96);
	} else if (sAppTab == APP_SKINS) {
		if (gSave.artStyle == ART_3D && m3d::ready()) {
			m3d::begin(400, gfx::stereoPerZ());
			if (gSave.skin > 0) m3d::setSkin(&shopSkinLook(gSave.skin - 1));
			m3d::Pose pose = {200, 124, 0, 0.25f, sTime * 1.1f, 0.1f, 1.35f, 0, 0, 0};
			m3d::draw(m3d::MESH_MELON, m3d::PART_WHOLE, pose);
			m3d::setSkin(nullptr);
			m3d::end();
		} else {
			gfx::sprite(SHEET_FRUIT, fruit_melon_idx, 200, 124, 1.35f, 1.35f);
		}
	} else if (sAppTab == APP_SPLATS) {
		const u32 col = e.value == 0 ? rgba(232, 68, 86, 225) : shopSplatColour(e.value - 1);
		gfx::sprite(SHEET_SPRITES, sprites_splat0_idx, 200, 124, 1.55f, 1.55f, 0.2f, col);
	} else if (sAppTab == APP_TRAILS || sAppTab == APP_BLADES) {
		sPreviewBlade.draw();
	} else if (sAppTab == APP_OSC) {
		drawOscPreview(66, 82, 268, 86, e.value, sTime);
#if PORT_TWO_SCREENS
	} else if (sAppTab == APP_SCENERY) {
		fitText("Live top-screen scenery preview", 200, 118, 0.36f, 310, COL_DIM);
#endif
	}
	gfx::setDepth(0.7f);
	gfx::text(inUse ? "Currently Equipped" : "Tap or press A to equip", 200, 186, 0.38f, inUse ? COL_ACCENT : COL_DIM, ALIGN_CENTER);
}
