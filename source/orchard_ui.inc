// Orchard Blast's menus during a run: the level-up cards, picking a slot for a charm, and the Inventory.
// Included by main.cpp, so it can use its sign and plank helpers. Everything here is drawn and tapped in
// field coordinates, like the blade.

static bool sOrcInv;          // the Inventory is open (it holds the run, like the pause menu)
static int sOrcInvFocus;      // the picked item (see orcInvItems)
static float sOrcOpenT;       // seconds the cards have been up: a short lock-out stops a swipe picking one
static bool sOrcSlotTouch;    // this touch began on a charm slot, so it never becomes a slice
static int sOrcCardDown = -1;
static int sOrcHowTo;         // a how-to panel is up: 0 none, else a HOWTO_ value
static float sOrcHowToT;
static void drawOrchardHowTo();

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

static void orcCardRect(int i, int n, float* x, float* y, float* w, float* h) {
	const float gap = 8, top = kFieldTop + 30;
	float cw = (kFieldW - 24 - gap * (n - 1)) / n;
	if (cw > 130) cw = 130;
	float ch = kFieldH - top - 42;
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
	gfx::sign(sprites_plank_idx, x, y, w, h, focused ? COL_ACCENT : COL_PLANK);
	gfx::rect(x + 4, y + 4, w - 8, 4, rc);
	gfx::sprite(SHEET_SPRITES, cardIcon(card), x + w * 0.5f, y + 20, 0.7f, 0.7f);
	char buf[48];
	snprintf(buf, sizeof buf, "%s %s", rarityName(info.rarity), cardKindName(info.kind));
	float ty = y + 33;
	orcFit(buf, x + w * 0.5f, ty, 0.22f, w - 10, rc);
	ty += 12;
	orcFit(info.name, x + w * 0.5f, ty, 0.36f, w - 10, COL_CREAM);
	ty += 20;
	orcFit(info.line1, x + w * 0.5f, ty, 0.25f, w - 10, COL_CREAM);
	ty += 12;
	if (info.line2[0]) orcFit(info.line2, x + w * 0.5f, ty, 0.25f, w - 10, COL_CREAM);
	ty += 15;
	if (info.kind == KIND_UPGRADE) {
		cardNextLine(card, o.upgrade[card], buf, sizeof buf);
		orcFit(buf, x + w * 0.5f, ty, 0.23f, w - 10, rgba(170, 230, 255));
		ty += 11;
		if (o.upgrade[card]) snprintf(buf, sizeof buf, "You have level %d of %d", o.upgrade[card], info.maxLevel);
		else snprintf(buf, sizeof buf, "Up to level %d", info.maxLevel);
		orcFit(buf, x + w * 0.5f, ty, 0.22f, w - 10, COL_DIM);
	} else if (info.kind == KIND_REUSABLE) {
		if (info.lasts > 0) snprintf(buf, sizeof buf, "Lasts %d s, ready again %d s", (int)info.lasts, (int)info.cooldown);
		else snprintf(buf, sizeof buf, "Ready again after %d s", (int)info.cooldown);
		orcFit(buf, x + w * 0.5f, ty, 0.22f, w - 10, rgba(170, 230, 255));
	} else if (info.kind == KIND_SINGLE) {
		orcFit("Goes in a slot. Used once.", x + w * 0.5f, ty, 0.22f, w - 10, rgba(170, 230, 255));
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
		gfx::sprite(SHEET_SPRITES, cardIcon(card), x + 20, y + h * 0.5f + 4, 0.7f, 0.7f);
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
		orcFit(buf, kFieldW * 0.5f, dy, 0.26f, kFieldW - 16, rarityColor(info.rarity));
		snprintf(buf, sizeof buf, "%s%s%s", info.line1, info.line2[0] ? " " : "", info.line2);
		char next[48] = "";
		if (info.kind == KIND_UPGRADE && o.upgrade[card] < info.maxLevel) cardNextLine(card, o.upgrade[card], next, sizeof next);
		if (next[0]) {
			const size_t len = strlen(buf);
			snprintf(buf + len, sizeof buf - len, "  Next: %s", next);
		}
		orcFit(buf, kFieldW * 0.5f, dy + 13, 0.22f, kFieldW - 16, COL_CREAM);
	} else if (it.kind == INV_SLOT) {
		orcFit("An empty slot. The next charm you take can go here.", kFieldW * 0.5f, dy + 6, 0.24f, kFieldW - 16, COL_DIM);
	} else if (it.kind == INV_BUTTON) {
		static const char* const kHelp[INVB_COUNT] = {"Back to the run, after a 3, 2, 1.", "Switch the charms in Slot 1 and Slot 2.",
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
	if (!newModeOn()) return;
	if (sOrcHowTo) {
		drawOrchardHowTo();
		return;
	}
	if (!sGame.orchardLike()) return;
	if (sGame.m12.rushPicking) {
		drawRushPick();
		return;
	}
	const Orchard& o = sGame.orc;
	char buf[64];
	if (o.choosing) {
		gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(20, 12, 8, 170));
		if (o.placing) {
			snprintf(buf, sizeof buf, "Pick a slot for %s", kCards[o.placingCard].name);
			orcFit(buf, kFieldW * 0.5f, kFieldTop + 30, 0.5f, kFieldW - 20, COL_CREAM);
			orcFit("It replaces the charm in that slot.", kFieldW * 0.5f, kFieldTop + 56, 0.3f, kFieldW - 20, COL_DIM);
			for (int s = 0; s < kSlots; s++) {
				float x, y, w, h;
				orcPlaceRect(s, &x, &y, &w, &h);
				drawOrcSlotBox(s, x, y, w, h, o.focus == s);
			}
			const Button k = orcKeepButton();
			drawSign(k, k.label, o.focus == 2, false, 0.5f);
			return;
		}
		snprintf(buf, sizeof buf, "LEVEL %d! PICK A CARD", o.level - o.levelsWaiting + 1);
		orcFit(buf, kFieldW * 0.5f, kFieldTop + 8, 0.6f, kFieldW - 20, rgba(255, 230, 140));
		for (int i = 0; i < o.offerCount; i++) {
			float x, y, w, h;
			orcCardRect(i, o.offerCount, &x, &y, &w, &h);
			drawOrcCard(o.offer[i], x, y, w, h, o.focus == i);
		}
		if (o.rerolls > 0) {
			const Button r = orcRerollButton();
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

// How-to panels, once each: Orchard Blast's first run and first boss, and each of the other new modes. They hold
// the run until tapped away. sOrcHowTo n is bit n - 1 of gSave.howToSeen.
enum { HOWTO_ORCHARD = 1, HOWTO_BOSS, HOWTO_RUSH, HOWTO_RECIPE, HOWTO_SHARP, HOWTO_JUGGLE };

static void drawOrchardHowTo() {
	if (!sOrcHowTo) return;
	const char* slots = PORT_3DS ? "L and R" : (gStickInUse ? "LB and RB" : "Q and E");
	const char* inv = PORT_3DS ? "Select" : (gStickInUse ? "Back" : "Tab");
	char a[64], b[64];
	const char* lines[6];
	const char* title;
	int n = 0;
	switch (sOrcHowTo) {
		case HOWTO_ORCHARD:
			title = "ORCHARD BLAST";
			lines[n++] = "Ten rooms. A boss waits in rooms 5 and 10.";
			lines[n++] = "Bombs and bosses cost HP. Dropped fruit never do.";
			lines[n++] = "Slice fruit for XP. Each level up, pick a card.";
			snprintf(a, sizeof a, "Charms sit in two slots: tap them, or use %s.", slots);
			lines[n++] = a;
			snprintf(b, sizeof b, "%s opens your Inventory.", inv);
			lines[n++] = b;
			break;
		case HOWTO_BOSS:
			title = "BOSS FIGHT";
			lines[n++] = "A blue ring means its guard is up.";
			lines[n++] = "Slicing it then costs you HP: Deflected!";
			lines[n++] = "A gold ring means the guard is down. Slice!";
			lines[n++] = "After a slice it's confused for a moment.";
			lines[n++] = "Slice its shots before they land.";
			break;
		case HOWTO_RUSH:
			title = "BOSS RUSH";
			lines[n++] = "Pick a build, then fight every boss back to back.";
			lines[n++] = "No level ups: your build is all you get.";
			lines[n++] = "Half your HP comes back after each boss.";
			lines[n++] = "Then they come back as Royals: tougher, quicker.";
			lines[n++] = "The faster you finish, the higher you score.";
			break;
		case HOWTO_RECIPE:
			title = "RECIPE";
			lines[n++] = "The recipe at the top shows what the cup needs.";
			lines[n++] = "Slice those fruit, in those amounts.";
			lines[n++] = "A wrong fruit, or one too many, spoils the cup.";
			lines[n++] = "Every full cup adds 8 seconds.";
			lines[n++] = "Bombs cost 10 points.";
			break;
		case HOWTO_SHARP:
			title = "SHARPSHOOTER";
			lines[n++] = "Fruit wear coloured rings, each with its own shape.";
			lines[n++] = "Only the called one scores, and it scores double.";
			lines[n++] = "A wrong one costs 3 points and your combo.";
			lines[n++] = "The call changes every few seconds.";
			lines[n++] = "Bombs cost 10 points.";
			break;
		default:
			title = "JUGGLE";
			lines[n++] = "Your blade knocks fruit back up, not in half.";
			lines[n++] = "Each knock scores the number of fruit in the air.";
			lines[n++] = "Swipe sideways to send a fruit sideways.";
			lines[n++] = "Three on the floor and the run ends.";
			lines[n++] = "Knocking a bomb counts as one of the three.";
			break;
	}
	gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(20, 12, 8, 190));
	const float w = kFieldW - 24 > 380 ? 380 : kFieldW - 24, x = (kFieldW - w) * 0.5f;
	const float h = 40 + n * 20 + 34, y = kFieldTop + (kFieldH - kFieldTop - h) * 0.5f;
	gfx::sign(sprites_plank_idx, x, y, w, h, COL_PLANK);
	orcFit(title, kFieldW * 0.5f, y + 8, 0.56f, w - 16, rgba(255, 230, 140));
	for (int i = 0; i < n; i++) orcFit(lines[i], kFieldW * 0.5f, y + 40 + i * 20, 0.3f, w - 16, COL_CREAM);
	if (sOrcHowToT > 0.6f) orcFit(PORT_3DS ? "Tap or press A" : "Tap, click or press A", kFieldW * 0.5f, y + h - 24, 0.28f, w - 16, COL_DIM);
}

// True while a how-to panel is up.
static bool orchardHowTo(const Input& in, bool wasTouching) {
	const Orchard& o = sGame.orc;
	if (!sOrcHowTo && !o.choosing && !sOrcInv) {
		const u8 seen = gSave.howToSeen;
		const bool bossUp = o.boss.on && !o.boss.beaten && o.boss.enterT <= 0;
		switch (sGame.mode) {
			case MODE_ORCHARD:
				if (!(seen & 1)) sOrcHowTo = HOWTO_ORCHARD;
				else if (bossUp && !(seen & 2)) sOrcHowTo = HOWTO_BOSS;
				break;
			case MODE_BOSSRUSH:
				if (!(seen & 4)) sOrcHowTo = HOWTO_RUSH;
				else if (bossUp && !(seen & 2)) sOrcHowTo = HOWTO_BOSS;
				break;
			case MODE_RECIPE: if (!(seen & 8)) sOrcHowTo = HOWTO_RECIPE; break;
			case MODE_SHARP: if (!(seen & 16)) sOrcHowTo = HOWTO_SHARP; break;
			case MODE_JUGGLE: if (!(seen & 32)) sOrcHowTo = HOWTO_JUGGLE; break;
			default: break;
		}
		if (sOrcHowTo) {
			sOrcHowToT = 0;
			sBlade.reset();
			audio::fuse(false);
		}
	}
	if (!sOrcHowTo) return false;
	sOrcHowToT += kDt;
	if (sOrcHowToT > 0.6f && ((in.touching && !wasTouching) || (in.down & (KEY_A | KEY_B | KEY_START | KEY_SELECT)))) {
		audio::click();
		gSave.howToSeen |= (u8)(1u << (sOrcHowTo - 1));
		sOrcHowTo = 0;
		if (sGame.orchardLike()) sGame.orcResume();
	}
	return true;
}

// Boss Rush's build signs, before the first boss.
static void rushBuildRect(int i, float* x, float* y, float* w, float* h) { orcCardRect(i, BUILD_COUNT, x, y, w, h); }

static void drawRushPick() {
	gfx::rect(kFillX, kFillX, kFillW, kFillW, rgba(20, 12, 8, 170));
	orcFit("PICK A BUILD", kFieldW * 0.5f, kFieldTop + 8, 0.6f, kFieldW - 20, rgba(255, 230, 140));
	static const int kIcon[BUILD_COUNT] = {CARD_ARMOR, CARD_GLASS, CARD_JUICE};
	for (int i = 0; i < BUILD_COUNT; i++) {
		float x, y, w, h;
		rushBuildRect(i, &x, &y, &w, &h);
		const RushBuildInfo& b = kRushBuilds[i];
		// The focused build gets a gold frame: the plank stays dark so its text reads.
		if (sGame.m12.rushFocus == i) gfx::rect(x - 3, y - 3, w + 6, h + 6, COL_ACCENT);
		gfx::sign(sprites_plank_idx, x, y, w, h, COL_PLANK);
		gfx::sprite(SHEET_SPRITES, cardIcon(kIcon[i]), x + w * 0.5f, y + 24, 0.8f, 0.8f);
		orcFit(b.name, x + w * 0.5f, y + 44, 0.4f, w - 10, COL_CREAM);
		orcFit(b.line1, x + w * 0.5f, y + 72, 0.25f, w - 10, COL_CREAM);
		orcFit(b.line2, x + w * 0.5f, y + 86, 0.25f, w - 10, COL_CREAM);
	}
	orcFit("No level ups in a Boss Rush.", kFieldW * 0.5f, kFieldH - 30, 0.28f, kFieldW - 20, COL_DIM);
}

static bool updateRushPick(const Input& in, bool wasTouching) {
	Modes12& m = sGame.m12;
	sOrcOpenT += kDt;
	if (sOrcOpenT < 0.45f) return true;
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
	if (!newModeOn() || sGame.phase != PHASE_PLAY) {
		sOrcInv = false;
		return false;
	}
	if (orchardHowTo(in, wasTouching)) return true;
	if (!sGame.orchardLike()) {
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
		if (sOrcOpenT < 0.45f) return true;
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
		} else if (!in.touching && wasTouching && sOrcCardDown >= 0) {
			pick = sOrcCardDown;
			sOrcCardDown = -1;
		}
#if PORT_MOUSE
		if (in.hovering)
			for (int i = 0; i < n; i++) {
				float x, y, w, h;
				orcCardRect(i, n, &x, &y, &w, &h);
				if (in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h) o.focus = i;
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
	// Select (Tab or I on a keyboard) opens and closes the Inventory.
	if (in.down & KEY_SELECT) {
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
