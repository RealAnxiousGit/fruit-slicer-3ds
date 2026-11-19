// Beat Slice: fruit is thrown as in any game, and the music sets when a slice counts. Only a slice on the
// beat scores, more for each one in a row; a slice off the beat cuts the fruit for nothing and ends the run.
// The field's edges glow while a slice would count, brightest on the beat itself.
//
// Timing: the game keeps its own song clock and eases it towards where the music really is. Every track
// of the game's own loops on whole bars, so the two are compared within one bar: if the music started late,
// or the game was paused while it played on, the clock jumps to the right beat of the bar and carries on.
// A player's own song (Windows, Linux and Android) can start its first beat anywhere and end mid-bar, so
// its beats are laid out afresh on every play of the song, and the clock is compared within the whole song.
#include <math.h>
#include <stdio.h>

#include "audio.h"
#include "game.h"
#include "render.h"
#include "shop.h"
#include "sprites.h"

static constexpr float kWindow = 0.12f;  // seconds either side of a beat that count as on it
static constexpr int kDefaultSong = 1;   // Picnic Stroll

void Game::beatStart() {
	beatSong = gSave.beatTrack ? gSave.beatTrack - 1 : kDefaultSong;
	if ((beatSong < kTrackCount && !trackUnlocked(beatSong)) || beatSong >= audio::trackCount() || !audio::trackHasBeat(beatSong)) beatSong = kDefaultSong;
	beatLen = audio::trackBeat(beatSong);
	beatFirst = audio::trackFirst(beatSong);
	beatLoop = 0;
	beatPerLoop = 0;
	// Slow tracks count every half beat as well, so a slice never waits long.
	beatStepLen = beatLen > 1.0f ? beatLen * 0.5f : beatLen;
	songT = 0;
	timeLeft = 60;
	beatLightFade = 0;
	audio::musicBeat(beatSong);
}

// The song clock as this player's slices meet it: Beat Timing (from the Latency Test) moves every beat
// later for someone whose sound or hands run late, so the glow, the lights and the scoring all agree.
float Game::beatClock() const { return songT - beatLatencySec(); }

float Game::beatOffset() const {
	const float now = beatClock();
	if (beatPerLoop <= 0) {
		const float t = now - beatFirst;
		return t - roundf(t / beatStepLen) * beatStepLen;
	}
	// The nearest beat in this play of the song, or the last of the one before, or the first of the next.
	const float q = now - floorf(now / beatLoop) * beatLoop;
	int j = (int)roundf((q - beatFirst) / beatStepLen);
	if (j < 0) j = 0;
	if (j >= beatPerLoop) j = beatPerLoop - 1;
	float best = q - (beatFirst + j * beatStepLen);
	const float next = q - (beatLoop + beatFirst);
	const float prev = q - (beatFirst + (beatPerLoop - 1) * beatStepLen - beatLoop);
	if (fabsf(next) < fabsf(best)) best = next;
	if (fabsf(prev) < fabsf(best)) best = prev;
	return best;
}

void Game::beatUpdate() {
	// The clock: real time, eased towards the music while it plays.
	songT += kDt;
	const bool custom = beatSong >= kTrackCount;
	if (custom && beatPerLoop <= 0) {
		// A player's song: its length is known once it has loaded, and with it every beat of each play.
		const float len = audio::musicLength();
		if (len > beatFirst + beatStepLen) {
			beatLoop = len;
			beatPerLoop = (int)ceilf((len - beatFirst) / beatStepLen - 0.001f);
		}
	}
	const float heard = audio::musicTime();
	if (heard >= 0) {
		const float period = custom ? beatLoop : beatLen * 4;
		float err = heard - songT;
		if (period > 0) err -= roundf(err / period) * period;
		songT += fabsf(err) < 0.25f ? err * 0.1f : err;
	}
	const int beatCombo = beatStreak > chain ? beatStreak : chain;
	const float target = phase == PHASE_PLAY ? clampf((beatCombo - 5) / 8.0f, 0.0f, 1.0f) : 0.0f;
	beatLightFade = lerpf(beatLightFade, target, 1.0f - powf(0.015f, kDt));
}

// A slice during play: true if it was on the beat and scores.
bool Game::beatSlice(const Obj& o) {
	if (audio::musicTime() < 0 || fabsf(beatOffset()) > kWindow) {
		if (beatStreak > 0 || beatHits == 0) addPopup("OFF BEAT", o.x, o.y - 18, withAlpha(COL_CREAM, 0.7f), 0.5f);
		beatStreak = 0;
		return false;
	}
	beatStreak++;
	beatHits++;
	const int bonus = beatStreak / 4 > 3 ? 3 : beatStreak / 4;
	if (bonus > 0) {
		award(bonus);
		char buf[16];
		snprintf(buf, sizeof buf, "BEAT +%d", bonus);
		addPopup(buf, o.x, o.y - 18, hueColor(beatHits * 0.07f), 0.7f);
	}
	return true;
}

