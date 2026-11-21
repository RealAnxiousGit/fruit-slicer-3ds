#pragma once
#include "port.h"
#include <math.h>
#include <stdio.h>

// Logging costs real time on a console and even more in an emulator, so release builds drop it.
#if defined(AUTOTEST) || defined(TEST_DLOG)
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
// Decides what each wave throws (fruit, bombs, coins), apart from where it flies. Daily Fruit seeds it
// from the date, so everyone gets the same run whatever their screen layout or effects.
extern Rng gSpawn;

// New modes go at the end of the list, so saved scores keep their places. The Practice run comes after
// the list: it keeps no score.
enum Mode {
	MODE_CLASSIC, MODE_ARCADE, MODE_ZEN, MODE_FRENZY, MODE_SUDDEN, MODE_MINEFIELD, MODE_COMBO, MODE_DAILY, MODE_BEAT,
	MODE_ORCHARD, MODE_BOSSRUSH, MODE_RECIPE, MODE_SHARP, MODE_JUGGLE,  // 1.2
	MODE_COUNT, MODE_PRACTICE = MODE_COUNT
};
// The Mixer's rules can change a run of this mode. Daily Fruit and Beat Slice throw a fixed sequence,
// and Boss Rush hands out fixed builds, so those three keep to their own rules.
inline bool mixerMode(int m) { return m < MODE_COUNT && m != MODE_DAILY && m != MODE_BEAT && m != MODE_BOSSRUSH; }

struct ModeInfo {
	const char* name;
	const char* line1;
	const char* line2;
};
extern const ModeInfo kModes[MODE_COUNT];
const char* modeName(Mode m);  // also names the Practice run

constexpr int kBaseBoards = 6;   // free; the rest are Shop themes
constexpr int kBoardCount = kBaseBoards
#define SHOP_THEME(...) +1
#include "shop_items.inc"
	;
constexpr int kSceneCount = 6;
constexpr int kBladeCount = 6;

enum ArtStyle { ART_2D, ART_3D };
enum Effects { FX_NONE, FX_LOW, FX_SIMPLE, FX_FULL, FX_MEDIUM = FX_SIMPLE };
enum Difficulty { DIFF_EASY, DIFF_NORMAL, DIFF_HARD, DIFF_INSANE, DIFF_COUNT };
// The Mixer's switches. Giant and Tiny can't both be on. Daily Fruit and Beat Slice ignore them all,
// since those two throw a fixed sequence.
enum MixerRule { MIX_LOW_GRAVITY, MIX_GIANT, MIX_TINY, MIX_MIRROR, MIX_DOUBLE_BOMBS, MIX_SIDES, MIX_COUNT };

static constexpr int kBaseTracks = 8;  // free; the rest come from the Shop
static constexpr int kTrackCount = kBaseTracks
#define SHOP_TRACK(...) +1
#include "shop_items.inc"
	;
static constexpr int kCustomSongsMax = 64;  // custom::kMax: Custom Music songs, after the game's tracks
static_assert(kTrackCount + kCustomSongsMax < 255, "Menu and Game Music hold 1 + a track in a byte");
constexpr int kTrailCount = 1  // 0 is the plain blade colour, 1.. are Shop trails
#define SHOP_TRAIL(...) +1
#include "shop_items.inc"
	;
constexpr int kMaxShopItems = 31;  // one bit each in the save, with bit 0 kept for the free set

