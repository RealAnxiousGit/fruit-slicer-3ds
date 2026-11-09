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
	const u32 headBytes = 8 + 20 * 3 * MESH_COUNT;
	if (size < headBytes || memcmp(base, "FSM1", 4) != 0 || *(const u32*)(base + 4) != MESH_COUNT) {
		fprintf(stderr, "[m3d] model file doesn't match this build\n");
		m3dport::release(blob);
		return false;
	}
	// Check every part fits in the file and every index names a real vertex before using any of it,
	// so a stale or cut-short file falls back to 2D instead of reading past its end.
	{
		const u32* head = (const u32*)(base + 8);
		u32 at = headBytes;
		for (int i = 0; i < MESH_COUNT * 3; i++, head += 5) {
			const u32 nv = head[0], ni = head[1];
			const u32 vBytes = nv * (u32)sizeof(Vertex), iBytes = (ni * 2 + 3) & ~3u;
			if (nv > 65535 || ni > 0x100000 || at + vBytes + iBytes > size) {
				fprintf(stderr, "[m3d] model file is damaged\n");
				m3dport::release(blob);
				return false;
			}
			const u16* idx = (const u16*)(base + at + vBytes);
			for (u32 k = 0; k < ni; k++) {
				if (idx[k] >= nv) {
					fprintf(stderr, "[m3d] model file is damaged\n");
					m3dport::release(blob);
					return false;
				}
			}
			at += vBytes + iBytes;
		}
	}
	const u32* head = (const u32*)(base + 8);
	const u8* cur = base + headBytes;
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

void begin(float screenW, float stereo, bool field) { m3dport::begin(screenW, stereo, field); }
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

// The cut outline arrives as one short segment per triangle. Segments are joined end to end into
// closed outlines, one per piece the plane passes through (each grape, each cherry), and each gets
// its own face. Worked out the same way as the ready-made halves in tools/gen_models.py.
static constexpr int kMaxSeg = 256;
struct CutSeg {
	float a[3], b[3];
};

static inline bool samePoint(const float* p, const float* q) {
	return fabsf(p[0] - q[0]) < 1e-3f && fabsf(p[1] - q[1]) < 1e-3f && fabsf(p[2] - q[2]) < 1e-3f;
}

