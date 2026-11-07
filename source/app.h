// The shared game, as seen by each port's main loop.
#pragma once
#include "common.h"

namespace app {
bool init();           // false if the assets are missing and there is nothing to draw
void showLoading();    // runs the loading screen until everything has arrived
// `steps` updates (each 1/60 s) and one draw; false when the game should quit. A display that can't keep
// up gets more steps per draw, so the game loses smoothness there and never speed.
bool frame(Input& in, int steps = 1);
void shutdown();
void notifySuspended();  // the game was interrupted, so pause it
bool inFieldView();      // touch coordinates are in the field rather than the menu layout
}  // namespace app
