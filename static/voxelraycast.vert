#version 300 es

in vec3 inPosition;

uniform vec3 position;

out vec2 UV;
out vec3 cameraPosition;
out vec3 endPosition;

vec4 vertices[6] = vec4[6](
	vec4(-1, 1, 0, 1), // topLeft
	vec4(1, 1, 0, 1), // topRight
	vec4(-1, -1, 0, 1), // bottomLeft
	vec4(1, 1, 0, 1), // topRight
	vec4(1, -1, 0, 1), // bottomRight
	vec4(-1, -1, 0, 1) // bottomLeft
);

void main()
{
	cameraPosition = position;
	gl_Position = vertices[gl_VertexID];
	UV = vertices[gl_VertexID].rg;
}
