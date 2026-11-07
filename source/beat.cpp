// Beat Slice: fruit is thrown to the music. Every throw is planned so the fruit reaches the top of its
// arc right on a beat, a big fruit on the first beat of each bar, and a slice on the beat scores extra.
//
// Timing: the game keeps its own song clock and eases it towards where the music really is. Every track
// loops on whole bars, so the two are compared within one bar: if the music started late, or the game
// was paused while it played on, the clock jumps to the right beat of the bar and carries on.
#include <math.h>
#include <stdio.h>
#include <time.h>

#include "audio.h"
#include "game.h"
#include "render.h"
#include "shop.h"
#include "sprites.h"

static constexpr int kMelon = 5, kPineapple = 9, kCoconut = 10;
static constexpr float kLead = 1.9f;      // game seconds ahead that a slot is planned: the longest flight to the top
static constexpr float kWindow = 0.11f;   // seconds either side of a slot that count as on the beat
static constexpr int kDefaultSong = 1;    // Picnic Stroll

void Game::beatStart() {
	beatSong = gSave.beatTrack ? gSave.beatTrack - 1 : kDefaultSong;
	if (beatSong >= kTrackCount || !trackUnlocked(beatSong)) beatSong = kDefaultSong;
	beatLen = audio::trackBeat(beatSong);
	// Slow tracks throw on every half beat as well, so the field never sits empty for long.
	beatStepLen = beatLen > 1.0f ? beatLen * 0.5f : beatLen;
	songT = 0;
	beatNext = -1;
	beatRunBar = -1;
	timeLeft = 60;
	audio::musicBeat(beatSong);
}

float Game::beatOffset() const {
	return songT - roundf(songT / beatStepLen) * beatStepLen;
}

void Game::beatUpdate() {
	// The clock: real time, eased towards the music while it plays.
	const float before = songT;
	songT += kDt;
	const float heard = audio::musicTime();
	if (heard >= 0) {
		const float bar = beatLen * 4;
		float err = heard - songT;
		err -= roundf(err / bar) * bar;
		songT += fabsf(err) < 0.25f ? err * 0.1f : err;
	}
	// Fruit flies at the Fruit Speed and Game Speed settings, on the song's clock rather than the frame's:
	// if frames run a little slow or fast, every arc still tops out on its beat. A jump (after a pause, or
	// when the music starts late) moves the clock but not the fruit.
	beatSpeed = (0.7f + slider(gSave.fruitSpeed) * 0.006f) * gameSpeedScale();
	beatDt = clampf(songT - before, 0, kDt * 3) * beatSpeed;
	if (phase != PHASE_PLAY) return;
	gravity = 330 * levelGravity(diffIndex());
	if (beatNext < 0) beatNext = (int)ceilf((songT + 1.2f) / beatStepLen);  // the first fruit, a moment after GO
	while ((beatNext * beatStepLen - songT) * beatSpeed <= kLead) {
		beatPlan(beatNext, (beatNext * beatStepLen - songT) * beatSpeed);
		beatNext++;
	}
#ifdef AUTOTEST
	// Test builds: how far from a slot each fruit reaches the top of its arc. A fruit is at the top in the
	// one frame where its rise has slowed to under a frame's worth of gravity.
	static int apexes;
	static float apexSum, apexMax;
	const float frameFall = gravity * beatDt;
	for (const auto& o : objs) {
		if (!o.on || o.kind > OBJ_GOLD || o.vy < -frameFall || o.vy >= 0 || o.hits > 1) continue;
		const float off = fabsf(beatOffset());
		apexes++;
		apexSum += off;
		if (off > apexMax) apexMax = off;
		if (apexes % 10 == 0) {
			fprintf(stderr, "[beat] %d tops, average %.0f ms from a slot, worst %.0f ms (music %s)\n", apexes, apexSum / apexes * 1000, apexMax * 1000, heard >= 0 ? "playing" : "silent");
			apexMax = 0;
		}
	}
#endif
}

