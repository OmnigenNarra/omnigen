#include "stdafx.h"
#include "FbxHelpers.h"
#include "Data/Assets/Common/Mesh.h"
#include <tbb/parallel_for.h>
#include <fbxsdk/core/fbxsystemunit.h>

FBXEssentialData FBXLoader::loadFBXFile(const std::string& path)
{
    fbxsdk::FbxManager* sdkManager = fbxsdk::FbxManager::Create();
    sdkManager->SetIOSettings(fbxsdk::FbxIOSettings::Create(sdkManager, IOSROOT));

    fbxsdk::FbxImporter* importer = fbxsdk::FbxImporter::Create(sdkManager, "");

    if (!importer->Initialize(&path[0], -1, sdkManager->GetIOSettings()))
    {
        Q_ASSERT(false);
        return {};
    }

    fbxsdk::FbxScene* scene = fbxsdk::FbxScene::Create(sdkManager, "scene");

    importer->Import(scene);

	fbxsdk::FbxGeometryConverter clsConverter(sdkManager);
	clsConverter.Triangulate(scene, true);
    
    importer->Destroy();

    auto unit = scene->GetGlobalSettings().GetSystemUnit();
    if (unit != FbxSystemUnit::cm)
    {
        const FbxSystemUnit::ConversionOptions lConversionOptions = {
          false, /* mConvertRrsNodes */
          true, /* mConvertAllLimits */
          true, /* mConvertClusters */
          true, /* mConvertLightIntensity */
          true, /* mConvertPhotometricLProperties */
          true  /* mConvertCameraClipPlanes */
        };

        // Convert the scene to meters using the defined options.
        FbxSystemUnit::cm.ConvertScene(scene, lConversionOptions);
    }

    //This configuration works for a model that is exported with +Y up and +Z front
    auto axis = scene->GetGlobalSettings().GetAxisSystem();

    FbxVector4 min(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    FbxVector4 max(std::numeric_limits<double>::min(), std::numeric_limits<double>::min(), std::numeric_limits<double>::min());
    computeBoundingBox(scene->GetRootNode(), &min, &max);

    FBXEssentialData data;
    data.boundingBox = BoundingBox(toQVector(min), QVector3D(max[0] - min[0], max[1] - min[1], max[2] - min[2]));

    FbxVector4 center = (min + max) / 2;

    // Calculate the maximum extend along the positive Z-axis
    FbxVector4 maxExtend(center[0], center[1], max[2]);

    data.forwardVector = toQVector(maxExtend);

    loadMaterialsData(scene, &data);
    loadNodesData(scene->GetRootNode(), &data);
    data.filepath = path;
    data.isLoaded = true;

    scene->Destroy();
    sdkManager->Destroy();
    return data;
}

void FBXLoader::loadNodesData(fbxsdk::FbxNode* parentNode, FBXEssentialData* outData)
{
    if (!parentNode)
        return;

    for (int i = 0; i < parentNode->GetChildCount(); i++)
    {
        FBXNodeData nodeData;
        auto&& node = parentNode->GetChild(i);

        nodeData.name = node->GetName();
        loadMeshData(node, &nodeData);
        loadMaterialsData(node, &nodeData);

        outData->nodes << std::move(nodeData);
        loadNodesData(node, outData);
    }
}


void FBXLoader::loadMeshData(fbxsdk::FbxNode* node, FBXNodeData* data)
{
    auto&& mesh = node->GetMesh();

    if (!mesh || mesh->GetControlPointsCount() == 0)
        return;

    data->meshGeometry = loadGeometryData(mesh);
}

QSharedPointer<MeshAssetGeometry> FBXLoader::loadGeometryData(fbxsdk::FbxMesh* mesh)
{
    auto geometryData = QSharedPointer<MeshAssetGeometry>::create();

    polyIndexMap.clear();
    loadVertexPositions(geometryData.get(), mesh);
    loadVertexNormals(geometryData.get(), mesh);
    loadVertexUVs(geometryData.get(), mesh);
    loadVertexMaterials(geometryData.get(), mesh);

    return geometryData;
}

void FBXLoader::loadMaterialsData(fbxsdk::FbxNode* node, FBXNodeData* data)
{
    for (int i = 0; i < node->GetMaterialCount(); i++)
    {
        auto&& material = node->GetMaterial(i);
        int sceneId = -1;

        for (int j = 0; j < node->GetScene()->GetMaterialCount(); j++)
        {
            auto* sceneMaterial = node->GetScene()->GetMaterial(j);
            if (material->GetUniqueID() == sceneMaterial->GetUniqueID())
            {
                sceneId = j;
                break;
            }
        }

        Q_ASSERT(sceneId != -1);
        data->sceneMaterialIds << sceneId;
    }
}

void FBXLoader::loadMaterialsData(fbxsdk::FbxScene* scene, FBXEssentialData* data)
{
    auto materialsData = std::vector<FBXMaterialData>();

    for (int i = 0; i < scene->GetMaterialCount(); i++)
    {
        auto materialData = FBXMaterialData();
        auto&& material = scene->GetMaterial(i);

        if (!material)
            continue;

        materialData.sceneId = i;
        materialData.name = material->GetName();
        loadTexturesData(material, &materialData);
        materialsData << std::move(materialData);
    }

    data->materials = std::move(materialsData);
}

void FBXLoader::loadTexturesData(fbxsdk::FbxSurfaceMaterial* material, FBXMaterialData* data)
{
    auto texturesData = std::map<EFBXTextureType, std::vector<FBXTextureData>>();
    auto layeredTexturesData = std::map<EFBXTextureType, std::vector<FBXLayeredTextureData>>();

    for (auto&& textureType : magic_enum::enum_values<EFBXTextureType>())
    {
        auto&& property = material->FindProperty(getMaterialProperty(textureType));
        auto texCountPerType = property.GetSrcObjectCount<FbxTexture>();

        if (texCountPerType > 1)
        {
            OmniLog(ELoggingLevel::Critical) <<= "Unsupported material, tex count per type is greater than 1!";
            Q_ASSERT(false);
            std::abort();
        }

        for (int i = 0; i < property.GetSrcObjectCount<FbxTexture>(); i++)
        {
            auto&& layeredTexture = FbxCast<FbxLayeredTexture>(property.GetSrcObject<FbxLayeredTexture>(i));

            if (layeredTexture)
            {
                // Unhandled!
                Q_ASSERT(false);

                auto layeredTextureData = FBXLayeredTextureData();

                for (int j = 0; j < layeredTexture->GetSrcObjectCount<FbxTexture>(); j++)
                {
                    auto textureData = FBXTextureData();
                    FbxTexture* texture = FbxCast<FbxTexture>(layeredTexture->GetSrcObject<FbxTexture>(j));

                    loadTextureData(texture, &textureData);
                    layeredTextureData.textures << std::move(textureData);
                }

                layeredTexturesData[textureType] << std::move(layeredTextureData);
            }
            else
            {
                auto textureData = FBXTextureData();
                FbxTexture* texture = FbxCast<FbxTexture>(property.GetSrcObject<FbxTexture>(i));

                loadTextureData(texture, &textureData);
                texturesData[textureType] << std::move(textureData);
            }
        }
    }

    data->textures = std::move(texturesData);
    data->layeredTextures = std::move(layeredTexturesData);
}

void FBXLoader::loadTextureData(fbxsdk::FbxTexture* texture, FBXTextureData* data)
{
    auto textureData = FBXTextureData();

    textureData.name = texture->GetName();
    textureData.filepath = fbxsdk::FbxCast<fbxsdk::FbxFileTexture>(texture)->GetFileName();

    *data = std::move(textureData);
}

void FBXLoader::loadVertexPositions(MeshAssetGeometry* geometry, fbxsdk::FbxMesh* mesh)
{
    bool unrollVertices = false;
    unrollVertices |= (mesh->GetElementNormal()->GetMappingMode() == FbxGeometryElement::eByPolygonVertex);
    unrollVertices |= (mesh->GetElementUV()->GetMappingMode() == FbxGeometryElement::eByPolygonVertex);
    for (int matIdx = 0; matIdx < mesh->GetElementMaterialCount(); ++matIdx)
        unrollVertices |= (mesh->GetElementMaterial(matIdx)->GetMappingMode() == FbxGeometryElement::eByPolygon);

    if (!unrollVertices)
    {
        geometry->vertices.resize(mesh->GetControlPointsCount());

        for (int i = 0; i < mesh->GetControlPointsCount(); ++i)
        {
            geometry->vertices[i].position = toQVector(mesh->GetControlPointAt(i));
            vertexInstanceMap[i] = { IndexType(i) };
        }
    }

    polyIndexMap.resize(mesh->GetPolygonCount());
    for (int polyIndex = 0; polyIndex < mesh->GetPolygonCount(); ++polyIndex)
    {
        auto&& polySize = mesh->GetPolygonSize(polyIndex);
        Q_ASSERT(polySize == 3);

        // Add in reverse, as fbx uses different polygon winding
        auto&& targetIndices = geometry->indices;
        for (int polyVertIndex = (polySize - 1); polyVertIndex >= 0; --polyVertIndex)
        {
            int CPidx = mesh->GetPolygonVertex(polyIndex, polyVertIndex);
            if (unrollVertices)
            {
                targetIndices << geometry->vertices.size();
                vertexInstanceMap[CPidx] << targetIndices.back();
                geometry->vertices <<= MeshAssetVertex{ .position = toQVector(mesh->GetControlPointAt(CPidx)) };
            }
            else
            {
                targetIndices << CPidx;
            }
            polyIndexMap[polyIndex][polyVertIndex] = targetIndices.back();
        }
    }

    // Optimize memory
    geometry->vertices.shrink_to_fit();
    geometry->indices.shrink_to_fit();
}

void FBXLoader::loadVertexNormals(MeshAssetGeometry* geometry, fbxsdk::FbxMesh* mesh)
{
    auto* normalElement = mesh->GetElementNormal();
    if (normalElement->GetMappingMode() == FbxGeometryElement::eByControlPoint)
    {
        //Let's get normals of each vertex, since the mapping mode of normal element is by control point
        for (int lVertexIndex = 0; lVertexIndex < mesh->GetControlPointsCount(); lVertexIndex++)
        {
            int lNormalIndex = 0;
            //reference mode is direct, the normal index is same as vertex index.
            //get normals by the index of control vertex
            if (normalElement->GetReferenceMode() == FbxGeometryElement::eDirect)
                lNormalIndex = lVertexIndex;

            //reference mode is index-to-direct, get normals by the index-to-direct
            if (normalElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
                lNormalIndex = normalElement->GetIndexArray().GetAt(lVertexIndex);

            //Got normals of each vertex.
            FbxVector4 lNormal = normalElement->GetDirectArray().GetAt(lNormalIndex);

            for (IndexType i : vertexInstanceMap[lVertexIndex])
                geometry->vertices[i].normal = toQVector(lNormal);
        }
    }

    //mapping mode is by polygon-vertex.
    //we can get normals by retrieving polygon-vertex.
    else if (normalElement->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
    {
        int lIndexByPolygonVertex = 0;
        //Let's get normals of each polygon, since the mapping mode of normal element is by polygon-vertex.
        for (int lPolygonIndex = 0; lPolygonIndex < mesh->GetPolygonCount(); lPolygonIndex++)
        {
            //get polygon size, you know how many vertices in current polygon.
            int lPolygonSize = mesh->GetPolygonSize(lPolygonIndex);
            //retrieve each vertex of current polygon.
            for (int i = 0; i < lPolygonSize; i++)
            {
                int lNormalIndex = 0;
                //reference mode is direct, the normal index is same as lIndexByPolygonVertex.
                if (normalElement->GetReferenceMode() == FbxGeometryElement::eDirect)
                    lNormalIndex = lIndexByPolygonVertex;

                //reference mode is index-to-direct, get normals by the index-to-direct
                if (normalElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
                    lNormalIndex = normalElement->GetIndexArray().GetAt(lIndexByPolygonVertex);

                //Got normals of each polygon-vertex.
                FbxVector4 lNormal = normalElement->GetDirectArray().GetAt(lNormalIndex);

                IndexType targetIdx = polyIndexMap.at(lPolygonIndex).at(i);
                geometry->vertices[targetIdx].normal = toQVector(lNormal);

                lIndexByPolygonVertex++;
            }//end for i //lPolygonSize
        }//end for lPolygonIndex //PolygonCount

    }//end eByPolygonVertex
}

void FBXLoader::loadVertexUVs(MeshAssetGeometry* geometry, fbxsdk::FbxMesh* mesh)
{
    //get all UV set names
    FbxStringList lUVSetNameList;
    mesh->GetUVSetNames(lUVSetNameList);

    //iterating over all uv sets
    for (int lUVSetIndex = 0; lUVSetIndex < lUVSetNameList.GetCount(); lUVSetIndex++)
    {
        //get lUVSetIndex-th uv set
        const char* lUVSetName = lUVSetNameList.GetStringAt(lUVSetIndex);
        const FbxGeometryElementUV* lUVElement = mesh->GetElementUV(lUVSetName);
        Q_ASSERT(lUVElement);

        //index array, where holds the index referenced to the uv data
        const bool lUseIndex = lUVElement->GetReferenceMode() != FbxGeometryElement::eDirect;
        const int lIndexCount = (lUseIndex) ? lUVElement->GetIndexArray().GetCount() : 0;

        //iterating through the data by polygon
        const int lPolyCount = mesh->GetPolygonCount();

        if (lUVElement->GetMappingMode() == FbxGeometryElement::eByControlPoint)
        {
            for (int lPolyIndex = 0; lPolyIndex < lPolyCount; ++lPolyIndex)
            {
                // build the max index array that we need to pass into MakePoly
                const int lPolySize = mesh->GetPolygonSize(lPolyIndex);
                for (int lVertIndex = 0; lVertIndex < lPolySize; ++lVertIndex)
                {
                    FbxVector2 lUVValue;

                    //get the index of the current vertex in control points array
                    int lPolyVertIndex = mesh->GetPolygonVertex(lPolyIndex, lVertIndex);

                    //the UV index depends on the reference mode
                    int lUVIndex = lUseIndex ? lUVElement->GetIndexArray().GetAt(lPolyVertIndex) : lPolyVertIndex;

                    lUVValue = lUVElement->GetDirectArray().GetAt(lUVIndex);

                    for (IndexType i : vertexInstanceMap[lVertIndex])
                        geometry->vertices[i].uv = toQVector(lUVValue);
                }
            }
        }
        else if (lUVElement->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
        {
            int lPolyIndexCounter = 0;
            for (int lPolyIndex = 0; lPolyIndex < lPolyCount; ++lPolyIndex)
            {
                // build the max index array that we need to pass into MakePoly
                const int lPolySize = mesh->GetPolygonSize(lPolyIndex);
                for (int lVertIndex = 0; lVertIndex < lPolySize; ++lVertIndex)
                {
                    if (lPolyIndexCounter < lIndexCount)
                    {
                        FbxVector2 lUVValue;

                        //the UV index depends on the reference mode
                        int lUVIndex = lUseIndex ? lUVElement->GetIndexArray().GetAt(lPolyIndexCounter) : lPolyIndexCounter;

                        lUVValue = lUVElement->GetDirectArray().GetAt(lUVIndex);

                        IndexType targetIdx = polyIndexMap.at(lPolyIndex).at(lVertIndex);
                        geometry->vertices[targetIdx].uv = toQVector(lUVValue);

                        lPolyIndexCounter++;
                    }
                }
            }
        }
        else
        {
            Q_ASSERT(false);
        }

        // Only load the first uv set.
        return;
    }
}

void FBXLoader::loadVertexMaterials(MeshAssetGeometry* geometry, fbxsdk::FbxMesh* mesh)
{
    for (int matIdx = 0; matIdx < mesh->GetElementMaterialCount(); ++matIdx)
    {
        auto* materialElement = mesh->GetElementMaterial(matIdx);
        if (materialElement->GetMappingMode() == FbxGeometryElement::eAllSame)
        {
            int matId = materialElement->GetIndexArray().GetAt(0);
            tbb::parallel_for(0, int(geometry->vertices.size()), [&](int i)
                {
                    geometry->vertices[i].materialId = matId;
                });
        }
        else if (materialElement->GetMappingMode() == FbxGeometryElement::eByPolygon)
        {
            auto&& matIndices = materialElement->GetIndexArray();
            for (int polyIndex = 0; polyIndex < mesh->GetPolygonCount(); ++polyIndex)
            {
                int matId = matIndices.GetAt(polyIndex);
                int polySize = mesh->GetPolygonSize(polyIndex);

                for (int vertexIdx = 0; vertexIdx < polySize; ++vertexIdx)
                {
                    IndexType targetIdx = polyIndexMap.at(polyIndex).at(vertexIdx);
                    geometry->vertices[targetIdx].materialId = matId;
                }
            }
        }
    }
}

void FBXLoader::computeBoundingBox(FbxNode* pNode, FbxVector4* min, FbxVector4* max)
{
    // Check if this node has a mesh
    if (pNode->GetMesh())
    {
        // Get the mesh and its vertices
        const FbxMesh* pMesh = pNode->GetMesh();
        FbxVector4* pVertices = pMesh->GetControlPoints();
        int numVertices = pMesh->GetControlPointsCount();

        // Update the min and max points to include the vertices of this mesh
        for (int i = 0; i < numVertices; i++)
        {
            min->Set(fmin(*min[0], pVertices[i][0]), fmin(*min[1], pVertices[i][1]), fmin(*min[2], pVertices[i][2]));
            max->Set(fmax(*max[0], pVertices[i][0]), fmax(*max[1], pVertices[i][1]), fmax(*max[2], pVertices[i][2]));
        }
    }

    // Recurse through the child nodes
    for (int i = 0; i < pNode->GetChildCount(); i++)
    {
        computeBoundingBox(pNode->GetChild(i), min, max);
    }
}

const char* FBXLoader::getMaterialProperty(EFBXTextureType textureType)
{
    switch (textureType)
    {
        case EFBXTextureType::DiffuseColor:
            return fbxsdk::FbxSurfaceMaterial::sDiffuse;
        case EFBXTextureType::NormalMap:
            return fbxsdk::FbxSurfaceMaterial::sNormalMap;
        case EFBXTextureType::DisplacementColor:
            return fbxsdk::FbxSurfaceMaterial::sDisplacementColor;

        default:
            return "";
    }
}

void initializeFbxSdkObjects(fbxsdk::FbxManager*& pManager, fbxsdk::FbxScene*& pScene)
{
    //The first thing to do is to create the FBX Manager which is the object allocator for almost all the classes in the SDK
    pManager = fbxsdk::FbxManager::Create();
    if (!pManager)
    {
        FBXSDK_printf("Error: Unable to create FBX Manager!\n");
        exit(1);
    }
    else FBXSDK_printf("Autodesk FBX SDK version %s\n", pManager->GetVersion());

    //Create an IOSettings object. This object holds all import/export settings.
    FbxIOSettings* ios = FbxIOSettings::Create(pManager, IOSROOT);
    pManager->SetIOSettings(ios);

    //Load plugins from the executable directory (optional)
    FbxString lPath = FbxGetApplicationDirectory();
    pManager->LoadPluginsDirectory(lPath.Buffer());

    //Create an FBX scene. This object holds most objects imported/exported from/to files.
    pScene = fbxsdk::FbxScene::Create(pManager, "My Scene");
    if (!pScene)
    {
        FBXSDK_printf("Error: Unable to create FBX scene!\n");
        exit(1);
    }
}

void destroySdkObjects(fbxsdk::FbxManager* pManager, bool pExitStatus)
{
    //Delete the FBX Manager. All the objects that have been allocated using the FBX Manager and that haven't been explicitly destroyed are also automatically destroyed.
    if (pManager) pManager->Destroy();
    if (pExitStatus) FBXSDK_printf("Program Success!\n");
}

bool saveScene(fbxsdk::FbxManager* pManager, FbxDocument* pScene, const char* pFilename, int pFileFormat, bool pEmbedMedia)
{
    int lMajor, lMinor, lRevision;
    bool lStatus = true;

    // Create an exporter.
    FbxExporter* lExporter = FbxExporter::Create(pManager, "");
    lExporter->SetFileExportVersion(FBX_2018_00_COMPATIBLE);

    if (pFileFormat < 0 || pFileFormat >= pManager->GetIOPluginRegistry()->GetWriterFormatCount())
    {
        // Write in fall back format in less no ASCII format found
        pFileFormat = pManager->GetIOPluginRegistry()->GetNativeWriterFormat();

        //Try to export in ASCII if possible
        int lFormatIndex, lFormatCount = pManager->GetIOPluginRegistry()->GetWriterFormatCount();

        for (lFormatIndex = 0; lFormatIndex < lFormatCount; lFormatIndex++)
        {
            if (pManager->GetIOPluginRegistry()->WriterIsFBX(lFormatIndex))
            {
                FbxString lDesc = pManager->GetIOPluginRegistry()->GetWriterFormatDescription(lFormatIndex);
                const char* lASCII = "ascii";
                if (lDesc.Find(lASCII) >= 0)
                {
                    pFileFormat = lFormatIndex;
                    break;
                }
            }
        }
    }

    // Set the export states. By default, the export states are always set to
    // true except for the option eEXPORT_TEXTURE_AS_EMBEDDED. The code below
    // shows how to change these states.
    IOS_REF.SetBoolProp(EXP_FBX_MATERIAL, true);
    IOS_REF.SetBoolProp(EXP_FBX_TEXTURE, true);
    IOS_REF.SetBoolProp(EXP_FBX_EMBEDDED, pEmbedMedia);
    IOS_REF.SetBoolProp(EXP_FBX_SHAPE, true);
    IOS_REF.SetBoolProp(EXP_FBX_GOBO, true);
    IOS_REF.SetBoolProp(EXP_FBX_ANIMATION, true);
    IOS_REF.SetBoolProp(EXP_FBX_GLOBAL_SETTINGS, true);

    // Initialize the exporter by providing a filename.
    if (lExporter->Initialize(pFilename, pFileFormat, pManager->GetIOSettings()) == false)
    {
        FBXSDK_printf("Call to FbxExporter::Initialize() failed.\n");
        FBXSDK_printf("Error returned: %s\n\n", lExporter->GetStatus().GetErrorString());
        return false;
    }

    fbxsdk::FbxManager::GetFileFormatVersion(lMajor, lMinor, lRevision);
    FBXSDK_printf("FBX file format version %d.%d.%d\n\n", lMajor, lMinor, lRevision);

    // Export the scene.
    lStatus = lExporter->Export(pScene);

    // Destroy the exporter.
    lExporter->Destroy();
    return lStatus;
}

fbxsdk::FbxVector4 toFbxVector(const QVector3D& source)
{
    return fbxsdk::FbxVector4(source.x(), source.y(), source.z());
}

QVector3D toQVector(const fbxsdk::FbxVector4& source)
{
    return QVector3D(source[0], source[1], source[2]);
}

QVector2D toQVector(const fbxsdk::FbxVector2& source)
{
    return QVector2D(source[0], source[1]);
}

std::optional<ETextureComponentIn> toAssetTextureType(EFBXTextureType textureType)
{
    static const std::map<EFBXTextureType, ETextureComponentIn> componentMap =
    {
        {EFBXTextureType::DiffuseColor, ETextureComponentIn::DiffuseAlpha},
        {EFBXTextureType::NormalMap, ETextureComponentIn::Normal},
        {EFBXTextureType::DisplacementColor, ETextureComponentIn::Displacement}
    };

    return componentMap.at(textureType);
}
