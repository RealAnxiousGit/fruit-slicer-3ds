// Orchard Blast's menus during a run: the level-up cards, picking a slot for a charm, and the Inventory.
// Included by main.cpp, so it can use its sign and plank helpers. Everything here is drawn and tapped in
// field coordinates, like the blade.

static bool sOrcInv;          // the Inventory is open (it holds the run, like the pause menu)
static int sOrcInvFocus;      // the picked item (see orcInvItems)
static float sOrcOpenT;       // seconds the cards have been up: a short lock-out stops a swipe picking one
static bool sOrcSlotTouch;    // this touch began on a charm slot, so it never becomes a slice
static int sOrcCardDown = -1;
static float sOrcPassiveScroll;  // vertical scroll offset for the passive charm grid
static bool sOrcPassiveDrag;
static float sOrcPassiveDragY;
static bool sOrcPassiveDragMoved;

static u32 rarityColor(int r) {
	return r == RARE_LEGEND ? rgba(255, 205, 80) : (r == RARE_RARE ? rgba(120, 190, 255) : rgba(220, 210, 190));
}
static const char* rarityName(int r) { return r == RARE_LEGEND ? "Legendary" : (r == RARE_RARE ? "Rare" : "Common"); }
static bool cardUncapped(const CardInfo& info) { return info.kind == KIND_UPGRADE && info.maxLevel >= 255; }

// Text that shrinks to fit a width.
static void orcFit(const char* s, float x, float y, float scale, float maxW, u32 color, Align align = ALIGN_CENTER) {
	const float w = gfx::textWidth(s, scale);
	if (w > maxW && w > 0) scale *= maxW / w;
	gfx::text(s, x, y, scale, color, align);
}

// Word-wraps text across multiple lines so cards keep large, readable font sizes.
static float orcWrapRun(const char* s, float cx, float y, float scale, float maxW, float step, u32 color, int maxLines, bool draw, int* outLines) {
	char buf[256];
	snprintf(buf, sizeof buf, "%s", s);
	char* p = buf;
	int line = 0;
	while (*p && line < maxLines) {
		while (*p == ' ') p++;
		if (!*p) break;
		if (gfx::textWidth(p, scale) <= maxW || line == maxLines - 1) {
			if (draw) orcFit(p, cx, y, scale, maxW, color);
			y += step;
			line++;
			break;
		}
		int split = -1;
		for (int i = 1; p[i]; i++) {
			if (p[i] == ' ') {
				p[i] = 0;
				const float w = gfx::textWidth(p, scale);
				p[i] = ' ';
				if (w <= maxW) split = i;
				else break;
			}
		}
		if (split < 0) {
			for (int i = 1; p[i]; i++)
				if (p[i] == ' ') { split = i; break; }
		}
		if (split < 0) {
			if (draw) orcFit(p, cx, y, scale, maxW, color);
			y += step;
			line++;
			break;
		}
		p[split] = 0;
		if (draw) orcFit(p, cx, y, scale, maxW, color);
		y += step;
		p += split + 1;
		line++;
	}
	if (outLines) *outLines = line;
	return y;
}

static float orcWrap(const char* s, float cx, float y, float scale, float maxW, float step, u32 color, int maxLines = 4) {
	return orcWrapRun(s, cx, y, scale, maxW, step, color, maxLines, true, nullptr);
}

static int orcCountWrapLines(const char* s, float maxW, float scale, int maxLines = 8) {
	int lines = 0;
	orcWrapRun(s, 0, 0, scale, maxW, 1.0f, 0, maxLines, false, &lines);
	return lines;
}

struct OrcCardLayout {
	float headerY, cardTop, cardGap, cardW, cardH, rowGap, warnY;
	bool twoRows;
	Button reroll;
};

static OrcCardLayout orcCardLayout(int n, bool withReroll = true) {
	OrcCardLayout L = {};
	L.twoRows = !PORT_TWO_SCREENS && kFieldW < 310.0f && kFieldH >= 310.0f && n >= 4;
	const float topClear = PORT_TWO_SCREENS ? (float)kFieldTop + 2.0f
	                                        : (kFieldH >= 285.0f ? fmaxf((float)kFieldTop + 14.0f, 32.0f * uiScale() + 26.0f) : (float)kFieldTop + 7.0f);
	const float headerH = 21.0f;
	const float rerollReserve = withReroll ? 42.0f : 26.0f;
	const float availH = (kFieldH - 6.0f) - topClear - headerH - rerollReserve;
	L.rowGap = L.twoRows ? 8.0f : 0.0f;
	if (L.twoRows) {
		L.cardH = clampf((availH - L.rowGap) * 0.5f, 108.0f, 150.0f);
	} else {
		L.cardH = clampf(availH, 104.0f, (PORT_TWO_SCREENS && !withReroll) ? 160.0f : 170.0f);
	}
	const float cardsTotalH = L.twoRows ? (L.cardH * 2.0f + L.rowGap) : L.cardH;
	const float rerollGap = kFieldH >= 285.0f ? 10.0f : 6.0f;
	const float totalH = headerH + cardsTotalH + rerollGap + (withReroll ? 38.0f : 18.0f);
	const float blockTop = (kFieldH >= 285.0f && (kFieldH - 6.0f - topClear) > totalH)
	                           ? topClear + ((kFieldH - 6.0f - topClear) - totalH) * 0.5f
	                           : topClear;
	L.headerY = blockTop;
	L.cardTop = blockTop + headerH;
	const int perRow = L.twoRows ? 3 : (n > 0 ? n : 1);
	L.cardGap = kFieldW < 280.0f ? (perRow >= 4 ? 4.0f : 6.0f) : (perRow >= 5 ? 5.0f : 8.0f);
	const float margin = kFieldW < 280.0f ? 12.0f : (perRow >= 5 ? 12.0f : 24.0f);
	L.cardW = fminf(132.0f, (kFieldW - margin - L.cardGap * (perRow - 1)) / perRow);
	const float rerollY = fminf(kFieldH - 40.0f, L.cardTop + cardsTotalH + rerollGap);
	const float rw = fminf(120.0f, kFieldW - 24.0f);
	L.reroll = {kFieldW * 0.5f - rw * 0.5f, rerollY, rw, 25.0f, "Reroll"};
	L.warnY = L.reroll.y + L.reroll.h + 2.0f;
	return L;
}

static void orcCardRect(int i, int n, float* x, float* y, float* w, float* h) {
	const OrcCardLayout L = orcCardLayout(n);
	int row = 0, col = i, rowCount = n;
	if (L.twoRows) {
		if (i < 3) {
			row = 0;
			col = i;
			rowCount = 3;
		} else {
			row = 1;
			col = i - 3;
			rowCount = n - 3;
		}
	}
	const float total = L.cardW * rowCount + L.cardGap * (rowCount - 1);
	*x = (kFieldW - total) * 0.5f + col * (L.cardW + L.cardGap);
	*y = L.cardTop + row * (L.cardH + L.rowGap);
	*w = L.cardW;
	*h = L.cardH;
}
static Button orcRerollButton() { return orcCardLayout(sGame.orc.offerCount > 0 ? sGame.orc.offerCount : 3).reroll; }
static float orcPlaceTopY() {
	const float avail = kFieldH - kFieldTop - 12.0f;
	const float totalH = 166.0f;
	return kFieldTop + (avail > totalH ? (avail - totalH) * 0.5f : 10.0f);
}
static Button orcKeepButton() {
	const float y = kFieldH >= 285.0f ? fminf(kFieldH - 42.0f, orcPlaceTopY() + 132.0f) : kFieldH - 42.0f;
	return {kFieldW * 0.5f - 70.0f, y, 140.0f, 32.0f, "Keep Mine"};
}

static void drawOrcCard(int card, float x, float y, float w, float h, bool focused) {
	const Orchard& o = sGame.orc;
	const CardInfo& info = kCards[card];
	const u32 rc = rarityColor(info.rarity);
	drawPlank(x, y, w, h, focused, false);
	gfx::rect(x + 4, y + 4, w - 8, 4, rc);
	const float iconS = PORT_3DS ? 0.64f : 0.68f;
	gfx::sprite(SHEET_SPRITES, cardIcon(card), x + w * 0.5f, y + 20, iconS, iconS);
	char buf[128];
	const float cx = x + w * 0.5f, maxW = w - 8;
	snprintf(buf, sizeof buf, "%s %s", rarityName(info.rarity), cardKindName(info.kind));
	float ty = orcWrap(buf, cx, y + 34, PORT_3DS ? 0.32f : 0.30f, maxW, 10.5f, rc, 2);
	ty += 2;
	orcFit(info.name, cx, ty, PORT_3DS ? 0.41f : 0.39f, maxW, COL_CREAM);
	ty += 15;

	// Build and measure footer FIRST so description text can never collide with footer lines.
	char foot[128] = "";
	char footSub[64] = "";
	if (info.kind == KIND_UPGRADE) {
		cardNextLine(card, o.upgrade[card], foot, sizeof foot);
		if (cardUncapped(info)) {
			if (o.upgrade[card]) snprintf(footSub, sizeof footSub, "Lv %d (Uncapped)", o.upgrade[card]);
			else snprintf(footSub, sizeof footSub, "Uncapped");
		} else if (o.upgrade[card]) {
			snprintf(footSub, sizeof footSub, "Lv %d / %d", o.upgrade[card], info.maxLevel);
		} else {
			snprintf(footSub, sizeof footSub, "Max Lv %d", info.maxLevel);
		}
	} else if (info.kind == KIND_REUSABLE) {
		if (info.lasts > 0) snprintf(foot, sizeof foot, "%d s on, %d s cooldown", (int)info.lasts, (int)info.cooldown);
		else snprintf(foot, sizeof foot, "%d s cooldown", (int)info.cooldown);
	} else if (info.kind == KIND_SINGLE) {
		snprintf(foot, sizeof foot, "Slot charm (1 use)");
	}

	const float footMaxW = fmaxf(48.0f, w - 18.0f);  // clear corner rivets on the bottom edge of narrow cards
	float footScale = PORT_3DS ? 0.285f : 0.27f, footStep = 10.0f;
	float footY = y + h - 7.0f;
	if (foot[0]) {
		float tw = gfx::textWidth(foot, footScale);
		if (tw > footMaxW) {
			float targetScale = footScale * (footMaxW / tw);
			if (targetScale >= 0.21f) {
				footScale = targetScale;
				tw = gfx::textWidth(foot, footScale);
			}
		}
		int footLines = orcCountWrapLines(foot, footMaxW, footScale, 2);
		if (footLines > 1) {
			footScale = clampf(footScale * 0.90f, 0.20f, footScale);
			footStep = fmaxf(8.5f, footScale * 34.0f);
			footLines = orcCountWrapLines(foot, footMaxW, footScale, 2);
		}
		const float subH = footSub[0] ? 10.0f : 0.0f;
		const float footH = footLines * footStep + subH;
		footY = y + h - 8.0f - footH;
		float nextY = orcWrap(foot, cx, footY, footScale, footMaxW, footStep, rgba(170, 230, 255), 2);
		if (footSub[0]) orcFit(footSub, cx, nextY, 0.24f, footMaxW, COL_DIM);
	}

	// Auto-fit description strictly above footY with at least a 4px gap.
	snprintf(buf, sizeof buf, "%s%s%s", info.line1, info.line2[0] ? " " : "", info.line2);
	float descScale = PORT_3DS ? 0.33f : 0.31f, descStep = 11.2f;
	const float descBudget = fmaxf(14.0f, footY - 4.0f - ty);
	while (descScale > (PORT_3DS ? 0.22f : 0.205f)) {
		descStep = descScale * (PORT_3DS ? 35.0f : 36.0f);
		const int linesNeeded = orcCountWrapLines(buf, maxW, descScale, 6);
		if (linesNeeded * descStep <= descBudget) break;
		descScale -= 0.015f;
	}
	orcWrap(buf, cx, ty, descScale, maxW, descStep, COL_CREAM, 6);
}

