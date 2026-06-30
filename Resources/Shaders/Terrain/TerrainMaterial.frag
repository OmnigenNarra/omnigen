#version 430
const vec3 lightDirection = vec3(0.57735026919f, -0.57735026919f, 0.57735026919f);
const float ambient = 0.2f;

const float worldDim = 50 * 10000.0f;
const float verticalMajorPlaneThreshold = sqrt(3)/3;
const float splatDepth = 0.2;

const int terrainPackSize = 4;
const int coverPackSize = 1;

uniform float terrainTileSizes[64];
uniform float coverTileSizes[64];
uniform uint terrainTexIds[4];
uniform uint biomeTexIds[4];
uniform sampler2DArray terrainDiffuseHeight;
uniform sampler2DArray terrainNormal;
uniform sampler2DArray coverDiffuseHeight;
uniform sampler2DArray coverNormal;
uniform sampler2D tilingNoise;
uniform vec4 debugColor;
uniform int objectID;

in VertexData
{
    vec3 pos;
    vec3 normal;
    float terrainTextureWeights[4];
    float biomeTextureWeights[4];
    float terrainTexSlot;
    float biomeTexSlot;
    float roadWeight;
    float debugValue;
} PixelIn;

layout (location = 0) out vec4 fragColor;
layout (location = 1) out vec4 outData;

struct PixelData
{
    vec4 rgba;
    vec4 normal;
};

vec3 getMatTextureCoords(float tileSize)
{
    return vec3(
        mod(PixelIn.pos.x, tileSize), 
        mod(PixelIn.pos.y, tileSize), 
        mod(PixelIn.pos.z, tileSize)
    ) / tileSize;
}

// Global variables
vec3 normal;
vec4 normalBaseQuat;

// Anti-tiling variations
float variantMixFactor;
vec3 offsetA;
vec3 offsetB;
vec4 quatA;
vec4 quatB;

// Two stages of terrain (erosion) to blend
ivec3 terrainSlots;
// Blend factors
float terrainSlotMixFactor;

// Biplanar texturing
vec2 biplanarBlendFactors;
float biplanarBlendFactorSum;

// [variant] [major/minor plane]
vec4 texGradients[2][2];
// [variant] [major/minor plane]
vec2 texCoords[2][2];

vec4 makeRotationQuaternion(vec3 start, vec3 dest)
{
	float cosTheta = dot(start, dest);

    // special case: opposite vectors
    if (cosTheta < -1 + 0.01f)
        return vec4(0,0,0,0); // special-case'd later

	vec3 rotationAxis = cross(start, dest);

	float s = sqrt( (1+cosTheta)*2 );
	float invs = 1 / s;

	return vec4(
		rotationAxis.x * invs,
		rotationAxis.y * invs,
		rotationAxis.z * invs,
        s * 0.5f
	);
}

vec3 rotateVector(in vec3 v, in vec4 q)
{
    // special case: opposite vectors
    if (q == vec4(0,0,0,0))
        return -v;

    // Extract the vector part of the quaternion
    vec3 u = vec3(q.x, q.y, q.z);

    // Extract the scalar part of the quaternion
    float s = q.w;

    // Do the math
    return vec3(2.0f * dot(u, v) * u
          + (s*s - dot(u, u)) * v
          + 2.0f * s * cross(u, v));
}

void computeTilingData()
{
    // Random variation
    const float variants = 6.0;

    const float blendTileSize = 40000;
    const float offsetStrength = 5.0;
    const float rotationStrength = 1.0;

    const vec2 blendCoords = getMatTextureCoords(blendTileSize).xz;
    const float variantPacked = texture( tilingNoise, blendCoords).x;

    // Variant info
    float variant = variantPacked * variants;
    int variantA = int(floor(variant));
    int variantB = int(mod(variantA + 1, variants));
    variantMixFactor = variant - float(variantA);

    // Offsets
    offsetA = offsetStrength * sin(vec3(3.0,3.0,7.0)*float(variantA));
    offsetB = offsetStrength * sin(vec3(3.0,3.0,7.0)*float(variantB));

    // Rotations
    const vec3 dirs[int(variants)] = 
    {
        vec3(0,1,0),
        vec3(-1,0,0),
        vec3(0,0,-1),
        vec3(1,0,0),
        vec3(0,0,1),
        vec3(0,-1,0),
    };

    quatA = makeRotationQuaternion(dirs[0], dirs[variantA]);
    quatB = makeRotationQuaternion(dirs[0], dirs[variantB]);
}

