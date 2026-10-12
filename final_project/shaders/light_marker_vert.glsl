#version 410

uniform mat4 viewProjection;
uniform vec3 markerPosition;

void main()
{
    gl_Position = viewProjection * vec4(markerPosition, 1.0);
}