static void drawOrcSlotBox(int s, float x, float y, float w, float h, bool focused) {
	const int card = sGame.orc.slot[s];
	gfx::sign(sprites_plank_idx, x, y, w, h, focused ? COL_ACCENT : COL_PLANK);
	char buf[24];
	snprintf(buf, sizeof buf, "Slot %d", s + 1);
	gfx::text(buf, x + w * 0.5f, y + 5, 0.31f, COL_DIM, ALIGN_CENTER);
	if (card == CARD_NONE) {
		orcFit("Empty", x + w * 0.5f, y + h * 0.5f - 6, 0.36f, w - 10, COL_DIM);
	} else {
		const float is = PORT_3DS ? 0.62f : 0.66f;
		gfx::sprite(SHEET_SPRITES, cardIcon(card), x + 21, y + h * 0.5f + 4, is, is);
		orcFit(kCards[card].name, x + 38, y + h * 0.5f - 4, 0.3f, w - 44, COL_CREAM, ALIGN_LEFT);
	}
}

static void orcPlaceRect(int s, float* x, float* y, float* w, float* h) {
	*w = fminf(130.0f, (kFieldW - 28.0f) * 0.5f);
	*h = 70.0f;
	const float gap = fminf(16.0f, kFieldW - (*w * 2.0f) - 12.0f);
	const float total = *w * 2.0f + gap;
	*x = (kFieldW - total) * 0.5f + s * (*w + gap);
	*y = kFieldH >= 285.0f ? (orcPlaceTopY() + 50.0f) : (kFieldTop + 80.0f);
}

// ---------------------------------------------------------------- the Inventory
// Three areas on a wide screen: Stats, Charms and Run. On a narrow one, Stats and Charms sit side by side (or
// stacked on a tall phone) and the Run numbers go under the title. Anything can be picked with a tap or the
// D-pad, and its details show in a strip above the buttons.
enum OrcInvKind { INV_SLOT, INV_PASSIVE, INV_UPGRADE, INV_BUTTON };
enum OrcInvButton { INVB_RESUME, INVB_SWAP, INVB_DROP, INVB_QUIT, INVB_COUNT };
struct OrcInvItem {
	u8 kind, index;
	float x, y, w, h;
};
static int sOrcConfirm;        // 0 none, 1 "Drop this charm?", 2 "End this run?"
static int sOrcConfirmYes;     // the focused answer: 1 yes, 0 no
static int sOrcInvSlot;        // the slot Drop acts on: the last one picked
static const char* const kInvButtons[INVB_COUNT] = {"Resume", "Swap Slots", "Drop", "Quit"};

struct OrcInvLayout {
	bool runCol, stacked;
	float top, bottom;            // the content area
	float sx, sy, sw;             // Stats
	float cx, cy, cw;             // Charms
	float rx, ry, rw;             // Run
	float row;                    // a Stats row
};

static OrcInvLayout orcInvLayout() {
	OrcInvLayout L = {};
	L.runCol = kFieldW >= 380;
	L.stacked = !L.runCol && kFieldH >= 380;
	L.top = kFieldTop + 26 + (L.runCol ? 0 : 12);
	L.bottom = (PORT_3DS || (!L.runCol && !L.stacked)) ? kFieldH - 72 : kFieldH - 64;
	const float pad = 8;
	if (L.runCol) {
		const float w = kFieldW - pad * 4;
		L.sw = w * 0.36f;
		L.cw = w * 0.36f;
		L.rw = w - L.sw - L.cw;
		L.sx = pad;
		L.cx = L.sx + L.sw + pad;
		L.rx = L.cx + L.cw + pad;
		L.sy = L.cy = L.ry = L.top;
	} else if (L.stacked) {
		L.sw = L.cw = kFieldW - pad * 2;
		L.sx = L.cx = pad;
		L.sy = L.top;
		L.cy = L.top + 14 + 14 + 9 * 11 + 8;
	} else {
		L.sw = L.cw = (kFieldW - pad * 3) * 0.5f;
		L.sx = pad;
		L.cx = pad * 2 + L.sw;
		L.sy = L.cy = L.top;
	}
	const float statsH = (L.stacked ? L.cy - 8 : L.bottom) - L.sy - 28;
	L.row = clampf(statsH / 9, 8.5f, 11.0f);
	return L;
}

static void orcPassiveArea(const OrcInvLayout& L, float* top, float* bottom, float* icon, int* perRow) {
	*top = L.cy + 75.0f;
	*bottom = L.bottom - 2.0f;
	*icon = 25.0f;
	const int cols = (int)((L.cw - 6.0f) / *icon);
	*perRow = cols > 0 ? cols : 1;
}

static float orcPassiveMaxScroll(const OrcInvLayout& L) {
	float top, bottom, icon;
	int perRow;
	orcPassiveArea(L, &top, &bottom, &icon, &perRow);
	const int rows = (sGame.orc.passiveCount + perRow - 1) / perRow;
	const float content = rows * icon;
	const float view = bottom - top;
	return content > view ? content - view : 0.0f;
}

// Every item that can be picked, with where it sits. Returns the count.
static int orcInvItems(OrcInvItem* out) {
	const Orchard& o = sGame.orc;
	const OrcInvLayout L = orcInvLayout();
	int n = 0;
	const float bw = (L.cw - 6) * 0.5f;
	for (int s = 0; s < kSlots; s++) out[n++] = {INV_SLOT, (u8)s, L.cx + s * (bw + 6), L.cy + 14, bw, 44};
	float pTop, pBottom, icon;
	int perRow;
	orcPassiveArea(L, &pTop, &pBottom, &icon, &perRow);
	sOrcPassiveScroll = clampf(sOrcPassiveScroll, 0.0f, orcPassiveMaxScroll(L));
	for (int i = 0; i < o.passiveCount; i++)
		out[n++] = {INV_PASSIVE, (u8)i, L.cx + (i % perRow) * icon, pTop + (i / perRow) * icon - sOrcPassiveScroll, icon, icon};
	int u = 0;
	for (int c = 0; c < CARD_COUNT; c++) {
		if (kCards[c].kind != KIND_UPGRADE) continue;
		out[n++] = {INV_UPGRADE, (u8)c, L.sx, L.sy + 28 + u * L.row, L.sw, L.row};
		u++;
	}
	const float gap = 6, w = clampf((kFieldW - 16 - gap * 3) / INVB_COUNT, 50, 96);
	const float x0 = (kFieldW - (w * INVB_COUNT + gap * (INVB_COUNT - 1))) * 0.5f;
	for (int b = 0; b < INVB_COUNT; b++) out[n++] = {INV_BUTTON, (u8)b, x0 + b * (w + gap), kFieldH - 28, w, 24};
	return n;
}

// Geometry for the Inventory bottom details strip, sized per port so header, wrapped description
// and the upgrade "Next:" line always stay clear of the bottom action buttons.
struct OrcInvDetails {
	float dy, descY, headScale, descScale, descStep, nextScale;
	bool compact;
};
static OrcInvDetails orcInvDetailsLayout() {
	const OrcInvLayout L = orcInvLayout();
	if (PORT_3DS || (!L.runCol && !L.stacked))
		return {kFieldH - 70.0f, kFieldH - 59.0f, PORT_3DS ? 0.27f : 0.25f, PORT_3DS ? 0.25f : 0.23f, 9.0f, PORT_3DS ? 0.24f : 0.22f, false};
	return {kFieldH - 63.0f, kFieldH - 54.0f, 0.25f, 0.22f, 8.0f, 0.21f, true};
}
static float orcInvDetailsBottom(int card) {
	const OrcInvDetails D = orcInvDetailsLayout();
	if (card == CARD_NONE) return D.dy + 8.0f + 32.0f * 0.24f * gfx::gTextBoost;
	const CardInfo& info = kCards[card];
	char buf[128];
	snprintf(buf, sizeof buf, "%s%s%s", info.line1, info.line2[0] ? " " : "", info.line2);
	const int lines = gfx::textWidth(buf, D.descScale) > (kFieldW - 20) ? 2 : 1;
	float endY = D.descY + (lines - 1) * D.descStep + 32.0f * D.descScale * gfx::gTextBoost;
	char next[48] = "";
	if (info.kind == KIND_UPGRADE && sGame.orc.upgrade[card] < info.maxLevel) cardNextLine(card, sGame.orc.upgrade[card], next, sizeof next);
	if (next[0]) {
		const float ny = D.descY + lines * D.descStep + 0.5f;
		endY = ny + 32.0f * D.nextScale * gfx::gTextBoost;
	}
	return endY;
}

static void orcPips(float x, float y, int have, int max, u32 on) {
	for (int i = 0; i < max; i++) {
		const float px = x + i * 7;
		if (i < have) {
			gfx::rect(px, y, 5, 5, on);
		} else {
			// Hollow, not just a darker shade, so it reads in High Contrast and for colour-blind players.
			gfx::rect(px, y, 5, 1, COL_DIM);
			gfx::rect(px, y + 4, 5, 1, COL_DIM);
			gfx::rect(px, y, 1, 5, COL_DIM);
			gfx::rect(px + 4, y, 1, 5, COL_DIM);
		}
	}
}

static void orcBar(float x, float y, float w, float h, float k, u32 color) {
	gfx::rect(x, y, w, h, rgba(40, 30, 20, 200));
	gfx::rect(x, y, w * clampf(k, 0, 1), h, color);
}

static const char* orcSlotKey(int s) {
	if (PORT_3DS || padUiActive() || gStickInUse) return s == 0 ? "L" : "R";
	if (touchUiActive()) return "";
	return s == 0 ? "Q" : "E";
}

