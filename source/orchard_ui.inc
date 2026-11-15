// Orchard Blast's menus during a run: the level-up cards, picking a slot for a charm, and the Inventory.
// Included by main.cpp, so it can use its sign and plank helpers. Everything here is drawn and tapped in
// field coordinates, like the blade.

static bool sOrcInv;          // the Inventory is open (it holds the run, like the pause menu)
static int sOrcInvFocus;      // the picked item (see orcInvItems)
static float sOrcOpenT;       // seconds the cards have been up: a short lock-out stops a swipe picking one
static bool sOrcSlotTouch;    // this touch began on a charm slot, so it never becomes a slice
static int sOrcCardDown = -1;

static u32 rarityColor(int r) {
	return r == RARE_LEGEND ? rgba(255, 205, 80) : (r == RARE_RARE ? rgba(120, 190, 255) : rgba(220, 210, 190));
}
static const char* rarityName(int r) { return r == RARE_LEGEND ? "Legendary" : (r == RARE_RARE ? "Rare" : "Common"); }

// Text that shrinks to fit a width.
static void orcFit(const char* s, float x, float y, float scale, float maxW, u32 color, Align align = ALIGN_CENTER) {
	const float w = gfx::textWidth(s, scale);
	if (w > maxW && w > 0) scale *= maxW / w;
	gfx::text(s, x, y, scale, color, align);
}

// Word-wraps text across multiple lines so cards keep large, readable font sizes.
static float orcWrap(const char* s, float cx, float y, float scale, float maxW, float step, u32 color, int maxLines = 4) {
	char buf[160];
	snprintf(buf, sizeof buf, "%s", s);
	char* p = buf;
	int line = 0;
	while (*p && line < maxLines) {
		while (*p == ' ') p++;
		if (!*p) break;
		const size_t len = strlen(p);
		if (gfx::textWidth(p, scale) <= maxW || line == maxLines - 1) {
			orcFit(p, cx, y, scale, maxW, color);
			y += step;
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
			orcFit(p, cx, y, scale, maxW, color);
			y += step;
			break;
		}
		p[split] = 0;
		orcFit(p, cx, y, scale, maxW, color);
		y += step;
		p += split + 1;
		line++;
	}
	return y;
}

static void orcCardRect(int i, int n, float* x, float* y, float* w, float* h) {
	const float gap = n >= 5 ? 5.0f : 8.0f, margin = n >= 5 ? 12.0f : 24.0f, top = kFieldTop + 35;
	float cw = (kFieldW - margin - gap * (n - 1)) / n;
	if (cw > 132) cw = 132;
	float ch = kFieldH - top - 38;
	if (ch > 170) ch = 170;
	const float total = cw * n + gap * (n - 1);
	*x = (kFieldW - total) * 0.5f + i * (cw + gap);
	*y = top;
	*w = cw;
	*h = ch;
}
static Button orcRerollButton() { return {kFieldW * 0.5f - 60, kFieldH - 36.0f, 120, 30, "Reroll"}; }
static Button orcKeepButton() { return {kFieldW * 0.5f - 70, kFieldH - 42.0f, 140, 32, "Keep Mine"}; }

