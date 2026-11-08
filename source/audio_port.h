// What a port's sound hardware must do. All track and volume decisions stay in audio.cpp.
#pragma once
#include "common.h"

struct Clip {
	s16* pcm;
	u32 samples;   // frames: one sample per channel
	u32 rate;
	u32 channels = 1;  // 2 only for recorded music on the universal port
};

namespace audioport {
bool init();  // false means no sound at all; the game still plays
void shutdown();
s16* alloc(u32 bytes);
void release(s16* p);
void ready(const s16* p, u32 bytes);  // this memory has finished being written
void settle();                        // gives the hardware time to let go of replaced music
void playSfx(const Clip& c, float left, float right, float rate);
void musicStart(const Clip& c, bool loop = true);  // a track that doesn't loop stops at its end, and musicTime goes to -1
// How much of the playing track has arrived so far, and its full length. Music starts before all of it is
// in; the mixer waits at the edge of what has arrived rather than play what isn't there yet.
void musicAvail(const s16* pcm, u32 frames, u32 total);
void musicStop();
void musicVolume(float v);
void musicRate(float factor);
float musicTime();  // seconds into the music, wrapping with its loop; -1 when none is playing  // 1 is normal; a little above 1 plays the track faster and higher
void fuseStart(const Clip& c);
void fuseMix(float left, float right);
void fuseStop();
#ifdef TEST_RECORD
void recordStep();  // trailer capture: mix one 60th of a second straight into the recording
#endif
}  // namespace audioport