static void drawOrcInventory() {
	const Orchard& o = sGame.orc;
	const OrcInvLayout L = orcInvLayout();
	OrcInvItem items[64];
	const int n = orcInvItems(items);
	char buf[96];
	gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(20, 12, 8, 200));
	gfx::text("INVENTORY", kFieldW * 0.5f, kFieldTop + 1, 0.46f, rgba(255, 230, 140), ALIGN_CENTER);
	const u32 head = rgba(255, 210, 150);
	const int room = o.room;
	const int mins = (int)sGame.elapsed / 60, secs = (int)sGame.elapsed % 60;
	if (!L.runCol) {
		snprintf(buf, sizeof buf, "Room %d   Level %d   %d:%02d   %d sliced", room, o.level, mins, secs, sGame.sliced);
		orcFit(buf, kFieldW * 0.5f, kFieldTop + 21, 0.30f, kFieldW - 16, COL_CREAM);
	}
	// Stats: HP, then each upgrade with its level as pips.
	gfx::text("Stats", L.sx, L.sy, 0.33f, head);
	snprintf(buf, sizeof buf, "HP %d of %d", (int)ceilf(o.hp), (int)o.maxHp);
	orcBar(L.sx, L.sy + 14, L.sw, 11, o.maxHp > 0 ? o.hp / o.maxHp : 0, rgba(230, 80, 80));
	orcFit(buf, L.sx + L.sw * 0.5f, L.sy + 14, 0.27f, L.sw - 4, COL_CREAM);
	// Run: where you are and how it is going.
	if (L.runCol) {
		float y = L.ry;
		gfx::text("Run", L.rx, y, 0.33f, head);
		y += 15;
		snprintf(buf, sizeof buf, "Room %d", room);
		orcFit(buf, L.rx, y, 0.29f, L.rw, COL_CREAM, ALIGN_LEFT);
		y += 12;
		snprintf(buf, sizeof buf, "Level %d", o.level);
		orcFit(buf, L.rx, y, 0.29f, L.rw, COL_CREAM, ALIGN_LEFT);
		y += 12;
		const int need = sGame.orcXpNeed();
		orcBar(L.rx, y + 1, L.rw, 6, need > 0 ? o.xp / need : 0, rgba(110, 190, 255));
		y += 9;
		snprintf(buf, sizeof buf, "%d XP to level %d", (int)ceilf(need - o.xp), o.level + 1);
		orcFit(buf, L.rx, y, 0.25f, L.rw, COL_DIM, ALIGN_LEFT);
		y += 14;
		snprintf(buf, sizeof buf, "Time %d:%02d", mins, secs);
		orcFit(buf, L.rx, y, 0.27f, L.rw, COL_CREAM, ALIGN_LEFT);
		y += 11;
		snprintf(buf, sizeof buf, "Fruit sliced %d", sGame.sliced);
		orcFit(buf, L.rx, y, 0.27f, L.rw, COL_CREAM, ALIGN_LEFT);
		y += 11;
		snprintf(buf, sizeof buf, "Bosses beaten %d", o.bossesBeaten);
		orcFit(buf, L.rx, y, 0.27f, L.rw, COL_CREAM, ALIGN_LEFT);
		y += 11;
		snprintf(buf, sizeof buf, "Charms used %d", o.charmsUsed);
		orcFit(buf, L.rx, y, 0.27f, L.rw, COL_CREAM, ALIGN_LEFT);
	}
	gfx::text("Charm slots", L.cx, L.cy, 0.33f, head);
	gfx::text(o.passiveCount ? "Passive charms" : "No passive charms yet", L.cx, L.cy + 61, 0.29f, o.passiveCount ? head : COL_DIM);
	const int focus = sOrcInvFocus >= 0 && sOrcInvFocus < n ? sOrcInvFocus : n - INVB_COUNT;
	float pTop, pBottom, pIcon;
	int pPerRow;
	orcPassiveArea(L, &pTop, &pBottom, &pIcon, &pPerRow);
	const float pMaxScroll = orcPassiveMaxScroll(L);
	bool clippingPassives = false;
	for (int i = 0; i < n; i++) {
		const OrcInvItem& it = items[i];
		const bool f = i == focus && !sOrcConfirm;
		if (it.kind == INV_PASSIVE && !clippingPassives) {
			gfx::setClip(L.cx - 2, pTop - 2, L.cw + 4, pBottom - pTop + 4);
			clippingPassives = true;
		} else if (it.kind != INV_PASSIVE && clippingPassives) {
			gfx::clearClip();
			clippingPassives = false;
		}
		switch (it.kind) {
			case INV_SLOT: {
				const int card = o.slot[it.index];
				gfx::sign(sprites_plank_idx, it.x, it.y, it.w, it.h, f ? COL_ACCENT : COL_PLANK);
				snprintf(buf, sizeof buf, "%d  %s", it.index + 1, orcSlotKey(it.index));
				gfx::text(buf, it.x + 4, it.y + 3, 0.26f, COL_DIM);
				if (card == CARD_NONE) {
					orcFit("Empty slot", it.x + it.w * 0.5f, it.y + 19, 0.28f, it.w - 6, COL_DIM);
					break;
				}
				gfx::sprite(SHEET_SPRITES, cardIcon(card), it.x + 15, it.y + 27, 0.62f, 0.62f);
				orcFit(kCards[card].name, it.x + 28, it.y + 15, 0.26f, it.w - 31, COL_CREAM, ALIGN_LEFT);
				if (kCards[card].kind == KIND_SINGLE) {
					orcFit("Used once", it.x + 28, it.y + 27, 0.24f, it.w - 31, COL_DIM, ALIGN_LEFT);
				} else if (o.slotCool[it.index] > 0) {
					snprintf(buf, sizeof buf, "Ready in %ds", (int)ceilf(o.slotCool[it.index]));
					orcFit(buf, it.x + 28, it.y + 27, 0.24f, it.w - 31, rgba(170, 230, 255), ALIGN_LEFT);
				} else {
					orcFit("Ready", it.x + 28, it.y + 27, 0.24f, it.w - 31, rgba(170, 255, 170), ALIGN_LEFT);
				}
				break;
			}
			case INV_PASSIVE:
				if (it.y + it.h < pTop - 2 || it.y > pBottom + 2) break;
				if (f) gfx::rect(it.x, it.y, it.w, it.h, withAlpha(COL_ACCENT, 0.5f));
				gfx::sprite(SHEET_SPRITES, cardIcon(o.passiveOrder[it.index]), it.x + it.w * 0.5f, it.y + it.h * 0.5f, 0.54f, 0.54f);
				break;
			case INV_UPGRADE: {
				const int lv = o.upgrade[it.index];
				const CardInfo& info = kCards[it.index];
				if (f) gfx::rect(it.x - 2, it.y, it.w + 4, it.h, withAlpha(COL_ACCENT, 0.45f));
				gfx::sprite(SHEET_SPRITES, cardIcon(it.index), it.x + 6, it.y + it.h * 0.5f, 0.34f, 0.34f);
				if (cardUncapped(info)) {
					char lvBuf[16];
					snprintf(lvBuf, sizeof lvBuf, "Lv %d", lv);
					const float badgeW = fmaxf(gfx::textWidth(lvBuf, 0.25f) + 4.0f, 24.0f);
					orcFit(info.name, it.x + 14, it.y + (it.h - 9) * 0.5f, 0.26f, it.w - 16 - badgeW, lv ? COL_CREAM : COL_DIM, ALIGN_LEFT);
					orcFit(lvBuf, it.x + it.w, it.y + (it.h - 8) * 0.5f, 0.25f, badgeW, lv ? rgba(255, 220, 110) : COL_DIM, ALIGN_RIGHT);
				} else {
					const int max = info.maxLevel;
					orcFit(info.name, it.x + 14, it.y + (it.h - 9) * 0.5f, 0.26f, it.w - 16 - max * 7, lv ? COL_CREAM : COL_DIM, ALIGN_LEFT);
					orcPips(it.x + it.w - max * 7, it.y + (it.h - 5) * 0.5f, lv, max, rgba(255, 220, 110));
				}
				break;
			}
			default: {
				const Button b = {it.x, it.y, it.w, it.h, kInvButtons[it.index]};
				const bool off = (it.index == INVB_DROP && o.slot[sOrcInvSlot] == CARD_NONE) ||
				                 (it.index == INVB_SWAP && o.slot[0] == CARD_NONE && o.slot[1] == CARD_NONE);
				drawSign(b, b.label, f, false, 0.34f);
				if (off) gfx::rect(it.x, it.y, it.w, it.h, rgba(20, 12, 8, 120));
				break;
			}
		}
	}
	if (clippingPassives) gfx::clearClip();
	if (pMaxScroll > 0.5f) {
		const float trackH = pBottom - pTop;
		const float totalH = trackH + pMaxScroll;
		const float thumbH = clampf(trackH * (trackH / totalH), 10.0f, trackH);
		const float thumbY = pTop + (trackH - thumbH) * (sOrcPassiveScroll / pMaxScroll);
		gfx::rect(L.cx + L.cw - 3, pTop, 3, trackH, rgba(255, 255, 255, 35));
		gfx::rect(L.cx + L.cw - 3, thumbY, 3, thumbH, withAlpha(COL_CREAM, 0.75f));
	}
	// The details strip: what the picked item is and does.
	const OrcInvItem& it = items[focus];
	int card = CARD_NONE;
	if (it.kind == INV_SLOT) card = o.slot[it.index];
	else if (it.kind == INV_PASSIVE) card = o.passiveOrder[it.index];
	else if (it.kind == INV_UPGRADE) card = it.index;
	const OrcInvDetails D = orcInvDetailsLayout();
	if (card != CARD_NONE) {
		const CardInfo& info = kCards[card];
		if (cardUncapped(info))
			snprintf(buf, sizeof buf, "%s: %s, Lv %d", info.name, cardKindName(info.kind), o.upgrade[card]);
		else if (info.kind == KIND_UPGRADE)
			snprintf(buf, sizeof buf, "%s: %s, level %d of %d", info.name, cardKindName(info.kind), o.upgrade[card], info.maxLevel);
		else
			snprintf(buf, sizeof buf, "%s: %s %s", info.name, rarityName(info.rarity), cardKindName(info.kind));
		orcFit(buf, kFieldW * 0.5f, D.dy, D.headScale, kFieldW - 16, rarityColor(info.rarity));
		snprintf(buf, sizeof buf, "%s%s%s", info.line1, info.line2[0] ? " " : "", info.line2);
		float nextY = orcWrap(buf, kFieldW * 0.5f, D.descY, D.descScale, kFieldW - 20, D.descStep, COL_CREAM, 2);
		char next[48] = "";
		if (info.kind == KIND_UPGRADE && o.upgrade[card] < info.maxLevel) cardNextLine(card, o.upgrade[card], next, sizeof next);
		if (next[0]) {
			char nextBuf[64];
			snprintf(nextBuf, sizeof nextBuf, "Next: %s", next);
			orcFit(nextBuf, kFieldW * 0.5f, nextY + 0.5f, D.nextScale, kFieldW - 20, rgba(170, 230, 255));
		}
	} else if (it.kind == INV_SLOT) {
		orcFit("An empty slot. The next charm you take can go here.", kFieldW * 0.5f, D.dy + 8, 0.24f, kFieldW - 16, COL_DIM);
	} else if (it.kind == INV_BUTTON) {
		static const char* const kHelp[INVB_COUNT] = {"Back to the run.", "Switch the charms in Slot 1 and Slot 2.",
		                                              "Empty the slot you last picked, to make room.", "End this run and keep its score."};
		orcFit(kHelp[it.index], kFieldW * 0.5f, D.dy + 8, 0.24f, kFieldW - 16, COL_DIM);
	}
	if (sOrcConfirm) {
		gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(10, 6, 4, 150));
		const float w = 220, h = 80, x = (kFieldW - w) * 0.5f, y = (kFieldH - h) * 0.5f;
		gfx::sign(sprites_plank_idx, x, y, w, h, COL_PLANK);
		orcFit(sOrcConfirm == 1 ? "Drop this charm?" : "End this run?", kFieldW * 0.5f, y + 10, 0.4f, w - 16, COL_CREAM);
		const Button yes = {x + 16, y + h - 36, 86, 26, "Yes"}, no = {x + w - 102, y + h - 36, 86, 26, "No"};
		drawSign(yes, yes.label, sOrcConfirmYes == 1, false, 0.4f);
		drawSign(no, no.label, sOrcConfirmYes == 0, false, 0.4f);
	}
}

static void orcInvClose() {
	audio::click();
	sOrcInv = false;
	sOrcConfirm = 0;
	sGame.orcResume();
}