void computeTextureSamplingData()
{
    // Terrain slots
    float terrainTexSlot = clamp(PixelIn.terrainTexSlot, 0, 2);
    terrainSlots[0] = int(floor(terrainTexSlot));
    terrainSlots[1] = int(ceil(terrainTexSlot));
    terrainSlots[2] = 3; // vertical
    terrainSlotMixFactor = terrainTexSlot - terrainSlots[0];

    // Biplanar texturing dominant planes
    vec3 aNormal = abs(normal);
    ivec3 planes;

    if (aNormal.x > aNormal.y) // x > y
    {
        if (aNormal.x > aNormal.z) // x > (y ? z) 
        {
            if (aNormal.y > aNormal.z) // x > y > z
            {
                planes = ivec3(0,1,2);
            }
            else // x > z > y
            {
                planes = ivec3(0,2,1);
            }
        }
        else // z > x > y
        {
            planes = ivec3(2,0,1);
        }
    }
    else // y > x
    {
        if (aNormal.y > aNormal.z) // y > (x ? z) 
        {
            if (aNormal.x > aNormal.z) // y > x > z
            {
                planes = ivec3(1,0,2);
            }
            else // y > z > x
            {
                planes = ivec3(1,2,0);
            }
        }
        else // z > y > x
        {
            planes = ivec3(2,1,0);
        }
    }

    // base factors from normal
    biplanarBlendFactors = vec2(aNormal[planes[0]], aNormal[planes[1]]);
    // make local support
    biplanarBlendFactors = clamp( (biplanarBlendFactors - 0.5773) / (1.0 - 0.5773), 0.0, 1.0 );
    // shape transition
    biplanarBlendFactors = pow( biplanarBlendFactors, vec2(1.0/8.0) );
    // precompute sum
    biplanarBlendFactorSum = biplanarBlendFactors.x + biplanarBlendFactors.y;

    // Texture coords and derivatives
    // Variant A
    vec3 baseCoords = getMatTextureCoords(terrainTileSizes[0]);
    vec3 matCoordsA = rotateVector(baseCoords - 0.5, vec4(1,0,0,0)) + offsetA + 0.5; // TODO apply quatA
    texCoords[0][0] = vec2(matCoordsA[planes[2]], matCoordsA[planes[1]]);
    texCoords[0][1] = vec2(matCoordsA[planes[2]], matCoordsA[planes[0]]);

    // major plane
    texGradients[0][0].xy = dFdx(texCoords[0][0]);
    texGradients[0][0].zw = dFdy(texCoords[0][0]);
    // minor plane
    texGradients[0][1].xy = dFdx(texCoords[0][1]);
    texGradients[0][1].zw = dFdy(texCoords[0][1]);

    // Variant B
    vec3 matCoordsB = rotateVector(baseCoords - 0.5, vec4(1,0,0,0)) + offsetB + 0.5; // TODO apply quatB
    texCoords[1][0] = vec2(matCoordsB[planes[2]], matCoordsB[planes[1]]);
    texCoords[1][1] = vec2(matCoordsB[planes[2]], matCoordsB[planes[0]]);

    // major plane
    texGradients[1][0].xy = dFdx(texCoords[1][0]);
    texGradients[1][0].zw = dFdy(texCoords[1][0]);
    // minor plane
    texGradients[1][1].xy = dFdx(texCoords[1][1]);
    texGradients[1][1].zw = dFdy(texCoords[1][1]);
}

