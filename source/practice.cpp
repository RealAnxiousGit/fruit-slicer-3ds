// The Practice run: short lessons, one at a time, offered the first time Play is chosen and again
// from How to Play. Each lesson throws its own fruit and waits; a lesson that goes wrong is thrown again.
// Lesson 7, slicing a menu fruit, happens on the result screen (main.cpp).
#include <string.h>

#include "audio.h"
#include "game.h"
#include "render.h"
#include "sprites.h"

static constexpr int kApple = 0, kOrange = 1, kLemon = 2, kPear = 3, kCoconut = 10;
static constexpr float kReadTime = 1.0f;  // a moment to read each lesson before its fruit arrives

void Game::practiceStart() {
	practiceStep = PS_SLICE;
	practiceT = 0;
	practiceGot = 0;
	practiceFail = false;
	practiceThrown = false;
	maxStrikes = 0;
	timeLeft = 0;
}

// Launch speed for a throw from below the field that peaks at height `apexY`.
static float launchFor(float gravity, float apexY) { return -sqrtf(2.0f * gravity * (Game::kThrowY - apexY)); }

// Where a lesson's throw peaks. `high` is how far below the top of the field it would peak on two screens,
// where the lesson sits above the field. On one screen the lesson plank hangs in the field, so throws
// peak just under it instead (see drawPractice), keeping the fruit in clear view.
static float lessonApex(float u, float high) {
#if PORT_TWO_SCREENS
	(void)u;
	return kFieldTop + high;
#else
	const float plankBottom = 30 * u + 28 + 64 * fminf(u, (kFieldW - 24) / 300.0f);
	return plankBottom + 26 + (high - 50) * 0.6f;
#endif
}

void Game::practiceUpdate() {
	practiceT += kDt;
	if (practiceStep >= PS_DONE) return;
	bool busy = false;
	for (const auto& o : objs) busy |= o.on;
	for (const auto& p : pending) busy |= p.on;
	const float mid = kFieldW * 0.5f;

	if (practiceStep == PS_FRENZY) {
		// Pairs of fruit until the meter fills; the lesson ends once the Frenzy has run its course.
		if (feverT > 0) practiceGot = 1;
		if (practiceGot) {
			if (feverT <= 0 && !busy) {
				practiceStep = PS_DONE;
				DLOG("[practice] all lessons done after %.1fs\n", elapsed);
				setCallout("ALL DONE!", rgba(140, 240, 150), 0.86f);
				audio::play(SFX_BEST);
				endGame();
			}
		} else if (practiceT > kReadTime) {
			practiceT = kReadTime - 1.1f;
			float vy = launchFor(gravity, lessonApex(uiScale(), 70));
			queue(0, mid - 90 + gRng.range(-20, 20), 20, vy, OBJ_FRUIT, gRng.below(kCoconut));
			queue(0.25f, mid + 90 + gRng.range(-20, 20), -20, vy, OBJ_FRUIT, gRng.below(kCoconut));
		}
		return;
	}

	if (busy) return;
	if (practiceThrown) {
		bool ok = !practiceFail;
		switch (practiceStep) {
			case PS_SLICE: ok = practiceGot >= 1; break;
			case PS_THREE: ok = practiceGot >= 3; break;
			case PS_COCONUT: ok = practiceGot >= 1; break;
			case PS_COIN: ok = practiceGot >= 1; break;
			default: break;
		}
		practiceThrown = false;
		practiceT = 0;
		practiceGot = 0;
		practiceFail = false;
		if (ok) {
			practiceStep++;
			DLOG("[practice] lesson %d done after %.1fs\n", practiceStep, elapsed);
			static const char* const kPraise[] = {"NICE!", "GREAT!", "WELL DONE!", "GOOD!", "NICE!"};
			setCallout(kPraise[(practiceStep - 1) % 5], rgba(140, 240, 150));
			audio::play(SFX_COMBO4);
		} else {
			DLOG("[practice] lesson %d again\n", practiceStep + 1);
			setCallout(practiceStep == PS_THREE ? "ONE SWIPE, ALL THREE!" : "TRY AGAIN!", rgba(255, 230, 140), 0.62f);
		}
		return;
	}
	if (practiceT < kReadTime) return;

	practiceThrown = true;
	switch (practiceStep) {
		case PS_SLICE:
			queue(0, mid, 0, launchFor(gravity, lessonApex(uiScale(), 60)), OBJ_FRUIT, kApple);
			break;
		case PS_THREE: {
			// Level, side by side: one flat stroke takes all three.
			const float vy = launchFor(gravity, lessonApex(uiScale(), 80));
			const int fruit[3] = {kOrange, kLemon, kPear};
			for (int i = 0; i < 3; i++) queue(0, mid + (i - 1) * 64, 0, vy, OBJ_FRUIT, fruit[i]);
			break;
		}
		case PS_COCONUT:
			queue(0, mid, 0, launchFor(gravity, lessonApex(uiScale(), 50)), OBJ_FRUIT, kCoconut);
			break;
		case PS_BOMB: {
			// A bomb in the middle and a fruit either side: slice around it.
			const float vy = launchFor(gravity, lessonApex(uiScale(), 70));
			queue(0, mid, 0, vy, OBJ_BOMB, 0);
			queue(0.15f, mid - 120, 10, launchFor(gravity, lessonApex(uiScale(), 90)), OBJ_FRUIT, kApple);
			queue(0.15f, mid + 120, -10, launchFor(gravity, lessonApex(uiScale(), 90)), OBJ_FRUIT, kOrange);
			break;
		}
		case PS_COIN:
			queue(0, mid, 0, launchFor(gravity, lessonApex(uiScale(), 60)), OBJ_COIN, 0);
			break;
		default:
			break;
	}
}