// The Inventory's input. Quitting goes through the pause menu's own path, so the score is kept.
static void quitRun();
static bool updateOrcInventory(const Input& in, bool wasTouching) {
	Orchard& o = sGame.orc;
	const bool tap = in.touching && !wasTouching;
	if (sOrcConfirm) {
		const float w = 220, h = 80, x = (kFieldW - w) * 0.5f, y = (kFieldH - h) * 0.5f;
		const Button yes = {x + 16, y + h - 36, 86, 26, "Yes"}, no = {x + w - 102, y + h - 36, 86, 26, "No"};
		if (in.down & (KEY_LEFT | KEY_RIGHT)) sOrcConfirmYes ^= 1;
		int answer = -1;
		if (tap && hit(yes, in.tx, in.ty)) answer = 1;
		if (tap && hit(no, in.tx, in.ty)) answer = 0;
		if (in.down & KEY_A) answer = sOrcConfirmYes;
		if (in.down & KEY_B) answer = 0;
		if (answer < 0) return true;
		audio::click();
		const int what = sOrcConfirm;
		sOrcConfirm = 0;
		if (answer == 1 && what == 1) sGame.orcDrop(sOrcInvSlot);
		if (answer == 1 && what == 2) {
			sOrcInv = false;
			fadeThen(quitRun);
		}
		return true;
	}
	OrcInvItem items[64];
	int n = orcInvItems(items);
	if (sOrcInvFocus < 0 || sOrcInvFocus >= n) sOrcInvFocus = n - INVB_COUNT;
	const OrcInvLayout L = orcInvLayout();
	float pTop, pBottom, pIcon;
	int pPerRow;
	orcPassiveArea(L, &pTop, &pBottom, &pIcon, &pPerRow);
	const float pMaxScroll = orcPassiveMaxScroll(L);
	const bool overPassive = in.tx >= L.cx - 2 && in.tx <= L.cx + L.cw + 2 && in.ty >= pTop && in.ty <= pBottom;
	if (in.wheel != 0 && pMaxScroll > 0 && (overPassive || items[sOrcInvFocus].kind == INV_PASSIVE)) {
		sOrcPassiveScroll = clampf(sOrcPassiveScroll - in.wheel * pIcon, 0.0f, pMaxScroll);
		n = orcInvItems(items);
	}
	if (tap && overPassive && pMaxScroll > 0) {
		sOrcPassiveDrag = true;
		sOrcPassiveDragY = (float)in.ty;
		sOrcPassiveDragMoved = false;
	} else if (in.touching && sOrcPassiveDrag) {
		const float dy = (float)in.ty - sOrcPassiveDragY;
		if (fabsf(dy) > 2.0f) {
			sOrcPassiveScroll = clampf(sOrcPassiveScroll - dy, 0.0f, pMaxScroll);
			sOrcPassiveDragY = (float)in.ty;
			sOrcPassiveDragMoved = true;
			n = orcInvItems(items);
		}
	}
	const bool ignorePassiveTap = sOrcPassiveDragMoved && !in.touching && wasTouching;
	if (!in.touching) {
		sOrcPassiveDrag = false;
		sOrcPassiveDragMoved = false;
	}
	if (in.down & (KEY_LEFT | KEY_UP)) {
		sOrcInvFocus = sOrcInvFocus == 0 ? n - 1 : sOrcInvFocus - 1;
	}
	if (in.down & (KEY_RIGHT | KEY_DOWN)) {
		sOrcInvFocus = (sOrcInvFocus + 1) % n;
	}
	if ((in.down & (KEY_LEFT | KEY_UP | KEY_RIGHT | KEY_DOWN)) && items[sOrcInvFocus].kind == INV_PASSIVE && pMaxScroll > 0) {
		const float iy = items[sOrcInvFocus].y;
		if (iy < pTop) sOrcPassiveScroll = clampf(sOrcPassiveScroll - (pTop - iy), 0.0f, pMaxScroll);
		else if (iy + items[sOrcInvFocus].h > pBottom) sOrcPassiveScroll = clampf(sOrcPassiveScroll + (iy + items[sOrcInvFocus].h - pBottom), 0.0f, pMaxScroll);
		n = orcInvItems(items);
	}
	auto itemVisible = [&](const OrcInvItem& it) {
		if (it.kind != INV_PASSIVE) return true;
		return it.y + it.h * 0.5f >= pTop && it.y + it.h * 0.5f <= pBottom;
	};
	int act = -1;
	if (!in.touching && wasTouching && !ignorePassiveTap) {
		// Wait — tap acts on initial press for instant feel when not scrolling passives.
	}
	if (tap && !ignorePassiveTap) {
		for (int i = 0; i < n; i++)
			if (itemVisible(items[i]) && in.tx >= items[i].x && in.tx < items[i].x + items[i].w && in.ty >= items[i].y && in.ty < items[i].y + items[i].h) {
				sOrcInvFocus = i;
				if (items[i].kind == INV_BUTTON) act = i;
			}
	}
#if PORT_MOUSE
	if (in.hovering)
		for (int i = 0; i < n; i++)
			if (itemVisible(items[i]) && in.tx >= items[i].x && in.tx < items[i].x + items[i].w && in.ty >= items[i].y && in.ty < items[i].y + items[i].h) sOrcInvFocus = i;
#endif
	if (items[sOrcInvFocus].kind == INV_SLOT) sOrcInvSlot = items[sOrcInvFocus].index;
	if ((in.down & KEY_A) && items[sOrcInvFocus].kind == INV_BUTTON) act = sOrcInvFocus;
	if (in.down & KEY_B) {
		orcInvClose();
		return true;
	}
	if (act < 0) return true;
	switch (items[act].index) {
		case INVB_RESUME: orcInvClose(); break;
		case INVB_SWAP:
			audio::click();
			sGame.orcSwapSlots();
			break;
		case INVB_DROP:
			if (o.slot[sOrcInvSlot] == CARD_NONE) break;
			audio::click();
			sOrcConfirm = 1;
			sOrcConfirmYes = 0;
			break;
		default:
			audio::click();
			sOrcConfirm = 2;
			sOrcConfirmYes = 0;
			break;
	}
	return true;
}

static bool newModeOn() { return sGame.mode >= MODE_ORCHARD && sGame.mode <= MODE_JUGGLE; }
static void drawRushPick();
static void drawResumePrompt();
static void drawRoom50Cutscene();
#ifdef DEV_TOOLS
static void drawDevSkip();
#endif

static void drawOrchardMenus() {
	if (!newModeOn() || !sGame.orchardLike()) return;
	if (sGame.m12.rushPicking) {
		drawRushPick();
		return;
	}
	const Orchard& o = sGame.orc;
	if (o.resumePrompt) {
		drawResumePrompt();
		return;
	}
#ifdef DEV_TOOLS
	if (o.devPrompt) {
		drawDevSkip();
		return;
	}
#endif
	if (o.cutscene) {
		drawRoom50Cutscene();
		return;
	}
	char buf[64];
	if (o.choosing) {
		const float openK = gSave.transitions && !gSave.calmFx ? clampf(sOrcOpenT / 0.24f, 0.0f, 1.0f) : 1.0f;
		const float ease = openK * openK * (3.0f - 2.0f * openK);
		gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(20, 12, 8, (u8)(170 * ease)));
		if (o.placing) {
			const float slide = (1.0f - ease) * 28.0f;
			const float py = kFieldH >= 285.0f ? orcPlaceTopY() : (kFieldTop + 30.0f);
			snprintf(buf, sizeof buf, "Pick a slot for %s", kCards[o.placingCard].name);
			orcFit(buf, kFieldW * 0.5f, py - slide * 0.5f, 0.5f, kFieldW - 20, COL_CREAM);
			orcFit("It replaces the charm in that slot.", kFieldW * 0.5f, py + 26.0f - slide * 0.5f, 0.3f, kFieldW - 20, COL_DIM);
			for (int s = 0; s < kSlots; s++) {
				float x, y, w, h;
				orcPlaceRect(s, &x, &y, &w, &h);
				drawOrcSlotBox(s, x, y + slide, w, h, o.focus == s);
			}
			Button k = orcKeepButton();
			k.y += slide;
			drawSign(k, k.label, o.focus == 2, false, 0.5f);
			return;
		}
		const OrcCardLayout CL = orcCardLayout(o.offerCount);
		snprintf(buf, sizeof buf, "LEVEL %d! PICK A CARD", o.level - o.levelsWaiting + 1);
		orcFit(buf, kFieldW * 0.5f, CL.headerY - (1.0f - ease) * 6.0f, 0.48f, kFieldW - 24, rgba(255, 230, 140));
		for (int i = 0; i < o.offerCount; i++) {
			float x, y, w, h;
			orcCardRect(i, o.offerCount, &x, &y, &w, &h);
			const float ct = gSave.transitions && !gSave.calmFx ? clampf((sOrcOpenT - i * 0.03f) / 0.22f, 0.0f, 1.0f) : 1.0f;
			const float ce = ct * ct * (3.0f - 2.0f * ct);
			drawOrcCard(o.offer[i], x, y + (1.0f - ce) * 14.0f, w, h, o.focus == i);
		}
		if (o.rerolls > 0 || sGame.mode == MODE_ORCHARD) {
			Button r = CL.reroll;
			const float rSlide = (1.0f - ease) * 4.0f;
			r.y += rSlide;
			const int cost = sGame.orcRerollPrice();
			const bool canAfford = sGame.orcCanReroll();
			const char* rKey = touchUiActive() ? "" : ((PORT_3DS || padUiActive() || gStickInUse) ? " (X)" : " (R)");
			if (o.rerolls > 0) snprintf(buf, sizeof buf, "Reroll (%d)%s", o.rerolls, rKey);
			else snprintf(buf, sizeof buf, "Reroll (%dc)%s", cost, rKey);
			drawSign(r, buf, o.focus == o.offerCount, false, 0.40f);
			if (!canAfford) {
				orcFit("Need more run coins", r.x + r.w * 0.5f, r.y + r.h + 2.0f, 0.24f, kFieldW - 16.0f, rgba(255, 130, 120));
			}
		}
		return;
	}
	if (!sOrcInv) return;
	drawOrcInventory();
}

static bool tapped(const Input& in, bool wasTouching, float x, float y, float w, float h) {
	return in.touching && !wasTouching && in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h;
}

// Boss Rush's build signs, before the first boss.
static void rushBuildRect(int i, float* x, float* y, float* w, float* h) { orcCardRect(i, BUILD_COUNT, x, y, w, h); }

static void drawRushPick() {
	const float openK = gSave.transitions && !gSave.calmFx ? clampf(sOrcOpenT / 0.24f, 0.0f, 1.0f) : 1.0f;
	const float ease = openK * openK * (3.0f - 2.0f * openK);
	gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(20, 12, 8, (u8)(170 * ease)));
	const OrcCardLayout CL = orcCardLayout(BUILD_COUNT, false);
	orcFit("PICK A BUILD", kFieldW * 0.5f, CL.headerY - (1.0f - ease) * 10.0f, 0.50f, kFieldW - 20, rgba(255, 230, 140));
	static const int kIcon[BUILD_COUNT] = {CARD_ARMOR, CARD_GLASS, CARD_JUICE};
	char desc[128];
	for (int i = 0; i < BUILD_COUNT; i++) {
		float x, y, w, h;
		rushBuildRect(i, &x, &y, &w, &h);
		const float ct = gSave.transitions && !gSave.calmFx ? clampf((sOrcOpenT - i * 0.035f) / 0.22f, 0.0f, 1.0f) : 1.0f;
		const float ce = ct * ct * (3.0f - 2.0f * ct);
		y += (1.0f - ce) * 24.0f;
		const RushBuildInfo& b = kRushBuilds[i];
		drawPlank(x, y, w, h, sGame.m12.rushFocus == i, false);
		const float is = PORT_3DS ? 0.72f : 0.76f;
		gfx::sprite(SHEET_SPRITES, cardIcon(kIcon[i]), x + w * 0.5f, y + 22, is, is);
		orcFit(b.name, x + w * 0.5f, y + 41, PORT_3DS ? 0.45f : 0.44f, w - 8, COL_CREAM);
		snprintf(desc, sizeof desc, "%s %s", b.line1, b.line2);
		orcWrap(desc, x + w * 0.5f, y + 62, PORT_3DS ? 0.36f : 0.32f, w - 8, 13.0f, COL_CREAM, 6);
	}
	orcFit("No level ups in a Boss Rush.", kFieldW * 0.5f, CL.cardTop + CL.cardH + (PORT_TWO_SCREENS ? 20.0f : 12.0f), PORT_3DS ? 0.36f : 0.34f, kFieldW - 20, rgba(235, 225, 200));
}

static bool updateRushPick(const Input& in, bool wasTouching) {
	Modes12& m = sGame.m12;
	sOrcOpenT += kDt;
	if (sOrcOpenT < 0.28f) return true;
	if (in.down & KEY_LEFT) m.rushFocus = (u8)(m.rushFocus == 0 ? BUILD_COUNT - 1 : m.rushFocus - 1);
	if (in.down & KEY_RIGHT) m.rushFocus = (u8)((m.rushFocus + 1) % BUILD_COUNT);
	int pick = -1;
	for (int i = 0; i < BUILD_COUNT; i++) {
		float x, y, w, h;
		rushBuildRect(i, &x, &y, &w, &h);
		if (tapped(in, wasTouching, x, y, w, h)) pick = i;
#if PORT_MOUSE
		if (in.hovering && in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h) m.rushFocus = (u8)i;
#endif
	}
	if (in.down & KEY_A) pick = m.rushFocus;
	if (pick >= 0) {
		sGame.rushPick(pick);
		sOrcOpenT = 0;
	}
	return true;
}