// Save data (save.cpp). Sliders hold 0..100.
// From 1.0 ("FLS5") the file records its own size, so this struct only ever grows:
// new shared fields take bytes from `reserved`, and older saves load with the rest at defaults.
constexpr int kOldModes = 12;   // the score tables up to 1.1, kept where they were and read once by 1.2
constexpr int kMaxModes = 24;   // the score tables from 1.2, after the port block
constexpr int kMaxFruitKinds = 24;
constexpr int kMaxTracks = 16;
constexpr int kDailyDays = 32;
constexpr int kMaxBosses = 32;  // one bit each in bossesBeaten
struct Settings {
	// Scores and progress
	u32 best11[kOldModes];  // 1.1's bests: copied into best[] once, then unused
	u32 totalFruit;
	u32 bestCombo;
	u32 bestAssisted11[kOldModes];
	u32 coins, coinsLifetime;
	u32 playSeconds;
	u32 fruitSliced[kMaxFruitKinds];  // Fruit Journal, by mesh kind
	u32 ownedThemes, ownedTrails, ownedTracks, ownedExtras;  // Shop: bit n = item n
	u32 dailyDay;                 // days since 2000-01-01 of the newest Daily Fruit played, 0 = never
	u32 dailyScore[kDailyDays];   // best score + 1 (0: not played); [0] is dailyDay, [1] the day before, ...
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
	u8 beatTrack;     // Beat Slice: 0 picks Picnic Stroll, otherwise the track number + 1
	u16 modePlays11[kOldModes];  // 1.1's counts: copied into modePlays[] once
	u16 dailyFirstDay;  // the day (as dailyDay) the Daily Fruit calendar began, 0 = not yet
	u8 splat;         // Shop splat colour, 0 keeps each fruit's own juice
	u8 scoreRules;    // which rule changes this save's bests have been through (see saveRetireOldBests)
	u8 seasonalOff;   // 1 keeps seasonal boards away from the free boards
	u8 skin;          // Shop fruit skin, 0 none
	u8 mixer[MIX_COUNT];  // 1 turns a Mixer rule on
	u8 beatLatency;   // Beat Timing, in 2 ms steps around 100 (100 = 0 ms, above = you slice late)
	u8 reserved[27];  // sized so the port block starts on a 4-byte boundary
	// The port's own settings, in a fixed-size block so shared fields never move.
	union {
		struct { u8 portRev; PORT_SETTINGS_FIELDS };
		u8 portBlock[32];
	};
	// Fields too big for `reserved` go here, after the port block, so nothing already saved moves.
	u32 bestMixer11[kOldModes];  // 1.1's Mixer bests: copied into bestMixer[] once
	// 1.2: the score tables, with room for 24 modes.
	u32 best[kMaxModes];
	u32 bestAssisted[kMaxModes];  // runs with Slice Assist on or Game Speed below 100%
	u32 bestMixer[kMaxModes];     // runs with any Mixer rule on
	u16 modePlays[kMaxModes];     // runs started, for the Stats page's favourite mode
	// Orchard Blast and the bosses.
	u32 bossesBeaten;       // bit n: boss n has been beaten (Fruit Journal, and its tracks in the Shop)
	u32 bossTracksSeen;     // bit n: boss n's tracks have been shown as new in the Shop
	u16 orchardBestRoom, orchardBestLevel;
	u16 orchardRuns, orchardWins;
	u8 bossMusic;           // 1 plays the boss tracks in a boss fight
	u8 howToSeen;           // how-tos shown: bit 0 Orchard Blast, 1 its first boss, 2 Boss Rush, 3 Recipe, 4 Sharpshooter, 5 Juggle
	u8 bossTrackLast[4];    // per boss, the track (+1) heard in its last fight, so the next one differs
	u16 bossWins[8];        // per boss, times beaten in any mode (Fruit Journal and its coin milestones)
	u16 rushBestSecs;       // Boss Rush: the fastest clear, in seconds, 0 = never cleared
	u32 bossTracksFound;    // bit i: Shop track i has been discovered in Orchard Blast or Boss Rush
	u8 reserved12[8];       // later 1.2.x fields take bytes from here
	// 2.0 Endless, Online Leaderboards & 3DS StreetPass (appended cleanly so pre-2.0 saves keep defaults).
	u16 endlessBestRoom;     // Orchard Blast Endless: highest room conquered
	u16 endlessBestLevel;    // highest level reached in Endless
	u32 endlessRuns;         // Endless runs started
	u64 leaderboardId;       // random non-zero persistent player ID for online leaderboard upserts
	char leaderboardName[8]; // saved 6-char max player name (NUL-terminated)
	u8 streetPassOn;         // 3DS StreetPass enabled (0 = off, 1 = on)
	u8 streetPassPrompted;   // 1 once startup StreetPass prompt has been answered
	u8 spReserve;
	char streetPassMsg[28];  // customizable 3DS StreetPass greeting (default "Hello! :)")
	u32 spChallengesWon;     // StreetPass challenges beaten
	u8 seenRoom50Cutscene;   // 1 once Room 50 first-encounter dialogue has played (cleared only on Reset Everything)
	u8 bossTrackLastEx[4];   // per boss (indices 4..7), last track (+1) heard
};
static_assert(MODE_COUNT + 1 <= kMaxModes, "more modes than the save holds");
static_assert(MODE_JUGGLE - MODE_ORCHARD + 2 <= 8, "howToSeen has a bit for each how-to");
extern Settings gSave;
// Beat Timing in seconds: how far after the beat this player's slices land (Latency Test sets it).
inline float beatLatencySec() { return ((int)gSave.beatLatency - 100) * 0.002f; }
inline u8 beatLatencyStore(float sec) {
	const int v = 100 + (int)(sec * 500 + (sec < 0 ? -0.5f : 0.5f));
	return (u8)(v < 0 ? 0 : (v > 200 ? 200 : v));
}
enum SaveSection { SEC_ALL = -1, SEC_AUDIO, SEC_VISUALS, SEC_GAMEPLAY, SEC_CONTROLS, SEC_ACCESS, SEC_DISPLAY };
void saveDefaults(bool keepScores, int section);  // section: SaveSection; coins and Shop items are never reset
void saveResetScores();
void saveRetireOldBests();
void saveLoad();
void saveWrite();

// Console facts, worked out once at boot.
extern bool gIsNew3ds;
extern bool gHas3dScreen;

// One frame of player input, from the hardware or the AUTOTEST bot.
enum InputMode : u8 { INPUT_KEYMOUSE = 0, INPUT_TOUCH = 1, INPUT_PAD = 2 };
extern InputMode gInputMode;
inline bool touchUiActive() { return !PORT_3DS && gInputMode == INPUT_TOUCH; }
inline bool padUiActive() { return !PORT_3DS && gInputMode == INPUT_PAD; }

struct Input {
	u32 down, held, repeat;
	bool touching;
	int tx, ty;
	bool hovering;  // a mouse cursor rests at tx, ty with no button held
	int wheel;      // mouse wheel notches this frame, up is positive
	float stickX, stickY;  // a stick (or two added up), -1 to 1, down is positive
	bool stickSlice;       // the slice button: right trigger, or A, L or R on a 3DS
	u8 mode;               // 0 keep current, 1+InputMode sets gInputMode explicitly
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
