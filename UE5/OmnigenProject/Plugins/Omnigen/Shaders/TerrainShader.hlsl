struct FPixelData
{
	float4 Color;
	float3 Normal;
};

struct FUtils
{
	// Copy from entrypoint
	FMaterialPixelParameters Parameters;
	SamplerState RockDiffuseHeightArraySampler;
	SamplerState RockNormalArraySampler;
	SamplerState CoverDiffuseHeightArraySampler;
	SamplerState CoverNormalArraySampler;

	Texture2DArray RockDiffuseHeightArray;
	Texture2DArray RockNormalArray;
	float4 RockTileSizes;					// TODO array
	float4 RockMaxDisplacements;			// TODO array
	float4 RockTexIDs;
	float4 RockTexWeights;
	float RockSlot;
 
	Texture2DArray CoverDiffuseHeightArray;
	Texture2DArray CoverNormalArray;
	float4 CoverTileSizes;					// TODO array
	float4 CoverMaxDisplacements;			// TODO array
	float4 CoverTexIDs;
	float4 CoverTexWeights;
	
	Texture2D TilingNoise;

	// Constants
	float SplatDepth;
	float VerticalMajorPlaneThreshold;

	float3 WorldPos;
	float3 Normal;

	// Stages of terrain (erosion) to blend
	uint2 RockSlots;
	// Blend factors
	float RockSlotMixFactor;

	// Biplanar texturing
	float2 BiplanarBlendFactors;
	float BiplanarBlendFactorSum;

	// [major/minor plane]
	float2 TexCoords[2];


	float3 GetMatTextureCoords(float TileSize)
	{
		return float3((WorldPos.x) % TileSize, (WorldPos.y) % TileSize, (WorldPos.z) % TileSize) / TileSize;
	}
	
	float3 SortPlanes(float3 aNormal)
	{
		if (aNormal.x > aNormal.y) // x > y
		{
			if (aNormal.x > aNormal.z) // x > (y ? z) 
			{
				if (aNormal.y > aNormal.z) // x > y > z
				{
					return float3(0,1,2);
				}
				else // x > z > y
				{
					return float3(0,2,1);
				}
			}
			else // z > x > y
			{
				return float3(2,0,1);
			}
		}
		else // y > x
		{
			if (aNormal.y > aNormal.z) // y > (x ? z) 
			{
				if (aNormal.x > aNormal.z) // y > x > z
				{
					return float3(1,0,2);
				}
				else // y > z > x
				{
					return float3(1,2,0);
				}
			}
			else // z > y > x
			{
				return float3(2,1,0);
			}
		}

		return float3(0,0,0);
	}

