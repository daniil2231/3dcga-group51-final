#version 410

out vec2 screenPosition;

void main()
{
    // One oversized triangle covers the window; no vertex buffer is needed.
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    screenPosition = positions[gl_VertexID];
    gl_Position = vec4(screenPosition, 0.0, 1.0);
}
