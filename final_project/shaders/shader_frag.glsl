#version 410

layout(std140) uniform Material // Must match the GPUMaterial defined in src/mesh.h
{
    vec3 kd;
	vec3 ks;
	float shininess;
	float transparency;
};

uniform sampler2D colorMap;
uniform bool hasTexCoords;
uniform bool useMaterial;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform float ambientStreantgh;
uniform vec3 cameraPos;

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

layout(location = 0) out vec4 fragColor;

void main()
{
    //vec3 normal = normalize(fragNormal);


    //if (hasTexCoords)       { fragColor = vec4(texture(colorMap, fragTexCoord).rgb, 1);}
    //else if (useMaterial)   { fragColor = vec4(kd, 1);}
    //else                    { fragColor = vec4(normal, 1); } // Output color value, change from (1, 0, 0) to something else

    vec3 N = normalize(fragNormal);

    if (!hasTexCoords && !useMaterial)
    {
        fragColor = vec4(N,1);
        return;
    }

    vec3 baseColor;
    if (hasTexCoords)
    {
        baseColor = texture(colorMap, fragTexCoord).rgb;
    } else {
        baseColor = kd;
    }
    //baseColor = vec3(0.7);
    vec3 L = normalize(sunDirection);
    float facingLight = max(dot(N,L), 0.0);

    vec3 ambient = ambientStreantgh * baseColor;
    vec3 diffuse = baseColor * sunColor * sunIntensity * facingLight;

    //specular calculations
    vec3 V = normalize(cameraPos - fragNormal);

    float specularFactor = 0.0;

    if (facingLight > 0.0)
    {
        vec3 halfway = L + V;

        if (dot(halfway,halfway) > 0.000001)
        {
            vec3 H = normalize(halfway);

            specularFactor = pow(max(dot(N,H),0.0),max(shininess, 1.0));
        }
    }
    vec3 specular = ks * specularFactor * sunIntensity;

    fragColor = vec4(ambient + diffuse + specular, 1.0);
}
