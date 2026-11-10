#version 450
// PS5-ECAM's UI kit: each quad's shape as a signed distance, so corners and edges are smooth at any
// scale. Kinds: 0 a filled rounded box, 1 a ring inside the box's edge, 2 a soft shadow or glow
// around it (with the ring width set, outside the box only: a glow drawn over what it lights), 3 an
// image in it, 4 a glyph (a signed-distance atlas), 5 grain, 6 a line with round
// ends (from gradient.xy to gradient.zw, the ring width wide), 7 a triangle (gradient.xy,
// gradient.zw and uv.xy; outlined when the ring width is set, its corners rounded by the radius),
// 8 a wave: everything below base + amplitude * sin(x / wavelength + phase) (gradient: base,
// amplitude, wavelength, phase), 9 liquid glass: the backdrop under it, blurred (gfx.cpp), bent and lit
// at its rim (gradient: the rim's width, the bend, the light, the colours' split), its colours lifted by
// shape.y, darkened by extra.z (lightened below 0), the first colour laid over it by that colour's alpha, the whole as opaque as the second
// colour's alpha. Kinds 6 to 9 take no gradient. Colours are straight RGBA, in the
// same sRGB values the design's tokens name; the output is premultiplied.

layout(location = 0) in vec2 vPos;
layout(location = 1) flat in vec4 vBox;
layout(location = 2) flat in vec4 vShape;
layout(location = 3) flat in vec4 vColour0;
layout(location = 4) flat in vec4 vColour1;
layout(location = 5) flat in vec4 vGradient;
layout(location = 6) flat in vec4 vUv;
layout(location = 7) flat in vec4 vClip;
layout(location = 8) flat in vec4 vExtra;

layout(set = 0, binding = 0) uniform sampler2D uAtlas;
layout(set = 0, binding = 1) uniform sampler2D uImage;

layout(push_constant) uniform Constants
{
	vec2 layoutSize;
	float scale;
	float time;
} pc;

layout(location = 0) out vec4 outColour;

float RoundBox(vec2 p, vec2 halfSize, float radius)
{
	vec2 q = abs(p) - halfSize + radius;
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float Segment(vec2 p, vec2 a, vec2 b)
{
	vec2 pa = p - a, ba = b - a;
	float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
	return length(pa - ba * h);
}

float Triangle(vec2 p, vec2 a, vec2 b, vec2 c)
{
	vec2 e0 = b - a, e1 = c - b, e2 = a - c;
	vec2 v0 = p - a, v1 = p - b, v2 = p - c;
	vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
	vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
	vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
	float s = sign(e0.x * e2.y - e0.y * e2.x);
	vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)), vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
		vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
	return -sqrt(d.x) * sign(d.y);
}

