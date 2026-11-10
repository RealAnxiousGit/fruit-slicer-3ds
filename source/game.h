#pragma once
#include "blade.h"
#include "common.h"

// All gameplay objects live in fixed arrays: nothing here allocates after boot,
// so rapid spawning can't fragment the heap.
enum ObjKind : u8 { OBJ_FRUIT, OBJ_GOLD, OBJ_BOMB, OBJ_FREEZE, OBJ_DOUBLE, OBJ_FRENZY, OBJ_COIN, OBJ_MAGNET, OBJ_WIDE, OBJ_SHIELD, OBJ_TIME, OBJ_SEED };

struct Obj {
	float x, y, vx, vy, rot, rotv, radius, age;
	float rx, ry, rxv, ryv;  // extra tumble, seen in the 3D art style
	float hitCooldown;  // tough fruit can't be hit twice by one swipe
	ObjKind kind;
	u8 fruit;
	u8 hits;            // slices still needed
	bool loose;         // broke off a trick fruit: worth a bonus, and never a miss if dropped
	bool on;
};
struct Half {
	float x, y, vx, vy, rot, rotv;
	float rx, ry;            // 3D: orientation kept from the moment of the cut
	float axisX, axisY, tilt, tiltv;  // 3D: rolls the cut face towards the player
	short sprite;
	short cut;               // 3D: exact-cut vertex slot, or -1 for a ready-made half
	u8 mesh, part;
	bool on;
};
struct Particle {
	float x, y, vx, vy, life, maxLife, size, gravity;
	u32 color;
	u8 sprite;  // 0 drop, 1 spark
	bool on;
};
struct Splat {
	float x, y, rot, scale, life;
	u32 color;
	u8 variant;
	bool on;
};
struct Popup {
	char text[24];
	float x, y, life, scale;
	u32 color;
	bool on;
};
struct Ring {
	float x, y, life, maxLife, maxScale;
	u32 color;
	bool on;
};
struct Pending {
	float delay, x, vx, vy;
	ObjKind kind;
	u8 fruit;
	bool on;
};

// The Practice run's lessons, in order (practice.cpp).
enum PracticeStep { PS_SLICE, PS_THREE, PS_COCONUT, PS_BOMB, PS_COIN, PS_FRENZY, PS_DONE };
extern bool gStickInUse;  // set by the menus: a controller is steering the blade, so lessons say so
extern bool gHudPaused;  // set by the menus: the pause menu is up, so the lesson sign steps aside

enum GamePhase { PHASE_READY, PHASE_PLAY, PHASE_FINALE, PHASE_DYING, PHASE_OVER };

struct Game {
	static constexpr int kMaxObjs = 40;
	static constexpr int kMaxHalves = 64;
	static constexpr int kMaxParticles = 260;
	static constexpr int kMaxSplats = 18;
	static constexpr int kMaxPopups = 14;
	static constexpr int kMaxRings = 8;
	static constexpr int kMaxPending = 28;
	static constexpr float kThrowY = 262.0f;  // where thrown things start, below the field

	Obj objs[kMaxObjs];
	Half halves[kMaxHalves];
	Particle parts[kMaxParticles];
	Splat splats[kMaxSplats];
	Popup popups[kMaxPopups];
	Ring rings[kMaxRings];
	Pending pending[kMaxPending];
	int partCursor, splatCursor, halfCursor, popupCursor, ringCursor;