static void resumeButtons(Button out[2], float* px, float* py, float* pw, float* ph) {
	const float w = fminf(kFieldW - 20.0f, 296.0f);
	char msg[160];
	snprintf(msg, sizeof msg, "Hey! Looks like you have a saved game up to room %u! Would you like to continue this run?", (unsigned)sGame.orc.savedRoom);
	const float msgScale = (PORT_3DS || w < 250.0f) ? 0.30f : 0.33f;
	const int lines = orcCountWrapLines(msg, w - 24.0f, msgScale, 8);
	const float h = clampf(96.0f + lines * 13.0f, 148.0f, kFieldH - kFieldTop - 18.0f);
	const float x = (kFieldW - w) * 0.5f, y = kFieldTop + (kFieldH - kFieldTop - h) * 0.5f;
	if (px) *px = x;
	if (py) *py = y;
	if (pw) *pw = w;
	if (ph) *ph = h;
	const float bw = (w - 30.0f) * 0.5f;
	out[0] = {x + 10.0f, y + h - 44.0f, bw, 32.0f, "Continue"};
	out[1] = {x + w - 10.0f - bw, y + h - 44.0f, bw, 32.0f, "Start Over"};
}

static void drawResumePrompt() {
	const Orchard& o = sGame.orc;
	const float openK = gSave.transitions && !gSave.calmFx ? clampf(sOrcOpenT / 0.24f, 0.0f, 1.0f) : 1.0f;
	const float ease = openK * openK * (3.0f - 2.0f * openK);
	gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(20, 12, 8, (u8)(180 * ease)));
	float x, y, w, h;
	Button btns[2];
	resumeButtons(btns, &x, &y, &w, &h);
	const float slide = (1.0f - ease) * 18.0f;
	drawPlank(x, y + slide, w, h, false, false);
	orcFit("ORCHARD BLAST", x + w * 0.5f, y + 14.0f + slide, 0.46f, w - 20.0f, COL_ACCENT);
	char msg[160];
	snprintf(msg, sizeof msg, "Hey! Looks like you have a saved game up to room %u! Would you like to continue this run?", (unsigned)o.savedRoom);
	const float msgScale = (PORT_3DS || w < 250.0f) ? 0.30f : 0.33f;
	orcWrap(msg, x + w * 0.5f, y + 40.0f + slide, msgScale, w - 24.0f, 13.0f, COL_CREAM, 8);
	for (int i = 0; i < 2; i++) {
		Button b = btns[i];
		b.y += slide;
		drawSign(b, b.label, o.resumeFocus == i, false, 0.42f);
	}
}

static bool updateResumePrompt(const Input& in, bool wasTouching) {
	Orchard& o = sGame.orc;
	sOrcOpenT += kDt;
	if (sOrcOpenT < 0.16f) return true;
	if (in.down & (KEY_LEFT | KEY_RIGHT)) o.resumeFocus ^= 1;
	Button btns[2];
	resumeButtons(btns, nullptr, nullptr, nullptr, nullptr);
	int pick = -1;
	for (int i = 0; i < 2; i++) {
		if (tapped(in, wasTouching, btns[i].x, btns[i].y, btns[i].w, btns[i].h)) pick = i;
#if PORT_MOUSE
		if (in.hovering && in.tx >= btns[i].x && in.tx < btns[i].x + btns[i].w && in.ty >= btns[i].y && in.ty < btns[i].y + btns[i].h) o.resumeFocus = (u8)i;
#endif
	}
	if (in.down & KEY_A) pick = o.resumeFocus;
	if (pick >= 0) {
		audio::click();
		sGame.orchardResumeChoice(pick == 0);
		sOrcOpenT = 0;
	}
	return true;
}

// ---------------------------------------------------------------- Room 25 first-encounter retro dialogue cutscene
static const char* const kRoom50Lines[10] = {
    "At last...",
    "You have made it to room 25.",
    "Tell me, are you proud of yourself?",
    "What exactly were you expecting when you went into this game? Another bootleg? Another clone? Just a simple game all about slicing fruit?",
    "Perhaps it's time you learned the truth...",
    "These aren't just regular fruits that you have been slicing.",
    "These fruits have lives! You have taken them away from me for far too long...",
    "You aren't just any ninja...you're a cold-blooded murderer. Do you feel good about yourself?",
    "Let's see how YOU like it when someone takes your life away from you! There's no going back now...",
    "Get ready, because you are about to be SLICED!",
};

struct Room50CutsceneBox {
	float x, y, w, h;
	float textY, textScale, step, maxW, promptY;
	int maxLines;
};

static Room50CutsceneBox room50CutsceneBox(const char* line = nullptr) {
	static char sLastText[256] = {};
	static float sLastFieldW = -1.0f;
	static u8 sLastLarge = 255;
	static Room50CutsceneBox sCachedBox;
	if (line && sLastFieldW == kFieldW && sLastLarge == gSave.largeText && strcmp(line, sLastText) == 0) {
		return sCachedBox;
	}
	Room50CutsceneBox B;
	const float padX = (PORT_3DS || kFieldW < 340.0f) ? 10.0f : 24.0f;
	B.w = fminf(kFieldW - padX * 2.0f, 440.0f);
	const bool narrow = B.w < 320.0f;
	B.textScale = PORT_3DS ? 0.34f : (narrow ? 0.31f : 0.34f);
	B.step = PORT_3DS ? 12.2f : (narrow ? 11.8f : 13.5f);
	B.maxW = B.w - 32.0f;
	B.maxLines = narrow ? 8 : 6;
	if (line) {
		while (B.textScale > (PORT_3DS ? 0.27f : 0.24f) && orcCountWrapLines(line, B.maxW, B.textScale, 8) > (narrow ? 7 : 5)) {
			B.textScale -= 0.02f;
			B.step -= 0.7f;
		}
	}
	const int linesNeeded = line ? orcCountWrapLines(line, B.maxW, B.textScale, B.maxLines) : 5;
	const float minH = PORT_3DS ? 50.0f : (narrow ? 50.0f : 54.0f);
	const float topPad = PORT_3DS ? 11.0f : 14.0f;
	B.h = fmaxf(minH, topPad + linesNeeded * B.step + 24.0f);
	B.x = (kFieldW - B.w) * 0.5f;
	B.y = kFieldH - B.h - (PORT_3DS ? 8.0f : 16.0f);
	B.textY = B.y + topPad;
	B.promptY = B.y + B.h - 17.0f;
	if (line) {
		snprintf(sLastText, sizeof sLastText, "%s", line);
		sLastFieldW = kFieldW;
		sLastLarge = gSave.largeText;
		sCachedBox = B;
	}
	return B;
}

struct DiagLine {
	int start, len;
	float width, scale;
};

static int orcLayoutLines(const char* full, float maxW, float scale, int maxLines, DiagLine* outLines) {
	int line = 0;
	int pos = 0;
	const int total = (int)strlen(full);
	char buf[256];
	while (pos < total && line < maxLines) {
		while (pos < total && full[pos] == ' ') pos++;
		if (pos >= total) break;
		const char* p = full + pos;
		const int rem = total - pos;
		const float fullW = gfx::textWidth(p, scale);
		int len = rem;
		if (fullW > maxW && line < maxLines - 1) {
			int split = -1;
			for (int i = 1; i < rem && i < (int)sizeof(buf) - 1; i++) {
				if (p[i] == ' ') {
					memcpy(buf, p, i);
					buf[i] = 0;
					if (gfx::textWidth(buf, scale) <= maxW) split = i;
					else break;
				}
			}
			if (split < 0) {
				for (int i = 1; i < rem; i++) {
					if (p[i] == ' ') { split = i; break; }
				}
			}
			if (split > 0) len = split;
		}
		if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
		memcpy(buf, p, len);
		buf[len] = 0;
		float ls = scale;
		float lw = gfx::textWidth(buf, ls);
		if (lw > maxW && lw > 0) {
			ls *= maxW / lw;
			lw = gfx::textWidth(buf, ls);
		}
		outLines[line++] = {pos, len, lw, ls};
		pos += len;
	}
	return line;
}

static constexpr float kDiagCharAnimDur = 0.14f;
static int sDiagAnimLine = -1;
static float sDiagAnimClock = 0.0f;
static float sDiagAnimPrevChars = -1.0f;
static float sDiagRevealAt[256];

static void resetDiagAnim(int line) {
	sDiagAnimLine = line;
	sDiagAnimClock = 0.0f;
	sDiagAnimPrevChars = 0.0f;
	for (int i = 0; i < 256; i++) sDiagRevealAt[i] = -1.0f;
}

static void room50CharAnim(float age, bool calm, float* outScale, float* outDx, float* outDy, float* outAlpha) {
	const float t = clampf(age / kDiagCharAnimDur, 0.0f, 1.0f);
	const float inv = 1.0f - t;
	const float easeOut = 1.0f - inv * inv * inv;
	if (calm) {
		*outScale = lerpf(0.80f, 1.0f, easeOut);
		*outDx = 0.0f;
		*outDy = inv * inv * 1.5f;
		*outAlpha = clampf(t * 2.0f, 0.0f, 1.0f);
	} else {
		const float u = t - 1.0f;
		const float c1 = 2.1f, c3 = c1 + 1.0f;
		const float back = 1.0f + c3 * u * u * u + c1 * u * u;
		*outScale = lerpf(0.32f, 1.0f, back);
		*outDx = (1.0f - easeOut) * 3.0f;
		*outDy = (1.0f - easeOut) * 3.5f;
		*outAlpha = clampf(t * 2.4f, 0.0f, 1.0f);
	}
}

static void drawRoom50Cutscene() {
	const Orchard& o = sGame.orc;
	const int idx = o.dialogueLine < 10 ? o.dialogueLine : 9;
	const char* full = kRoom50Lines[idx];
	const int total = (int)strlen(full);
	int shown = (int)o.dialogueChars;
	if (shown > total) shown = total;

	if (o.dialogueLine != sDiagAnimLine || o.dialogueChars < sDiagAnimPrevChars) {
		sDiagAnimLine = o.dialogueLine;
		sDiagAnimPrevChars = o.dialogueChars;
		for (int i = 0; i < 256; i++) sDiagRevealAt[i] = -1.0f;
		for (int i = 0; i < shown && i < 256; i++) {
			sDiagRevealAt[i] = (shown >= total) ? -10.0f : (sDiagAnimClock - (o.dialogueChars - (float)i) / 28.0f);
		}
	}

	const Room50CutsceneBox B = room50CutsceneBox(full);
	gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(8, 6, 12, 185));
	drawPlank(B.x, B.y, B.w, B.h, false, false);
	gfx::rect(B.x + 4, B.y + 4, B.w - 8, 2, rgba(255, 110, 95, 180));

	DiagLine lines[8];
	const int lineCount = orcLayoutLines(full, B.maxW, B.textScale, B.maxLines, lines);
	const bool calm = gSave.calmFx != 0;
	for (int pass = 0; pass < 2; pass++) {
		float y = B.textY;
		for (int li = 0; li < lineCount; li++) {
			const DiagLine& ln = lines[li];
			float penX = kFieldW * 0.5f - ln.width * 0.5f;
			for (int k = 0; k < ln.len; k++) {
				const int ci = ln.start + k;
				const char ch = full[ci];
				const float adv = gfx::glyphWidth(ch, ln.scale);
				if (ci < shown && ch != ' ') {
					const float revealT = (ci < 256 && sDiagRevealAt[ci] > -5.0f) ? sDiagRevealAt[ci] : (sDiagAnimClock - kDiagCharAnimDur);
					const float age = (ci < 256 && sDiagRevealAt[ci] < -5.0f) ? kDiagCharAnimDur : (sDiagAnimClock - revealT);
					float mulS = 1.0f, dx = 0.0f, dy = 0.0f, alpha = 1.0f;
					room50CharAnim(age, calm, &mulS, &dx, &dy, &alpha);
					const float s = ln.scale * mulS;
					const float gx = penX + adv * 0.5f + dx;
					const float gy = y + (ln.scale - s) * gfx::gTextBoost * 20.0f + dy;
					const u32 col = withAlpha(COL_CREAM, alpha);
					if (pass == 0 && gfx::gHighContrast) {
						const float off = 1.0f + s * gfx::gTextBoost * 1.2f;
						const u32 edge = C2D_Color32(0, 0, 0, (u8)(230.0f * alpha));
						gfx::glyph(ch, gx - off, gy, s, s, edge, false);
						gfx::glyph(ch, gx + off, gy, s, s, edge, false);
						gfx::glyph(ch, gx, gy - off, s, s, edge, false);
						gfx::glyph(ch, gx, gy + off, s, s, edge, false);
					}
					gfx::glyph(ch, gx, gy, s, s, col, pass == 1);
				}
				penX += adv;
			}
			y += B.step;
		}
	}
	if (shown >= total && !o.dialogueWaitRelease) {
		const float bob = gSave.calmFx ? 0.0f : sinf(sTime * 7.5f) * 1.4f;
		const float ax = B.x + B.w - 15.0f;
		const float ay = B.promptY + 2.5f + bob;
		const u32 sh = rgba(16, 10, 8, 220);
		gfx::tri(ax - 5.8f, ay - 0.8f, sh, ax + 5.8f, ay - 0.8f, sh, ax, ay + 6.4f, sh);
		gfx::tri(ax - 4.5f, ay, COL_ACCENT, ax + 4.5f, ay, COL_ACCENT, ax, ay + 5.0f, COL_ACCENT);
	}
}

