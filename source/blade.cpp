#include "blade.h"

#include "render.h"

static constexpr float kTrailLife = 0.17f;   // the plain stroke, with no Shop trail
static constexpr float kStyledLife = 0.3f;   // a Shop trail lingers a little longer behind the blade
static float trailLife();
static constexpr float kMaxHalfWidth = 4.2f;

void Blade::reset() {
	count = head = hn = 0;
	active = false;
	speed = 0;
	strokeId++;
}

void Blade::pushPoint(float nx, float ny) {
	trail[head] = {nx, ny, 0};
	head = (head + 1) % kTrail;
	if (count < kTrail) count++;
}

void Blade::update(const Input& in) {
	for (int i = 0; i < count; i++) trail[(head - 1 - i + kTrail) % kTrail].age += kDt;
	while (count > 0 && trail[(head - count + kTrail) % kTrail].age > trailLife()) count--;
	if (swishCooldown > 0) swishCooldown -= kDt;

	strokeStart = false;
	px = x;
	py = y;
	if (!in.touching) {
		active = false;
		speed = 0;
		hn = 0;
		return;
	}

	rawX = (float)in.tx;
	rawY = (float)in.ty;
	bool jump = active && (fabsf(rawX - x) > kJumpReject || fabsf(rawY - y) > kJumpReject);
	if (!active || jump) {
		// A new contact (or a pressure spike) restarts the stroke with no filter lag.
		x = px = rawX;
		y = py = rawY;
		active = true;
		strokeStart = true;
		strokeId++;
		hn = 0;
		speed = 0;
		pushPoint(x, y);
		return;
	}

	// Speed-adaptive low-pass: heavy smoothing when slow removes jitter,
	// almost none when fast keeps the blade under the stylus.
	float dx = rawX - x, dy = rawY - y;
	float dist = sqrtf(dx * dx + dy * dy);
	// The sensitivity slider sets how much of that smoothing there is. The panel's own accuracy can't
	// change, but less filtering means the blade follows smaller, quicker movements.
	const float sensitivity = gSave.stylus / 100.0f;
	const float floorAlpha = 0.2f + 0.6f * sensitivity;
	float alpha = clampf(floorAlpha + dist / 20.0f, floorAlpha, 1.0f);
	alpha *= 1.0f - 0.75f * gBladeDrag;
	x += dx * alpha;
	y += dy * alpha;
	float mx = x - px, my = y - py;
	speed = sqrtf(mx * mx + my * my);

	if (speed < lerpf(0.9f, 0.15f, sensitivity)) return;  // ignore tiny drift for the trail

	// Keep the last three samples and insert one Catmull-Rom midpoint for curvature.
	if (hn == 3) {
		hx[0] = hx[1]; hy[0] = hy[1];
		hx[1] = hx[2]; hy[1] = hy[2];
		hn = 2;
	}
	hx[hn] = x;
	hy[hn] = y;
	hn++;
	if (hn == 3) {
		float qx = hx[1] + (hx[2] - hx[0]) * 0.125f;
		float qy = hy[1] + (hy[2] - hy[0]) * 0.125f;
		float mx2 = (hx[1] + hx[2]) * 0.5f + (qx - hx[1]) * 0.5f;
		float my2 = (hy[1] + hy[2]) * 0.5f + (qy - hy[1]) * 0.5f;
		pushPoint(mx2, my2);
	}
	pushPoint(x, y);
}

float gBladeDull = 0, gBladeDrag = 0;

u32 hueColor(float t);

static u32 mixCol(u32 a, u32 b, float t) {
	auto ch = [&](int sh) { return (u8)(((a >> sh) & 255) + ((((b >> sh) & 255) - (int)((a >> sh) & 255)) * t)); };
	return rgba(ch(0), ch(8), ch(16));
}

// The Shop trails (gSave.trail): 1 Ink, 2 Flame, 3 Rainbow, 4 Ribbon, 5 Sparkle, 6 Comet.
enum { TR_PLAIN, TR_INK, TR_FLAME, TR_RAINBOW, TR_RIBBON, TR_SPARKLE, TR_COMET };