	Mode mode;
	GamePhase phase;
	float phaseT;       // seconds in the current phase
	float elapsed;      // seconds of play
	float timeLeft;     // for timed modes
	float waveTimer;
	float gravity;
	float flash;        // white flash from bomb
	u32 flashColor;
	float shake;
	u32 score;
	u32 best;
	bool newBest;
	int strikes;
	int maxStrikes;     // 0 means no strike rule
	int sliced;
	int bestCombo;
	bool scoreCommitted;
	bool assisted;  // Slice Assist or a slower Game Speed was on: the score counts toward the assisted bests
	bool mix[MIX_COUNT];  // the Mixer rules this run plays by
	bool mixed;           // any of them is on: the score counts toward the Mixer bests instead
	const char* passName; // Pass and Play: whose turn this is. These runs leave the bests and coins alone
	float feverGrace;   // fruit thrown by a Frenzy is still landing: misses stay free
	int chain;          // the running combo: fruit in a row while the meter stays alive
	int comboCount;
	float comboTimer;
	u32 comboStroke;
	float comboX, comboY;
	char callout[40];   // large top-screen message
	float calloutT;
	float calloutSize;
	u32 calloutColor;
	float freezeT, doubleT, frenzyT;
	float magnetT, wideT;  // Arcade power-ups: fruit drift to the blade; the blade cuts a wider path
	bool shield;           // the next bomb fizzles
	float chiliT;          // chili pepper: faster game, double points, faster music
	float stunT;           // spiky fruit: the blade can't cut
	float stickyT;         // honey melon: the blade drags behind the finger
	float frenzyTimer;
	float feverMeter;   // 0..1, fills as you slice and never stops draining; running dry ends the combo
	float comboFadeT;   // the COMBO text fades out for this long after the meter runs dry
	int fadingChain;    // the combo that is fading
	float feverT;       // seconds of Fruit Frenzy left
	float feverTimer;
	float slowT;        // brief slow motion after a huge combo
	float scorePulse;
	float strikePulse;
	// The giant fruit that ends a timed run.
	float finaleT;      // seconds since it was thrown
	float finaleX, finaleY;
	float finaleSwell;  // grows with every hit
	float finaleJoltX, finaleJoltY;
	float finaleTravel; // blade distance inside the fruit since the last hit
	int finaleHits;
	bool finaleOn;
	int timeThrown;     // Arcade: +5 Seconds thrown this run, kept low so a run can't go on for ever
	int pinataThrown;   // kept low: a pinata is a rare treat
	int coinsThrown;    // this run, up to a cap so no one mode is by far the best way to earn
	int coinsCaught;
	int coinsBonus;     // for a new best, paid when the run ends
	u32 coinsEarned;    // what the run paid in total, for the result screen
	float coinPulse;    // the HUD's coin count swells for a moment when a coin is caught
	// Coins the run has made so far, as the HUD shows them: 5 a caught coin and 1 every 100 points.
	// End-of-run bonuses (new best, Daily, Journal) come on top and show on the result screen.
	u32 runCoins() const { return mode == MODE_PRACTICE ? 0 : (u32)coinsCaught * 5 + score / 100; }
	u32 journalCoins;   // part of it from Fruit Journal milestones
	u32 dailyCoins;     // part of it from the day's first Daily Fruit
	u32 day;            // Daily Fruit: the day this run belongs to, taken when it starts
	u32 dailyWave;      // Daily Fruit: waves thrown so far
	u32 journalStart[kMaxFruitKinds];  // Journal counts when the run started, to pay milestones crossed
	// The biggest swipe of the run, replayed on the result screen after a new best.
	static constexpr int kSwipePoints = 32;
	float swipeX[kSwipePoints], swipeY[kSwipePoints];
	int swipeLen, swipeBest;
	float strokeX[kSwipePoints], strokeY[kSwipePoints];  // the stroke in progress
	int strokeLen;
	u32 strokeOf;
	// Practice run.
	int practiceStep;
	float practiceT;      // seconds since the step began or last retried
	int practiceGot;      // what the step has seen so far (slices, a big enough swipe, ...)
	bool practiceFail;    // the step went wrong: try it again
	bool practiceThrown;  // this attempt's fruit is out
	// Beat Slice. Song time follows the music where it can, so fruit peaks right on the beat.
	int beatSong;         // the track
	float songT;          // seconds since the track began, never wrapping
	float beatLen;        // seconds per beat
	float beatStepLen;    // seconds between throw slots: a beat, or half of one on slow tracks
	int beatStreak;       // slices on the beat in a row
	int beatHits;         // slices on the beat this run
	float beatFirst;      // seconds into the track of the first beat of a bar: 0 for the game's own tracks
	float beatLoop;       // seconds before a custom song starts again, once it has loaded
	int beatPerLoop;      // throw slots in one play of a custom song; 0 for the game's tracks, which run on forever
#ifdef AUTOTEST
	// Measures how often a fruit is actually drawn on top of a bomb.
	u32 statBombFrames, statOverlapFrames, statWaves, statFrenzies, statResets;
	u32 statThrows, statFeverThrows, statBombHits, statDropped, statPeakObjs;
	float statFeverTime;
	u32 statFruitAt[3], statBombsAt[3];  // thrown outside a Frenzy, by minute and a half of play: 0-30 s, 30-60 s, after
	u32 statPendingMask;  // queue slots already taken when a wave began
	void logWave() const;  // Daily Fruit: what this wave threw, to prove every run gets the same
#endif

