#version 430

struct MeshAssetInstanceData
{
    mat4 world;
    vec3 surfaceNormal;
};

uniform mat4 viewProjection;

layout(std430, binding = 0) buffer allWorldMatrices
{
    MeshAssetInstanceData instanceData[];
};

in vec3 pos;
in vec3 normal;
in uint matId;
in vec2 UV;
in uint gInstanceIdx;
in vec3 surfaceNormal;

out vec3 pNormal;
out vec3 pSurfaceNormal;
flat out uint pMatId;
out vec2 pUV;
out flat uint instanceID;

void main(void)
{
    gl_Position = viewProjection * instanceData[gInstanceIdx].world * vec4(pos, 1);
    mat3 normalWorldMatrix = transpose(inverse(mat3(instanceData[gInstanceIdx].world)));
    pNormal = normalWorldMatrix * normal;
    pMatId = matId;
    pUV = UV;
    pSurfaceNormal = surfaceNormal;
    instanceID = gInstanceIdx;
}