void textureTerrainNoTile( in sampler2DArray texArray, in uint chunkTexIdx, in uint plane, inout vec4 results[2])
{
    uint texPack = terrainTexIds[chunkTexIdx];
    uint texIdxA = terrainPackSize * texPack + terrainSlots[0];
    uint texIdxB = terrainPackSize * texPack + terrainSlots[1];

    vec4 slot_A_variant_A = textureGrad( texArray, vec3(texCoords[0][plane], texIdxA), texGradients[0][plane].xy, texGradients[0][plane].zw );
    vec4 slot_A_variant_B = textureGrad( texArray, vec3(texCoords[1][plane], texIdxA), texGradients[1][plane].xy, texGradients[1][plane].zw );
    results[0] = mix(slot_A_variant_A, slot_A_variant_B, variantMixFactor);

    vec4 slot_B_variant_A = textureGrad( texArray, vec3(texCoords[0][plane], texIdxB), texGradients[0][plane].xy, texGradients[0][plane].zw );
    vec4 slot_B_variant_B = textureGrad( texArray, vec3(texCoords[1][plane], texIdxB), texGradients[1][plane].xy, texGradients[1][plane].zw );
    results[1] = mix(slot_B_variant_A, slot_B_variant_B, variantMixFactor);
}

vec4 textureTerrainNoTileVertical( in sampler2DArray texArray, in uint chunkTexIdx, in uint plane)
{
    uint texPack = terrainTexIds[chunkTexIdx];
    uint texIdx = terrainPackSize * texPack + (terrainPackSize - 1); // last slot in pack

    vec4 variant_A = textureGrad( texArray, vec3(texCoords[0][plane], texIdx), texGradients[0][plane].xy, texGradients[0][plane].zw );
    vec4 variant_B = textureGrad( texArray, vec3(texCoords[1][plane], texIdx), texGradients[1][plane].xy, texGradients[1][plane].zw );

    return mix(variant_A, variant_B, variantMixFactor);
}

vec4 textureGrassNoTile( in sampler2DArray texArray, in uint chunkTexIdx, in uint plane)
{
    uint texPack = biomeTexIds[chunkTexIdx];
    uint texIdx = coverPackSize * texPack;

    vec4 variant_A = textureGrad( texArray, vec3(texCoords[0][plane], texIdx), texGradients[0][plane].xy, texGradients[0][plane].zw );
    vec4 variant_B = textureGrad( texArray, vec3(texCoords[1][plane], texIdx), texGradients[1][plane].xy, texGradients[1][plane].zw );

    vec4 result = mix(variant_A, variant_B, variantMixFactor);
    result.a = 1;
    return result;
}

vec4 biplanarBiomeComponent(in sampler2DArray texArray, in uint chunkTexIdx)
{
    // project+fetch
    vec4 x = textureGrassNoTile(texArray, chunkTexIdx, 0);
    vec4 y = textureGrassNoTile(texArray, chunkTexIdx, 1);
    
    // blend and return
    return (x * biplanarBlendFactors.x + y * biplanarBlendFactors.y) / biplanarBlendFactorSum;
}

vec4 biplanarBiomeNormalComponent(in sampler2DArray texArray, in uint chunkTexIdx)
{
    // project+fetch
    vec4 x = textureGrassNoTile(texArray, chunkTexIdx, 0);
    vec4 y = textureGrassNoTile(texArray, chunkTexIdx, 1);

    // transform to pixel space
    x.xyz = rotateVector(x.xyz, normalBaseQuat);
    y.xyz = rotateVector(y.xyz, normalBaseQuat);
    
    // blend and return
    return (x*biplanarBlendFactors.x + y*biplanarBlendFactors.y) / biplanarBlendFactorSum;
}

void advancedSplat(in vec4 dhA, in vec4 dhB, in vec3 nA, in vec3 nB, in float w1, in float w2, inout PixelData result)
{
    // Advanced splatting
    float ma = max(dhA.a + w1, dhB.a + w2) - splatDepth;

    float b1 = max(dhA.a + w1 - ma, 0);
    float b2 = max(dhB.a + w2 - ma, 0);

    result.rgba = (dhA * b1 + dhB * b2) / (b1 + b2);
    result.normal.xyz = (nA.xyz * b1 + nB.xyz * b2) / (b1 + b2);
}

