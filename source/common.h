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

constexpr int kBaseBoards = 6;   // free; the rest are Shop themes
constexpr int kBoardCount = 11;
constexpr int kSceneCount = 3;
constexpr int kBladeCount = 6;

enum ArtStyle { ART_2D, ART_3D };
enum Effects { FX_NONE, FX_LOW, FX_MEDIUM, FX_FULL };
enum Difficulty { DIFF_EASY, DIFF_NORMAL, DIFF_HARD, DIFF_INSANE, DIFF_COUNT };

static constexpr int kBaseTracks = 8;  // free; the rest come from the Shop
static constexpr int kTrackCount = 13;
constexpr int kTrailCount = 7;  // 0 is the plain blade colour, 1.. are Shop trails

// Save data (save.cpp). Sliders hold 0..100.
// From 1.0 ("FLS5") the file records its own size, so this struct only ever grows:
// new shared fields take bytes from `reserved`, and older saves load with the rest at defaults.
constexpr int kMaxModes = 12;
constexpr int kMaxFruitKinds = 24;
constexpr int kMaxTracks = 16;
constexpr int kDailyDays = 32;
struct Settings {
	// Scores and progress
	u32 best[kMaxModes];
	u32 totalFruit;
	u32 bestCombo;
	u32 bestAssisted[kMaxModes];  // runs with Slice Assist on or Game Speed below 100%
	u32 coins, coinsLifetime;
	u32 playSeconds;
	u32 fruitSliced[kMaxFruitKinds];  // Fruit Journal, by mesh kind
	u32 ownedThemes, ownedTrails, ownedTracks, ownedExtras;  // Shop: bit n = item n
	u32 dailyDay;                 // days since 2000-01-01 of the newest Daily Fruit played, 0 = never
	u32 dailyScore[kDailyDays];   // [0] is dailyDay, [1] the day before, ...
	u16 dailyStreak, dailyBestStreak;
	u32 beatBest[kMaxTracks];     // Beat Slice, by track
	u32 updLastDay;               // day of the last update check
	char updSkipped[16];          // version the player chose Later for
	// Audio
	u8 sfx, music, menuSfx;
	u8 menuTrack, gameTrack;  // 0 lets the game choose, otherwise 1 + a track
	// Visuals
	u8 artStyle, board, scene, blade;
	u8 effects;
	u8 depth;       // 3D depth; 50 is the normal strength
	u8 shake;
	u8 bgDetail;    // 0 simple, 1 full
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
	// Controls
	u8 stickStyle;   // 0 hold the slice button, 1 flick
	u8 stickSpeed;   // 50 is normal
	u8 snapAssist;   // 0 off, 1 low, 2 high
	u8 vibration;    // 0 off, 1 on
	// Accessibility
	u8 uiScale;      // 0 100%, 1 125%, 2 150%
	u8 colorBlind;   // 0 off, 1 red-weak, 2 green-weak, 3 blue-weak
	u8 highContrast, largeText, calmFx, sliceAssist, captions, sliceNoHold;
	u8 gameSpeed;    // 0 100%, 1 85%, 2 70%
	// Updates and first-start flags
	u8 updConsent;   // 0 not asked, 1 check, 2 don't
	u8 practiceDone;
	u8 trail;        // Shop blade trail, 0 none
	u8 reserved[65];  // sized so the port block starts on a 4-byte boundary
	// The port's own settings, in a fixed-size block so shared fields never move.
	union {
		struct { u8 portRev; PORT_SETTINGS_FIELDS };
		u8 portBlock[32];
	};
};
static_assert(MODE_COUNT <= kMaxModes, "more modes than the save holds");
static_assert(kTrackCount <= kMaxTracks, "more tracks than the save holds");
extern Settings gSave;
enum SaveSection { SEC_ALL = -1, SEC_AUDIO, SEC_VISUALS, SEC_GAMEPLAY, SEC_CONTROLS, SEC_ACCESS, SEC_DISPLAY };
void saveDefaults(bool keepScores, int section);  // section: SaveSection; coins and Shop items are never reset
void saveResetScores();
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
	float stickX, stickY;  // a stick (or two added up), -1 to 1, down is positive
	bool stickSlice;       // the slice button: right trigger, or A, L or R on a 3DS
};

// Sound pans with position on the field.
inline float fieldPan(float x) { return (x - kFieldW * 0.5f) / 200.0f; }

// ---------------------------------------------------------------- accessibility
// Read by drawing and game code. applyAccessibility() refreshes the drawing flags after a change.
void applyAccessibility();
float uiScale();                       // HUD and in-game text: 1, 1.25 or 1.5
float gameSpeedScale();                // Game Speed: 1, 0.85 or 0.7
bool assistedRun();                    // Slice Assist or a slower Game Speed: scores kept apart
void showCaption(const char* text);    // Sound Captions: shown only when they are on
