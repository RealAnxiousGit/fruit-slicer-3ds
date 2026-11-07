#pragma once
#include "port.h"
#include <math.h>
#include <stdio.h>

// Logging costs real time on a console and even more in an emulator, so release builds drop it.
#ifdef AUTOTEST
#define DLOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DLOG(...) ((void)0)
#endif

constexpr float kPi = 3.14159265f;
constexpr float kDt = 1.0f / 60.0f;
constexpr int kBottomW = 320;
constexpr int kBottomH = 240;
constexpr int kTopW = 400;
constexpr int kTopH = 240;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// xorshift32: gameplay randomness never touches the heap or libc state.
struct Rng {
	u32 s = 0x51CE51CE;
	u32 next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
	float unit() { return (next() & 0xFFFFFF) / 16777216.0f; }
	float range(float a, float b) { return a + (b - a) * unit(); }
	int below(int n) { return (int)(next() % (u32)n); }
};
extern Rng gRng;

enum Mode { MODE_CLASSIC, MODE_ARCADE, MODE_ZEN, MODE_FRENZY, MODE_SUDDEN, MODE_MINEFIELD, MODE_COMBO, MODE_COUNT };

struct ModeInfo {
	const char* name;
	const char* line1;
	const char* line2;
};
extern const ModeInfo kModes[MODE_COUNT];

constexpr int kBoardCount = 6;
constexpr int kSceneCount = 3;
constexpr int kBladeCount = 6;

enum ArtStyle { ART_2D, ART_3D };
enum Effects { FX_NONE, FX_LOW, FX_MEDIUM, FX_FULL };
enum Difficulty { DIFF_EASY, DIFF_NORMAL, DIFF_HARD, DIFF_INSANE, DIFF_COUNT };

static constexpr int kTrackCount = 8;

// Save data (save.cpp). Sliders hold 0..100.
struct Settings {
	u32 best[MODE_COUNT];
	u32 totalFruit;
	u32 bestCombo;
	// Audio
	u8 sfx, music, menuSfx;
	u8 menuTrack, gameTrack;  // 0 lets the game choose, otherwise 1 + a track
	// Visuals
	u8 artStyle, board, scene, blade;
	u8 effects;
	u8 depth;       // 3D depth; 50 is the normal strength
	u8 shake;
	u8 bgDetail;    // 0 simple, 1 full
	u8 topFruit;    // no longer a setting; kept so that existing saves still line up
	u8 detail;      // 3D model detail: 0 low, 1 high
	u8 sliceStyle;  // 0 ready-made halves, 1 cut along the actual swipe
	u8 transitions;
	// Gameplay
	u8 stylus;
	u8 difficulty;
	u8 fruitSpeed;   // 50 is normal
	u8 bombs;        // 50 is normal
	u8 forgiveness;  // 50 is normal
	u8 frenzyDrain;  // 50 is normal
	u8 misses;       // 1..5, Classic only
	PORT_SETTINGS_FIELDS
};
extern Settings gSave;
void saveDefaults(bool keepScores, int section);  // section: -1 all, 0 audio, 1 visuals, 2 gameplay
void saveLoad();
void saveWrite();

// Console facts, worked out once at boot.
extern bool gIsNew3ds;
extern bool gHas3dScreen;

// One frame of player input, from the hardware or the AUTOTEST bot.
struct Input {
	u32 down, held, repeat;
	bool touching;
	int tx, ty;
	bool hovering;  // a mouse cursor rests at tx, ty with no button held
	int wheel;      // mouse wheel notches this frame, up is positive
};

// Sound pans with position on the field.
inline float fieldPan(float x) { return (x - kFieldW * 0.5f) / 200.0f; }
