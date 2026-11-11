#version 450
// PS5-ECAM's UI kit (port/ui/gfx.cpp): the backdrop's blur, a pass over the whole of a small target. One
// triangle covers it; its corners come from the vertex index, so nothing is bound but the source.

layout(location = 0) out vec2 vUv;

void main()
{
	vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	vUv = corner;
	gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