// A pixel's noise, 0 to 1, from its whole coordinates: integer hashing (PCG's output step), the same
// at any coordinate. The float hash before it, fract(p * 443.897), ran out of precision at 1080p and
// 4K, and its noise became stripes; moved each tick, they crossed the screen as lines (#72)
uint PcgHash(uint v)
{
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float Hash(vec2 p)
{
	uvec2 q = uvec2(max(p, vec2(0.0)));
	return float(PcgHash(q.x + PcgHash(q.y)) >> 8u) * (1.0 / 16777216.0);
}

void main()
{
	// one output pixel, in layout units: the width of every edge's anti-aliasing
	float pixel = 1.0 / pc.scale;
	vec2 halfSize = vBox.zw * 0.5;
	vec2 centre = vBox.xy + halfSize;
	float radius = min(vShape.x, min(halfSize.x, halfSize.y));
	float d = RoundBox(vPos - centre, halfSize, radius);
	int kind = int(vShape.w + 0.5);

	// the colour: flat, or along a line, or out from a centre, starting at extra.w of the way
	vec4 colour = vColour0;
	int gradient = kind >= 6 ? 0 : int(vExtra.y + 0.5);
	if (gradient == 1)
	{
		vec2 along = vGradient.zw - vGradient.xy;
		float t = clamp(dot(vPos - vGradient.xy, along) / max(dot(along, along), 1e-6), 0.0, 1.0);
		colour = mix(vColour0, vColour1, clamp((t - vExtra.w) / max(1.0 - vExtra.w, 1e-3), 0.0, 1.0));
	}
	else if (gradient == 2)
	{
		vec2 ellipse = vec2(vGradient.z, vGradient.w > 0.0 ? vGradient.w : vGradient.z);
		float t = clamp(length((vPos - vGradient.xy) / max(ellipse, vec2(1e-3))), 0.0, 1.0);
		colour = mix(vColour0, vColour1, clamp((t - vExtra.w) / max(1.0 - vExtra.w, 1e-3), 0.0, 1.0));
	}

	float coverage = 1.0;
	if (kind == 0)
		coverage = clamp(0.5 - d / pixel, 0.0, 1.0);
	else if (kind == 1)
	{
		float width = vShape.y;
		coverage = clamp(0.5 - (abs(d + width * 0.5) - width * 0.5) / pixel, 0.0, 1.0);
	}
	else if (kind == 2)
	{
		float soft = max(vShape.z, pixel);
		// a Gaussian's edge, near enough: full inside, fading over the softness either side
		float t = clamp((d + soft) / (2.0 * soft), 0.0, 1.0);
		coverage = 1.0 - t * t * (3.0 - 2.0 * t);
		if (vShape.y > 0.0)
			coverage *= clamp(0.5 + d / pixel, 0.0, 1.0);
	}
	else if (kind == 3)
	{
		vec2 uv = mix(vUv.xy, vUv.zw, (vPos - vBox.xy) / max(vBox.zw, vec2(1e-3)));
		colour *= texture(uImage, uv);
		coverage = clamp(0.5 - d / pixel, 0.0, 1.0);
	}
	else if (kind == 4)
	{
		vec2 uv = mix(vUv.xy, vUv.zw, (vPos - vBox.xy) / max(vBox.zw, vec2(1e-3)));
		float distance = texture(uAtlas, uv).r;
		// the edge at 0.5, widened for weight; anti-aliased over one output pixel
		float edge = 0.5 - vExtra.z;
		float width = max(fwidth(distance), 1e-4) * 0.75;
		coverage = smoothstep(edge - width, edge + width, distance);
	}
	else if (kind == 5)
	{
		// still: grain that changed 24 times a second read as a flicker over the whole screen (#72)
		float n = Hash(floor(vPos * pc.scale));
		colour.a *= n;
		coverage = clamp(0.5 - d / pixel, 0.0, 1.0);
	}
	else if (kind == 6)
		coverage = clamp(0.5 - (Segment(vPos, vGradient.xy, vGradient.zw) - vShape.y * 0.5) / pixel, 0.0, 1.0);
	else if (kind == 7)
	{
		float t = Triangle(vPos, vGradient.xy, vGradient.zw, vUv.xy) - vShape.x;
		if (vShape.y > 0.0)
			t = abs(t + vShape.y * 0.5) - vShape.y * 0.5;
		coverage = clamp(0.5 - t / pixel, 0.0, 1.0);
	}
	else if (kind == 8)
	{
		float surface = vGradient.x + vGradient.y * sin(vPos.x / max(vGradient.z, 1e-3) + vGradient.w);
		coverage = clamp(0.5 + (vPos.y - surface) / pixel, 0.0, 1.0) * clamp(0.5 - d / pixel, 0.0, 1.0);
	}
	else if (kind == 9)
	{
		// Liquid glass: what is behind, as the blur left it, bent at the rim as a thick lens bends it, split a
		// little into its colours there, its colours lifted, the tint over it, and light caught along its edge
		// (gradient: the rim's width, how far it bends, the light's strength, the split)
		vec2 p = vPos - centre;
		vec2 e = vec2(0.75, 0.0);
		vec2 n = vec2(RoundBox(p + e.xy, halfSize, radius) - RoundBox(p - e.xy, halfSize, radius),
			RoundBox(p + e.yx, halfSize, radius) - RoundBox(p - e.yx, halfSize, radius));
		n = length(n) > 1e-5 ? normalize(n) : vec2(0.0, -1.0);
		float depth = max(-d, 0.0);
		float rim = vGradient.x > 0.0 ? clamp(1.0 - depth / vGradient.x, 0.0, 1.0) : 0.0;
		float bend = rim * rim * rim * vGradient.y;
		vec2 uv = vPos / pc.layoutSize;
		vec2 shift = n * bend / pc.layoutSize;
		vec3 behind = vec3(texture(uImage, uv + shift * (1.0 + vGradient.w)).r, texture(uImage, uv + shift).g,
			texture(uImage, uv + shift * (1.0 - vGradient.w)).b);
		float grey = dot(behind, vec3(0.2126, 0.7152, 0.0722));
		behind = clamp(mix(vec3(grey), behind, vShape.y), 0.0, 1.0);
		// toward legibility: darker under light text, lighter under dark (extra.z, -1 to 1)
		behind = vExtra.z >= 0.0 ? behind * (1.0 - vExtra.z) : mix(behind, vec3(1.0), -vExtra.z);
		vec3 glass = mix(behind, vColour0.rgb, vColour0.a);
		// the light: from the top left, strongest on the edge facing it, a fainter echo on the edge across
		vec2 light = normalize(vec2(-0.55, -1.0));
		float facing = pow(max(dot(n, light), 0.0), 1.5) + 0.45 * pow(max(dot(n, -light), 0.0), 2.0);
		float edge = clamp(1.0 - depth / 2.2, 0.0, 1.0);
		float sheen = rim * rim * 0.35 + edge;
		glass += vec3(1.0) * vGradient.z * facing * sheen;
		colour = vec4(clamp(glass, 0.0, 1.0), vColour1.a);
		coverage = clamp(0.5 - d / pixel, 0.0, 1.0);
	}

	// the clip: a rounded rectangle everything is kept inside
	if (vClip.z > 0.0)
	{
		vec2 clipHalf = vClip.zw * 0.5;
		float clipD = RoundBox(vPos - (vClip.xy + clipHalf), clipHalf, min(vExtra.x, min(clipHalf.x, clipHalf.y)));
		coverage *= clamp(0.5 - clipD / pixel, 0.0, 1.0);
	}

	float alpha = colour.a * coverage;
	// half a step of noise at the target's 8 bits a channel: the dark gradients behind the menus came out
	// in bands, a line wherever a level changed, which only the grain had hidden
	float dither = (Hash(gl_FragCoord.xy) - 0.5) / 255.0;
	outColour = vec4((colour.rgb + dither) * alpha, alpha);
}
