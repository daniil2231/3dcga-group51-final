#version 410

layout(std140) uniform Material // Must match the GPUMaterial defined in src/mesh.h
{
    vec3 kd;
	vec3 ks;
	float shininess;
	float transparency;
};

uniform bool enableHaze;
uniform vec3 horizonColor;
uniform vec3 zenithColor;

uniform sampler2D colorMap;
uniform bool hasTexCoords;
uniform bool useMaterial;
uniform vec3 sunDirection;
uniform bool usePointLight;
uniform vec3 sunPosition;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform float ambientStreantgh;
uniform vec3 ambientColor;
uniform vec3 cameraPos;
uniform bool enableShadows;
uniform bool enablePCF;
uniform float shadowBias;
uniform mat4 lightViewProjection;
uniform sampler2D shadowMap;
uniform sampler2D normalMap;
uniform bool useNormalMap;
uniform float normalStreangth;
uniform bool usePBR;
uniform float metallic;
uniform float roughness;
uniform samplerCube environmentMap;
uniform float reflectionStrength;
uniform bool usePBREnvironment;
uniform float pbrEnvironmentStrength;
uniform bool unlit;

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

layout(location = 0) out vec4 fragColor;

float lightVisibility(vec3 geometricNormal, vec3 L)
{
    if (!enableShadows)
    {
        return 1.0f;
    }

    vec4 lightClip = lightViewProjection * vec4(fragPosition, 1.0);
    if (lightClip.w <= 0.0) {
        return 1.0;
    }

    vec3 lightCoords = lightClip.xyz / lightClip.w;
    lightCoords = lightCoords * 0.5 + 0.5;
    if (lightCoords.x < 0.0 || lightCoords.x > 1.0 ||
        lightCoords.y < 0.0 || lightCoords.y > 1.0 ||
        lightCoords.z < 0.0 || lightCoords.z > 1.0) {
        return 1.0;
    }
    float slope =
        1.0 - max(dot(normalize(geometricNormal), L), 0.0);

    float bias = shadowBias * (1.0 + 2.0 * slope);

    vec2 texelSize =
        1.0 / vec2(textureSize(shadowMap, 0));

    if (!enablePCF) {
        float vis2 = (lightCoords.z - bias <= texture(shadowMap, lightCoords.xy).r) ? 1.0: 0.0;
        return vis2;
    }

    float visibility = 0.0;

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 offset = vec2(x, y) * texelSize;

            float nearestDepth =
                texture(shadowMap, lightCoords.xy + offset).r;

            visibility += (lightCoords.z - bias <= nearestDepth) ? 1.0: 0.0;
        }
    }

    return visibility / 9.0;
}

const float PI = 3.14159265359;

float distributionGGX(float NdotH, float r)
{
    float alpha = pow(r, 2.0);
    float alphaSquared = pow(alpha, 2.0);
    float denominator = pow(NdotH, 2.0) * (alphaSquared - 1.0) + 1.0;
    float result = alphaSquared / max(PI * pow(denominator, 2.0), 0.00000001);

    return result;
}

//masking function
float geometryGGX(float NdotDirection, float r)
{
    float alpha = pow(r, 2.0);
    float alphaSquared = pow(alpha, 2.0);
    float root = sqrt(alphaSquared + (1.0 - alphaSquared) * pow(NdotDirection, 2.0));
    float result = (2.0 * NdotDirection) / max(NdotDirection + root, 0.000001);

    return result;
}

vec3 fresnelSchlick(float VdotH, vec3 F0)
{
    float grazing = pow(1.0 - clamp(VdotH, 0.0, 1.0), 5.0);
    vec3 difference = vec3(1.0) - F0;
    vec3 result = F0 + difference * grazing;

    return result;
}