	void Init
	(
		FMaterialPixelParameters InParameters,
		SamplerState InRockDiffuseHeightArraySampler,
		SamplerState InRockNormalArraySampler,
		SamplerState InCoverDiffuseHeightArraySampler,
		SamplerState InCoverNormalArraySampler,

		Texture2DArray InRockDiffuseHeightArray,
		Texture2DArray InRockNormalArray,
		float4 InRockTileSizes,
		float4 InRockMaxDisplacements,
		float4 InRockTexIDs,
		float4 InRockTexWeights,
		float InRockSlot,

		Texture2DArray InCoverDiffuseHeightArray,
		Texture2DArray InCoverNormalArray,
		float4 InCoverTileSizes,
		float4 InCoverMaxDisplacements,
		float4 InCoverTexIDs,
		float4 InCoverTexWeights,

		Texture2D InTilingNoise
	)
	{
		Parameters						= InParameters;
		RockDiffuseHeightArraySampler	= InRockDiffuseHeightArraySampler;
		RockNormalArraySampler			= InRockNormalArraySampler;
		CoverDiffuseHeightArraySampler	= InCoverDiffuseHeightArraySampler;
		CoverNormalArraySampler			= InCoverNormalArraySampler;

		RockDiffuseHeightArray			= InRockDiffuseHeightArray;
		RockNormalArray					= InRockNormalArray;
		RockTileSizes					= InRockTileSizes;
		RockMaxDisplacements			= InRockMaxDisplacements;
		RockTexIDs						= InRockTexIDs;
		RockTexWeights					= InRockTexWeights;
		RockSlot						= InRockSlot;

		CoverDiffuseHeightArray			= InCoverDiffuseHeightArray;
		CoverNormalArray				= InCoverNormalArray;
		CoverTileSizes					= InCoverTileSizes;
		CoverMaxDisplacements			= InCoverMaxDisplacements;
		CoverTexIDs						= InCoverTexIDs;
		CoverTexWeights					= InCoverTexWeights;

		TilingNoise						= InTilingNoise;

		SplatDepth = 0.3f;
		VerticalMajorPlaneThreshold = 0.5773502691896258f; //sqrt(3)/3;

		WorldPos = LWCToFloat(GetWorldPosition(Parameters));
		Normal = normalize(Parameters.TangentToWorld[2]);

		// Biplanar texturing dominant planes
		float3 aNormal = abs(Normal);
		float3 Planes = SortPlanes(aNormal);

		RockSlots[0] = uint(floor(RockSlot));
		float3 Coords3D = GetMatTextureCoords(RockTileSizes[0]);
		
		const float VThreshold = 0.95f;
		if (aNormal.z > VThreshold)
		{
			RockSlots[1] = uint(ceil(RockSlot));
			RockSlotMixFactor = RockSlot - RockSlots[0];

			TexCoords[0] = float2(Coords3D[Planes[2]], Coords3D[Planes[1]]);
			TexCoords[1] = float2(Coords3D[Planes[2]], Coords3D[Planes[0]]);
		}
		else
		{
			RockSlots[1] = 3; // vertical
			RockSlotMixFactor = 1.0f - pow(aNormal.z / VThreshold, 8);

			TexCoords[0] = Coords3D.xz;
			TexCoords[1] = Coords3D.yz;
		}

		// base factors from normal
		BiplanarBlendFactors = float2(aNormal[Planes[0]], aNormal[Planes[1]]);
		// make local support
		BiplanarBlendFactors = clamp((BiplanarBlendFactors - VerticalMajorPlaneThreshold) / (1.0f - VerticalMajorPlaneThreshold), 0.0f, 1.0f);
		// shape transition
		BiplanarBlendFactors = pow(BiplanarBlendFactors, float2(1.0f, 1.0f) / 8.0f);
		// precompute sum
		BiplanarBlendFactorSum = BiplanarBlendFactors.x + BiplanarBlendFactors.y;
	}

	FPixelData BiplanarBlend(FPixelData A, FPixelData B)
	{
		FPixelData Result;
		Result.Color = (A.Color * BiplanarBlendFactors.x + B.Color * BiplanarBlendFactors.y) / BiplanarBlendFactorSum;

		Result.Normal = (A.Normal * BiplanarBlendFactors.x + B.Normal * BiplanarBlendFactors.y) / BiplanarBlendFactorSum;
		Result.Normal = normalize(Result.Normal);
		return Result;
	}

	FPixelData AdvancedSplat(FPixelData A, FPixelData B, float WeightA, float WeightB)
	{
		float ma = max(A.Color.a + WeightA, B.Color.a + WeightB) - SplatDepth;
		float f1 = max(A.Color.a + WeightA - ma, 0);
		float f2 = max(B.Color.a + WeightB - ma, 0);

		FPixelData Result;
		Result.Color = (A.Color * f1 + B.Color * f2) / (f1 + f2);
		Result.Normal = (A.Normal * f1 + B.Normal * f2) / (f1 + f2);
		return Result;
	}

	FPixelData SampleRock(uint RockIdx, uint Slot, uint PlaneIdx)
	{
		FPixelData Result;
		Result.Color = Texture2DArraySample(RockDiffuseHeightArray, RockDiffuseHeightArraySampler, float3(TexCoords[PlaneIdx], 4 * RockIdx + RockSlots[Slot]));
		Result.Normal = Texture2DArraySample(RockNormalArray, RockNormalArraySampler, float3(TexCoords[PlaneIdx], 4 * RockIdx + RockSlots[Slot])).xyz;
		return Result;
	}

