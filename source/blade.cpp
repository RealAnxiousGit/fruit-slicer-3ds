#include "blade.h"

#include "render.h"

static constexpr float kTrailLife = 0.17f;
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
	while (count > 0 && trail[(head - count + kTrail) % kTrail].age > kTrailLife) count--;
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

u32 hueColor(float t);

void Blade::draw() const {
	if (count < 2) return;
	static const u32 kColors[kBladeCount] = {0xFF3A8CFFu, 0xFFFFD060u, 0xFFA070FFu, 0xFF70F090u, 0xFF30C0FFu, 0};
	const bool rainbow = gSave.blade == kBladeCount - 1;
	const u32 baseColor = kColors[gSave.blade % kBladeCount];
	// Rebuilt every frame as a tapered triangle strip in citro2D's dynamic vertex buffer.
	float prevLx = 0, prevLy = 0, prevRx = 0, prevRy = 0;
	u32 prevCol = 0, prevCore = 0;
	for (int i = 0; i < count; i++) {
		const Point& p = trail[(head - count + i + kTrail) % kTrail];
		const Point& n = trail[(head - count + (i + 1 < count ? i + 1 : i) + kTrail) % kTrail];
		const Point& b = trail[(head - count + (i > 0 ? i - 1 : i) + kTrail) % kTrail];
		float dx = n.x - b.x, dy = n.y - b.y;
		float len = sqrtf(dx * dx + dy * dy);
		if (len < 0.01f) len = 0.01f;
		float nx = -dy / len, ny = dx / len;
		float t = (float)i / (count - 1);  // 0 tail, 1 head
		float fade = 1.0f - p.age / kTrailLife;
		float w = kMaxHalfWidth * sinf(clampf(t, 0.02f, 1.0f) * kPi * 0.62f) * clampf(fade + 0.2f, 0, 1);
		float lx = p.x + nx * w, ly = p.y + ny * w;
		float rx = p.x - nx * w, ry = p.y - ny * w;
		u32 color = rainbow ? hueColor(t * 0.8f + (float)strokeId * 0.13f) : baseColor;
		u32 col = withAlpha(color, fade * 0.75f);
		u32 core = withAlpha(WHITE, fade);
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
	// A pointed tip, so the head of the stroke doesn't end in a flat edge.
	const Point& last = trail[(head - 1 + kTrail) % kTrail];
	const Point& before = trail[(head - 2 + kTrail) % kTrail];
	float dx = last.x - before.x, dy = last.y - before.y;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.01f) return;
	float wx = prevLx - last.x, wy = prevLy - last.y;
	float reach = sqrtf(wx * wx + wy * wy) * 1.8f;
	float tipX = last.x + dx / len * reach, tipY = last.y + dy / len * reach;
	gfx::tri(prevLx, prevLy, prevCol, prevRx, prevRy, prevCol, tipX, tipY, prevCol);
	gfx::tri(last.x + wx * 0.45f, last.y + wy * 0.45f, prevCore, last.x - wx * 0.45f, last.y - wy * 0.45f, prevCore, tipX, tipY, prevCore);
}
