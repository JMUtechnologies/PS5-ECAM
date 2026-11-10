#version 450
// PS5-ECAM: a DS screen (or a libretro core's picture) scaled to the TV, as the screen filter asks (port/melonds/screens.cpp),
// through a bilinear sampler:
//  0 sharp: each of the DS's pixels a solid square, its edges blended over one pixel of the TV, so
//    the squares are even at any scale (11.25x, 5.625x...)
//  1 smooth: bilinear, the DS's pixels blurred into each other
//  2 square pixels: the nearest of the DS's pixels, its edges hard (uneven at a scale that is not
//    a whole number)
//  3 CRT: the picture as a tube TV drew it, each line a beam (wider where it is brighter) with dark between,
//    through an aperture grille's red, green and blue stripes
//  4 curved CRT: the same on a tube's rounded glass, darker toward its corners
//  9 flat white: a bar of the touch cursor, which its pipeline's blend inverts what is under

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0) uniform sampler2D uScreen;

layout(push_constant) uniform Constants
{
	vec4 rect;
	vec2 size;
	float mode;
	float rotation;
	vec2 uvScale; // the part of the image the screen fills: a libretro core's picture is in the top left of a larger one
} pc;

layout(location = 0) out vec4 outColour;

vec3 Pixel(vec2 texel)
{
	return texture(uScreen, texel / pc.size * pc.uvScale).rgb;
}

// one line's beam at a distance d (in lines) from its centre, its colour c (linear): brighter, wider
vec3 Beam(float d, vec3 c)
{
	vec3 sigma = mix(vec3(0.20), vec3(0.36), sqrt(c));
	return c * exp(-0.5 * d * d / (sigma * sigma));
}

vec3 Crt(vec2 uv, bool curved)
{
	float vignette = 1.0;
	if (curved)
	{
		// the glass bulges: the picture pulled in toward its corners, nothing past its edges
		vec2 c = uv * 2.0 - 1.0;
		c *= 1.0 + vec2(c.y * c.y, c.x * c.x) * vec2(0.045, 0.06);
		uv = c * 0.5 + 0.5;
		if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
			return vec3(0.0);
		vignette = pow(16.0 * uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y), 0.18);
	}
	vec2 texel = uv * pc.size;
	vec2 width = max(fwidth(texel), vec2(1e-5));
	// across a line: sharp pixels, as mode 0; down: the two nearest lines' beams
	float x = floor(texel.x + 0.5);
	x = x + clamp((texel.x - x) / width.x, -0.5, 0.5);
	float row = floor(texel.y) + 0.5;
	float d = texel.y - row;
	float other = row + (d < 0.0 ? -1.0 : 1.0);
	vec3 a = pow(Pixel(vec2(x, row)), vec3(2.2));
	vec3 b = pow(Pixel(vec2(x, clamp(other, 0.5, pc.size.y - 0.5))), vec3(2.2));
	vec3 light = Beam(d, a) + Beam(texel.y - other, b);
	// the grille's stripes, three TV pixels to a triad, where a line is tall enough to show them
	if (width.y < 0.34)
	{
		int stripe = int(mod(gl_FragCoord.x, 3.0));
		vec3 mask = vec3(0.78);
		mask[stripe] = 1.22;
		light *= mask;
	}
	return pow(light * 1.3 * vignette, vec3(1.0 / 2.2));
}

void main()
{
	if (pc.mode > 8.5)
	{
		outColour = vec4(1.0);
		return;
	}
	if (pc.mode > 2.5)
	{
		outColour = vec4(Crt(vUv, pc.mode > 3.5), 1.0);
		return;
	}
	vec2 texel = vUv * pc.size;
	if (pc.mode < 0.5)
	{
		vec2 seam = floor(texel + 0.5);
		vec2 width = max(fwidth(texel), vec2(1e-5));
		texel = seam + clamp((texel - seam) / width, -0.5, 0.5);
	}
	else if (pc.mode > 1.5)
		texel = floor(texel) + 0.5;
	// the DS's pixels have no alpha worth reading
	outColour = vec4(texture(uScreen, texel / pc.size * pc.uvScale).rgb, 1.0);
}
