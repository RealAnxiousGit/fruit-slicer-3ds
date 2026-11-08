#include "boom.h"

#include "render.h"
#include "sprites.h"

namespace boom {
namespace {
constexpr int kBlasts = 16, kSmoke = 10, kShards = 12, kEmbers = 20;

struct Bit {
	float x, y, vx, vy, rot, spin, size;
};
struct Blast {
	bool on;
	float x, y, size, t;
	int smoke, shards, embers;
	Bit smokeBit[kSmoke], shard[kShards], ember[kEmbers];
};
Blast sBlasts[kBlasts];
int sNext;
u32 sSeed = 12345;

float rnd() {
	sSeed = sSeed * 1664525u + 1013904223u;
	return (sSeed >> 8) / 16777216.0f;
}

// How long each part lasts, in seconds; a bigger blast burns a little longer.
float fireLife(const Blast& b) { return 0.42f + 0.06f * b.size; }
constexpr float kSmokeLife = 1.3f, kShardLife = 0.9f, kEmberLife = 0.7f, kWaveLife = 0.4f;

// Fewer pieces at lower Effects, and Calm Effects keeps it soft.
float amount() {
	static const float kByEffects[] = {0.3f, 0.5f, 0.8f, 1.0f};
	float k = kByEffects[gSave.effects & 3];
	return gSave.calmFx ? k * 0.6f : k;
}
}  // namespace

void start(float x, float y, float size, float delay) {
	Blast& b = sBlasts[sNext];
	sNext = (sNext + 1) % kBlasts;
	b = {};
	b.on = true;
	b.x = x;
	b.y = y;
	b.size = size;
	b.t = -delay;  // waits unseen until its turn
	const float k = amount(), speed = 60 + 40 * size;
	b.smoke = (int)(kSmoke * k);
	b.shards = (int)(kShards * k);
	b.embers = (int)(kEmbers * k);
	for (int i = 0; i < b.smoke; i++) {
		const float a = i * 6.2831853f / (b.smoke ? b.smoke : 1) + rnd() * 0.5f, v = (0.3f + 0.4f * rnd()) * speed;
		b.smokeBit[i] = {x, y, cosf(a) * v, sinf(a) * v * 0.6f - 20 * size, 0, 0, (14 + 10 * rnd()) * size};
	}
	for (int i = 0; i < b.shards; i++) {
		const float a = rnd() * 6.2831853f, v = (1.6f + 1.6f * rnd()) * speed;
		b.shard[i] = {x, y, cosf(a) * v, sinf(a) * v - 80 * size, rnd() * 6, (rnd() - 0.5f) * 24, (3 + 3 * rnd()) * (0.6f + 0.4f * size)};
	}
	for (int i = 0; i < b.embers; i++) {
		const float a = rnd() * 6.2831853f, v = (2 + 3 * rnd()) * speed;
		b.ember[i] = {x, y, cosf(a) * v, sinf(a) * v - 60 * size, 0, 0, 0.35f + 0.4f * rnd()};
	}
}

void update(float dt) {
#ifdef TEST_SLOWBOOM
	dt *= 0.12f;  // test builds only: slow motion, so screenshots can catch every stage
#endif
	for (Blast& b : sBlasts) {
		if (!b.on) continue;
		b.t += dt;
		if (b.t < 0) continue;
		const float g = 520 * (0.6f + 0.4f * b.size);
		for (int i = 0; i < b.smoke; i++) {
			Bit& s = b.smokeBit[i];
			s.x += s.vx * dt;
			s.y += s.vy * dt;
			s.vx *= 1 - 2.2f * dt;
			s.vy = s.vy * (1 - 2.2f * dt) - 14 * dt;  // smoke drifts up as it slows
		}
		for (int i = 0; i < b.shards; i++) {
			Bit& s = b.shard[i];
			s.x += s.vx * dt;
			s.y += s.vy * dt;
			s.vy += g * dt;
			s.rot += s.spin * dt;
		}
		for (int i = 0; i < b.embers; i++) {
			Bit& e = b.ember[i];
			e.x += e.vx * dt;
			e.y += e.vy * dt;
			e.vx *= 1 - 1.5f * dt;
			e.vy = e.vy * (1 - 1.5f * dt) + g * 0.5f * dt;
		}
		if (b.t > kSmokeLife && b.t > fireLife(b)) b.on = false;
	}
}

void draw(float ox, float oy) {
	for (const Blast& b : sBlasts) {
		if (!b.on || b.t < 0) continue;
		const float x = b.x + ox, y = b.y + oy, t = b.t;
		const float calm = gSave.calmFx ? 0.6f : 1.0f;
		// Smoke first, behind everything: dark puffs that swell and thin out.
		for (int i = 0; i < b.smoke; i++) {
			const Bit& s = b.smokeBit[i];
			const float u = clampf((t - 0.08f) / kSmokeLife, 0, 1);
			if (u <= 0 || u >= 1) continue;
			const float r = s.size * (0.6f + 1.2f * u);
			gfx::sprite(SHEET_SPRITES, sprites_glow_idx, s.x + ox, s.y + oy, r / 18, r / 18, 0, rgba(58, 52, 50, (u8)(150 * (1 - u) * (1 - u) * calm)));
		}
		// The fireball: dark red outside, then orange and yellow, with a white-hot core that goes first.
		// A few lumps round the middle billow out with it, so it never looks like a perfect circle.
		const float fl = fireLife(b), f = clampf(t / fl, 0, 1);
		if (f < 1) {
			const float grow = 1 - (1 - f) * (1 - f) * (1 - f), r = 34 * b.size * (0.35f + 0.65f * grow), fade = 1 - f * f;
			const float seed = b.x * 0.37f + b.y * 0.61f;
			for (int k = 0; k < 5; k++) {
				const float a = seed + k * 1.2566f, d = r * (0.25f + 0.2f * grow);
				const float lx = x + cosf(a) * d, ly = y + sinf(a) * d * 0.8f - r * 0.1f * f, lr = r * (0.55f + 0.1f * sinf(seed + k));
				gfx::sprite(SHEET_SPRITES, sprites_glow_idx, lx, ly, lr / 14, lr / 14, 0, rgba(190, 40, 20, (u8)(170 * fade * calm)));
				gfx::sprite(SHEET_SPRITES, sprites_glow_idx, lx, ly, lr / 19, lr / 19, 0, rgba(255, 120, 30, (u8)(220 * fade * calm)));
			}
			gfx::sprite(SHEET_SPRITES, sprites_glow_idx, x, y, r / 15, r / 15, 0, rgba(255, 140, 40, (u8)(255 * fade * calm)));
			gfx::sprite(SHEET_SPRITES, sprites_glow_idx, x, y, r / 21, r / 21, 0, rgba(255, 210, 90, (u8)(255 * fade * calm)));
			const float core = clampf(1 - t / (fl * 0.5f), 0, 1);
			if (core > 0) gfx::sprite(SHEET_SPRITES, sprites_glow_idx, x, y, r / 26, r / 26, 0, rgba(255, 255, 235, (u8)(255 * core * calm)));
		}
		// The shockwave: a thin warm ring racing out.
		const float w = t / kWaveLife;
		if (w < 1) {
			const float r = 30 * b.size * (0.4f + 2.2f * w);
			gfx::sprite(SHEET_SPRITES, sprites_ring_idx, x, y, r / 32, r / 32, 0, rgba(255, 210, 140, (u8)(170 * (1 - w) * (1 - w) * calm)));
		}
		// Pieces of the black shell, tumbling as they fall.
		for (int i = 0; i < b.shards; i++) {
			const Bit& s = b.shard[i];
			const float u = t / kShardLife;
			if (u >= 1) continue;
			const float c = cosf(s.rot) * s.size, sn = sinf(s.rot) * s.size, px = s.x + ox, py = s.y + oy;
			const u32 col = rgba(34, 30, 40, (u8)(255 * clampf((1 - u) * 3, 0, 1)));
			gfx::tri(px + c, py + sn, col, px - sn * 0.7f, py + c * 0.7f, col, px - c * 0.8f, py - sn * 0.8f, col);
		}
		// Embers: hot sparks that cool from yellow to red.
		for (int i = 0; i < b.embers; i++) {
			const Bit& e = b.ember[i];
			const float u = t / kEmberLife;
			if (u >= 1) continue;
			const u8 g = (u8)(190 - 150 * u);
			gfx::sprite(SHEET_SPRITES, sprites_spark_idx, e.x + ox, e.y + oy, e.size, e.size, 0, rgba(255, g, 30, (u8)(255 * (1 - u))));
		}
	}
}

bool active() {
	for (const Blast& b : sBlasts)
		if (b.on) return true;
	return false;
}

void clear() {
	for (Blast& b : sBlasts) b.on = false;
}
}  // namespace boom