vec3 calculateEnvironmentPBR(vec3 albedo, vec3 N, vec3 V)
{
    float m = clamp(metallic, 0.0, 1.0);
    float r = clamp(roughness, 0.1, 1.0);

    vec3 reflectionDirection = reflect(-V,N);

    float faceSize = float(textureSize(environmentMap, 0).x);
    float maxMipLevel = floor(log2(faceSize));
    float mipLevel = r * maxMipLevel;

    vec3 environmentColor = textureLod(environmentMap, reflectionDirection, mipLevel).rgb;

    vec3 environmentLinear = pow(max(environmentColor, vec3(0.0)), vec3(2.2));

    float NdotV = max(dot(N,V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, m);
    vec3 F = fresnelSchlick(NdotV, F0);

    return environmentLinear * F * pbrEnvironmentStrength;
}

vec3 calculatePBR(vec3 albedo, vec3 N, vec3 V, vec3 L)
{
    float m = clamp(metallic, 0.0, 1.0);
    float r = clamp(roughness, 0.1, 1.0);

    float NdotL = max(dot(N,L), 0.0);
    float NdotV = max(dot(N,V), 0.0);

    if(NdotL <= 0.0 || NdotV <= 0.0)
    {
        return vec3(0.0);
    }

    vec3 halfway = L + V;
    if(dot(halfway, halfway) < 0.000001)
    {
        return vec3(0.0);
    }

    vec3 H = normalize(halfway);
    float NdotH = max(dot(N,H), 0.0);
    float VdotH = max(dot(V,H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, m);
    vec3 F = fresnelSchlick(VdotH, F0);
    float D = distributionGGX(NdotH, r);
    float G = geometryGGX(NdotV, r) * geometryGGX(NdotL, r);
    
    vec3 specularBRDF = (D*G*F) / max(4.0 * NdotV * NdotL, 0.000001);
    vec3 diffuseWeight = (vec3(1.0) - F) * (1.0 - m);
    vec3 diffuseBRDF = diffuseWeight * albedo / PI;

    vec3 radiance = sunColor * sunIntensity;

    return (diffuseBRDF + specularBRDF) * radiance * NdotL;
}

void main()
{
    //vec3 normal = normalize(fragNormal);


    //if (hasTexCoords)       { fragColor = vec4(texture(colorMap, fragTexCoord).rgb, 1);}
    //else if (useMaterial)   { fragColor = vec4(kd, 1);}
    //else                    { fragColor = vec4(normal, 1); } // Output color value, change from (1, 0, 0) to something else

    vec3 N = normalize(fragNormal);
    if(useNormalMap)
    {
        vec3 mappedNormal = texture(normalMap, fragTexCoord).rgb;
        mappedNormal = mappedNormal * 2.0 - 1.0;
        mappedNormal.xy = mappedNormal.xy * normalStreangth;

        vec3 T = vec3(1.0, 0.0, 0.0);
        vec3 B = vec3(0.0, 0.0, 1.0);
        mat3 TBN = mat3(T,B,N);

        N = normalize(TBN * mappedNormal);
    }

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
    // Keep intensity identical between modes to compare light direction alone.
    // The point mode deliberately has no distance attenuation (like exercise 3).
    vec3 toLight = usePointLight ? sunPosition - fragPosition : sunDirection;
    vec3 L = toLight / max(length(toLight), 0.000001);
    float facingLight = max(dot(N,L), 0.0);

    vec3 ambient = ambientStreantgh * ambientColor * baseColor;
    vec3 diffuse = baseColor * sunColor * sunIntensity * facingLight;

    //specular calculations
    vec3 V = normalize(cameraPos - fragPosition);

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
    vec3 specular = ks * sunColor * specularFactor * sunIntensity;
    float visibility = lightVisibility(fragNormal, L);
    vec3 finalColor = ambient + visibility * (diffuse + specular);

    if(usePBR)
    {
        vec3 albedo = clamp(baseColor, vec3(0.0), vec3(1.0));
        if(hasTexCoords)
        {
            albedo = pow(albedo, vec3(2.2));
        }
        vec3 directLight = calculatePBR(albedo, N, V, L);
        vec3 indirectApprox = ambientStreantgh * ambientColor * albedo * (1.0 - clamp(metallic, 0.0, 1.0));
        vec3 environmentLight = vec3(0.0);
        if (usePBREnvironment) {
            environmentLight = calculateEnvironmentPBR(albedo, N, V);
        }

        vec3 linearColor = indirectApprox + visibility * directLight + environmentLight;
        vec3 mappedColor = linearColor / (linearColor + vec3(1.0));

        finalColor = pow(max(mappedColor, vec3(0.0)), vec3(1.0 / 2.2));
    }

    // for env mapping with pbr base
    if (!usePBR && reflectionStrength > 0.0) {
        vec3 reflectionDirection = reflect(-V, N);
        vec3 environmentColor = texture(environmentMap, reflectionDirection).rgb;
        finalColor = mix(finalColor, environmentColor, reflectionStrength);
    }

    if(unlit){
        finalColor = baseColor;
    }

    if (enableHaze) {
        vec3 toSurface = fragPosition - cameraPos;
        float distanceToCamera = length(toSurface);
        vec3 viewDirection = toSurface / max(distanceToCamera, 0.000001);
        float skyBlend = pow(clamp(viewDirection.y, 0.0, 1.0), 0.55);
        vec3 hazeColor = mix(horizonColor, zenithColor, skyBlend);
        float haze = smoothstep(25.0, 90.0, distanceToCamera);
        finalColor = mix(finalColor, hazeColor, haze);
    }

    fragColor = vec4(finalColor, 1.0);
}