static void drawOrcCard(int card, float x, float y, float w, float h, bool focused) {
	const Orchard& o = sGame.orc;
	const CardInfo& info = kCards[card];
	const u32 rc = rarityColor(info.rarity);
	drawPlank(x, y, w, h, focused, false);
	gfx::rect(x + 4, y + 4, w - 8, 4, rc);
	const float iconS = PORT_3DS ? 0.55f : 0.58f;
	gfx::sprite(SHEET_SPRITES, cardIcon(card), x + w * 0.5f, y + 19, iconS, iconS);
	char buf[128];
	const float cx = x + w * 0.5f, maxW = w - 8;
	snprintf(buf, sizeof buf, "%s %s", rarityName(info.rarity), cardKindName(info.kind));
	float ty = orcWrap(buf, cx, y + 31, 0.26f, maxW, 10.0f, rc, 2);
	ty += 2;
	orcFit(info.name, cx, ty, 0.38f, maxW, COL_CREAM);
	ty += 15;
	snprintf(buf, sizeof buf, "%s%s%s", info.line1, info.line2[0] ? " " : "", info.line2);

	// Dynamic text scaling: ensure description + footer never overflow card bottom.
	float descScale = 0.28f, descStep = 11.0f;
	float footScale = 0.26f, footStep = 10.0f;
	const float safeBottom = y + h - 8.0f;
	const float footerReserve = (info.kind == KIND_UPGRADE ? 22.0f : (info.kind == KIND_REUSABLE ? 18.0f : 12.0f));
	const float descBudget = safeBottom - footerReserve - ty;
	if (descBudget < 36.0f) {
		descScale = 0.24f;
		descStep = 9.5f;
	}

	ty = orcWrap(buf, cx, ty, descScale, maxW, descStep, COL_CREAM, 4);

	// Position footer neatly within the bottom bounds of the card plank.
	char foot[128] = "";
	char footSub[64] = "";
	if (info.kind == KIND_UPGRADE) {
		cardNextLine(card, o.upgrade[card], foot, sizeof foot);
		if (o.upgrade[card]) snprintf(footSub, sizeof footSub, "Lv %d / %d", o.upgrade[card], info.maxLevel);
		else snprintf(footSub, sizeof footSub, "Max Lv %d", info.maxLevel);
	} else if (info.kind == KIND_REUSABLE) {
		if (info.lasts > 0) snprintf(foot, sizeof foot, "%d s on, %d s cooldown", (int)info.lasts, (int)info.cooldown);
		else snprintf(foot, sizeof foot, "%d s cooldown", (int)info.cooldown);
	} else if (info.kind == KIND_SINGLE) {
		snprintf(foot, sizeof foot, "Slot charm (1 use)");
	}

	if (foot[0]) {
		// If footer text is wider than maxW, scale down dynamically so it fits on 1 line or wraps safely.
		float tw = gfx::textWidth(foot, footScale);
		if (tw > maxW) {
			float targetScale = footScale * (maxW / tw);
			if (targetScale >= 0.20f) {
				footScale = targetScale;
				tw = gfx::textWidth(foot, footScale);
			}
		}
		int linesNeeded = (tw > maxW) ? 2 : 1;
		if (linesNeeded > 1) {
			float halfW = tw * 0.55f;
			if (halfW > maxW) footScale = clampf(footScale * (maxW / halfW), 0.18f, footScale);
		}
		int totalLines = linesNeeded + (footSub[0] ? 1 : 0);
		float footH = totalLines * footStep;
		float footY = y + h - 10.0f - footH;
		if (footY < ty + 2.0f) {
			footScale = 0.20f;
			footStep = 8.5f;
			footH = totalLines * footStep;
			footY = y + h - 8.0f - footH;
		}
		float nextY = orcWrap(foot, cx, footY, footScale, maxW, footStep, rgba(170, 230, 255), 2);
		if (footSub[0]) {
			orcFit(footSub, cx, nextY, 0.24f, maxW, COL_DIM);
		}
	}
}

static void drawOrcSlotBox(int s, float x, float y, float w, float h, bool focused) {
	const int card = sGame.orc.slot[s];
	gfx::sign(sprites_plank_idx, x, y, w, h, focused ? COL_ACCENT : COL_PLANK);
	char buf[24];
	snprintf(buf, sizeof buf, "Slot %d", s + 1);
	gfx::text(buf, x + w * 0.5f, y + 6, 0.26f, COL_DIM, ALIGN_CENTER);
	if (card == CARD_NONE) {
		orcFit("Empty", x + w * 0.5f, y + h * 0.5f - 6, 0.36f, w - 10, COL_DIM);
	} else {
		const float is = PORT_3DS ? 0.55f : 0.58f;
		gfx::sprite(SHEET_SPRITES, cardIcon(card), x + 20, y + h * 0.5f + 4, is, is);
		orcFit(kCards[card].name, x + 36, y + h * 0.5f - 4, 0.3f, w - 42, COL_CREAM, ALIGN_LEFT);
	}
}

