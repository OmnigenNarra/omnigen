#version 430
in vec3 pos;
in vec2 uv;

uniform mediump mat4 view;

out vec2 TexCoord;

void main()
{
    gl_Position = view * vec4(pos.xyz, 1.0);
    TexCoord = uv;
}