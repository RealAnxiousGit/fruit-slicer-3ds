// Bomb blasts: a fireball, a shockwave, smoke, flying shell pieces and embers. Shared by the bombs in a
// game and by the Quit Game bomb on the title, whose blast fills the whole screen.
#pragma once
#include "common.h"

namespace boom {
void start(float x, float y, float size = 1, float delay = 0);  // size 1 is a bomb in a game; delay in seconds
void update(float dt);
void draw(float ox = 0, float oy = 0);          // in the current drawing space, moved by ox, oy
bool active();
void clear();
}  // namespace boom
