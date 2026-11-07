#pragma once
#include "common.h"

// Set by the game each step. Dull: a stunned blade, drawn grey (0 or 1). Drag: honey, so the blade
// follows the finger more slowly (0 to 1).
extern float gBladeDull, gBladeDrag;
u32 trailSwatch(int trail, float t);  // a Shop trail's colour along its length, for icons

// Turns raw resistive touch samples into a smooth blade stroke.
struct Blade {
	static constexpr int kTrail = 32;
	struct Point {
		float x, y, age;
	};

	Point trail[kTrail];
	int head = 0;
	int count = 0;

	bool active = false;     // finger is down this frame
	bool strokeStart = false;
	float x = 0, y = 0;      // filtered position
	float px = 0, py = 0;    // filtered position last frame
	float speed = 0;         // px per frame
	float rawX = 0, rawY = 0;
	float swishCooldown = 0;
	u32 strokeId = 0;

	void reset();
	void update(const Input& in);
	void draw() const;  // uses the blade chosen in Options and the Shop trail

private:
	void drawExtras(int style) const;
	void pushPoint(float nx, float ny);
	float hx[3], hy[3];  // recent filtered samples, for curve interpolation
	int hn = 0;
};