PixelData biplanarTerrainTex(in uint chunkTexIdx)
{
    PixelData result;
    vec4 dSlots[2], nSlots[2];
    PixelData M, m;

    // Major plane:
    textureTerrainNoTile(terrainDiffuseHeight, chunkTexIdx, 0, dSlots);
    textureTerrainNoTile(terrainNormal, chunkTexIdx, 0, nSlots);
    advancedSplat(dSlots[0], dSlots[1], nSlots[0].xyz, nSlots[1].xyz, 1.0 - terrainSlotMixFactor, terrainSlotMixFactor, M);

    // Minor plane:
    textureTerrainNoTile(terrainDiffuseHeight, chunkTexIdx, 1, dSlots);
    textureTerrainNoTile(terrainNormal, chunkTexIdx, 1, nSlots);
    advancedSplat(dSlots[0], dSlots[1], nSlots[0].xyz, nSlots[1].xyz, 1.0 - terrainSlotMixFactor, terrainSlotMixFactor, m);
    
    // Biplanar blend diffuse
    result.rgba = (M.rgba * biplanarBlendFactors.x + m.rgba * biplanarBlendFactors.y) / biplanarBlendFactorSum;

    // Transform normal to pixel space
    M.normal.xyz = rotateVector(M.normal.xyz, normalBaseQuat);
    m.normal.xyz = rotateVector(m.normal.xyz, normalBaseQuat);
    
    // Biplanar blend normal
    result.normal.xyz = (M.normal.xyz * biplanarBlendFactors.x + m.normal.xyz * biplanarBlendFactors.y) / biplanarBlendFactorSum;
    result.normal.w = 0;
    result.normal = normalize(result.normal);

    return result;
}

PixelData biplanarBiomeTex(in uint chunkTexIdx)
{
    PixelData result;

    // Color
    result.rgba = biplanarBiomeComponent(coverDiffuseHeight, chunkTexIdx);
    result.rgba.a = 1;

    // Normal
    result.normal = biplanarBiomeNormalComponent(coverNormal, chunkTexIdx);
    result.normal.w = 0;
    result.normal = normalize(result.normal);

    return result;
}

PixelData getColorForTerrainComponent(in uint i)
{
    // Query rock slab / rock grain / soil
    PixelData baseColor = biplanarTerrainTex(i);

    // Vertical sections
    const float threshold = 0.95f;
    if (abs(normal.y) <= threshold)
    {
        float mixFactor = pow(abs(normal.y) / threshold, 8);

        uint plane = (abs(normal.y) < verticalMajorPlaneThreshold) ? 0 : 1;
        vec4 verticalColor = textureTerrainNoTileVertical(terrainDiffuseHeight, i, plane);
        verticalColor.a = 1;
        vec4 verticalNormal = textureTerrainNoTileVertical(terrainNormal, i, plane);
        verticalNormal = vec4(rotateVector(verticalNormal.xyz, normalBaseQuat), 1);

        baseColor.rgba = mix(verticalColor, baseColor.rgba, mixFactor);
        baseColor.normal = mix(verticalNormal, baseColor.normal, mixFactor);
    }

    return baseColor;
}

