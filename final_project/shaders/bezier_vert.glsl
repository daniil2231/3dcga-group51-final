#version 410 core

layout(location = 0) in vec3 position;
uniform mat4 viewProjection;

void main()
{
    gl_Position = viewProjection * vec4(position, 1.0);
}