static bool updateRoom50Cutscene(const Input& in, bool wasTouching) {
	Orchard& o = sGame.orc;
	if (!o.cutscene) return false;
	if (o.dialogueLine != sDiagAnimLine || o.dialogueChars < sDiagAnimPrevChars) resetDiagAnim(o.dialogueLine);
	sDiagAnimClock += kDt;
	const int idx = o.dialogueLine < 10 ? o.dialogueLine : 9;
	const char* full = kRoom50Lines[idx];
	const int total = (int)strlen(full);
	// KEY_START is reserved for Pause on main.cpp so Escape/Start doesn't advance and pause on the same frame.
	const bool proceedHeld = ((in.held & KEY_A) != 0) || in.touching;
	const bool justPressed = ((in.down & KEY_A) != 0) || (in.touching && !wasTouching);

	if ((int)o.dialogueChars < total) {
		if (o.dialoguePauseT > 0) {
			o.dialoguePauseT -= kDt * (proceedHeld ? 3.5f : 1.0f);
		} else {
			const float rate = proceedHeld ? 96.0f : 28.0f;
			o.dialogueChars += rate * kDt;
			int cur = (int)o.dialogueChars;
			if (cur > total) cur = total;
			static float sLastBlipT = -10.0f;
			if (sTime < sLastBlipT || (o.dialogueLine == 0 && o.dialogueChars < 0.5f)) sLastBlipT = -10.0f;
			while (o.dialogueLastChar + 1 < cur) {
				o.dialogueLastChar++;
				if (o.dialogueLastChar >= 0 && o.dialogueLastChar < 256) sDiagRevealAt[o.dialogueLastChar] = sDiagAnimClock;
				const char ch = full[o.dialogueLastChar];
				if (ch != ' ' && ch != '\n') {
					const float cooldown = proceedHeld ? 0.055f : 0.042f;
					if (sTime - sLastBlipT >= cooldown) {
						sLastBlipT = sTime;
						const float pitch = 0.94f + (float)(((unsigned)ch * 37u + (unsigned)o.dialogueLastChar * 19u) % 13u) * 0.01f;
						audio::play(SFX_BOSS_BLIP, 0.46f, 0.0f, pitch);
					}
				}
				if (ch == '.' || ch == '!' || ch == '?') {
					o.dialoguePauseT = 0.13f;
					o.dialogueChars = (float)(o.dialogueLastChar + 1);
					break;
				}
				if (ch == ',' || ch == ';' || ch == ':') {
					o.dialoguePauseT = 0.055f;
					o.dialogueChars = (float)(o.dialogueLastChar + 1);
					break;
				}
			}
		}
		if ((int)o.dialogueChars >= total && proceedHeld) o.dialogueWaitRelease = true;
		sDiagAnimPrevChars = o.dialogueChars;
		return true;
	}

	if (o.dialogueWaitRelease) {
		if (!proceedHeld) o.dialogueWaitRelease = false;
		sDiagAnimPrevChars = o.dialogueChars;
		return true;
	}

	if (justPressed) {
		if (o.dialogueLine + 1 >= 10) {
			sGame.orcStartBossAfterCutscene();
		} else {
			o.dialogueLine++;
			o.dialogueChars = 0;
			o.dialogueLastChar = -1;
			o.dialoguePauseT = 0;
			o.dialogueWaitRelease = false;
			resetDiagAnim(o.dialogueLine);
			if (o.dialogueLine == 5 && !o.tensionStarted) {
				o.tensionStarted = true;
				audio::tensionMusic(true);
			}
		}
	}
	sDiagAnimPrevChars = o.dialogueChars;
	return true;
}

#ifdef DEV_TOOLS
static int sDevTyping = 0;  // 0 none, 1 room, 2 level
static int sDevPrevValue = 1;
static int sDevTouchBtn = -1;
static int sDevHoldBtn = -1;
static HoldRepeat sDevHold;

static void devSkipLayout(float* px, float* py, float* pw, float* ph, Button* outBtns) {
	const float w = PORT_3DS ? 290.0f : clampf(kFieldW - 24.0f, 212.0f, 340.0f);
	const float h = 160.0f;
	const float x = (kFieldW - w) * 0.5f, y = kFieldTop + (kFieldH - kFieldTop - h) * 0.5f;
	if (px) *px = x;
	if (py) *py = y;
	if (pw) *pw = w;
	if (ph) *ph = h;
	const float pad = 12.0f, bw = w < 260.0f ? 32.0f : 38.0f, gap = 6.0f;
	const float midX = x + pad + bw + gap, midW = w - (pad + bw + gap) * 2.0f;
	const float r1y = y + 40.0f, r2y = y + 74.0f, r3y = y + 114.0f;
	outBtns[0] = {x + pad, r1y, bw, 26.0f, "-"};
	outBtns[1] = {midX, r1y, midW, 26.0f, nullptr};
	outBtns[2] = {x + w - pad - bw, r1y, bw, 26.0f, "+"};
	outBtns[3] = {x + pad, r2y, bw, 26.0f, "-"};
	outBtns[4] = {midX, r2y, midW, 26.0f, nullptr};
	outBtns[5] = {x + w - pad - bw, r2y, bw, 26.0f, "+"};
	const float sPad = w < 260.0f ? 28.0f : 40.0f;
	outBtns[6] = {x + sPad, r3y, w - sPad * 2.0f, 30.0f, "Start Run"};
}

static void devStepField(int btnIdx) {
	Orchard& o = sGame.orc;
	audio::click();
	if (btnIdx == 0 && o.devRoomTarget > 1) o.devRoomTarget--;
	else if (btnIdx == 2 && o.devRoomTarget < 999) o.devRoomTarget++;
	else if (btnIdx == 3 && o.devLevelTarget > 1) o.devLevelTarget--;
	else if (btnIdx == 5 && o.devLevelTarget < 255) o.devLevelTarget++;
}

static void drawDevSkip() {
	const Orchard& o = sGame.orc;
	float x, y, w, h;
	Button b[7];
	devSkipLayout(&x, &y, &w, &h, b);
	gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(16, 10, 8, 195));
	drawPlank(x, y, w, h, false, false);
	orcFit("DEV: SKIP TO WHICH ROOM?", x + w * 0.5f, y + 12.0f, 0.39f, w - 20.0f, COL_ACCENT);
	char buf[48], typed[16] = {};
	if (sDevTyping > 0) port::textPoll(typed, sizeof typed);

	drawSign(b[0], b[0].label, o.devField == 0, sDevHoldBtn == 0, 0.38f);
	if (sDevTyping == 1) {
		drawSign(b[1], "", true, false, 0.33f);
		snprintf(buf, sizeof buf, "ROOM: %s", typed);
		drawTypingWithCursor(buf, b[1].x + b[1].w * 0.5f, b[1].y + b[1].h * 0.5f - 20 * 0.33f - 1, 0.33f, b[1].w - 22.0f, COL_ACCENT, true);
	} else {
		snprintf(buf, sizeof buf, "ROOM: %d", o.devRoomTarget);
		drawSign(b[1], buf, o.devField == 1, sDevHoldBtn == 1, 0.33f);
	}
	drawSign(b[2], b[2].label, o.devField == 2, sDevHoldBtn == 2, 0.38f);

	drawSign(b[3], b[3].label, o.devField == 3, sDevHoldBtn == 3, 0.38f);
	if (sDevTyping == 2) {
		drawSign(b[4], "", true, false, 0.33f);
		snprintf(buf, sizeof buf, "LEVEL: %s", typed);
		drawTypingWithCursor(buf, b[4].x + b[4].w * 0.5f, b[4].y + b[4].h * 0.5f - 20 * 0.33f - 1, 0.33f, b[4].w - 22.0f, COL_ACCENT, true);
	} else {
		snprintf(buf, sizeof buf, "LEVEL: %d", o.devLevelTarget);
		drawSign(b[4], buf, o.devField == 4, sDevHoldBtn == 4, 0.33f);
	}
	drawSign(b[5], b[5].label, o.devField == 5, sDevHoldBtn == 5, 0.38f);

	drawSign(b[6], b[6].label, o.devField == 6, sDevHoldBtn == 6, 0.42f);
}

