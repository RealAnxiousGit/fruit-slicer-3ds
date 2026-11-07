#include "render.h"

#include <stdio.h>
#include <string.h>

#include "model3d.h"

namespace gfx {
u32 drawCalls = 0;
u32 drawCallsLive = 0;

static C3D_RenderTarget* sTopL;
static C3D_RenderTarget* sTopR;
static C3D_RenderTarget* sBottom;
static C2D_SpriteSheet sSheets[SHEET_COUNT];
static bool sOnTop;
static int sEye;
static float sIod;
static float sDepthX;
static float sShakeX, sShakeY;
static float sBaseX, sBaseY;
static bool sRightEye;
static bool sForceStereo;

// Sheets being replaced stay alive until the GPU can no longer be reading them.
static C2D_SpriteSheet sRetired[3];
static int sRetiredFrames[3];
static int sLoadedScene = -1, sLoadedBoard = -1;

// Pixels of total disparity at full slider per unit of depth.
static constexpr float kMaxDisparity = 5.0f;
static constexpr float kZ = 0.5f;
static constexpr u32 kMaxObjects = 3072;

static C2D_SpriteSheet load(const char* p) {
	C2D_SpriteSheet s = C2D_SpriteSheetLoad(p);
	if (!s) {
		fprintf(stderr, "[gfx] failed to load %s\n", p);
		return nullptr;
	}
	C2D_Image first = C2D_SpriteSheetGetImage(s, 0);
	C3D_TexSetFilter(first.tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(first.tex, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
	return s;
}

static void retire(C2D_SpriteSheet s) {
	if (!s) return;
	for (int i = 0; i < 3; i++) {
		if (!sRetired[i]) {
			sRetired[i] = s;
			sRetiredFrames[i] = 3;
			return;
		}
	}
	// All slots waiting: this is the rare case of a very fast setting change, so wait it out.
	C3D_FrameSync();
	C3D_FrameSync();
	C2D_SpriteSheetFree(s);
}

bool init() {
	gfxInitDefault();
	gfxSet3D(true);
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(kMaxObjects);
	C2D_Prepare();
	// Sprites keep painter's order. The depth buffer belongs to the 3D fruit alone.
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	sTopL = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	sTopR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	sBottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	sSheets[SHEET_FONT] = load("romfs:/gfx/font.t3x");
	sSheets[SHEET_SPRITES] = load("romfs:/gfx/sprites.t3x");
	sSheets[SHEET_LOGO] = load("romfs:/gfx/logo.t3x");
	// The loading screen already wears the player's chosen scenery and board.
	sync();
	sync();
	return sSheets[SHEET_FONT] && sSheets[SHEET_SPRITES] && sSheets[SHEET_LOGO] && sSheets[SHEET_BGTOP] && sSheets[SHEET_BOARD];
}

int loadStepCount() { return 1; }

void loadStep(int i) {
	(void)i;
	sync();  // one sheet per call, so the loading bar keeps moving
}

void sync() {
	for (int i = 0; i < 3; i++) {
		if (sRetired[i] && --sRetiredFrames[i] <= 0) {
			C2D_SpriteSheetFree(sRetired[i]);
			sRetired[i] = nullptr;
		}
	}
	char path[40];
	if (sLoadedScene != gSave.scene) {
		snprintf(path, sizeof path, "romfs:/gfx/bgtop%d.t3x", gSave.scene);
		C2D_SpriteSheet next = load(path);
		if (next) {
			retire(sSheets[SHEET_BGTOP]);
			sSheets[SHEET_BGTOP] = next;
		}
		sLoadedScene = gSave.scene;
		return;
	}
	if (sLoadedBoard != gSave.board) {
		snprintf(path, sizeof path, "romfs:/gfx/board%d.t3x", gSave.board);
		C2D_SpriteSheet next = load(path);
		if (next) {
			retire(sSheets[SHEET_BOARD]);
			sSheets[SHEET_BOARD] = next;
		}
		sLoadedBoard = gSave.board;
		return;
	}
	bool wantFruit = gSave.artStyle == 0 || !m3d::usable();
	if (wantFruit && !sSheets[SHEET_FRUIT]) {
		sSheets[SHEET_FRUIT] = load("romfs:/gfx/fruit.t3x");
	} else if (!wantFruit && sSheets[SHEET_FRUIT]) {
		retire(sSheets[SHEET_FRUIT]);
		sSheets[SHEET_FRUIT] = nullptr;
	}
}

void shutdown() {
	for (auto& s : sRetired)
		if (s) C2D_SpriteSheetFree(s);
	for (auto& s : sSheets)
		if (s) C2D_SpriteSheetFree(s);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
}

void forceStereo(bool on) { sForceStereo = on; }

void beginFrame() {
	float slider = sForceStereo ? 1.0f : osGet3DSliderState();
	sIod = slider * (gSave.depth / 50.0f);
	sRightEye = slider > 0.0f;
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C2D_SetTintMode(C2D_TintMult);
	drawCallsLive = 0;
}

void endFrame() {
#ifdef AUTOTEST
	// How close each frame comes to the GPU's limits, worst of the last second.
	static float sPeakCmd;
	static u32 sPeakDraws, sLogFrames;
	u32* cmdAddr;
	u32 cmdSize, cmdUsed;
	GPUCMD_GetBuffer(&cmdAddr, &cmdSize, &cmdUsed);
	const u32 kCmdWords = C3D_DEFAULT_CMDBUF_SIZE / 4;
	float used = (float)(kCmdWords - (cmdSize - cmdUsed)) / kCmdWords;  // the whole frame, not just the last screen
	if (used > sPeakCmd) sPeakCmd = used;
	if (drawCallsLive > sPeakDraws) sPeakDraws = drawCallsLive;
	if (++sLogFrames == 60) {
		fprintf(stderr, "[gpu] cmd %.1f%% draws %lu linear %lu KB\n", sPeakCmd * 100, (unsigned long)sPeakDraws, (unsigned long)(linearSpaceFree() / 1024));
		sPeakCmd = 0;
		sPeakDraws = 0;
		sLogFrames = 0;
	}
#endif
	C3D_FrameEnd(0);
	drawCalls = drawCallsLive;
}

bool beginTop(int eye) {
	// The right eye is still drawn at zero depth while the slider is up, just without any offset.
	if (eye == 1 && !sRightEye) return false;
	C3D_RenderTarget* t = eye == 0 ? sTopL : sTopR;
	C2D_TargetClear(t, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(t);
	sOnTop = true;
	sEye = eye;
	sDepthX = 0;
	return true;
}

void beginBottom() {
	C2D_TargetClear(sBottom, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(sBottom);
	sOnTop = false;
	sDepthX = 0;
}

bool onTop() { return sOnTop; }

void setDepth(float depth) {
	if (!sOnTop) return;
	// A plane in front of the screen is shifted right for the left eye, left for the right eye.
	float half = sIod * kMaxDisparity * depth * 0.5f;
	sDepthX = sEye == 0 ? half : -half;
}

float stereoPerZ() {
	if (!sOnTop) return 0;
	// A fruit's radius is about 23, so this puts a clear couple of pixels of roundness in each eye.
	float k = sIod * 0.1f;
	return sEye == 0 ? k : -k;
}

void setShake(float dx, float dy) {
	sShakeX = dx;
	sShakeY = dy;
}

void setBase(float dx, float dy) {
	sBaseX = dx;
	sBaseY = dy;
}

void getOffset(float* x, float* y) {
	*x = sBaseX + (sOnTop ? 0 : sShakeX);
	*y = sBaseY + (sOnTop ? 0 : sShakeY);
}

static inline float offX() { return sBaseX + sDepthX + (sOnTop ? 0 : sShakeX); }
static inline float offY() { return sBaseY + (sOnTop ? 0 : sShakeY); }

void sprite(SheetId sheet, int idx, float x, float y, float sx, float sy, float rot, u32 color) {
	if (!sSheets[sheet] || !gpuHasRoom()) return;
	C2D_Image img = C2D_SpriteSheetGetImage(sSheets[sheet], idx);
	float w = img.subtex->width * sx;
	float h = img.subtex->height * sy;
	C2D_DrawParams p;
	p.pos = {x + offX(), y + offY(), w, h};
	p.center = {w * 0.5f, h * 0.5f};
	p.depth = kZ;
	p.angle = rot;
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, color, 1.0f);
	C2D_DrawImage(img, &p, &tint);
	drawCallsLive++;
}

void image(SheetId sheet, int idx, float x, float y, float w, float h, u32 color) {
	if (!sSheets[sheet] || !gpuHasRoom()) return;
	C2D_Image img = C2D_SpriteSheetGetImage(sSheets[sheet], idx);
	C2D_DrawParams p;
	p.pos = {x + offX(), y + offY(), w, h};
	p.center = {0, 0};
	p.depth = kZ;
	p.angle = 0;
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, color, 1.0f);
	C2D_DrawImage(img, &p, &tint);
	drawCallsLive++;
}

void imagePart(SheetId sheet, int idx, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 color) {
	if (!sSheets[sheet] || !gpuHasRoom()) return;
	C2D_Image img = C2D_SpriteSheetGetImage(sSheets[sheet], idx);
	const Tex3DS_SubTexture& s = *img.subtex;
	Tex3DS_SubTexture part = s;
	part.left = s.left + (s.right - s.left) * u0;
	part.right = s.left + (s.right - s.left) * u1;
	part.top = s.top + (s.bottom - s.top) * v0;
	part.bottom = s.top + (s.bottom - s.top) * v1;
	img.subtex = &part;
	C2D_DrawParams p;
	p.pos = {x + offX(), y + offY(), w, h};
	p.center = {0, 0};
	p.depth = kZ;
	p.angle = 0;
	// libctru reads top < bottom as "rotated in the atlas", and a negative height is culled. So a
	// mirror in v is a half turn with u reversed, which leaves the winding alone.
	if (v0 > v1) {
		part.top = s.top + (s.bottom - s.top) * v1;
		part.bottom = s.top + (s.bottom - s.top) * v0;
		part.left = s.left + (s.right - s.left) * u1;
		part.right = s.left + (s.right - s.left) * u0;
		p.pos.x += w / 2;
		p.pos.y += h / 2;
		p.center = {w / 2, h / 2};
		p.angle = kPi;
	}
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, color, 1.0f);
	C2D_DrawImage(img, &p, &tint);
	drawCallsLive++;
}

void rect(float x, float y, float w, float h, u32 color) {
	C2D_DrawRectSolid(x + offX(), y + offY(), kZ, w, h, color);
	drawCallsLive++;
}

void tri(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2, float y2, u32 c2) {
	float ox = offX(), oy = offY();
	C2D_DrawTriangle(x0 + ox, y0 + oy, c0, x1 + ox, y1 + oy, c1, x2 + ox, y2 + oy, c2, kZ);
	drawCallsLive++;
}

}  // namespace gfx
