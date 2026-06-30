#include "stdafx.h"
#include "NurbsMarker.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"

DNurbsMarker::DNurbsMarker(const SerializableNurbs<3, float>& inNurbSurface, int rowsN, int columnsN, const QVector4D& inColor, float renderHeightOffset, ERenderType inRenderMode)
	: DSharedMeshMarker<>(generateNurbMesh<RenderGeometryData>(inNurbSurface, rowsN, columnsN), GL_QUADS, inColor, inRenderMode, QVector3D(0.f, renderHeightOffset, 0.f))
	, nurbSurface(inNurbSurface)
	, rows(rowsN)
	, columns(columnsN)
{
}

void omniSave(const DNurbsMarker& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << static_cast<const DSharedMeshMarker<>&>(object);
	omniSave<3, float>(object.nurbSurface, omniBin);
	omniBin << object.rows;
	omniBin << object.columns;
}

void omniLoad(DNurbsMarker& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> static_cast<DSharedMeshMarker<>&>(object);
    omniLoad<3, float>(object.nurbSurface, omniBin);
	omniBin >> object.rows;
	omniBin >> object.columns;
}