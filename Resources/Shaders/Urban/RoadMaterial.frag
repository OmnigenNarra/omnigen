#version 430

uniform sampler2D roadTexture;
uniform sampler2D crossTexture;
uniform bool isCross;

in vec2 TexCoord;

out vec4 fragColor;

void main()
{
    vec3 colorRoad = texture(roadTexture, TexCoord).rgb;
    vec3 colorCross = texture(crossTexture, TexCoord).rgb;

    fragColor = vec4(isCross ? colorCross : colorRoad, 1.0);
}