u32 trailSwatch(int trail, float t) {
	switch (trail) {
		case TR_INK: return rgba(34, 32, 52);
		case TR_FLAME: return mixCol(rgba(220, 40, 20), rgba(255, 220, 70), t);
		case TR_RAINBOW: return hueColor(t);
		case TR_RIBBON: return ((int)(t * 6)) & 1 ? rgba(255, 240, 246) : rgba(255, 110, 170);
		case TR_SPARKLE: return rgba(255, 236, 150);
		case TR_COMET: return mixCol(rgba(90, 150, 255), rgba(225, 245, 255), t * t);
		default: return rgba(255, 140, 58);
	}
}

// Blade and trail are two layers. The trail (Shop) streaks behind and lingers a little; the blade's
// edge (Options) is the bright stroke at the front, always in the blade colour, drawn over the trail.
static constexpr float kEdgeLife = 0.09f;
static float trailLife() { return gSave.trail > 0 && gSave.trail < kTrailCount ? kStyledLife : kTrailLife; }

void Blade::draw() const {
	if (count < 2) return;
	const int style = gBladeDull > 0 || gSave.trail >= kTrailCount ? TR_PLAIN : gSave.trail;
	if (style == TR_PLAIN) {
		drawStroke(TR_PLAIN, kTrailLife, 1.0f, true);
		return;
	}
	drawStroke(style, kStyledLife, 1.15f, false);
	if (style == TR_SPARKLE || style == TR_COMET) drawExtras(style);
	drawStroke(TR_PLAIN, kEdgeLife, 0.8f, true);
}

// One tapered strip through the points younger than `life`, thick at the head.
void Blade::drawStroke(int style, float life, float width, bool tip) const {
	static const u32 kColors[kBladeCount] = {0xFF3A8CFFu, 0xFFFFD060u, 0xFFA070FFu, 0xFF70F090u, 0xFF30C0FFu, 0};
	const bool rainbow = gSave.blade == kBladeCount - 1 && gBladeDull <= 0;
	const u32 baseColor = gBladeDull > 0 ? rgba(130, 130, 140) : kColors[gSave.blade % kBladeCount];
	// Only the points young enough for this layer: the newest ones are at the end.
	int first = 0;
	while (first < count && trail[(head - count + first + kTrail) % kTrail].age > life) first++;
	const int n = count - first;
	if (n < 2) return;
	auto at = [&](int i) -> const Point& { return trail[(head - n + i + kTrail) % kTrail]; };
	float prevLx = 0, prevLy = 0, prevRx = 0, prevRy = 0;
	u32 prevCol = 0, prevCore = 0;
	for (int i = 0; i < n; i++) {
		const Point& p = at(i);
		const Point& nx_ = at(i + 1 < n ? i + 1 : i);
		const Point& b = at(i > 0 ? i - 1 : i);
		float dx = nx_.x - b.x, dy = nx_.y - b.y;
		float len = sqrtf(dx * dx + dy * dy);
		if (len < 0.01f) len = 0.01f;
		float nx = -dy / len, ny = dx / len;
		float t = (float)i / (n - 1);  // 0 tail, 1 head
		float fade = 1.0f - p.age / life;
		float w = width * kMaxHalfWidth * sinf(clampf(t, 0.02f, 1.0f) * kPi * 0.62f) * clampf(fade + 0.2f, 0, 1);
		switch (style) {
			case TR_INK: w *= 1.5f + 0.2f * sinf(i * 1.7f + strokeId); break;
			case TR_FLAME: w *= 1.25f * (0.85f + 0.15f * sinf(p.age * 90 + i * 2.1f)); break;
			case TR_RAINBOW: w *= 1.3f; break;
			case TR_RIBBON: w *= 0.35f + 0.85f * fabsf(sinf(t * 9 + strokeId)); break;
			case TR_COMET: w = width * kMaxHalfWidth * 1.5f * t * t * t * clampf(fade + 0.2f, 0, 1); break;
			default: break;
		}
		float lx = p.x + nx * w, ly = p.y + ny * w;
		float rx = p.x - nx * w, ry = p.y - ny * w;
		u32 color = rainbow ? hueColor(t * 0.8f + (float)strokeId * 0.13f) : baseColor;
		u32 coreColor = WHITE;
		switch (style) {
			case TR_INK: color = trailSwatch(TR_INK, t); coreColor = rgba(80, 76, 120); break;
			case TR_FLAME: color = trailSwatch(TR_FLAME, t); coreColor = rgba(255, 246, 200); break;
			case TR_RAINBOW: color = hueColor(t * 1.4f + (float)strokeId * 0.13f); break;
			case TR_RIBBON: color = trailSwatch(TR_RIBBON, t + strokeId * 0.05f); coreColor = color; break;
			case TR_SPARKLE: color = trailSwatch(TR_SPARKLE, t); break;
			case TR_COMET: color = trailSwatch(TR_COMET, t); break;
			default: break;
		}
		u32 col = withAlpha(color, fade * (style == TR_INK ? 0.92f : 0.75f));
		u32 core = withAlpha(coreColor, fade);
		if (i > 0) {
			gfx::tri(prevLx, prevLy, prevCol, prevRx, prevRy, prevCol, lx, ly, col);
			gfx::tri(lx, ly, col, prevRx, prevRy, prevCol, rx, ry, col);
			// Hot inner core at half width.
			float cx0 = (prevLx + prevRx) * 0.5f, cy0 = (prevLy + prevRy) * 0.5f;
			float cx1 = p.x, cy1 = p.y;
			float h0x = (prevLx - cx0) * 0.45f, h0y = (prevLy - cy0) * 0.45f;
			float h1x = (lx - cx1) * 0.45f, h1y = (ly - cy1) * 0.45f;
			gfx::tri(cx0 + h0x, cy0 + h0y, prevCore, cx0 - h0x, cy0 - h0y, prevCore, cx1 + h1x, cy1 + h1y, core);
			gfx::tri(cx1 + h1x, cy1 + h1y, core, cx0 - h0x, cy0 - h0y, prevCore, cx1 - h1x, cy1 - h1y, core);
		}
		prevLx = lx; prevLy = ly;
		prevRx = rx; prevRy = ry;
		prevCol = col;
		prevCore = core;
	}
	if (!tip) return;
	// A pointed tip, so the head of the stroke doesn't end in a flat edge.
	const Point& last = at(n - 1);
	const Point& before = at(n - 2);
	float dx = last.x - before.x, dy = last.y - before.y;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.01f) return;
	float wx = prevLx - last.x, wy = prevLy - last.y;
	float reach = sqrtf(wx * wx + wy * wy) * 1.8f;
	float tipX = last.x + dx / len * reach, tipY = last.y + dy / len * reach;
	gfx::tri(prevLx, prevLy, prevCol, prevRx, prevRy, prevCol, tipX, tipY, prevCol);
	gfx::tri(last.x + wx * 0.45f, last.y + wy * 0.45f, prevCore, last.x - wx * 0.45f, last.y - wy * 0.45f, prevCore, tipX, tipY, prevCore);
}

