#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jobs.h"
#include "model3d_internal.h"
#include "port_api.h"
#include "render.h"

namespace m3d {

static PartData sParts[MESH_COUNT][3];
static void* sBlob;
static int sLoadedLod = -1;
static void* sRetiredBlob;
static int sRetiredFrames;

// A kept half of a High detail fruit reaches about 1600 vertices and 77 outline points, plus its face.
static constexpr int kCutSlots = 16;
static constexpr int kCutMaxVerts = 2450;
static Vertex* sCutVerts;
static volatile u32 sCutCount[kCutSlots];
static volatile bool sCutDirty[kCutSlots];  // built, but still sitting in the CPU's cache
static u32 sCutSurface[kCutSlots];
static int sCutMesh[kCutSlots];
static bool sCutUsed[kCutSlots];

static bool sShaderOk;

static bool loadFile(const char* path, void** out, u32* size) {
	port::AssetFile* f = port::assetOpen(path);
	if (!f) return false;
	long n = port::assetSize(f);
	void* p = m3dport::alloc(n);
	bool ok = p && port::assetRead(p, n, f) == (size_t)n;
	port::assetClose(f);
	if (!ok) {
		if (p) m3dport::release(p);
		return false;
	}
	*out = p;
	*size = (u32)n;
	return true;
}

static bool loadMeshes(int lod) {
	u32 size = 0;
	void* blob = nullptr;
	if (!loadFile(lod ? "models/fruit_hi.bin" : "models/fruit_lo.bin", &blob, &size)) return false;
	m3dport::flush(blob, size);
	const u8* base = (const u8*)blob;
	if (memcmp(base, "FSM1", 4) != 0 || *(const u32*)(base + 4) != MESH_COUNT) {
		m3dport::release(blob);
		return false;
	}
	const u32* head = (const u32*)(base + 8);
	const u8* cur = base + 8 + 20 * 3 * MESH_COUNT;
	for (int m = 0; m < MESH_COUNT; m++) {
		for (int p = 0; p < 3; p++) {
			PartData& d = sParts[m][p];
			d.nv = *head++;
			d.ni = *head++;
			d.body = *head++;
			d.flesh = *head++;
			d.rim = *head++;
			d.v = (const Vertex*)cur;
			cur += d.nv * sizeof(Vertex);
			d.idx = (const u16*)cur;
			cur += (d.ni * 2 + 3) & ~3u;
		}
	}
	// The old meshes may still be in use by the GPU, so they're freed a few frames later.
	sRetiredBlob = sBlob;
	sRetiredFrames = 3;
	sBlob = blob;
	sLoadedLod = lod;
	return true;
}

bool init() {
	sShaderOk = m3dport::init();
	return sShaderOk;
}

void shutdown() {
	jobs::wait();
	if (sCutVerts) m3dport::release(sCutVerts);
	if (sRetiredBlob) m3dport::release(sRetiredBlob);
	if (sBlob) m3dport::release(sBlob);
	m3dport::shutdown();
	sShaderOk = false;
}

void sync() {
	if (sRetiredBlob && --sRetiredFrames <= 0) {
		m3dport::release(sRetiredBlob);
		sRetiredBlob = nullptr;
	}
	if (!sShaderOk || sRetiredBlob) return;
	if (gSave.artStyle == ART_2D) {
		if (sBlob) {
			jobs::wait();
			sRetiredBlob = sBlob;
			sRetiredFrames = 3;
			sBlob = nullptr;
			sLoadedLod = -1;
		}
		return;
	}
	if (sLoadedLod != gSave.detail) {
		jobs::wait();
		if (!loadMeshes(gSave.detail)) {
			fprintf(stderr, "[m3d] could not load meshes, using 2D\n");
			gSave.artStyle = ART_2D;
		}
	}
}

bool ready() { return sShaderOk && sBlob != nullptr; }
bool usable() { return sShaderOk; }

void begin(float screenW, float stereo) { m3dport::begin(screenW, stereo); }
void end() { m3dport::end(); }

void draw(int mesh, Part part, const Pose& pose) {
	if (!sBlob) return;
	const PartData& d = sParts[mesh][part];
	if (d.ni == 0) return;
	m3dport::drawIndexed(mesh, d, pose);
}

// ---------------------------------------------------------------- exact cuts
int cutAcquire() {
	if (!sCutVerts) {
		sCutVerts = (Vertex*)m3dport::alloc(sizeof(Vertex) * kCutMaxVerts * kCutSlots);
		if (!sCutVerts) return -1;
	}
	for (int i = 0; i < kCutSlots; i++) {
		if (!sCutUsed[i]) {
			sCutUsed[i] = true;
			sCutCount[i] = 0;
			return i;
		}
	}
	return -1;
}

void cutRelease(int handle) {
	if (handle >= 0 && handle < kCutSlots) sCutUsed[handle] = false;
}

void cutReleaseAll() { memset(sCutUsed, 0, sizeof sCutUsed); }

static inline Vertex mixVertex(const Vertex& a, const Vertex& b, float t) {
	Vertex o;
	o.x = a.x + (b.x - a.x) * t;
	o.y = a.y + (b.y - a.y) * t;
	o.z = a.z + (b.z - a.z) * t;
	o.nx = (s8)(a.nx + (b.nx - a.nx) * t);
	o.ny = (s8)(a.ny + (b.ny - a.ny) * t);
	o.nz = (s8)(a.nz + (b.nz - a.nz) * t);
	o.pad = 0;
	o.r = (u8)(a.r + (b.r - a.r) * t);
	o.g = (u8)(a.g + (b.g - a.g) * t);
	o.b = (u8)(a.b + (b.b - a.b) * t);
	o.a = 255;
	return o;
}

static void cutBuild(int handle, int mesh, float nx, float ny, float nz, float offset) {
	const PartData& d = sParts[mesh][PART_WHOLE];
	Vertex* out = sCutVerts + handle * kCutMaxVerts;
	u32 n = 0;
	const u32 room = kCutMaxVerts - 800;  // keeps space for the cut face

	static constexpr int kMaxEdge = 84;
	float ex[kMaxEdge], ey[kMaxEdge], ez[kMaxEdge];
	int edges = 0;

	for (u32 t = 0; t + 2 < d.ni && n + 6 < room; t += 3) {
		const Vertex* tri[3] = {&d.v[d.idx[t]], &d.v[d.idx[t + 1]], &d.v[d.idx[t + 2]]};
		float dist[3];
		for (int k = 0; k < 3; k++) dist[k] = tri[k]->x * nx + tri[k]->y * ny + tri[k]->z * nz - offset;
		Vertex poly[4];
		int count = 0;
		for (int k = 0; k < 3; k++) {
			int j = (k + 1) % 3;
			if (dist[k] >= 0 && count < 4) poly[count++] = *tri[k];
			if ((dist[k] > 0) != (dist[j] > 0)) {
				Vertex mid = mixVertex(*tri[k], *tri[j], dist[k] / (dist[k] - dist[j]));
				if (count < 4) poly[count++] = mid;
				// Each outline point once, and only from the fruit itself: stems stay out of the face.
				if (dist[k] > 0 && t < d.body && edges < kMaxEdge) {
					ex[edges] = mid.x;
					ey[edges] = mid.y;
					ez[edges] = mid.z;
					edges++;
				}
			}
		}
		for (int k = 1; k + 1 < count; k++) {
			out[n++] = poly[0];
			out[n++] = poly[k];
			out[n++] = poly[k + 1];
		}
	}

	sCutSurface[handle] = n;
	sCutMesh[handle] = mesh;

	// Close the fruit with a coloured face: rind ring, flesh and a lighter centre.
	if (edges >= 3) {
		float cx = 0, cy = 0, cz = 0;
		for (int i = 0; i < edges; i++) {
			cx += ex[i];
			cy += ey[i];
			cz += ez[i];
		}
		cx /= edges;
		cy /= edges;
		cz /= edges;
		// Two axes in the plane, to sort the outline by angle.
		float ux = fabsf(nx) < 0.8f ? 1.0f : 0.0f, uy = fabsf(nx) < 0.8f ? 0.0f : 1.0f, uz = 0;
		float dot = ux * nx + uy * ny + uz * nz;
		ux -= nx * dot;
		uy -= ny * dot;
		uz -= nz * dot;
		float wx = ny * uz - nz * uy, wy = nz * ux - nx * uz, wz = nx * uy - ny * ux;
		float ang[kMaxEdge];
		for (int i = 0; i < edges; i++) {
			float px = ex[i] - cx, py = ey[i] - cy, pz = ez[i] - cz;
			ang[i] = atan2f(px * wx + py * wy + pz * wz, px * ux + py * uy + pz * uz);
		}
		for (int i = 1; i < edges; i++) {
			float a = ang[i], x = ex[i], y = ey[i], z = ez[i];
			int j = i - 1;
			while (j >= 0 && ang[j] > a) {
				ang[j + 1] = ang[j];
				ex[j + 1] = ex[j];
				ey[j + 1] = ey[j];
				ez[j + 1] = ez[j];
				j--;
			}
			ang[j + 1] = a;
			ex[j + 1] = x;
			ey[j + 1] = y;
			ez[j + 1] = z;
		}
		const u32 flesh = d.flesh, rim = d.rim;
		auto vert = [&](float x, float y, float z, u32 c, float lighten) {
			Vertex o;
			o.x = x;
			o.y = y;
			o.z = z;
			o.nx = (s8)(-nx * 127);
			o.ny = (s8)(-ny * 127);
			o.nz = (s8)(-nz * 127);
			o.pad = 0;
			u8 r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
			o.r = (u8)(r + (255 - r) * lighten);
			o.g = (u8)(g + (255 - g) * lighten);
			o.b = (u8)(b + (255 - b) * lighten);
			o.a = 255;
			return o;
		};
		for (int i = 0; i < edges && n + 9 < kCutMaxVerts; i++) {
			int k = (i + 1) % edges;
			float ix = cx + (ex[i] - cx) * 0.86f, iy = cy + (ey[i] - cy) * 0.86f, iz = cz + (ez[i] - cz) * 0.86f;
			float jx = cx + (ex[k] - cx) * 0.86f, jy = cy + (ey[k] - cy) * 0.86f, jz = cz + (ez[k] - cz) * 0.86f;
			out[n++] = vert(ex[i], ey[i], ez[i], rim, 0);
			out[n++] = vert(ix, iy, iz, flesh, 0);
			out[n++] = vert(ex[k], ey[k], ez[k], rim, 0);
			out[n++] = vert(ex[k], ey[k], ez[k], rim, 0);
			out[n++] = vert(ix, iy, iz, flesh, 0);
			out[n++] = vert(jx, jy, jz, flesh, 0);
			out[n++] = vert(ix, iy, iz, flesh, 0);
			out[n++] = vert(cx, cy, cz, flesh, 0.35f);
			out[n++] = vert(jx, jy, jz, flesh, 0);
		}
	}
	sCutCount[handle] = n;
	sCutDirty[handle] = true;
}

struct CutJob {
	int handle, mesh;
	float nx, ny, nz, offset;
};
static CutJob sCutJobs[32];
static int sCutJobNext;

static void cutJobMain(void* arg) {
	const CutJob* j = (const CutJob*)arg;
	cutBuild(j->handle, j->mesh, j->nx, j->ny, j->nz, j->offset);
}

void cutRequest(int handle, int mesh, float nx, float ny, float nz, float offset) {
	if (handle < 0 || !sBlob) return;
	CutJob* j = &sCutJobs[sCutJobNext];
	sCutJobNext = (sCutJobNext + 1) % 32;
	*j = {handle, mesh, nx, ny, nz, offset};
	jobs::run(cutJobMain, j);
}

void drawCut(int handle, const Pose& pose) {
	if (handle < 0 || sCutCount[handle] == 0) return;
	if (sCutDirty[handle]) {
		// A real console's GPU reads memory directly, so it would otherwise see stale vertices. Emulators hide this.
		sCutDirty[handle] = false;
		m3dport::flush(sCutVerts + handle * kCutMaxVerts, sCutCount[handle] * sizeof(Vertex));
	}
	m3dport::drawCut(sCutMesh[handle], sCutVerts + handle * kCutMaxVerts, sCutCount[handle], sCutSurface[handle], pose);
}

}  // namespace m3d
