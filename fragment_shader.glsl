#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D texture_diffuse1;

uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;

uniform vec3 objectColor; // for fireflies
uniform int solidMode;    // 0 = textured, 1 = solid objectColor

void main()
{
    // Solid color override (debug/fireflies)
    if (solidMode == 1) {
        FragColor = vec4(objectColor, 1.0);
        return;
    }

    // --- Texture ---
    vec4 texColor = texture(texture_diffuse1, TexCoord);
    // Discard fully transparent fragments to avoid unintended glow color leaking
    if (texColor.a < 0.1) discard;

    // --- Lighting (simple directional) ---
    vec3 norm = normalize(Normal);
    vec3 lightDirNorm = normalize(-lightDir);
    // Slightly soften diffuse contribution a bit more
    float diff = max(dot(norm, lightDirNorm), 0.0) * 0.80;

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(lightDirNorm, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 16.0) * 0.06; // slightly softer specular

    // A touch more ambient to make scene a bit gentler
    float ambient = 0.24;
    vec3 lighting = (ambient + diff + spec) * lightColor;

    vec3 color = texColor.rgb * lighting;

    // --- Fog ---
    float distance = length(viewPos - FragPos);
    float fogFactor = 1.0 - exp(-pow(distance * fogDensity, 2.0));
    fogFactor = clamp(fogFactor, 0.0, 1.0);

    vec3 finalColor = mix(color, fogColor, fogFactor);

    FragColor = vec4(finalColor, 1.0);
}