// One slot: what it throws, each launched so that it peaks `lead` game seconds from now.
void Game::beatPlan(int k, float lead) {
	const int perBar = (int)roundf(4 * beatLen / beatStepLen);
	const int bar = k / perBar, pos = k % perBar;
	const float d = clampf(elapsed / 50.0f, 0, 1);
	const float density = perBar > 4 ? 0.8f : 1.0f;  // half-beat slots: fewer per slot

	struct Throw {
		ObjKind kind;
		int fruit;
	};
	Throw list[3];
	int n = 0;
#ifdef AUTOTEST
	if (pos == 0) {
		double wall = 0;
#if defined(__linux__)
		timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		wall = ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
		fprintf(stderr, "[beat] bar %d at song %.2f, played %.2f, wall %.2f, music %.2f, thrown %lu\n", bar, songT, elapsed, wall, audio::musicTime(), (unsigned long)statThrows);
	}
#endif
	if (pos == 0) {
		// The first beat of the bar: a big fruit. Some bars the whole bar plays a run, one fruit a slot.
		list[n++] = {OBJ_FRUIT, gSpawn.below(2) ? kMelon : kPineapple};
		if (gSpawn.unit() < 0.3f + d * 0.5f) list[n++] = {OBJ_FRUIT, everydayFruit(gSpawn)};
		beatRunBar = bar % 4 == 3 && d > 0.15f && gSpawn.unit() < 0.6f ? bar : -1;
		maybeCoin(d);
	} else if (beatRunBar == bar) {
		list[n++] = {OBJ_FRUIT, everydayFruit(gSpawn)};
	} else if (feverT > 0) {
		// Fruit Frenzy: a fruit on every slot, two on most, and never a bomb.
		list[n++] = {OBJ_FRUIT, everydayFruit(gSpawn)};
		if (gSpawn.unit() < 0.6f) list[n++] = {OBJ_FRUIT, everydayFruit(gSpawn)};
	} else {
		float bombChance = lerpf(0.05f, 0.14f, d) * levelBombs(diffIndex()) * (slider(gSave.bombs) / 50.0f);
		if (gSpawn.unit() < clampf(bombChance, 0, 0.5f) && !bombAround()) {
			list[n++] = {OBJ_BOMB, 0};
		} else if (gSpawn.unit() < lerpf(0.65f, 0.95f, d) * density) {
			list[n++] = {OBJ_FRUIT, gSpawn.unit() < 0.06f ? kCoconut : everydayFruit(gSpawn)};
			if (gSpawn.unit() < lerpf(0.15f, 0.5f, d)) list[n++] = {OBJ_FRUIT, everydayFruit(gSpawn)};
		}
		if (pos == perBar / 2) maybeCoin(d);
	}

	for (int i = 0; i < n; i++) {
		const bool bomb = list[i].kind == OBJ_BOMB;
		float x = 160, vx = 0, vy = -300;
		if (beatRunBar == bar && !bomb && pos > 0) {
			// The run sweeps across the field at one height, so it reads as a line of notes.
			const float apexY = kFieldTop + 50;
			vy = -sqrtf(2.0f * gravity * (kThrowY - apexY));
			x = 40 + (kFieldW - 80.0f) * (pos - 1) / (perBar > 2 ? perBar - 2 : 1);
		} else {
			pickThrow(bomb, lead - 1.2f > 0 ? lead - 1.2f : 0, x, vx, vy);
		}
		const float delay = lead + vy / gravity;  // the top of the arc comes -vy/gravity after launch
		if (delay < 0) continue;
		queue(delay, x, vx, vy, list[i].kind, list[i].fruit);
	}
}

int Game::beatSlice(const Obj& o) {
	if (fabsf(beatOffset()) > kWindow) {
		beatStreak = 0;
		return 0;
	}
	beatStreak++;
	beatHits++;
	const int bonus = 1 + (beatStreak / 4 > 3 ? 3 : beatStreak / 4);
	award(bonus);
	char buf[16];
	snprintf(buf, sizeof buf, "BEAT +%d", bonus);
	addPopup(buf, o.x, o.y - 18, hueColor(beatHits * 0.07f), 0.7f);
	return bonus;
}

// Four lights, one for each beat of the bar. The one playing now is lit and swells as it lands.
void Game::drawBeat(float x, float y, float s) const {
	if (beatLen <= 0) return;
	const float beats = songT / beatLen;
	const int now = ((int)floorf(beats) % 4 + 4) % 4;
	const float fresh = 1 - (beats - floorf(beats));
	for (int i = 0; i < 4; i++) {
		const bool lit = i == now;
		const float size = (lit ? 7 + 3 * fresh * fresh : 6) * s;
		const float cx = x + (i * 13 + 4) * s;
		u32 c = lit ? (i == 0 ? COL_ACCENT : COL_CREAM) : withAlpha(COL_CREAM, 0.3f);
		gfx::rect(cx - size * 0.5f, y - size * 0.5f, size, size, c);
	}
}