static void orcPlaceRect(int s, float* x, float* y, float* w, float* h) {
	*w = 130;
	*h = 70;
	*x = kFieldW * 0.5f + (s == 0 ? -*w - 8 : 8);
	*y = kFieldTop + 80;
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
	L.bottom = kFieldH - 64;
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

// Every item that can be picked, with where it sits. Returns the count.
static int orcInvItems(OrcInvItem* out) {
	const Orchard& o = sGame.orc;
	const OrcInvLayout L = orcInvLayout();
	int n = 0;
	const float bw = (L.cw - 6) * 0.5f;
	for (int s = 0; s < kSlots; s++) out[n++] = {INV_SLOT, (u8)s, L.cx + s * (bw + 6), L.cy + 14, bw, 40};
	const float icon = 20;
	const int perRow = (int)(L.cw / icon) > 0 ? (int)(L.cw / icon) : 1;
	for (int i = 0; i < o.passiveCount; i++)
		out[n++] = {INV_PASSIVE, (u8)i, L.cx + (i % perRow) * icon, L.cy + 72 + (i / perRow) * icon, icon, icon};
	int u = 0;
	for (int c = 0; c < CARD_COUNT; c++) {
		if (kCards[c].kind != KIND_UPGRADE) continue;
		out[n++] = {INV_UPGRADE, (u8)c, L.sx, L.sy + 28 + u * L.row, L.sw, L.row};
		u++;
	}
	const float gap = 6, w = clampf((kFieldW - 16 - gap * 3) / INVB_COUNT, 50, 96);
	const float x0 = (kFieldW - (w * INVB_COUNT + gap * (INVB_COUNT - 1))) * 0.5f;
	for (int b = 0; b < INVB_COUNT; b++) out[n++] = {INV_BUTTON, (u8)b, x0 + b * (w + gap), kFieldH - 32, w, 26};
	return n;
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
	if (PORT_3DS) return s == 0 ? "L" : "R";
	if (gStickInUse) return s == 0 ? "LB" : "RB";
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
	const int room = o.room > kOrchardRooms ? kOrchardRooms : o.room;
	const int mins = (int)sGame.elapsed / 60, secs = (int)sGame.elapsed % 60;
	if (!L.runCol) {
		snprintf(buf, sizeof buf, "Room %d of %d   Level %d   %d:%02d   %d sliced", room, kOrchardRooms, o.level, mins, secs, sGame.sliced);
		orcFit(buf, kFieldW * 0.5f, kFieldTop + 21, 0.26f, kFieldW - 16, COL_CREAM);
	}
	// Stats: HP, then each upgrade with its level as pips.
	gfx::text("Stats", L.sx, L.sy, 0.3f, head);
	snprintf(buf, sizeof buf, "HP %d of %d", (int)ceilf(o.hp), (int)o.maxHp);
	orcBar(L.sx, L.sy + 14, L.sw, 10, o.maxHp > 0 ? o.hp / o.maxHp : 0, rgba(230, 80, 80));
	orcFit(buf, L.sx + L.sw * 0.5f, L.sy + 14, 0.24f, L.sw - 4, COL_CREAM);
	// Run: where you are and how it is going.
	if (L.runCol) {
		float y = L.ry;
		gfx::text("Run", L.rx, y, 0.3f, head);
		y += 15;
		snprintf(buf, sizeof buf, "Room %d of %d", room, kOrchardRooms);
		orcFit(buf, L.rx, y, 0.26f, L.rw, COL_CREAM, ALIGN_LEFT);
		y += 12;
		snprintf(buf, sizeof buf, "Level %d", o.level);
		orcFit(buf, L.rx, y, 0.26f, L.rw, COL_CREAM, ALIGN_LEFT);
		y += 12;
		const int need = sGame.orcXpNeed();
		orcBar(L.rx, y + 1, L.rw, 6, need > 0 ? o.xp / need : 0, rgba(110, 190, 255));
		y += 9;
		snprintf(buf, sizeof buf, "%d XP to level %d", (int)ceilf(need - o.xp), o.level + 1);
		orcFit(buf, L.rx, y, 0.22f, L.rw, COL_DIM, ALIGN_LEFT);
		y += 14;
		snprintf(buf, sizeof buf, "Time %d:%02d", mins, secs);
		orcFit(buf, L.rx, y, 0.24f, L.rw, COL_CREAM, ALIGN_LEFT);
		y += 11;
		snprintf(buf, sizeof buf, "Fruit sliced %d", sGame.sliced);
		orcFit(buf, L.rx, y, 0.24f, L.rw, COL_CREAM, ALIGN_LEFT);
		y += 11;
		snprintf(buf, sizeof buf, "Bosses beaten %d", o.bossesBeaten);
		orcFit(buf, L.rx, y, 0.24f, L.rw, COL_CREAM, ALIGN_LEFT);
		y += 11;
		snprintf(buf, sizeof buf, "Charms used %d", o.charmsUsed);
		orcFit(buf, L.rx, y, 0.24f, L.rw, COL_CREAM, ALIGN_LEFT);
	}
	gfx::text("Charm slots", L.cx, L.cy, 0.3f, head);
	gfx::text(o.passiveCount ? "Passive charms" : "No passive charms yet", L.cx, L.cy + 58, 0.26f, o.passiveCount ? head : COL_DIM);
	const int focus = sOrcInvFocus >= 0 && sOrcInvFocus < n ? sOrcInvFocus : n - INVB_COUNT;
	for (int i = 0; i < n; i++) {
		const OrcInvItem& it = items[i];
		const bool f = i == focus && !sOrcConfirm;
		switch (it.kind) {
			case INV_SLOT: {
				const int card = o.slot[it.index];
				gfx::sign(sprites_plank_idx, it.x, it.y, it.w, it.h, f ? COL_ACCENT : COL_PLANK);
				snprintf(buf, sizeof buf, "%d  %s", it.index + 1, orcSlotKey(it.index));
				gfx::text(buf, it.x + 4, it.y + 3, 0.22f, COL_DIM);
				if (card == CARD_NONE) {
					orcFit("Empty slot", it.x + it.w * 0.5f, it.y + 18, 0.24f, it.w - 6, COL_DIM);
					break;
				}
				gfx::sprite(SHEET_SPRITES, cardIcon(card), it.x + 13, it.y + 25, 0.5f, 0.5f);
				orcFit(kCards[card].name, it.x + 25, it.y + 15, 0.22f, it.w - 28, COL_CREAM, ALIGN_LEFT);
				if (kCards[card].kind == KIND_SINGLE) {
					orcFit("Used once", it.x + 25, it.y + 26, 0.2f, it.w - 28, COL_DIM, ALIGN_LEFT);
				} else if (o.slotCool[it.index] > 0) {
					snprintf(buf, sizeof buf, "Ready in %ds", (int)ceilf(o.slotCool[it.index]));
					orcFit(buf, it.x + 25, it.y + 26, 0.2f, it.w - 28, rgba(170, 230, 255), ALIGN_LEFT);
				} else {
					orcFit("Ready", it.x + 25, it.y + 26, 0.2f, it.w - 28, rgba(170, 255, 170), ALIGN_LEFT);
				}
				break;
			}
			case INV_PASSIVE:
				if (f) gfx::rect(it.x, it.y, it.w, it.h, withAlpha(COL_ACCENT, 0.5f));
				gfx::sprite(SHEET_SPRITES, cardIcon(o.passiveOrder[it.index]), it.x + it.w * 0.5f, it.y + it.h * 0.5f, 0.42f, 0.42f);
				break;
			case INV_UPGRADE: {
				const int lv = o.upgrade[it.index];
				if (f) gfx::rect(it.x - 2, it.y, it.w + 4, it.h, withAlpha(COL_ACCENT, 0.45f));
				gfx::sprite(SHEET_SPRITES, cardIcon(it.index), it.x + 5, it.y + it.h * 0.5f, 0.26f, 0.26f);
				const int max = kCards[it.index].maxLevel;
				orcFit(kCards[it.index].name, it.x + 12, it.y + (it.h - 9) * 0.5f, 0.22f, it.w - 14 - max * 7, lv ? COL_CREAM : COL_DIM, ALIGN_LEFT);
				orcPips(it.x + it.w - max * 7, it.y + (it.h - 5) * 0.5f, lv, max, rgba(255, 220, 110));
				break;
			}
			default: {
				const Button b = {it.x, it.y, it.w, it.h, kInvButtons[it.index]};
				const bool off = (it.index == INVB_DROP && o.slot[sOrcInvSlot] == CARD_NONE) ||
				                 (it.index == INVB_SWAP && o.slot[0] == CARD_NONE && o.slot[1] == CARD_NONE);
				drawSign(b, b.label, f, false, 0.36f);
				if (off) gfx::rect(it.x, it.y, it.w, it.h, rgba(20, 12, 8, 120));
				break;
			}
		}
	}
	// The details strip: what the picked item is and does.
	const OrcInvItem& it = items[focus];
	int card = CARD_NONE;
	if (it.kind == INV_SLOT) card = o.slot[it.index];
	else if (it.kind == INV_PASSIVE) card = o.passiveOrder[it.index];
	else if (it.kind == INV_UPGRADE) card = it.index;
	const float dy = kFieldH - 60;
	if (card != CARD_NONE) {
		const CardInfo& info = kCards[card];
		if (info.kind == KIND_UPGRADE)
			snprintf(buf, sizeof buf, "%s: %s, level %d of %d", info.name, cardKindName(info.kind), o.upgrade[card], info.maxLevel);
		else
			snprintf(buf, sizeof buf, "%s: %s %s", info.name, rarityName(info.rarity), cardKindName(info.kind));
		orcFit(buf, kFieldW * 0.5f, dy, 0.28f, kFieldW - 16, rarityColor(info.rarity));
		snprintf(buf, sizeof buf, "%s%s%s", info.line1, info.line2[0] ? " " : "", info.line2);
		float nextY = orcWrap(buf, kFieldW * 0.5f, dy + 12, 0.26f, kFieldW - 20, 10.5f, COL_CREAM, 2);
		char next[48] = "";
		if (info.kind == KIND_UPGRADE && o.upgrade[card] < info.maxLevel) cardNextLine(card, o.upgrade[card], next, sizeof next);
		if (next[0]) {
			char nextBuf[64];
			snprintf(nextBuf, sizeof nextBuf, "Next: %s", next);
			orcWrap(nextBuf, kFieldW * 0.5f, nextY + 1.0f, 0.24f, kFieldW - 20, 10.0f, rgba(170, 230, 255), 2);
		}
	} else if (it.kind == INV_SLOT) {
		orcFit("An empty slot. The next charm you take can go here.", kFieldW * 0.5f, dy + 6, 0.24f, kFieldW - 16, COL_DIM);
	} else if (it.kind == INV_BUTTON) {
		static const char* const kHelp[INVB_COUNT] = {"Back to the run.", "Switch the charms in Slot 1 and Slot 2.",
		                                              "Empty the slot you last picked, to make room.", "End this run and keep its score."};
		orcFit(kHelp[it.index], kFieldW * 0.5f, dy + 6, 0.24f, kFieldW - 16, COL_DIM);
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
	const int n = orcInvItems(items);
	if (sOrcInvFocus < 0 || sOrcInvFocus >= n) sOrcInvFocus = n - INVB_COUNT;
	if (in.down & (KEY_LEFT | KEY_UP)) sOrcInvFocus = sOrcInvFocus == 0 ? n - 1 : sOrcInvFocus - 1;
	if (in.down & (KEY_RIGHT | KEY_DOWN)) sOrcInvFocus = (sOrcInvFocus + 1) % n;
	int act = -1;
	if (tap) {
		for (int i = 0; i < n; i++)
			if (in.tx >= items[i].x && in.tx < items[i].x + items[i].w && in.ty >= items[i].y && in.ty < items[i].y + items[i].h) {
				sOrcInvFocus = i;
				if (items[i].kind == INV_BUTTON) act = i;
			}
	}
#if PORT_MOUSE
	if (in.hovering)
		for (int i = 0; i < n; i++)
			if (in.tx >= items[i].x && in.tx < items[i].x + items[i].w && in.ty >= items[i].y && in.ty < items[i].y + items[i].h) sOrcInvFocus = i;
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

static void drawOrchardMenus() {
	if (!newModeOn() || !sGame.orchardLike()) return;
	if (sGame.m12.rushPicking) {
		drawRushPick();
		return;
	}
	const Orchard& o = sGame.orc;
	char buf[64];
	if (o.choosing) {
		const float openK = gSave.transitions && !gSave.calmFx ? clampf(sOrcOpenT / 0.24f, 0.0f, 1.0f) : 1.0f;
		const float ease = openK * openK * (3.0f - 2.0f * openK);
		gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(20, 12, 8, (u8)(170 * ease)));
		if (o.placing) {
			const float slide = (1.0f - ease) * 28.0f;
			snprintf(buf, sizeof buf, "Pick a slot for %s", kCards[o.placingCard].name);
			orcFit(buf, kFieldW * 0.5f, kFieldTop + 30 - slide * 0.5f, 0.5f, kFieldW - 20, COL_CREAM);
			orcFit("It replaces the charm in that slot.", kFieldW * 0.5f, kFieldTop + 56 - slide * 0.5f, 0.3f, kFieldW - 20, COL_DIM);
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
		snprintf(buf, sizeof buf, "LEVEL %d! PICK A CARD", o.level - o.levelsWaiting + 1);
		orcFit(buf, kFieldW * 0.5f, kFieldTop + 1 - (1.0f - ease) * 16.0f, 0.52f, kFieldW - 20, rgba(255, 230, 140));
		for (int i = 0; i < o.offerCount; i++) {
			float x, y, w, h;
			orcCardRect(i, o.offerCount, &x, &y, &w, &h);
			const float ct = gSave.transitions && !gSave.calmFx ? clampf((sOrcOpenT - i * 0.03f) / 0.22f, 0.0f, 1.0f) : 1.0f;
			const float ce = ct * ct * (3.0f - 2.0f * ct);
			drawOrcCard(o.offer[i], x, y + (1.0f - ce) * 34.0f, w, h, o.focus == i);
		}
		if (o.rerolls > 0) {
			Button r = orcRerollButton();
			r.y += (1.0f - ease) * 24.0f;
			snprintf(buf, sizeof buf, "Reroll (%d)", o.rerolls);
			drawSign(r, buf, o.focus == o.offerCount, false, 0.46f);
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
	orcFit("PICK A BUILD", kFieldW * 0.5f, kFieldTop + 4 - (1.0f - ease) * 16.0f, 0.52f, kFieldW - 20, rgba(255, 230, 140));
	static const int kIcon[BUILD_COUNT] = {CARD_ARMOR, CARD_GLASS, CARD_JUICE};
	char desc[128];
	for (int i = 0; i < BUILD_COUNT; i++) {
		float x, y, w, h;
		rushBuildRect(i, &x, &y, &w, &h);
		const float ct = gSave.transitions && !gSave.calmFx ? clampf((sOrcOpenT - i * 0.035f) / 0.22f, 0.0f, 1.0f) : 1.0f;
		const float ce = ct * ct * (3.0f - 2.0f * ct);
		y += (1.0f - ce) * 34.0f;
		const RushBuildInfo& b = kRushBuilds[i];
		drawPlank(x, y, w, h, sGame.m12.rushFocus == i, false);
		const float is = PORT_3DS ? 0.65f : 0.68f;
		gfx::sprite(SHEET_SPRITES, cardIcon(kIcon[i]), x + w * 0.5f, y + 24, is, is);
		orcFit(b.name, x + w * 0.5f, y + 44, 0.44f, w - 10, COL_CREAM);
		snprintf(desc, sizeof desc, "%s %s", b.line1, b.line2);
		orcWrap(desc, x + w * 0.5f, y + 70, 0.31f, w - 10, 13.0f, COL_CREAM, 5);
	}
	orcFit("No level ups in a Boss Rush.", kFieldW * 0.5f, kFieldH - 30, 0.3f, kFieldW - 20, COL_DIM);
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
		const int last = o.rerolls > 0 ? n : n - 1;  // the Reroll sign is one more stop for the keys
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
			if (o.rerolls > 0 && hit(orcRerollButton(), in.tx, in.ty)) reroll = true;
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
			if (o.rerolls > 0 && (hit(rBtn, in.tx, in.ty) || (in.tx >= rBtn.x - 4 && in.tx < rBtn.x + rBtn.w + 4 && in.ty >= rBtn.y - 4 && in.ty < rBtn.y + rBtn.h + 4))) o.focus = n;
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
	if (sGame.mode == MODE_ORCHARD && (in.down & KEY_SELECT)) {
		audio::click();
		if (sOrcInv) {
			sOrcInv = false;
			sGame.orcResume();
		} else {
			sOrcInv = true;
			sOrcInvFocus = -1;  // Resume
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
