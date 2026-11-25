// Native Mod Manager UI (Milestone D): folders + .zip archives, mod.txt metadata,
// icon.png / black '?' fallback, priority ordering, Web .zip Import, and hot-reload.
// Included from source/main.cpp after achievements.inc.

static int sModSel = 0;
static float sModScroll = 0.0f;
static float sModScrollDraw = 0.0f;
static bool sModDrag = false;
static int sModDragY = 0;
static int sModLastX = 0;
static int sModLastY = 0;
static bool sModMoved = false;

static void openMods() {
	mods::rescan();
	sModSel = 0;
	sModScroll = sModScrollDraw = 0.0f;
	sModDrag = false;
	goTo(SCREEN_MODS, 1);
}

static void leaveMods() {
	if (mods::isDirty()) {
		gfx::reloadAll();
		audio::reapply();
		mods::clearDirty();
	}
	goTo(SCREEN_TITLE, -1);
}

static void modToggle(int idx) {
	const mods::ModInfo* m = mods::get(idx);
	if (!m) return;
	const bool next = !m->enabled;
	mods::setEnabled(idx, next);
	audio::play(next ? SFX_GOLD : SFX_CLICK, 0.65f);
	if (next) {
		gSave.achFlags |= (1 << 1);
		achUnlock(ACH_NEW_COAT_OF_PAINT);
	}
}

static void drawModsTop() {
	topPanel(24, 16, 352, 208);
	gfx::text("MODS", 200, 26, 0.54f, COL_ACCENT, ALIGN_CENTER);

	char dir[480];
	mods::modsDir(dir, sizeof dir);
	const int n = mods::count();
	if (n == 0) {
		gfx::text("NO MODS INSTALLED", 200, 66, 0.42f, COL_CREAM, ALIGN_CENTER);
		fitText("Drop mod folders or .zip files with mod.txt", 200, 96, 0.34f, 320, COL_DIM, ALIGN_CENTER);
		fitText("into your mods folder (modTemplate.zip is ignored).", 200, 114, 0.34f, 320, COL_DIM, ALIGN_CENTER);
		fitText(dir, 200, 152, 0.29f, 324, COL_ACCENT, ALIGN_CENTER);
		return;
	}

	if (sModSel >= n) sModSel = n - 1;
	const mods::ModInfo* m = mods::get(sModSel);
	if (!m) return;

	gfx::drawModIcon(sModSel, 42, 54, 54);
	fitText(m->name, 108, 54, 0.44f, 246, COL_CREAM, ALIGN_LEFT);
	char meta[96];
	snprintf(meta, sizeof meta, "BY %s  |  V%s", m->author, m->version);
	fitText(meta, 108, 76, 0.31f, 246, COL_DIM, ALIGN_LEFT);

	char badge[64];
	snprintf(badge, sizeof badge, "PRIORITY #%d  |  %s  |  %s", sModSel + 1, m->isZip ? ".ZIP" : "FOLDER",
	         m->enabled ? "ENABLED" : "DISABLED");
	fitText(badge, 108, 94, 0.30f, 246, m->enabled ? COL_ACCENT : COL_DIM, ALIGN_LEFT);

	drawWrappedText(m->desc, 42, 122, 0.34f, 316, 14.0f, COL_CREAM, ALIGN_LEFT);
	fitText(dir, 200, 196, 0.27f, 324, COL_DIM, ALIGN_CENTER);
}