// Sparkle leaves little four-point stars along the stroke; Comet gets a bright glowing head.
void Blade::drawExtras(int style) const {
	if (style == TR_COMET) {
		const Point& h = trail[(head - 1 + kTrail) % kTrail];
		u32 glow = withAlpha(rgba(200, 230, 255), 0.55f), clear = withAlpha(rgba(200, 230, 255), 0);
		const int kSides = 10;
		for (int k = 0; k < kSides; k++) {
			float a0 = k * 2 * kPi / kSides, a1 = (k + 1) * 2 * kPi / kSides;
			gfx::tri(h.x, h.y, glow, h.x + cosf(a0) * 9, h.y + sinf(a0) * 9, clear, h.x + cosf(a1) * 9, h.y + sinf(a1) * 9, clear);
		}
		return;
	}
	for (int i = 0; i < count; i += 3) {
		const Point& p = trail[(head - count + i + kTrail) % kTrail];
		float fade = 1.0f - p.age / kStyledLife;
		float tw = 0.5f + 0.5f * sinf(i * 7.3f + p.age * 60 + strokeId);
		float r = (1.5f + 2.5f * tw) * fade;
		if (r < 0.4f) continue;
		float ox = sinf(i * 3.1f + strokeId) * 5, oy = cosf(i * 2.3f + strokeId) * 5;
		u32 c = withAlpha(WHITE, fade), e = withAlpha(rgba(255, 230, 140), 0);
		float x = p.x + ox, y = p.y + oy;
		gfx::tri(x - r, y, c, x + r, y, c, x, y - r * 0.25f, e);
		gfx::tri(x - r, y, c, x + r, y, c, x, y + r * 0.25f, e);
		gfx::tri(x, y - r, c, x, y + r, c, x - r * 0.25f, y, e);
		gfx::tri(x, y - r, c, x, y + r, c, x + r * 0.25f, y, e);
	}
}