static bool updateDevSkip(const Input& in, bool wasTouching) {
	Orchard& o = sGame.orc;
	if (!o.devPrompt) return false;
	float x, y, w, h;
	Button b[7];
	devSkipLayout(&x, &y, &w, &h, b);
	auto finishTyping = [&](bool accept, const char* typedStr) {
		if (accept) {
			if (typedStr[0]) {
				const int v = atoi(typedStr);
				if (sDevTyping == 1) o.devRoomTarget = v < 1 ? 1 : (v > 999 ? 999 : v);
				else o.devLevelTarget = v < 1 ? 1 : (v > 255 ? 255 : v);
			} else {
				if (sDevTyping == 1) o.devRoomTarget = sDevPrevValue;
				else o.devLevelTarget = sDevPrevValue;
			}
		} else {
			if (sDevTyping == 1) o.devRoomTarget = sDevPrevValue;
			else o.devLevelTarget = sDevPrevValue;
		}
		port::textStop();
		sDevTyping = 0;
	};
	if (sDevTyping > 0) {
		char typed[16] = {};
		const int res = port::textPoll(typed, sizeof typed);
		if (res != 0) {
			finishTyping(res > 0, typed);
			return true;
		}
		if (in.touching && !wasTouching) {
			const int own = sDevTyping == 1 ? 1 : 4;
			if (!hit(b[own], in.tx, in.ty)) {
				finishTyping(true, typed);
			} else {
				return true;
			}
		} else {
			return true;
		}
	}
#if PORT_MOUSE
	if (in.hovering) {
		for (int i = 0; i < 7; i++)
			if (hit(b[i], in.tx, in.ty)) o.devField = (u8)i;
	}
#endif
	if (in.down & KEY_UP) {
		if (o.devField < 3) o.devField = 6;
		else if (o.devField < 6) o.devField -= 3;
		else o.devField = 4;
		audio::click(0.5f);
	}
	if (in.down & KEY_DOWN) {
		if (o.devField < 3) o.devField += 3;
		else if (o.devField < 6) o.devField = 6;
		else o.devField = 1;
		audio::click(0.5f);
	}
	if (in.down & KEY_LEFT) {
		if (o.devField < 3) o.devField = (u8)((o.devField + 2) % 3);
		else if (o.devField < 6) o.devField = (u8)(3 + (o.devField - 3 + 2) % 3);
		audio::click(0.5f);
	}
	if (in.down & KEY_RIGHT) {
		if (o.devField < 3) o.devField = (u8)((o.devField + 1) % 3);
		else if (o.devField < 6) o.devField = (u8)(3 + (o.devField - 3 + 1) % 3);
		audio::click(0.5f);
	}

	int activeStepBtn = -1;
	sDevHoldBtn = -1;
	if (in.touching) {
		if (!wasTouching) {
			sDevTouchBtn = -1;
			for (int i = 0; i < 7; i++) {
				if (hit(b[i], in.tx, in.ty)) {
					sDevTouchBtn = i;
					o.devField = (u8)i;
					break;
				}
			}
		}
		if (sDevTouchBtn >= 0 && hit(b[sDevTouchBtn], in.tx, in.ty)) {
			sDevHoldBtn = sDevTouchBtn;
			if (sDevTouchBtn == 0 || sDevTouchBtn == 2 || sDevTouchBtn == 3 || sDevTouchBtn == 5)
				activeStepBtn = sDevTouchBtn;
		}
	} else {
		sDevTouchBtn = -1;
		if ((in.held & KEY_A) && (o.devField == 0 || o.devField == 2 || o.devField == 3 || o.devField == 5)) {
			sDevHoldBtn = o.devField;
			activeStepBtn = o.devField;
		}
	}
	if (sDevHold.step(activeStepBtn, activeStepBtn >= 0)) devStepField(activeStepBtn);

	auto startTyping = [&](int which, int cur) {
		char init[12];
		sDevPrevValue = cur;
		snprintf(init, sizeof init, "%d", cur);
		sDevTyping = which;
		port::textStart(init, 3, true);
	};
	if (tapped(in, wasTouching, b[1].x, b[1].y, b[1].w, b[1].h)) { audio::click(); o.devField = 1; startTyping(1, o.devRoomTarget); return true; }
	if (tapped(in, wasTouching, b[4].x, b[4].y, b[4].w, b[4].h)) { audio::click(); o.devField = 4; startTyping(2, o.devLevelTarget); return true; }
	if (tapped(in, wasTouching, b[6].x, b[6].y, b[6].w, b[6].h) || ((in.down & KEY_A) && o.devField == 6) || (in.down & KEY_START)) {
		audio::click();
		sDevHoldBtn = -1;
		sDevTouchBtn = -1;
		sDevHold.reset();
		if (o.devRoomTarget >= 25 && o.devRoomTarget % 25 == 0) gSave.seenRoom50Cutscene = 0;
		sGame.orcApplyDevSkip(o.devRoomTarget, o.devLevelTarget);
		return true;
	}
	if ((in.down & KEY_A) && (o.devField == 1 || o.devField == 4)) {
		audio::click();
		startTyping(o.devField == 1 ? 1 : 2, o.devField == 1 ? o.devRoomTarget : o.devLevelTarget);
		return true;
	}
	return true;
}
#endif

// Runs the Orchard menus for the game screen. True when they took the frame, so the blade and game input wait.
static bool updateOrchardMenus(const Input& in, bool wasTouching) {
	if (!newModeOn() || sGame.phase != PHASE_PLAY || !sGame.orchardLike()) {
		sOrcInv = false;
		return false;
	}
	if (sGame.m12.rushPicking) {
		sOrcInv = false;
		return updateRushPick(in, wasTouching);
	}
	Orchard& o = sGame.orc;
	if (o.resumePrompt) {
		sOrcInv = false;
		return updateResumePrompt(in, wasTouching);
	}
#ifdef DEV_TOOLS
	if (o.devPrompt) {
		sOrcInv = false;
		return updateDevSkip(in, wasTouching);
	}
#endif
	if (o.cutscene) {
		sOrcInv = false;
		return updateRoom50Cutscene(in, wasTouching);
	}
	if (o.choosing) {
		sOrcInv = false;
		sOrcOpenT += kDt;
		// A swipe still held from play can't pick: a card needs a fresh press after the grace time.
		if (sOrcOpenT < 0.40f) {
			sOrcCardDown = -1;
			return true;
		}
		if (o.placing) {
			int pick = -2;
			for (int s = 0; s < kSlots; s++) {
				float x, y, w, h;
				orcPlaceRect(s, &x, &y, &w, &h);
				if (tapped(in, wasTouching, x, y, w, h)) pick = s;
			}
			const Button k = orcKeepButton();
			if (in.touching && !wasTouching && hit(k, in.tx, in.ty)) pick = -1;
			if (in.down & KEY_LEFT) o.focus = o.focus == 0 ? 2 : o.focus - 1;
			if (in.down & KEY_RIGHT) o.focus = (o.focus + 1) % 3;
			if (in.down & KEY_A) pick = o.focus == 2 ? -1 : o.focus;
			if (in.down & KEY_B) pick = -1;
			if (pick != -2) {
				sGame.orcPlace(pick);
				sOrcOpenT = 0.2f;
			}
			return true;
		}
		const int n = o.offerCount;
		const bool hasRerollBtn = o.rerolls > 0 || sGame.mode == MODE_ORCHARD;
		const int last = hasRerollBtn ? n : n - 1;  // the Reroll sign is one more stop for the keys
		if (in.down & KEY_LEFT) o.focus = o.focus <= 0 ? last : o.focus - 1;
		if (in.down & KEY_RIGHT) o.focus = o.focus >= last ? 0 : o.focus + 1;
		if (in.down & KEY_DOWN) o.focus = last;
		if (in.down & KEY_UP && o.focus >= n) o.focus = 0;
		int pick = -1;
		bool reroll = false;
		// A tap picks on release over the same card, so a press that slides off cancels.
		if (in.touching && !wasTouching) {
			sOrcCardDown = -1;
			for (int i = 0; i < n; i++) {
				float x, y, w, h;
				orcCardRect(i, n, &x, &y, &w, &h);
				if (in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h) sOrcCardDown = o.focus = i;
			}
			if (hasRerollBtn && hit(orcRerollButton(), in.tx, in.ty)) reroll = true;
		} else if (in.touching && sOrcCardDown >= 0) {
			float x, y, w, h;
			orcCardRect(sOrcCardDown, n, &x, &y, &w, &h);
			if (in.tx < x || in.tx >= x + w || in.ty < y || in.ty >= y + h) sOrcCardDown = -1;
		} else if (!in.touching && wasTouching && sOrcCardDown >= 0) {
			pick = sOrcCardDown;
			sOrcCardDown = -1;
		}
#if PORT_MOUSE
		if (in.hovering) {
			for (int i = 0; i < n; i++) {
				float x, y, w, h;
				orcCardRect(i, n, &x, &y, &w, &h);
				if (in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h) o.focus = i;
			}
			const Button rBtn = orcRerollButton();
			if (hasRerollBtn && (hit(rBtn, in.tx, in.ty) || (in.tx >= rBtn.x - 4 && in.tx < rBtn.x + rBtn.w + 4 && in.ty >= rBtn.y - 4 && in.ty < rBtn.y + rBtn.h + 4))) o.focus = n;
		}
#endif
		if (in.down & KEY_A) {
			if (o.focus >= n) reroll = true;
			else pick = o.focus;
		}
		if (in.down & (KEY_X | KEY_Y)) reroll = true;
		if (reroll && sGame.orcCanReroll()) {
			audio::click();
			sGame.orcReroll();
			sOrcOpenT = 0.25f;
		} else if (pick >= 0 && pick < n) {
			sGame.orcPick(pick);
			sOrcOpenT = 0.2f;
		}
		return true;
	}
	sOrcOpenT = 0;
	// Select (Tab or I on a keyboard) opens and closes the Inventory in Orchard Blast.
#if PORT_3DS && defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
	if (sGame.mode == MODE_ORCHARD && (in.down & KEY_Y)) {
#else
	if (sGame.mode == MODE_ORCHARD && (in.down & KEY_SELECT)) {
#endif
		audio::click();
		if (sOrcInv) {
			sOrcInv = false;
			sGame.orcResume();
		} else {
			sOrcInv = true;
			sOrcInvFocus = -1;  // Resume
			sOrcPassiveScroll = 0;
			sOrcPassiveDrag = false;
			sOrcPassiveDragMoved = false;
			sOrcConfirm = 0;
			sBlade.reset();
			audio::fuse(false);
		}
		return true;
	}
	if (sOrcInv) return updateOrcInventory(in, wasTouching);
	// Charm slots: L and R (Q and E on a keyboard), or a tap on the slot. A touch that starts on a slot
	// waits: let go quickly without moving and the charm fires; move away and it becomes a slice after all.
	static int slotDown = -1;
	static float slotT, slotX, slotY;
	if (sGame.orchardHeld()) {
		sOrcSlotTouch = false;
		return false;
	}
	if (in.down & KEY_L) sGame.orcUse(0);
	if (in.down & KEY_R) sGame.orcUse(1);
	if (in.touching && !wasTouching) {
		for (int s = 0; s < kSlots; s++) {
			float x, y, w, h;
			Game::orcSlotRect(s, &x, &y, &w, &h);
			if (in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h && o.slot[s] != CARD_NONE) {
				slotDown = s;
				slotT = 0;
				slotX = (float)in.tx;
				slotY = (float)in.ty;
				sOrcSlotTouch = true;
			}
		}
	}
	if (sOrcSlotTouch) {
		slotT += kDt;
		if (in.touching) {
			if (fabsf(in.tx - slotX) + fabsf(in.ty - slotY) > 14) sOrcSlotTouch = false;  // a swipe after all
		} else {
			if (slotT < 0.4f && slotDown >= 0) sGame.orcUse(slotDown);
			sOrcSlotTouch = false;
		}
		if (!sOrcSlotTouch) slotDown = -1;
	}
	return false;
}

#if defined(DEV_TOOLS) && defined(BOSS_TEST_APP)
static constexpr int kBossTestBtnCount = 15;
static const Button kBossTestBtns[kBossTestBtnCount] = {
	{10, 29, 146, 23, nullptr},       // 0: Mode (Orchard / Boss Rush)
	{164, 29, 146, 23, nullptr},      // 1: Tier (Regular / Royal)
	{10, 56, 126, 23, nullptr},       // 2: Difficulty (Easy..Insane)
	{142, 56, 168, 23, nullptr},      // 3: Build Preset
	{10, 85, 146, 26, "Big Melon"},   // 4: BOSS_MELON
	{164, 85, 146, 26, "Bomb King"},  // 5: BOSS_KING
	{10, 115, 146, 26, "Pineapple"},  // 6: BOSS_PINEAPPLE
	{164, 115, 146, 26, "Pepper"},    // 7: BOSS_CHILI
	{10, 156, 96, 46, "Durian"},      // 8: BOSS_DURIAN
	{112, 156, 96, 46, "Lemon"},      // 9: BOSS_LEMON
	{214, 156, 96, 46, "Apple"},      // 10: BOSS_APPLE
	{10, 208, 130, 26, "Fight"},      // 11: Fight selected
	{144, 208, 104, 26, "Dialogue"},  // 12: Test Dialogue cutscene
	{252, 208, 58, 26, "Quit"},       // 13: Quit
	{208, 5, 102, 20, nullptr},       // 14: Art 2D/3D [TAB / SELECT]
};

static const char* const kBossTestNames[BOSS_COUNT] = {
	"THE BIG MELON", "THE BOMB KING", "THE PINEAPPLE GUARD", "THE INFERNO PEPPER",
	"DUSTY DURIAN", "LORD LEMON", "AGGRO APPLE"
};
static const char* const kBossTestShort[BOSS_COUNT] = {
	"BIG MELON", "BOMB KING", "PINEAPPLE GUARD", "INFERNO PEPPER",
	"DUSTY DURIAN", "LORD LEMON", "AGGRO APPLE"
};
static const char* const kBossTestDiffs[DIFF_COUNT] = {"EASY", "NORMAL", "HARD", "INSANE"};
static const char* const kBossTestPresets[5] = {"STARTER", "TANK", "GLASS CANNON", "CHARMER", "FULL CHARMS"};
static const char* const kBossTestPresetDesc[5] = {
	"Starter: Base Lv1 HP, no extra charms",
	"Tank: Double HP, Armor Lv2, Smoothie slot",
	"Glass Cannon: Half HP, Blade Lv3, Cleaver + Boss Bane",
	"Charmer: 0.8x HP, Juice + Peel, Freeze Pop + Gold Apple",
	"Full Charms: +10 HP, Blade 3, CutBoard + Lifesteal + Harvest"
};

static bool sBossTestDialogue = false;

static void startBossTestFight() {
	sCardNoteT = 0;
	sResultBoard = false;
	sLbTypingName = false;
	sOrcInv = false;
	gBossTestDone = false;
	sGame.bossTestLaunch(sBossTestBoss, sBossTestUseRush, sBossTestRoyal, sBossTestDiff, sBossTestPreset, sBossTestDialogue);
	sBossTestDialogue = false;
	sBlade.reset();
	gfx::setShake(0, 0);
	sScreen = SCREEN_GAME;
	sPressed = -1;
}

static void startBossTestDialogue() {
	sBossTestDialogue = true;
	if (sBossTestBoss < kFirstMilestoneBoss) sBossTestBoss = BOSS_DURIAN;
	startBossTestFight();
}

static void drawBossTestTop() {
	char buf[96];
	topPanel(20, 12, 360, 216);
	const char* rawName = kBossTestNames[sBossTestBoss % BOSS_COUNT];
	if (sBossTestRoyal) {
		const char* base = strncmp(rawName, "THE ", 4) == 0 ? rawName + 4 : rawName;
		snprintf(buf, sizeof buf, "ROYAL %s", base);
	} else {
		snprintf(buf, sizeof buf, "%s", rawName);
	}
	gfx::setDepth(0.8f);
	orcFit(buf, 200, 19, 0.44f, 336, sBossTestRoyal ? rgba(255, 220, 110) : COL_ACCENT);
	gfx::setDepth(1.0f);
	sGame.bossPreview(sBossTestBoss, sBossTestRoyal, 200, 79, 27.0f, sTime, 400.0f);
	gfx::setDepth(0.8f);
	const int slices = kBosses[sBossTestBoss].slices + (sBossTestRoyal ? 1 : 0);
	snprintf(buf, sizeof buf, "SLICES: %d   |   %s", slices, sBossTestBoss >= kFirstMilestoneBoss ? "MILESTONE BOSS" : "REGULAR BOSS");
	orcFit(buf, 200, 113, 0.32f, 336, COL_CREAM);
	const int roomPreview = sBossTestBoss >= kFirstMilestoneBoss
	                            ? (sBossTestRoyal ? 50 : 25)
	                            : (sBossTestRoyal ? 30 + (sBossTestBoss + 1) * 5 : (sBossTestBoss + 1) * 5);
	if (sBossTestUseRush) {
		snprintf(buf, sizeof buf, "BOSS RUSH (%s)   |   %s", sBossTestRoyal ? "ROYAL" : "REGULAR", kBossTestDiffs[sBossTestDiff]);
	} else {
		snprintf(buf, sizeof buf, "ORCHARD ROOM %d (%s)   |   %s", roomPreview, sBossTestRoyal ? "ROYAL" : "REGULAR", kBossTestDiffs[sBossTestDiff]);
	}
	orcFit(buf, 200, 132, 0.30f, 336, rgba(170, 220, 255));
	orcFit(kBossTestPresetDesc[sBossTestPreset], 200, 151, 0.29f, 336, COL_DIM);
	orcFit(gSave.artStyle == ART_3D
	           ? (PORT_3DS ? "ART: 3D MODELS  (SELECT TO SWITCH)" : "ART: 3D MODELS  (PRESS TAB TO SWITCH)")
	           : (PORT_3DS ? "ART: 2D STICKERS  (SELECT TO SWITCH)" : "ART: 2D STICKERS  (PRESS TAB TO SWITCH)"),
	       200, 173, 0.31f, 336, rgba(160, 235, 190));
	orcFit("SAVES ISOLATED - BEATING BOSS CLOSES APP", 200, 196, 0.29f, 336, rgba(255, 220, 140));
}

static void drawBossTestSelector(const Button& b, const char* text, bool focused, bool pressed, float textScale = 0.28f) {
	drawPlank(b.x, b.y, b.w, b.h, focused, pressed);
	const float cy = b.y + b.h * 0.5f;
	gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, b.x + 11, cy, 0.36f, 0.36f, 0, focused ? WHITE : COL_ACCENT);
	gfx::sprite(SHEET_SPRITES, sprites_arrow_idx, b.x + b.w - 11, cy, 0.36f, 0.36f, kPi, focused ? WHITE : COL_ACCENT);
	orcFit(text, b.x + b.w * 0.5f, b.y + (b.h - 26.0f * textScale) * 0.5f - 1.0f, textScale, b.w - 28.0f, focused ? COL_ACCENT : COL_CREAM);
}

