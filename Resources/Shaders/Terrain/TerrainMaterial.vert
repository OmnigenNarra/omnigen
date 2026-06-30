#version 430

uniform mat4 viewProjection;

in vec3 pos;
in vec3 normal;
in uint terrainWeights;
in uint biomeWeights;
in uint packParams;
in float dispFac;

const float texSlotDeconverter = 2.0 / 255.0;

out VertexData
{
    vec3 pos;
    vec3 normal;
    float terrainTextureWeights[4];
    float biomeTextureWeights[4];
    float terrainTexSlot;
    float biomeTexSlot;
    float roadWeight;
    float debugValue;
} VertexOut;

void main(void)
{
    gl_Position = viewProjection * vec4(pos.xyz, 1);
    VertexOut.pos = pos;
    VertexOut.normal = normal;
    VertexOut.terrainTexSlot = float((packParams & (0xFF << 0)) >> 0) * texSlotDeconverter;
    VertexOut.biomeTexSlot = float((packParams & (0xFF << 8)) >> 8) * texSlotDeconverter;
    VertexOut.roadWeight = float((packParams & (0xFF << 16)) >> 16) / 255.0f;
    VertexOut.debugValue = float((packParams & (0xFF << 24)) >> 24) / 255.0f;

    for (uint i=0; i<4; ++i)
    {
        uint off = 8 * i;
        VertexOut.terrainTextureWeights[i] = float((terrainWeights & (0xFF << off)) >> off) / 255.0f;
        VertexOut.biomeTextureWeights[i] = float((biomeWeights & (0xFF << off)) >> off) / 255.0f;
    }
}