#pragma once
#include "fbxsdk.h"
#include "Data/Assets/Common/MeshData.h"
#include "Data/Assets/Common/Texture.h"
#include "Scene/BoundingBox.h"

#ifdef IOS_REF
#undef  IOS_REF
#define IOS_REF (*(pManager->GetIOSettings()))
#endif

namespace fbxsdk
{
    class FbxNode;
    class FbxManager;
    class FbxScene;
}

enum class EFBXTextureType
{
    DiffuseColor,
    DisplacementColor,
    NormalMap
};

struct FBXTextureData
{
    std::string name;
    std::string filepath;
};

struct FBXLayeredTextureData
{
    std::vector<FBXTextureData> textures;
};

struct FBXMaterialData
{
    int sceneId;
    std::string name;
    std::map<EFBXTextureType, std::vector<FBXTextureData>> textures;
    std::map<EFBXTextureType, std::vector<FBXLayeredTextureData>> layeredTextures;
};

struct FBXNodeData
{
    std::string name;
    QSharedPointer<MeshAssetGeometry> meshGeometry;
    std::vector<int> sceneMaterialIds;
};

struct FBXEssentialData
{
    bool isLoaded;
    std::string filepath;
    std::vector<FBXNodeData> nodes;
    std::vector<FBXMaterialData> materials;
    QVector3D forwardVector;
    BoundingBox boundingBox;
};

class FBXLoader
{
public:
    FBXEssentialData loadFBXFile(const std::string& path);

private:
    void loadNodesData(fbxsdk::FbxNode* parentNode, FBXEssentialData* outData);
    void loadMeshData(fbxsdk::FbxNode* node, FBXNodeData* data);
    QSharedPointer<MeshAssetGeometry> loadGeometryData(fbxsdk::FbxMesh* mesh);
    void loadMaterialsData(fbxsdk::FbxNode* node, FBXNodeData* data);
    void loadMaterialsData(fbxsdk::FbxScene* scene, FBXEssentialData* data);
    void loadTexturesData(fbxsdk::FbxSurfaceMaterial* material, FBXMaterialData* data);
    void loadTextureData(fbxsdk::FbxTexture* texture, FBXTextureData* data);

    void loadVertexPositions(MeshAssetGeometry* geometry, fbxsdk::FbxMesh* mesh);
    void loadVertexNormals(MeshAssetGeometry* geometry, fbxsdk::FbxMesh* mesh);
    void loadVertexUVs(MeshAssetGeometry* geometry, fbxsdk::FbxMesh* mesh);
    void loadVertexMaterials(MeshAssetGeometry* geometry, fbxsdk::FbxMesh* mesh);

    void computeBoundingBox(FbxNode* pNode, FbxVector4* min, FbxVector4* max);

    const char* getMaterialProperty(EFBXTextureType textureType);

    using PolyIndexMap = std::vector<std::map<IndexType, IndexType>>;
    PolyIndexMap polyIndexMap;

    using VertexInstanceMap = std::unordered_map<IndexType, std::vector<IndexType>>;
    VertexInstanceMap vertexInstanceMap;
};


void initializeFbxSdkObjects(fbxsdk::FbxManager*& pManager, fbxsdk::FbxScene*& pScene);
void destroySdkObjects(fbxsdk::FbxManager* pManager, bool pExitStatus);
bool saveScene(fbxsdk::FbxManager* pManager, FbxDocument* pScene, const char* pFilename, int pFileFormat = -1, bool pEmbedMedia = false);

fbxsdk::FbxVector4 toFbxVector(const QVector3D& source);
QVector3D toQVector(const fbxsdk::FbxVector4& source);
QVector2D toQVector(const fbxsdk::FbxVector2& source);

std::optional<ETextureComponentIn> toAssetTextureType(EFBXTextureType textureType);