// High-combo stage light show behind the field: smoothly rotates and shifts hue with the beat tempo.
void Game::drawBeatLightShow() const {
	if (mode != MODE_BEAT || beatLen <= 0 || beatLightFade <= 0.01f) return;
	const float clock = beatClock();
	const float q = beatPerLoop > 0 ? clock - floorf(clock / beatLoop) * beatLoop : clock;
	const float beats = (q - beatFirst) / beatLen;
	const float bFloor = floorf(beats);
	const float frac = beats - bFloor;
	const int barBeat = ((int)bFloor % 4 + 4) % 4;
	const float pulse = powf(1.0f - frac, 2.0f);
	const float barBoost = barBeat == 0 ? 1.28f : 1.0f;
	const float calm = gSave.calmFx ? 0.36f : 1.0f;
	const float alpha = beatLightFade * calm * (0.62f + 0.38f * pulse * barBoost);
	const float hueBase = clock * 0.05f + bFloor * 0.12f + 0.06f * pulse;
	const float speed = gSave.calmFx ? 0.35f : 1.0f;
	const float spanY = kFieldH - kFieldTop;
	const float w = (float)kFieldW;
	struct Rig { float ax, bx, sweep, width, dir, phase; };
	static const Rig kRigs[4] = {
		{0.16f, 0.34f, 0.24f, 0.22f,  1.0f, 0.0f},
		{0.38f, 0.56f, 0.26f, 0.25f, -1.0f, 1.3f},
		{0.62f, 0.44f, 0.26f, 0.25f,  1.0f, 2.6f},
		{0.84f, 0.66f, 0.24f, 0.22f, -1.0f, 4.1f},
	};
	auto emitBeams = [&](float coreMul, float alphaScale) {
		for (int i = 0; i < 4; i++) {
			const Rig& r = kRigs[i];
			const float sway = sinf(beats * 0.785398f * r.dir * speed + r.phase) * r.sweep * speed;
			const float ax = w * r.ax;
			const float ay = kFieldTop - 6.0f;
			const float tx = w * (r.bx + sway);
			const float by = kFieldH + 14.0f;
			const float bw = w * r.width * (0.85f + 0.25f * pulse) * coreMul;
			const u32 col = hueColor(hueBase + i * 0.22f);
			const u32 cTop = withAlpha(col, 0.24f * alpha * alphaScale);
			const u32 cBot = withAlpha(col, 0.03f * alpha * alphaScale);
			gfx::tri(ax, ay, cTop, tx - bw, by, cBot, tx + bw, by, cBot);
		}
	};
	emitBeams(1.0f, 1.0f);
#if !PORT_3DS
	if (gfx::fullGpuFx() && !gSave.calmFx) {
		gfx::setAdditive(true);
		emitBeams(0.56f, 0.62f);
		gfx::setAdditive(false);
	}
#endif
	// Subtle wash pulse along the floor edge at peak bar downbeats.
	if (barBeat == 0 && pulse > 0.15f && !gSave.calmFx) {
		const u32 rim = withAlpha(hueColor(hueBase + 0.5f), 0.10f * alpha * pulse);
		gfx::rect(kFillX, kFieldH - spanY * 0.18f, kFillW, spanY * 0.18f, rim);
	}
}

// While a slice would count, the field's edges glow: the first beat of the bar in the accent colour.
void Game::drawBeatPulse() const {
	if (beatLen <= 0 || phase != PHASE_PLAY || audio::musicTime() < 0) return;
	const float near = 1 - fabsf(beatOffset()) / kWindow;
	if (near <= 0) return;
	const float now = beatClock();
	const float q = beatPerLoop > 0 ? now - floorf(now / beatLoop) * beatLoop : now;
	const float b = (q - beatFirst) / beatLen, r = roundf(b);
	const bool down = fabsf(b - r) * beatLen <= kWindow && ((int)r % 4 + 4) % 4 == 0;  // not a half beat between
	const u32 c = withAlpha(down ? COL_ACCENT : COL_CREAM, near * near * (gSave.calmFx ? 0.2f : 0.45f));
	const float w = 5 + 3 * near, top = kFieldTop;
	gfx::rect(0, top, w, kFieldH - top, c);
	gfx::rect(kFieldW - w, top, w, kFieldH - top, c);
	gfx::rect(w, kFieldH - w, kFieldW - 2 * w, w, c);
}

// Four lights, one for each beat of the bar. The one playing now is lit and swells as it lands.
void Game::drawBeat(float x, float y, float s) const {
	if (beatLen <= 0) return;
	const float clock = beatClock();
	const float q = beatPerLoop > 0 ? clock - floorf(clock / beatLoop) * beatLoop : clock;
	const float beats = (q - beatFirst) / beatLen;
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
