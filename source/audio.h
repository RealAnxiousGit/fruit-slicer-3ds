#pragma once
#include "common.h"

enum Sfx {
	SFX_SWISH0, SFX_SWISH1, SFX_SWISH2, SFX_SHING,
	SFX_CRISP0, SFX_CRISP1, SFX_JUICY0, SFX_JUICY1, SFX_SOFT0, SFX_SOFT1, SFX_MELON0, SFX_MELON1,
	SFX_KNOCK, SFX_SHELL,
	SFX_THROW, SFX_BOOM,
	SFX_COMBO3, SFX_COMBO4, SFX_COMBO5, SFX_COMBO6, SFX_COMBO8, SFX_COMBO10,
	SFX_STRIKE, SFX_CLICK, SFX_POWER, SFX_FREEZE, SFX_FEVER, SFX_GO, SFX_OVER, SFX_BEST, SFX_GOLD,
	SFX_FINALE_RISE, SFX_FINALE_HIT, SFX_FINALE_BURST,
	SFX_COIN, SFX_PLINK, SFX_SHOPBELL,
	SFX_COUNT
};
enum Music { MUSIC_NONE = -1, MUSIC_MENU, MUSIC_PLAY, MUSIC_ZEN, MUSIC_COUNT };

namespace audio {
// Starts the DSP; returns false if ndsp is unavailable. Sound effects then load one per loadStep,
// so the loading screen can show real progress. A music track loads a little each frame, so
// starting one never stalls the game. It begins playing once its first half second is in.
bool init();
int loadStepCount();
void loadStep(int i);
void update(float dt);  // once per frame, with the game time that passed: reads music in, and frees a replaced track when it's safe
void shutdown();
bool available();
// pan is -1 (left) to +1 (right). pitch multiplies the playback rate, so one clip can vary.
void play(Sfx s, float volume = 1.0f, float pan = 0.0f, float pitch = 1.0f);
void click(float volume = 1.0f);  // menu sound, on its own volume slider
void music(Music m);        // what the current screen wants; the player's track choice applies on top
void preview(Music m);      // plays what that screen would, for the settings rows
void reapply();             // back to the current screen's track after a preview or a changed choice
void refreshVolumes();
// The sizzle that plays while a bomb is in the air.
void fuse(bool on, float pan = 0.0f);
void musicSpeed(float factor);  // the chili pepper plays the music a little faster for a while
// Beat Slice: plays this track from its very start, whatever the Game Music choice, until the next music().
void musicBeat(int track);
float musicTime();           // seconds into the playing track, or -1 while it is still loading or silent
float trackBeat(int track);  // seconds per beat
float trackFirst(int track); // seconds to the first beat of a bar: 0 for the game's own tracks, which start on one
int trackCount();            // the game's tracks, then any custom songs (Windows, Linux and Android)
bool trackHasBeat(int track); // a custom song only once its beat has been found
float musicLength();         // seconds in the playing track, or -1 while it is still loading
}  // namespace audio
