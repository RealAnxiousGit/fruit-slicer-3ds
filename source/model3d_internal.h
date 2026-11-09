// Meshes as they sit in memory, and what a port's 3D renderer must do with them.
#pragma once
#include "model3d.h"

namespace m3d {
struct Vertex {
	float x, y, z;
	s8 nx, ny, nz, pad;
	u8 r, g, b, a;
};

struct PartData {
	const Vertex* v;
	const u16* idx;
	u32 nv, ni, body;
	u32 flesh, rim;
};
}  // namespace m3d

namespace m3dport {
bool init();  // false sends the game to the 2D art style
void shutdown();
void* alloc(u32 bytes);  // memory the renderer can read meshes from
void release(void* p);
void flush(const void* p, u32 bytes);  // the CPU just wrote this mesh data, and the renderer is about to read it
void begin(float screenW, float stereo, bool field);
void end();
void drawIndexed(int mesh, const m3d::PartData& d, const m3d::Pose& pose);
// A freshly cut half: `surface` of its vertices are the fruit's skin, and the rest are its cut face.
void drawCut(int mesh, const m3d::Vertex* v, u32 count, u32 surface, const m3d::Pose& pose);
}  // namespace m3dport
