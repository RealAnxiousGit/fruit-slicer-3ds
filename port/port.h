// Everything the shared game needs to know about this port: Nintendo 3DS, two screens, stylus.
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#define PORT_3DS 1
#define PORT_TWO_SCREENS 1
#define PORT_CUSTOM_MUSIC 0  // Custom Music is for Windows, Linux and Android only
#define PORT_HAS_SENSITIVITY 1
#define PORT_PAUSE_BUTTON 0
#define PORT_MOUSE 0
#define PORT_LIGHT_FRENZY 1
#define PORT_SENSITIVITY_LABEL "Stylus Sensitivity"
#define PORT_SENSITIVITY_HINT "Higher follows small, quick moves."
#define PORT_ART_HINT "2D is lighter on an Old 3DS."
#define PORT_DETAIL_HINT "Low suits an Old 3DS."
#define PORT_RESET_HINT "Back to the best settings for this console."
#define PORT_HOWTO_PAUSE "START pauses. B goes back."
#define PORT_PRACTICE_SKIP "START to skip the lessons"
#define PORT_CONFIRM_TEXT "Tap again to confirm"
#define PORT_RESET_VISUALS
// The Credits' "Built With" lines for this port.
#define PORT_CREDITS_BUILT "devkitARM and libctru", "citro2d and citro3d", "libcurl, Mbed TLS and zlib", "tex3ds, bannertool and makerom", "Azahar"
#define PORT_CHECK_SETTINGS
#define PORT_RESET_DISPLAY
#define PORT_UPGRADE_V4(data, size)
#define PORT_VISUALS_HINT "Art style, board, scenery, blade,"
#define PORT_GAMEPLAY_HINT "Stylus, difficulty, and how the"
#define PORT_DATA_HINT "Speed test, and resetting scores."
#define PORT_LOADING_TEXT (gIsNew3ds ? "Setting up for New 3DS" : "Setting up for Old 3DS")
#define PORT_NO_SOUND_TEXT "No sound: needs 3ds/dspfirm.cdc"
#define PORT_DEFAULT_SENSITIVITY 40
#define PORT_SETTINGS_FIELDS
#define PORT_HAS_DISPLAY 0
#define PORT_SHADOWS (gSave.effects >= FX_MEDIUM ? 1 : 0)
#define PORT_BLOOM 1
#define PORT_VIBRATE(strength, ms) ((void)0)
#define PORT_STICK_HINT "Hold: slices while A, L or R is held."

// The 3DSX build updating itself (update_port.cpp).
namespace port {
bool replaceFile(const char* target, const char* incoming);
void updateTidy();    // at boot: clears anything an unfinished update left beside the game
void updateFinish();  // after the game has closed its own file: puts a downloaded update in place
}

// Filling the GPU's command list drops its closing commands and hangs the GPU, so draws stop short of that.
inline bool gpuHasRoom() {
	u32* addr;
	u32 size, used;
	GPUCMD_GetBuffer(&addr, &size, &used);
	return size - used > 2048;
}

// The field fruit flies over, in game units.
constexpr int kFieldW = 320;
constexpr int kFieldH = 240;
constexpr float kFieldTop = 0;     // nothing covers the field here: the score has its own screen
constexpr float kFillX = 0;        // full-field washes cover this much
constexpr float kFillW = 320;
constexpr float kJumpReject = 150.0f;  // resistive panels spike when pressure changes
constexpr float kCutReach = 1.0f;      // scales how close a swipe must pass
