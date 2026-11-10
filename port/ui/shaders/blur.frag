#version 450
// PS5-ECAM's UI kit: the backdrop's blur, the dual Kawase filter (Marius Bjørge, "Bandwidth-efficient
// rendering", SIGGRAPH 2015): each pass down to a target half the size takes five taps of the one above,
// each pass back up eight of the one below, every tap between texels so the sampler averages four. A few
// passes over targets a sixteenth of the screen and smaller blur as widely as a large Gaussian would.

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 1) uniform sampler2D uSource;

layout(push_constant) uniform Constants
{
	vec2 step; // the source's texel, times the spread
	float up;  // 0: down, 1: up
	float unused;
} pc;

layout(location = 0) out vec4 outColour;

void main()
{
	vec2 s = pc.step;
	if (pc.up < 0.5)
	{
		vec4 sum = texture(uSource, vUv) * 4.0;
		sum += texture(uSource, vUv - s);
		sum += texture(uSource, vUv + s);
		sum += texture(uSource, vUv + vec2(s.x, -s.y));
		sum += texture(uSource, vUv - vec2(s.x, -s.y));
		outColour = sum / 8.0;
	}
	else
	{
		vec4 sum = texture(uSource, vUv + vec2(-s.x * 2.0, 0.0));
		sum += texture(uSource, vUv + vec2(-s.x, s.y)) * 2.0;
		sum += texture(uSource, vUv + vec2(0.0, s.y * 2.0));
		sum += texture(uSource, vUv + vec2(s.x, s.y)) * 2.0;
		sum += texture(uSource, vUv + vec2(s.x * 2.0, 0.0));
		sum += texture(uSource, vUv + vec2(s.x, -s.y)) * 2.0;
		sum += texture(uSource, vUv + vec2(0.0, -s.y * 2.0));
		sum += texture(uSource, vUv + vec2(-s.x, -s.y)) * 2.0;
		outColour = sum / 12.0;
	}
}
