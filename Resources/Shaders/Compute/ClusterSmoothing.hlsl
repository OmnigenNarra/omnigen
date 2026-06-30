#pragma warning( disable : 3557 )

struct BorderPointInfo
{
    float3 pos;

    float distance;                 // edited
    float weight;                   // edited
    uint terrainTexWeights;
    uint biomeTexWeights;
    uint packParams;
    float temperature;
    float humidity;
};

struct TerrainMeshVertex
{
    float3 pos;                     // edited
    float3 normal;
    uint terrainTexWeights;         // edited
    uint biomeTexWeights;           // edited
    uint packParams;                // edited
    float displacementFactor;
    float temperature;
    float humidity;
};

// Input
StructuredBuffer<float> smoothingRange;

// Input /Output
RWStructuredBuffer<BorderPointInfo> borderPts;
RWStructuredBuffer<TerrainMeshVertex> pts;

float smoothstep(float t)
{
    return t * t * t * t * (35.0f - 84.0f * t + 70.0f * t * t - 20.0f * t * t * t);
}

float halfSmoothstep(float t)
{
    float x = t * 0.5f + 0.5f;
    return 2.0f * smoothstep(x) - 1.0f;
}

float getTexWeight(uint weights, uint idx)
{
    uint off = 8 * idx;
    return float((weights & (0xFF << off)) >> off) / 255.0f;
}

float getPackParam(uint params, uint idx)
{
    uint off = 8 * idx;
    return float((params & (0xFF << off)) >> off) / 255.0f;
}

uint compileTexWeights(float4 weights)
{
    uint result = 0;
    for (uint i = 0; i < 4; ++i)
    {
        if (weights[i] > 0.0f)
        {
            uint off = 8 * i;
            result += (uint(round(weights[i] * 255.0f)) << off);
        }
    }

    return result;
}

uint compilePackParams(float4 params)
{
    uint result = 0;
    for (uint i = 0; i < 4; ++i)
    {
        if (params[i] > 0.0f)
        {
            uint off = 8 * i;
            result += (uint(round(params[i] * 255.0f)) << off);
        }
    }

    return result;
}

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float range = smoothingRange[0];
    uint pointIdx = DTid.x;
    uint i;

    uint bpSize, unused_stride;
    borderPts.GetDimensions(bpSize, unused_stride);

    ///////////////////////////////////////////////////////
    float minD = range + 1.0f;
    uint bestBp = 0;
    bool needsSmoothing = false;

    for (i = 0; i < bpSize; ++i)
    {
        float d = distance(float2(pts[pointIdx].pos.x, pts[pointIdx].pos.z), float2(borderPts[i].pos.x, borderPts[i].pos.z));
        if (d >= range)
            continue;

        needsSmoothing = true;
        borderPts[i].distance = d;

        if (d < minD)
        {
            minD = d;
            bestBp = i;
        }
    }

    if (!needsSmoothing)
        return;

    if (minD < 1.0f)
    {
        pts[pointIdx].pos.y = borderPts[bestBp].pos.y;
        pts[pointIdx].terrainTexWeights = borderPts[bestBp].terrainTexWeights;
        pts[pointIdx].biomeTexWeights = borderPts[bestBp].biomeTexWeights;
        pts[pointIdx].packParams = borderPts[bestBp].packParams;
        return;
    }

    // MAYBE: Fix and restore weighted approach later?
    /*float weightSum = 0.0f;
    for (i = 0; i < bpSize; ++i)
    {
        if (borderPts[i].distance > 0.0f)
        {
            borderPts[i].weight = minD / borderPts[i].distance;
            weightSum += borderPts[i].weight;
        }
    }

    float targetH = 0.0f;
    for (i = 0; i < bpSize; ++i)
        if (borderPts[i].weight > 0.0f)
            targetH += (borderPts[i].pos.y * borderPts[i].weight / weightSum);*/

    float f = halfSmoothstep(minD / range);
    pts[pointIdx].pos.y = lerp(borderPts[bestBp].pos.y, pts[pointIdx].pos.y, f);

    float4 blendedTerrainWeights;
    blendedTerrainWeights.x = lerp(getTexWeight(borderPts[bestBp].terrainTexWeights, 0), getTexWeight(pts[pointIdx].terrainTexWeights, 0), f);
    blendedTerrainWeights.y = lerp(getTexWeight(borderPts[bestBp].terrainTexWeights, 1), getTexWeight(pts[pointIdx].terrainTexWeights, 1), f);
    blendedTerrainWeights.z = lerp(getTexWeight(borderPts[bestBp].terrainTexWeights, 2), getTexWeight(pts[pointIdx].terrainTexWeights, 2), f);
    blendedTerrainWeights.w = lerp(getTexWeight(borderPts[bestBp].terrainTexWeights, 3), getTexWeight(pts[pointIdx].terrainTexWeights, 3), f);

    pts[pointIdx].terrainTexWeights = compileTexWeights(blendedTerrainWeights);

    float4 blendedBiomeWeights;
    blendedBiomeWeights.x = lerp(getTexWeight(borderPts[bestBp].biomeTexWeights, 0), getTexWeight(pts[pointIdx].biomeTexWeights, 0), f);
    blendedBiomeWeights.y = lerp(getTexWeight(borderPts[bestBp].biomeTexWeights, 1), getTexWeight(pts[pointIdx].biomeTexWeights, 1), f);
    blendedBiomeWeights.z = lerp(getTexWeight(borderPts[bestBp].biomeTexWeights, 2), getTexWeight(pts[pointIdx].biomeTexWeights, 2), f);
    blendedBiomeWeights.w = lerp(getTexWeight(borderPts[bestBp].biomeTexWeights, 3), getTexWeight(pts[pointIdx].biomeTexWeights, 3), f);

    pts[pointIdx].biomeTexWeights = compileTexWeights(blendedBiomeWeights);

    pts[pointIdx].temperature = lerp(borderPts[bestBp].temperature, pts[pointIdx].temperature, f);
    pts[pointIdx].humidity = lerp(borderPts[bestBp].humidity, pts[pointIdx].humidity, f);

    // Not blending pack params
    //pts[pointIdx].packParams = lerp(borderPts[bestBp].packParams, pts[pointIdx].packParams, f);
}