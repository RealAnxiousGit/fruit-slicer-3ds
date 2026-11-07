#pragma once
#include "blade.h"
#include "common.h"

// All gameplay objects live in fixed arrays: nothing here allocates after boot,
// so rapid spawning can't fragment the heap.
enum ObjKind : u8 { OBJ_FRUIT, OBJ_GOLD, OBJ_BOMB, OBJ_FREEZE, OBJ_DOUBLE, OBJ_FRENZY, OBJ_COIN, OBJ_MAGNET, OBJ_WIDE, OBJ_SHIELD, OBJ_TIME };

struct Obj {
	float x, y, vx, vy, rot, rotv, radius, age;
	float rx, ry, rxv, ryv;  // extra tumble, seen in the 3D art style
	float hitCooldown;  // tough fruit can't be hit twice by one swipe
	ObjKind kind;
	u8 fruit;
	u8 hits;            // slices still needed
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

enum GamePhase { PHASE_READY, PHASE_PLAY, PHASE_FINALE, PHASE_DYING, PHASE_OVER };

struct Game {
	static constexpr int kMaxObjs = 40;
	static constexpr int kMaxHalves = 64;
	static constexpr int kMaxParticles = 260;
	static constexpr int kMaxSplats = 18;
	static constexpr int kMaxPopups = 14;
	static constexpr int kMaxRings = 8;
	static constexpr int kMaxPending = 28;

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
	int coinsThrown;    // this run, up to a cap so no one mode is by far the best way to earn
	int coinsCaught;
	int coinsBonus;     // for a new best, paid when the run ends
	u32 coinsEarned;    // what the run paid in total, for the result screen
#ifdef AUTOTEST
	// Measures how often a fruit is actually drawn on top of a bomb.
	u32 statBombFrames, statOverlapFrames, statWaves, statFrenzies, statResets;
	u32 statThrows, statFeverThrows, statBombHits, statDropped, statPeakObjs;
	float statFeverTime;
	u32 statFruitAt[3], statBombsAt[3];  // thrown outside a Frenzy, by minute and a half of play: 0-30 s, 30-60 s, after
#endif

	void start(Mode m);
	void commitScore();
	bool keepBestSoFar();
	void update(Blade& blade);
	void drawField(const Blade& blade) const;   // bottom screen
	void drawFieldTop() const;                  // top screen: fruit thrown high, one eye at a time
	void kickParticles();                       // steps particles, on the spare core when there is one
	void drawMarks(bool top, float ox, float oy) const;  // High Contrast outlines and colour-blind bomb rings
	// Snap assist: the closest fruit ahead of a moving point, within range. False if none.
	bool fruitAhead(float x, float y, float dx, float dy, float range, float* fx, float* fy) const;
	void drawHud() const;                       // top screen, one eye at a time
	int activeObjects() const;
	int activeParticles() const;
	// First live object the AUTOTEST bot should aim for (or avoid).
	const Obj* botTarget(bool wantBomb) const;

private:
	void spawnWave();
	bool spawnPattern(float d);
	void maybeCoin(float d);
	void bounceJelly(Obj& o, const Blade& blade);
	void trickEffect(const Obj& o, float dx, float dy);
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
