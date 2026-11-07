// Nintendo 3DS 3D fruit: citro3D with a Picasso vertex shader, in between citro2D's 2D layers.
#include <stdio.h>
#include <stdlib.h>

#include "model3d_internal.h"
#include "port_api.h"
#include "render.h"

namespace m3dport {
using m3d::PartData;
using m3d::Pose;
using m3d::Vertex;

static DVLB_s* sDvlb;
static void* sShaderData;
static shaderProgram_s sProgram;
static C3D_AttrInfo sAttr;
static int sLocProj, sLocMv, sLocLight, sLocHalf, sLocShade;

bool init() {
	port::AssetFile* f = port::assetOpen("gfx/fruit.shbin");
	if (!f) return false;
	long size = port::assetSize(f);
	sShaderData = malloc(size);
	bool ok = sShaderData && port::assetRead(sShaderData, size, f) == (size_t)size;
	port::assetClose(f);
	if (!ok) {
		free(sShaderData);
		sShaderData = nullptr;
		return false;
	}
	sDvlb = DVLB_ParseFile((u32*)sShaderData, size);
	shaderProgramInit(&sProgram);
	shaderProgramSetVsh(&sProgram, &sDvlb->DVLE[0]);
	sLocProj = shaderInstanceGetUniformLocation(sProgram.vertexShader, "projection");
	sLocMv = shaderInstanceGetUniformLocation(sProgram.vertexShader, "modelView");
	sLocLight = shaderInstanceGetUniformLocation(sProgram.vertexShader, "light");
	sLocHalf = shaderInstanceGetUniformLocation(sProgram.vertexShader, "halfVec");
	sLocShade = shaderInstanceGetUniformLocation(sProgram.vertexShader, "shade");
	AttrInfo_Init(&sAttr);
	AttrInfo_AddLoader(&sAttr, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(&sAttr, 1, GPU_BYTE, 4);
	AttrInfo_AddLoader(&sAttr, 2, GPU_UNSIGNED_BYTE, 4);
	return true;
}

void shutdown() {
	if (sDvlb) {
		shaderProgramFree(&sProgram);
		DVLB_Free(sDvlb);
	}
	if (sShaderData) free(sShaderData);
}

void* alloc(u32 bytes) { return linearAlloc(bytes); }
void release(void* p) { linearFree(p); }
void flush(const void* p, u32 bytes) { GSPGPU_FlushDataCache(p, bytes); }

void begin(float screenW, float stereo) {
	C2D_Flush();
	C3D_BindProgram(&sProgram);
	C3D_SetAttrInfo(&sAttr);
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	C3D_CullFace(GPU_CULL_NONE);

	C3D_Mtx ortho, shear, proj;
	Mtx_OrthoTilt(&ortho, 0, screenW, 240, 0, 300, -300, true);
	Mtx_Identity(&shear);
	shear.r[0].z = stereo;  // each vertex shifts by its own z, so depth is true volume for this eye
	Mtx_Multiply(&proj, &ortho, &shear);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, sLocProj, &proj);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, sLocShade, 0.62f, 0.5f, 0.32f, 0);
}

void end() {
	C2D_Prepare();
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
}

static void setPose(const Pose& p) {
	C3D_Mtx mv;
	float ox, oy;
	gfx::getOffset(&ox, &oy);
	Mtx_Identity(&mv);
	Mtx_Translate(&mv, p.x + ox, p.y + oy, p.z, true);
	if (p.tilt != 0) {
		C3D_FVec axis = FVec3_New(p.axisX, p.axisY, 0);
		Mtx_Rotate(&mv, axis, p.tilt, true);
	}
	Mtx_RotateZ(&mv, p.rz, true);
	Mtx_RotateX(&mv, p.rx, true);
	Mtx_RotateY(&mv, p.ry, true);
	Mtx_Scale(&mv, p.scale, p.scale, p.scale);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, sLocMv, &mv);
	// Normals pick up the model's scale; shrinking the light by the same amount cancels it out.
	float k = 1.0f / (p.scale > 0.01f ? p.scale : 0.01f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, sLocLight, -0.46f * k, -0.62f * k, 0.64f * k, 0);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, sLocHalf, -0.25f * k, -0.34f * k, 0.9f * k, 0);
}

void drawIndexed(int, const PartData& d, const Pose& pose) {
	if (!gpuHasRoom()) return;
	setPose(pose);
	C3D_BufInfo* buf = C3D_GetBufInfo();
	BufInfo_Init(buf);
	BufInfo_Add(buf, d.v, sizeof(Vertex), 3, 0x210);
	C3D_DrawElements(GPU_TRIANGLES, d.ni, C3D_UNSIGNED_SHORT, d.idx);
	gfx::drawCallsLive++;
}

void drawCut(int, const Vertex* v, u32 count, u32, const Pose& pose) {
	if (!gpuHasRoom()) return;
	setPose(pose);
	C3D_BufInfo* buf = C3D_GetBufInfo();
	BufInfo_Init(buf);
	BufInfo_Add(buf, v, sizeof(Vertex), 3, 0x210);
	C3D_DrawArrays(GPU_TRIANGLES, 0, count);
	gfx::drawCallsLive++;
}
}  // namespace m3dport