static void cutBuild(int handle, int mesh, float nx, float ny, float nz, float offset) {
	const PartData& d = sParts[mesh][PART_WHOLE];
	Vertex* out = sCutVerts + handle * kCutMaxVerts;
	u32 n = 0;
	const u32 room = kCutMaxVerts - 800;  // keeps space for the cut faces

	CutSeg seg[kMaxSeg];
	int segs = 0;
	// A hair off the requested plane, so no point (a fruit's top or bottom) lies exactly on it.
	offset -= 1e-4f;

	for (u32 t = 0; t + 2 < d.ni && n + 6 < room; t += 3) {
		const Vertex* tri[3] = {&d.v[d.idx[t]], &d.v[d.idx[t + 1]], &d.v[d.idx[t + 2]]};
		float dist[3];
		for (int k = 0; k < 3; k++) dist[k] = tri[k]->x * nx + tri[k]->y * ny + tri[k]->z * nz - offset;
		Vertex poly[4];
		int count = 0, cuts = 0;
		float cut[2][3];
		for (int k = 0; k < 3; k++) {
			int j = (k + 1) % 3;
			if (dist[k] >= 0 && count < 4) poly[count++] = *tri[k];
			if ((dist[k] > 0) != (dist[j] > 0)) {
				// Worked out from the same end every time, so neighbouring triangles agree exactly.
				int a = k, b = j;
				if (tri[b]->x < tri[a]->x || (tri[b]->x == tri[a]->x && (tri[b]->y < tri[a]->y || (tri[b]->y == tri[a]->y && tri[b]->z < tri[a]->z)))) {
					a = j;
					b = k;
				}
				Vertex mid = mixVertex(*tri[a], *tri[b], dist[a] / (dist[a] - dist[b]));
				if (count < 4) poly[count++] = mid;
				if (cuts < 2) {
					cut[cuts][0] = mid.x;
					cut[cuts][1] = mid.y;
					cut[cuts][2] = mid.z;
					cuts++;
				}
			}
		}
		for (int k = 1; k + 1 < count; k++) {
			out[n++] = poly[0];
			out[n++] = poly[k];
			out[n++] = poly[k + 1];
		}
		// Only the fruit itself makes faces: stems and leaves stay out of them.
		if (cuts == 2 && t < d.body && segs < kMaxSeg) {
			memcpy(seg[segs].a, cut[0], sizeof cut[0]);
			memcpy(seg[segs].b, cut[1], sizeof cut[1]);
			segs++;
		}
	}

	sCutSurface[handle] = n;
	sCutMesh[handle] = mesh;

	// Two axes in the plane, so every outline turns the same way.
	float ux = fabsf(nx) < 0.8f ? 1.0f : 0.0f, uy = fabsf(nx) < 0.8f ? 0.0f : 1.0f, uz = 0;
	float dot = ux * nx + uy * ny + uz * nz;
	ux -= nx * dot;
	uy -= ny * dot;
	uz -= nz * dot;
	float ul = sqrtf(ux * ux + uy * uy + uz * uz);
	ux /= ul;
	uy /= ul;
	uz /= ul;
	float wx = ny * uz - nz * uy, wy = nz * ux - nx * uz, wz = nx * uy - ny * ux;
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

	static constexpr int kMaxRing = 96;
	bool used[kMaxSeg] = {};
	int faces = 0;
	for (int start = 0; start < segs; start++) {
		if (used[start]) continue;
		used[start] = true;
		float ring[kMaxRing][3];
		int len = 0;
		memcpy(ring[len++], seg[start].a, sizeof ring[0]);
		memcpy(ring[len++], seg[start].b, sizeof ring[0]);
		const float* cur = seg[start].b;
		for (;;) {
			int next = -1;
			const float* other = nullptr;
			for (int k = 0; k < segs && next < 0; k++) {
				if (used[k]) continue;
				if (samePoint(seg[k].a, cur)) next = k, other = seg[k].b;
				else if (samePoint(seg[k].b, cur)) next = k, other = seg[k].a;
			}
			if (next < 0) break;
			used[next] = true;
			cur = other;
			if (samePoint(cur, ring[0])) break;
			// Points closer than this add triangles and nothing anyone can see.
			float dx = cur[0] - ring[len - 1][0], dy = cur[1] - ring[len - 1][1], dz = cur[2] - ring[len - 1][2];
			if (dx * dx + dy * dy + dz * dz > 0.36f && len < kMaxRing) memcpy(ring[len++], cur, sizeof ring[0]);
		}
		if (len < 3) continue;
		float cx = 0, cy = 0, cz = 0;
		for (int i = 0; i < len; i++) {
			cx += ring[i][0];
			cy += ring[i][1];
			cz += ring[i][2];
		}
		cx /= len;
		cy /= len;
		cz /= len;
		// Same turning direction for every outline (as the angle-sorted outline had before).
		float area = 0;
		for (int i = 0; i < len; i++) {
			const float* p = ring[i];
			const float* q = ring[(i + 1) % len];
			float pu = (p[0] - cx) * ux + (p[1] - cy) * uy + (p[2] - cz) * uz, pw = (p[0] - cx) * wx + (p[1] - cy) * wy + (p[2] - cz) * wz;
			float qu = (q[0] - cx) * ux + (q[1] - cy) * uy + (q[2] - cz) * uz, qw = (q[0] - cx) * wx + (q[1] - cy) * wy + (q[2] - cz) * wz;
			area += pu * qw - qu * pw;
		}
		// Later faces sit a hair in front, so overlapping pieces (a grape bunch) don't flicker.
		float lift = 0.04f * faces++;
		float lx = -nx * lift, ly = -ny * lift, lz = -nz * lift;
		for (int s = 0; s < len && n + 9 < kCutMaxVerts; s++) {
			int i = area >= 0 ? s : len - 1 - s;
			int k = area >= 0 ? (s + 1) % len : (len - 1 - (s + 1) % len);
			const float* p = ring[i];
			const float* q = ring[k];
			float ix = cx + (p[0] - cx) * 0.86f + lx, iy = cy + (p[1] - cy) * 0.86f + ly, iz = cz + (p[2] - cz) * 0.86f + lz;
			float jx = cx + (q[0] - cx) * 0.86f + lx, jy = cy + (q[1] - cy) * 0.86f + ly, jz = cz + (q[2] - cz) * 0.86f + lz;
			out[n++] = vert(p[0] + lx, p[1] + ly, p[2] + lz, rim, 0);
			out[n++] = vert(ix, iy, iz, flesh, 0);
			out[n++] = vert(q[0] + lx, q[1] + ly, q[2] + lz, rim, 0);
			out[n++] = vert(q[0] + lx, q[1] + ly, q[2] + lz, rim, 0);
			out[n++] = vert(ix, iy, iz, flesh, 0);
			out[n++] = vert(jx, jy, jz, flesh, 0);
			out[n++] = vert(ix, iy, iz, flesh, 0);
			out[n++] = vert(cx + lx, cy + ly, cz + lz, flesh, 0.35f);
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