static void updateMods(const Input& in, bool wasTouching) {
	if (in.down & KEY_B) {
		audio::click();
		leaveMods();
		return;
	}
	const int n = mods::count();
	if (sModSel >= n) sModSel = n > 0 ? n - 1 : 0;

	const float viewH = 192.0f;
	const float rowH = 50.0f;
	const float maxScroll = clampf(n * rowH - viewH, 0.0f, 9999.0f);
	if (in.wheel != 0) sModScroll = clampf(sModScroll - in.wheel * 28.0f, 0.0f, maxScroll);

	const Button refreshBtn = {166, 10, 68, 22, "REFRESH"};
	const Button folderBtn = {238, 10, 72, 22, ""};

	if (in.touching) {
		sModLastX = in.tx;
		sModLastY = in.ty;
	}

	if (in.touching && !wasTouching) {
		if (hit(kBack, in.tx, in.ty)) {
			audio::click();
			leaveMods();
			return;
		}
		if (radioHit(refreshBtn, in.tx, in.ty)) {
			audio::click();
			mods::rescan();
			return;
		}
		if (radioHit(folderBtn, in.tx, in.ty)) {
			audio::click();
			mods::openFolder();
			return;
		}
		if (in.ty >= 42 && in.ty <= 234) {
			sModDrag = true;
			sModDragY = in.ty;
			sModMoved = false;
		}
	}

	if (sModDrag && in.touching) {
		const int dy = in.ty - sModDragY;
		if (dy > 4 || dy < -4) sModMoved = true;
		if (sModMoved) {
			sModScroll = clampf(sModScroll - dy, 0.0f, maxScroll);
			sModDragY = in.ty;
		}
	}

	if (sModDrag && !in.touching) {
		sModDrag = false;
		if (!sModMoved && sModLastY >= 42 && sModLastY <= 234) {
			const float relY = (sModLastY - 44.0f) + sModScrollDraw;
			const int idx = (int)(relY / rowH);
			if (idx >= 0 && idx < n) {
				sModSel = idx;
				const float rx = 14.0f;
				const float ry = 44.0f + idx * rowH - sModScrollDraw;
				const Button upBtn = {rx + 180, ry + 10, 22, 26, "^"};
				const Button dnBtn = {rx + 205, ry + 10, 22, 26, "v"};
				const Button togBtn = {rx + 232, ry + 10, 52, 26, ""};
				if (radioHit(upBtn, sModLastX, sModLastY) && idx > 0) {
					audio::click(0.55f);
					mods::move(idx, -1);
					sModSel = idx - 1;
				} else if (radioHit(dnBtn, sModLastX, sModLastY) && idx + 1 < n) {
					audio::click(0.55f);
					mods::move(idx, +1);
					sModSel = idx + 1;
				} else if (radioHit(togBtn, sModLastX, sModLastY)) {
					modToggle(idx);
				} else {
					audio::click(0.45f);
				}
			}
		}
	}

	if (n > 0) {
		if (in.down & KEY_UP) {
			sModSel = sModSel > 0 ? sModSel - 1 : 0;
			audio::click(0.45f);
		}
		if (in.down & KEY_DOWN) {
			sModSel = sModSel + 1 < n ? sModSel + 1 : n - 1;
			audio::click(0.45f);
		}
		if (in.down & KEY_A) modToggle(sModSel);
		if ((in.down & KEY_L) && sModSel > 0) {
			audio::click(0.55f);
			mods::move(sModSel, -1);
			sModSel--;
		}
		if ((in.down & KEY_R) && sModSel + 1 < n) {
			audio::click(0.55f);
			mods::move(sModSel, +1);
			sModSel++;
		}
		const float topY = sModSel * rowH;
		const float botY = topY + rowH;
		if (topY < sModScroll) sModScroll = topY;
		if (botY > sModScroll + viewH) sModScroll = botY - viewH;
	}

	sModScroll = clampf(sModScroll, 0.0f, maxScroll);
	sModScrollDraw += (sModScroll - sModScrollDraw) * 0.28f;
}

static void drawMods() {
	drawBackArrow(false, false);
	gfx::text("MODS", 58, 11, 0.50f, COL_CREAM, ALIGN_LEFT);

	drawPlank(166, 10, 68, 22, false, false);
	gfx::text("REFRESH", 200, 15, 0.30f, COL_CREAM, ALIGN_CENTER);

	drawPlank(238, 10, 72, 22, false, false);
#if defined(__EMSCRIPTEN__)
	gfx::text("IMPORT", 274, 15, 0.30f, COL_ACCENT, ALIGN_CENTER);
#elif PORT_3DS || defined(__ANDROID__)
	gfx::text("SD PATH", 274, 15, 0.30f, COL_DIM, ALIGN_CENTER);
#else
	gfx::text("FOLDER", 274, 15, 0.30f, COL_ACCENT, ALIGN_CENTER);
#endif

	const int n = mods::count();
	gfx::setClip(10, 42, 300, 192);
	if (n == 0) {
		gfx::text("No mods found.", 160, 122, 0.42f, COL_CREAM, ALIGN_CENTER);
		gfx::text("modTemplate.zip is ignored.", 160, 144, 0.32f, COL_DIM, ALIGN_CENTER);
	} else {
		const float rowH = 50.0f;
		for (int i = 0; i < n; i++) {
			const float x = 14.0f;
			const float y = 44.0f + i * rowH - sModScrollDraw;
			const float w = 292.0f, h = 46.0f;
			if (y + h < 40.0f || y > 236.0f) continue;

			const mods::ModInfo* m = mods::get(i);
			if (!m) continue;
			const bool sel = (i == sModSel);
			drawPlank(x, y, w, h, sel, false);
			if (m->enabled) gfx::rect(x, y, 3, h, COL_ACCENT);

			gfx::drawModIcon(i, x + 8, y + 6, 34.0f);
			fitText(m->name, x + 48, y + 7, 0.35f, 126, sel ? COL_ACCENT : COL_CREAM, ALIGN_LEFT);
			char sub[64];
			snprintf(sub, sizeof sub, "#%d %s v%s", i + 1, m->isZip ? "ZIP" : "DIR", m->version);
			fitText(sub, x + 48, y + 24, PORT_3DS ? 0.30f : 0.27f, 126, COL_DIM, ALIGN_LEFT);

			drawPlank(x + 180, y + 10, 22, 26, false, false);
			gfx::text("UP", x + 191, y + 17, 0.25f, i > 0 ? COL_CREAM : COL_DIM, ALIGN_CENTER);
			drawPlank(x + 205, y + 10, 22, 26, false, false);
			gfx::text("DN", x + 216, y + 17, 0.25f, i + 1 < n ? COL_CREAM : COL_DIM, ALIGN_CENTER);

			drawPlank(x + 232, y + 10, 52, 26, m->enabled, false);
			gfx::text(m->enabled ? "ON" : "OFF", x + 258, y + 15, 0.34f, m->enabled ? COL_ACCENT : COL_DIM, ALIGN_CENTER);
		}
	}
	gfx::clearClip();
}