void Game::practiceSliced(const Obj& o) {
	switch (practiceStep) {
		case PS_SLICE: practiceGot++; break;
		case PS_COCONUT:
			if (o.kind == OBJ_FRUIT && o.fruit == kCoconut) practiceGot = 1;
			break;
		case PS_COIN:
			if (o.kind == OBJ_COIN) practiceGot = 1;
			break;
		default: break;
	}
}

void Game::practiceSwipe(int n) {
	if (practiceStep == PS_THREE && n > practiceGot) practiceGot = n;
}

void Game::practiceBomb() {
	practiceFail = true;
	setCallout("LEAVE BOMBS ALONE!", rgba(255, 110, 90), 0.62f);
}

const char* Game::practicePrompt(int line) const {
	// Two lines a lesson, worded for a stick when one is steering the blade.
	static const char* const kTouch[][2] = {
		{"Swipe through the fruit.", "A quick stroke cuts it in two."},
		{"Slice all three in one swipe.", "Bigger swipes score more."},
		{"Coconuts are tough.", "Slice it twice."},
		{"Leave the bomb alone!", "Slice the fruit around it."},
		{"Slice the coin.", "Coins buy things in the Shop."},
		{"Keep slicing to fill the meter.", "A full meter starts a Frenzy!"},
		{"All done!", ""},
	};
	static const char* const kStick[][2] = {
		{"Hold the slice button and", "sweep through the fruit."},
		{"Slice all three in one sweep.", "Bigger swipes score more."},
		{"Coconuts are tough.", "Slice it twice."},
		{"Leave the bomb alone!", "Slice the fruit around it."},
		{"Slice the coin.", "Coins buy things in the Shop."},
		{"Keep slicing to fill the meter.", "A full meter starts a Frenzy!"},
		{"All done!", ""},
	};
	int s = practiceStep < 0 ? 0 : (practiceStep > PS_DONE ? PS_DONE : practiceStep);
	return (gStickInUse ? kStick : kTouch)[s][line & 1];
}

// The lesson on a plank near the top of the field, see-through enough to keep fruit behind it in view.
// On one screen it sits below the score sign and its meter, and is drawn over them and everything
// else. It grows with UI Scale but never past the edges of the field.
void Game::drawPractice() const {
	if (phase != PHASE_PLAY && phase != PHASE_READY) return;
	const float s = fminf(uiScale(), (kFieldW - 24) / 300.0f);
	const float w = 300 * s, h = 64 * s;
#if PORT_TWO_SCREENS
	const float y = kFieldTop + 6;
#else
	const float y = 30 * uiScale() + 28;  // under the score sign, its meter and the power-up row
#endif
	const float x = kFieldW * 0.5f - w / 2;
	gfx::sign(sprites_plank_idx, x, y, w, h, withAlpha(COL_PLANK, 0.82f));
	char head[24];
	snprintf(head, sizeof head, "LESSON %d OF 7", (practiceStep > PS_DONE ? PS_DONE : practiceStep) + 1);
	gfx::text(head, kFieldW * 0.5f, y + 5 * s, 0.3f * s, COL_ACCENT, ALIGN_CENTER);
	for (int i = 0; i < 2; i++) {
		const char* line = practicePrompt(i);
		float ts = 0.4f * s;
		float tw = gfx::textWidth(line, ts);
		if (tw > w - 24) ts *= (w - 24) / tw;
		gfx::text(line, kFieldW * 0.5f, y + (19 + i * 18) * s, ts, COL_CREAM, ALIGN_CENTER);
	}
	gfx::text(PORT_PRACTICE_SKIP, kFieldW * 0.5f, y + h + 2, 0.28f * s, withAlpha(COL_CREAM, 0.7f), ALIGN_CENTER);
}