	void start(Mode m);
	void commitScore();
	void finishRun();   // Zen has no end of its own: the pause menu finishes the run
#ifdef AUTOTEST
	void labProbes(Blade& blade, void (*check)(bool, const char*, const char*));
	void labShowcase();
	void labThrow(float& x, float& vx, float& vy) { pickThrow(false, 0, x, vx, vy); }
	Input bladeInput(Input in) const {  // Mixer: on Mirror the blade goes where your hand isn't
		if (mix[MIX_MIRROR] && in.touching) in.tx = kFieldW - in.tx;
		return in;
	}
	float fruitSize() const { return mix[MIX_GIANT] ? 1.5f : (mix[MIX_TINY] ? 0.65f : 1.0f); }  // Mixer: everything but bombs and coins
	u32* bestTable() const { return mixed ? gSave.bestMixer : (assisted ? gSave.bestAssisted : gSave.best); }
#endif
	bool keepBestSoFar();
	void update(Blade& blade);
	void drawField(const Blade& blade) const;   // bottom screen
	void drawFieldTop() const;                  // top screen: fruit thrown high, one eye at a time
	void kickParticles();                       // steps particles, on the spare core when there is one
	void drawMarks(bool top, float ox, float oy) const;  // High Contrast outlines and colour-blind bomb rings
	// Snap assist: the closest fruit ahead of a moving point, within range. False if none.
	bool fruitAhead(float x, float y, float dx, float dy, float range, float* fx, float* fy) const;
	bool calloutShown() const { return calloutT > 0 && !gHudPaused; }  // the big words, hidden behind a menu
	void drawHud() const;                       // top screen, one eye at a time
	int activeObjects() const;
	int activeParticles() const;
	// First live object the AUTOTEST bot should aim for (or avoid).
	const Obj* botTarget(bool wantBomb) const;
	bool timed() const { return timeLeft > 0 || mode == MODE_ARCADE || mode == MODE_FRENZY || mode == MODE_DAILY || mode == MODE_BEAT; }
	const char* practicePrompt(int line) const;  // the current lesson, two lines
	void drawPractice() const;
	void drawSwipeReplay() const;
	void drawBeat(float x, float y, float s) const;  // Beat Slice: four lights for the beats of the bar
	float beatOffset() const;  // seconds from the nearest throw slot, negative when early
	void drawBeatPulse() const;  // Beat Slice: the field's edges glow while a slice would count

private:
	int diffIndex() const;            // Daily Fruit always plays on Normal
	float slider(u8 value) const;     // a Gameplay slider, 0-100; Daily Fruit holds them at the middle
	void practiceStart();
	void practiceUpdate();
	// The Frenzy meter and COMBO count only run once the lessons reach the Frenzy lesson, so the earlier ones stay calm.
	bool meterLive() const { return mode != MODE_PRACTICE || practiceStep >= PS_FRENZY; }
	void practiceSliced(const Obj& o);
	void practiceSwipe(int n);
	void practiceBomb();
	void recordStroke(const Blade& blade);
	void beatStart();
	void beatUpdate();
	bool beatSlice(const Obj& o);  // whether a slice is on the beat and scores; adds the streak bonus
	void spawnWave();
	bool spawnPattern(float d);
	void maybeCoin(float d);
	void bounceJelly(Obj& o, const Blade& blade);
	void trickEffect(const Obj& o, float dx, float dy);
	Obj* spawnPiece(const Obj& from, ObjKind kind, int fruit, float radius);
	void updateTricks(const Blade& blade);
	void pickThrow(bool bomb, float delay, float& x, float& vx, float& vy);
	float clearance(float x, float vx, float vy, float delay, bool bomb) const;
	bool bombAround() const;
	void queue(float delay, float x, float vx, float vy, ObjKind kind, int fruit);
	void launch(const Pending& p);
	void updateObjects(float dt);
	void updateEffects(float dt);
	void testSlices(Blade& blade);
	bool hitTest(const Obj& o, const Blade& blade, float radius) const;
	void sliceObj(Obj& o, const Blade& blade);
	void sliceFruit(Obj& o, const Blade& blade);
	void hitBomb(Obj& o);
	void award(int points);
	void addStrike(float x);
	void finishCombo();
	void addFever(float amount);
	void addHalves(const Obj& o, const struct FruitDef& def, const Blade& blade, float dx, float dy);
	void drawObjects3D(bool top, float ox, float oy) const;
	void drawObjects2D(bool top, float ox, float oy) const;
	void emitJuice(float x, float y, u32 color, int count, float dirX, float dirY);
	void emitSparks(float x, float y, u32 color, int count, float speed);
	bool popupBlocked(float x, float y, float hw, float hh) const;
	void addPopup(const char* s, float x, float y, u32 color, float scale);
	void addRing(float x, float y, u32 color, float maxScale, float life);
	void setCallout(const char* s, u32 color, float size = 0.72f);
	void endGame();
	void startFinale();
	void updateFinale(Blade& blade);
	void finaleHit(const Blade& blade);
	void finaleBurst();
	float difficulty() const;
#ifdef TEST_SPAWN
public:
	void spawnReport();
#endif
};

u32 hueColor(float t);
int fruitSprite(int fruit);  // the 2D sticker of a fruit kind, on SHEET_FRUIT
int everydayFruit(Rng& rng);   // any everyday fruit but the coconut
float levelGravity(int diff);  // the difficulty's gravity, against Normal
float levelBombs(int diff);    // and how many bombs it throws, against Normal
