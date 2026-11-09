#pragma once
#include "common.h"

// Real 3D fruit, drawn by each port in between the 2D layers.
namespace m3d {

enum MeshId {
	MESH_APPLE, MESH_ORANGE, MESH_LEMON, MESH_PEAR, MESH_PLUM, MESH_MELON, MESH_KIWI, MESH_BERRY, MESH_PEACH,
	MESH_PINEAPPLE, MESH_COCONUT, MESH_GOLD, MESH_BOMB, MESH_SUNBURST, MESH_BANANA, MESH_MANGO, MESH_CHERRY,
	MESH_DRAGON, MESH_SPIKY, MESH_GRAPES, MESH_GRAPE, MESH_HONEY, MESH_JELLY, MESH_CHILI, MESH_COIN, MESH_BUNCH, MESH_POMEGRANATE, MESH_PINATA, MESH_COUNT
};
enum Part { PART_WHOLE, PART_LEFT, PART_RIGHT };

struct Pose {
	float x, y, z;
	float rx, ry, rz;
	float scale;
	// An extra turn about an axis in the screen plane, applied last. It rolls a cut face into view.
	float axisX, axisY, tilt;
};

bool init();
void shutdown();

// Keeps exactly one detail level in memory, and none at all while the 2D art style is chosen.
// Call once per frame, outside of drawing.
void sync();
bool ready();
bool usable();  // false if the shader never loaded, so the game must fall back to 2D art

// Starts a run of 3D draws on the current target. screenW is 400 (top) or 320 (bottom). stereo is
// the sideways shift in pixels per unit of z for this eye; the bottom screen passes 0.
// field: these are fruit in play; anything else (a menu's fruit) always draws in front of them.
void begin(float screenW, float stereo, bool field = false);
void draw(int mesh, Part part, const Pose& pose);
void end();  // returns to citro2D

// Halves cut along the actual swipe. A handle is a slot in a small vertex pool, allocated the first
// time it's needed. -1 means there's no free slot, so the caller uses a ready-made half instead.
int cutAcquire();
void cutRelease(int handle);
void cutReleaseAll();
// Cuts `mesh` by the plane normal.p = offset (model space), keeping the side where normal.p > offset.
// Runs on the worker thread when there is one; drawCut waits for it.
void cutRequest(int handle, int mesh, float nx, float ny, float nz, float offset);
void drawCut(int handle, const Pose& pose);

}  // namespace m3d