static void drawBossTestBottom() {
	char buf[48];
	drawMenuBack();
	const int f = sFocus[SCREEN_BOSS_TEST];
	gfx::text("BOSS TEST", 104, 5, 0.48f, COL_CREAM, ALIGN_CENTER);
	{
		const Button& ab = kBossTestBtns[14];
		drawPlank(ab.x, ab.y, ab.w, ab.h, f == 14, sPressed == 14);
		orcFit(gSave.artStyle == ART_3D ? (PORT_3DS ? "SEL: 3D" : "TAB: 3D") : (PORT_3DS ? "SEL: 2D" : "TAB: 2D"),
		       ab.x + ab.w * 0.5f, ab.y + 4.5f, 0.28f, ab.w - 10.0f, f == 14 ? COL_ACCENT : rgba(160, 235, 190));
	}
	drawBossTestSelector(kBossTestBtns[0], sBossTestUseRush ? "MODE: RUSH" : "MODE: ORCHARD", f == 0, sPressed == 0, 0.27f);
	drawBossTestSelector(kBossTestBtns[1], sBossTestRoyal ? "TIER: ROYAL" : "TIER: REGULAR", f == 1, sPressed == 1, 0.27f);
	snprintf(buf, sizeof buf, "DIFF: %s", kBossTestDiffs[sBossTestDiff]);
	drawBossTestSelector(kBossTestBtns[2], buf, f == 2, sPressed == 2, 0.26f);
	snprintf(buf, sizeof buf, "BUILD: %s", kBossTestPresets[sBossTestPreset]);
	drawBossTestSelector(kBossTestBtns[3], buf, f == 3, sPressed == 3, 0.25f);

	for (int bi = 0; bi < 4; bi++) {
		const Button& b = kBossTestBtns[4 + bi];
		const bool foc = f == 4 + bi;
		const bool sel = sBossTestBoss == bi;
		drawPlank(b.x, b.y, b.w, b.h, foc || sel, sPressed == 4 + bi);
		gfx::sprite(SHEET_SPRITES, bossSprite(bi), b.x + 18, b.y + b.h * 0.5f, 0.23f, 0.23f);
		orcFit(kBossTestShort[bi], b.x + 28 + (b.w - 34) * 0.5f, b.y + 7.5f, 0.26f, b.w - 38.0f, foc ? COL_ACCENT : (sel ? rgba(160, 235, 190) : COL_CREAM));
	}
	gfx::text("MILESTONE BOSSES (ROOM 25/50)", 160, 143, 0.27f, rgba(255, 160, 140), ALIGN_CENTER);
	for (int mi = 0; mi < 3; mi++) {
		const int bi = BOSS_DURIAN + mi;
		const Button& b = kBossTestBtns[8 + mi];
		const bool foc = f == 8 + mi;
		const bool sel = sBossTestBoss == bi;
		drawPlank(b.x, b.y, b.w, b.h, foc || sel, sPressed == 8 + mi);
		gfx::sprite(SHEET_SPRITES, bossSprite(bi), b.x + b.w * 0.5f, b.y + 15.0f, 0.30f, 0.30f);
		orcFit(kBossTestShort[bi], b.x + b.w * 0.5f, b.y + 30.0f, 0.24f, b.w - 14.0f, foc ? COL_ACCENT : (sel ? rgba(160, 235, 190) : rgba(255, 195, 175)));
	}
	{
		const Button& fb = kBossTestBtns[11];
		snprintf(buf, sizeof buf, "FIGHT %s", kBossTestShort[sBossTestBoss]);
		drawPlank(fb.x, fb.y, fb.w, fb.h, f == 11, sPressed == 11);
		orcFit(buf, fb.x + fb.w * 0.5f, fb.y + 7.0f, 0.27f, fb.w - 10.0f, f == 11 ? COL_ACCENT : COL_CREAM);
		const Button& db = kBossTestBtns[12];
		drawPlank(db.x, db.y, db.w, db.h, f == 12, sPressed == 12);
		orcFit("TEST DIALOGUE", db.x + db.w * 0.5f, db.y + 7.5f, 0.25f, db.w - 10.0f, f == 12 ? COL_ACCENT : rgba(175, 225, 255));
		const Button& qb = kBossTestBtns[13];
		drawPlank(qb.x, qb.y, qb.w, qb.h, f == 13, sPressed == 13);
		orcFit("QUIT", qb.x + qb.w * 0.5f, qb.y + 7.0f, 0.27f, qb.w - 10.0f, f == 13 ? COL_ACCENT : rgba(255, 160, 150));
	}
}

static void updateBossTest(const Input& in, bool wasTouching) {
	int& f = sFocus[SCREEN_BOSS_TEST];
	if (f < 0 || f >= kBossTestBtnCount) f = 4;
	const int prevF = f;
	int t = menuInput(kBossTestBtns, kBossTestBtnCount, SCREEN_BOSS_TEST, in, wasTouching, 0);
	if (f != prevF && f >= 4 && f <= 10) sBossTestBoss = f - 4;
	if (f >= 4 && f <= 10) sBossTestBoss = f - 4;
	if (in.down & KEY_L) {
		audio::click(0.6f);
		if (f == 0) sBossTestUseRush = !sBossTestUseRush;
		else if (f == 1) sBossTestRoyal = !sBossTestRoyal;
		else if (f == 2) sBossTestDiff = (sBossTestDiff + DIFF_COUNT - 1) % DIFF_COUNT;
		else if (f == 3) sBossTestPreset = (sBossTestPreset + 4) % 5;
	}
	if (in.down & KEY_R) {
		audio::click(0.6f);
		if (f == 0) sBossTestUseRush = !sBossTestUseRush;
		else if (f == 1) sBossTestRoyal = !sBossTestRoyal;
		else if (f == 2) sBossTestDiff = (sBossTestDiff + 1) % DIFF_COUNT;
		else if (f == 3) sBossTestPreset = (sBossTestPreset + 1) % 5;
	}
	if (t < 0) return;
	audio::click();
	if (t == 0) sBossTestUseRush = !sBossTestUseRush;
	else if (t == 1) sBossTestRoyal = !sBossTestRoyal;
	else if (t == 2) sBossTestDiff = (sBossTestDiff + 1) % DIFF_COUNT;
	else if (t == 3) sBossTestPreset = (sBossTestPreset + 1) % 5;
	else if (t >= 4 && t <= 10) {
		sBossTestBoss = t - 4;
		sBossTestDialogue = false;
		fadeThen(startBossTestFight);
	} else if (t == 11) {
		sBossTestDialogue = false;
		fadeThen(startBossTestFight);
	} else if (t == 12) {
		fadeThen(startBossTestDialogue);
	} else if (t == 13) {
		sLeaving = true;
	} else if (t == 14) {
		gSave.artStyle = gSave.artStyle == ART_3D ? ART_2D : ART_3D;
	}
}
#endif