vec4 addHue(vec4 color, float hueAdjust) {
    //This is based on a stack overflow post that claims that trying to perform these kinds of adjustments 
    // in color spaces other than YIQ can be troublesome.

    const vec4  kRGBToYPrime = vec4 (0.299, 0.587, 0.114, 0.0);
    const vec4  kRGBToI     = vec4 (0.596, -0.275, -0.321, 0.0);
    const vec4  kRGBToQ     = vec4 (0.212, -0.523, 0.311, 0.0);

    const vec4  kYIQToR   = vec4 (1.0, 0.956, 0.621, 0.0);
    const vec4  kYIQToG   = vec4 (1.0, -0.272, -0.647, 0.0);
    const vec4  kYIQToB   = vec4 (1.0, -1.107, 1.704, 0.0);

    // Convert to YIQ
    float   YPrime  = dot (color, kRGBToYPrime);
    float   I      = dot (color, kRGBToI);
    float   Q      = dot (color, kRGBToQ);

    // Calculate the hue and chroma
    float   hue     = atan (Q, I);
    float   chroma  = sqrt (I * I + Q * Q);

    // Make the adjustments
    hue += hueAdjust;

    // Convert back to YIQ
    Q = chroma * sin (hue);
    I = chroma * cos (hue);

    // Convert back to RGB
    vec4    yIQ   = vec4 (YPrime, I, Q, 0.0);
    color.r = dot (yIQ, kYIQToR);
    color.g = dot (yIQ, kYIQToG);
    color.b = dot (yIQ, kYIQToB);

    return color;
}

void main(void)
{
    normal = normalize(PixelIn.normal);
    normalBaseQuat = makeRotationQuaternion(vec3(0,0,1), normal);

    computeTilingData();
    computeTextureSamplingData();

    PixelData baseColor;

    // Grass
    // Blending
    float grassWeight;
    for (uint i=0; i<4; ++i)
    {
        float coef = PixelIn.biomeTextureWeights[i];
        if (coef > 0.0f)
        {
            PixelData data = biplanarBiomeTex(i);
            baseColor.rgba += data.rgba * coef;
            baseColor.normal.xyz += data.normal.xyz * coef;

            grassWeight += coef;
        }
    }

    if (grassWeight > 0.0f)
    {
        // Normalization
        baseColor.rgba /= grassWeight;
        baseColor.normal = normalize(baseColor.normal);
    }

    grassWeight = min(grassWeight, 0.9);    
    //fragColor = vec4(PixelIn.roadWeight, PixelIn.roadWeight, PixelIn.roadWeight, 1);
    //return;

    // Litho textures - advanced splatting
    if (grassWeight < 1.0f)
    {
        PixelData rockColor;

        // Components and max height
        PixelData comps[4];
        float ma = 0.0f;
        for (uint i=0; i<4; ++i)
        {
            if (PixelIn.terrainTextureWeights[i] > 0.0f)
            {
                comps[i] = getColorForTerrainComponent(i);
                ma = max(comps[i].rgba.a, ma);
            }
        }
        ma -= splatDepth;

        // Blending
        float coefSum = 0.0f;
        for (uint i=0; i<4; ++i)
        {
            if (PixelIn.terrainTextureWeights[i] > 0.0f)
            {
                float coef = max(comps[i].rgba.a + PixelIn.terrainTextureWeights[i] - ma, 0);

                rockColor.rgba += comps[i].rgba * coef;
                rockColor.normal.xyz += comps[i].normal.xyz * coef;

                coefSum += coef;
            }
        }

        // Normalization
        rockColor.rgba /= coefSum;
        rockColor.normal = normalize(rockColor.normal);

        // Blend with grass
        advancedSplat(rockColor.rgba, baseColor.rgba, rockColor.normal.xyz, baseColor.normal.xyz, 1.0, grassWeight, baseColor);
    }

    normal = normalize(baseColor.normal.xyz);

    if (debugColor != vec4(0,0,0,0))
        baseColor.rgba = mix(debugColor, baseColor.rgba, 0.3);

    // Lambert
    float factor = dot(lightDirection, -normal);
    factor = clamp(factor, 0.0f, 1.0f - ambient) + ambient;

    if (PixelIn.roadWeight > 0.0001f) {
        //Add hue
        fragColor = mix(vec4(0.69,0.48,0.24,1), baseColor.rgba, 0.5);
        return;
    }

    fragColor = vec4(baseColor.rgba.rgb * factor, 1);
    //fragColor = vec4(PixelIn.debugValue, PixelIn.debugValue, PixelIn.debugValue, 1);

    outData = vec4(objectID, 0, gl_PrimitiveID, 1);
}