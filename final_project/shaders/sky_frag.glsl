#version 410

uniform mat4 inverseProjection;
uniform mat3 inverseViewRotation;
uniform vec3 horizonColor;
uniform vec3 zenithColor;

in vec2 screenPosition;
layout(location = 0) out vec4 fragColor;

void main()
{
    vec4 viewPoint = inverseProjection * vec4(screenPosition, 1.0, 1.0);
    vec3 viewDirection = normalize(viewPoint.xyz / viewPoint.w);
    vec3 worldDirection = normalize(inverseViewRotation * viewDirection);
    float skyBlend = pow(clamp(worldDirection.y, 0.0, 1.0), 0.55);
    fragColor = vec4(mix(horizonColor, zenithColor, skyBlend), 1.0);
}