	FPixelData SampleCover(uint CoverIdx, uint PlaneIdx)
	{
		FPixelData Result;
		Result.Color = Texture2DArraySample(CoverDiffuseHeightArray, CoverDiffuseHeightArraySampler, float3(TexCoords[PlaneIdx], CoverIdx));
		Result.Normal = Texture2DArraySample(CoverNormalArray, CoverNormalArraySampler, float3(TexCoords[PlaneIdx], CoverIdx)).xyz;
		return Result;
	}

	FPixelData BiplanarRockSample(uint RockIdx)
	{
		// Sample Slot A
		FPixelData MajorPlaneDataSlotA = SampleRock(RockIdx, 0, 0);
		FPixelData MinorPlaneDataSlotA = SampleRock(RockIdx, 0, 1);

		// Sample Slot B
		FPixelData MajorPlaneDataSlotB = SampleRock(RockIdx, 1, 0);
		FPixelData MinorPlaneDataSlotB = SampleRock(RockIdx, 1, 1);

		// Perform advanced splatting on both planes
		FPixelData MajorData = AdvancedSplat(MajorPlaneDataSlotA, MajorPlaneDataSlotB, 1.0 - RockSlotMixFactor, RockSlotMixFactor);
		FPixelData MinorData = AdvancedSplat(MinorPlaneDataSlotA, MinorPlaneDataSlotB, 1.0 - RockSlotMixFactor, RockSlotMixFactor);

		// Final blend
		return BiplanarBlend(MajorData, MinorData);
	}

	FPixelData BiplanarCoverSample(uint CoverIdx)
	{
		// Sample
		FPixelData MajorData = SampleCover(CoverIdx, 0);
		FPixelData MinorData = SampleCover(CoverIdx, 1);

		// Final blend
		return BiplanarBlend(MajorData, MinorData);
	}
};

// Shader body start
FUtils Utils;
Utils.Init
(
	Parameters, RockDiffuseHeightArraySampler, RockNormalArraySampler, CoverDiffuseHeightArraySampler, CoverNormalArraySampler,
	RockDiffuseHeightArray, RockNormalArray, RockTileSizes, RockMaxDisplacements, RockTexIDs, RockTexWeights, RockSlot,
	CoverDiffuseHeightArray, CoverNormalArray, CoverTileSizes, CoverMaxDisplacements, CoverTexIDs, CoverTexWeights,
	TilingNoise
);

FPixelData RockData[4];
uint i = 0;
OutputColor = float4(0, 0, 0, 0);
OutputNormal = float3(0, 0, 0);
float ma = 0.0f;

//////////////////////////////////////////////////////////////////
// Rocks
//////////////////////////////////////////////////////////////////
[unroll]
for (i = 0; i < 4; ++i)
{
	RockData[i] = Utils.BiplanarRockSample(RockTexIDs[i]);
	ma = max(RockData[i].Color.a, ma);
}

ma -= Utils.SplatDepth;
float FactorSum = 0.0f;

[unroll]
for (i = 0; i < 4; ++i)
{
	float Factor = clamp(RockData[i].Color.a + RockTexWeights[i] - ma, 0, 1) * RockTexWeights[i];

	OutputColor += RockData[i].Color * Factor;
	OutputNormal += normalize(RockData[i].Normal) * Factor;

	FactorSum += Factor;
}

OutputColor /= FactorSum;
OutputColor.a = 1.0f;
OutputNormal = normalize(OutputNormal);

//////////////////////////////////////////////////////////////////
// Covers
//////////////////////////////////////////////////////////////////
//float TotalCoverWeight = CoverTexWeights[0] + CoverTexWeights[1] + CoverTexWeights[2] + CoverTexWeights[3];

FPixelData CoverData = Utils.BiplanarCoverSample(CoverTexIDs[0]);
OutputColor = lerp(OutputColor, CoverData.Color, CoverTexWeights[0]);

return 0.0f;