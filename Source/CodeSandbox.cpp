#include "stdafx.h"
#include "CodeSandbox.h"
#include "Utils/EnumAsConstexpr.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Utils/Resumable.h"
//#include "C:/Users/ryder/Desktop/quickhull-master/QuickHull.hpp"

#include "Scene/Generation/OmnigenGenerationData.h"
#include "Scene/Generation/Common/Markers/SharedMeshMarker.h"
#include "Scene/Generation/Common/Markers/PointCloudMarker.h"
#include <Mathematics/IntrPlane3Triangle3.h>
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Omnigen.h"

#include "Utils/Poisson.h"
#include "Scene/Generation/Common/Objects/Heightfield.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Data/Assets/RockMaterial/AssetRockMaterial.h"

#include <gli/core/load.inl>
#include <gli/convert.hpp>
#include <gli/texture2d.hpp>
#include <tbb/blocked_range2d.h>
#include <gli/sampler2d.hpp>

void BinTest::init()
{
    test0 = "bardzo_latwy_testa";
    test = { 1, 2, 3, 4, 5 };
    test2 = { {"kupadddddddd", 2}, {"dupaaaaaaaaaaaaaa", 400}, {"dzika", 1000} };
    test3 = { 10, "hyc_HYC_hah!", 0.33 };
    test4 = { {{-1,2}, QSharedPointer<float>::create(0.9f)}, {{23, -777}, QSharedPointer<float>::create(-3.456f)} };
    test5.reset(new QString("Gyahaha_Kyahaha"));
    test6 = { "Scarlett", "Johanson", "Afro", "Mudzin" };

    for (int i = 0; i < TEST_VOLUME; ++i)
    {
        mapTest.insert(i, i);
        hashTest.insert(i, i);
        arrayTest[i] = i;

        miniMapTest[i].insert(i, i);
        miniMapTest[i].insert(i+1, i+1);

        miniHashTest[i].insert(i, i);
        miniHashTest[i].insert(i+1, i+1);

        miniArrayTest[i][0] = i;
        miniArrayTest[i][1] = i+1;
    }
}

void BinTest::save()
{
    QString name = QFileDialog::getSaveFileName(nullptr, "Save Project", "", "Omnigen project files (*.ogn)");
    OmniBin<std::ios::out> writer(name.toStdString());
    writer << test0;
    writer << test;
    writer << test2;
    writer << test3;
    writer << test4;
    writer << test5;
    writer << test6;

    OmniLog() <<= "SAVING:";
    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        writer << mapTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        writer << hashTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        writer << arrayTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        writer << miniMapTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        writer << miniHashTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        writer << miniArrayTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }
}

void BinTest::load()
{
    QString name = QFileDialog::getOpenFileName(nullptr, "Save Project", "", "Omnigen project files (*.ogn)");
    OmniBin<std::ios::in> reader(name.toStdString());
    reader >> test0;
    reader >> test;
    reader >> test2;
    reader >> test3;
    reader >> test4;
    reader >> test5;
    reader >> test6;

    OmniLog() <<= "LOADING:";

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        reader >> mapTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        reader >> hashTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        reader >> arrayTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        reader >> miniMapTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        reader >> miniHashTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }

    {
        double start = timeFromLastEntrance<std::chrono::milliseconds>(789);
        reader >> miniArrayTest;
        double end = timeFromLastEntrance<std::chrono::milliseconds>(789);
        OmniLog() <<= int(end);
    }
}

enum class ETestEnum
{
    Zero,
    One,
    Two,
    Last
};
ENABLE_ENUM_AS_CONSTEXPR(ETestEnum, ETestEnum::Last);

struct TestAction
{
    template<ETestEnum e>
    static void Action()
    {
        std::array<ETestEnum, int(e)> arr;
        for (int i = 0; i < arr.size(); ++i)
            OmniLog() <<= e;
    }
};

struct TestGeom
{
    QVector3D position;
    QVector2D uv;
    float texIdx;
};

resumable::RetVal counter()
{
    int i = 0;
    while (true) 
    {
        OmniLog() << "counter: " <<= ++i;
        co_await resumable::Awaiter::get();
    }
}

void coroTest()
{
    static bool first = true;

    if (first)
    {
        counter();
        first = false;
    }
    else
    {
        resumable::Awaiter::resume();
    }
}

void convexHullTest()
{
    OmniStartProfiling;
    Generation::Data::get()->setCurrentGeneratedStage(EGenerationStage());

	// Generation space: 1 grid cube in the center
	QVector3D center = QVector3D(25, 5, 25) * GRID_SEGMENT_WIDTH;
	QVector3D nbl = center - QVector3D(0.5, 0.5, 0.5) * GRID_SEGMENT_WIDTH;
	QVector3D sizes = QVector3D(1, 1, 1) * GRID_SEGMENT_WIDTH;

	// Generate test Fault
	auto faultGeometry = QSharedPointer<RenderGeometryData<>>::create();
	faultGeometry->vertices.resize(8);
	// Lower plate
	faultGeometry->vertices[0] = nbl + QVector3D(0, 0, 0) * GRID_SEGMENT_WIDTH;;
	faultGeometry->vertices[1] = nbl + QVector3D(1, 0, 0) * GRID_SEGMENT_WIDTH;
	faultGeometry->vertices[2] = nbl + QVector3D(1, 0, 1) * GRID_SEGMENT_WIDTH;
	faultGeometry->vertices[3] = nbl + QVector3D(0, 0, 1) * GRID_SEGMENT_WIDTH;
	// Upper plate
	faultGeometry->vertices[4] = faultGeometry->vertices[0] + QVector3D(1, 1, 0) * GRID_SEGMENT_WIDTH;;
	faultGeometry->vertices[5] = faultGeometry->vertices[1] + QVector3D(1, 1, 0) * GRID_SEGMENT_WIDTH;
	faultGeometry->vertices[6] = faultGeometry->vertices[2] + QVector3D(1, 1, 0) * GRID_SEGMENT_WIDTH;
	faultGeometry->vertices[7] = faultGeometry->vertices[3] + QVector3D(1, 1, 0) * GRID_SEGMENT_WIDTH;
	faultGeometry->indices = { 0,1,2,3, 4,5,6,7, /*vertical section*/ 4,7,2,1 };
	spawn<DSharedMeshMarker<>, true>(faultGeometry, GL_QUADS);

    auto&& assets = QOmnigenAssetMgrSection::getAssets<EAsset::RockMaterial>();
    auto&& rock = assets.begin()->second;
    auto&& tex = rock->getTextures()[3].outputs.at(ETextureComponentOut::DiffuseHeight);

    gli::fsampler2D sampler(tex.getData(), gli::wrap::WRAP_REPEAT);

    QSize size = { tex.getData().extent().x, tex.getData().extent().y };

    Generation::Heightfield heightData(GVector2D(), sizes, 10.0f);
	tbb::parallel_for(tbb::blocked_range2d<int, int>(0, heightData.getSize().x, 0, heightData.getSize().z), [&](tbb::blocked_range2d<int>& r)
		{
			for (int z = r.cols().begin(); z <= r.cols().end(); ++z)
                for (int x = r.rows().begin(); x <= r.rows().end(); ++x)
                {
                    GVector2D point = heightData.getPoint2D(x, z);

                    gli::ivec2 texCoords;
                    texCoords.x = size.width() * float(x) / float(heightData.getSize().x + 1);
                    texCoords.y = size.height() * float(z) / float(heightData.getSize().z + 1);
                    gli::vec4 color = sampler.texel_fetch(gli::texture2d::extent_type(texCoords), 0);

                    heightData.setHeight(z, x, color.a * 2000.0f);
                }
		});

    // Generate random pts
	struct PosNrm
	{
		QVector3D position;
		QVector3D normal;
	};

    float triangleSize = 10.0f;
    int dim = size.width() / triangleSize;
    auto geometry = QSharedPointer<RenderGeometryData<PosNrm>>::create();
    geometry->vertices.resize(dim * dim);

    // Latching surface
    auto lsOrigin = nbl + sizes;
    QVector3D lsVecA = QVector3D{ 0, -1, 0 };
    QVector3D lsVecB = QVector3D{ 0, 0, -1 };

    float mult = 1.0f;
    std::mt19937 engine;
    engine.seed(rand());
    std::uniform_real_distribution<float> randomCoord(0.0f, 1.0f);
    float sY = randomCoord(engine);
    float sZ = randomCoord(engine);

    // Pts
    for (int y = 0; y < dim; ++y)
        for (int z = 0; z < dim; ++z)
        {
            float fY = float(y) / float(dim - 1);
            float fZ = float(z) / float(dim - 1);
            auto p = lsOrigin + (lsVecA * fY + lsVecB * fZ) * GRID_SEGMENT_WIDTH;
            float tY = sY + fY * mult;
            float tZ = sZ + fZ * mult;
            tY -= int(tY);
            tZ -= int(tZ);
            p.setX(p.x() - heightData.sample({sizes.x() * tY , sizes.z() * tZ }));
            geometry->vertices[y * dim + z].position = p;
        }

    // Quads
    auto&& triangles = geometry->indices;
	for (int y = 1; y < dim; ++y)
        for (int z = 1; z < dim; ++z)
        {
            quint32 BL = (y - 1) * dim + (z - 1);
			quint32 TL = (y - 0) * dim + (z - 1);
			quint32 TR = (y - 0) * dim + (z - 0);
			quint32 BR = (y - 1) * dim + (z - 0);

			// 2 triangles
            triangles << BL << TL << TR;
            triangles << BL << TR << BR;
        }

	// Compute normals
	tbb::parallel_for(0, int(triangles.size() / 3), [&](int ti)
		{
			IndexType i = ti * 3;

			auto&& mp1 = geometry->vertices[triangles[i]];
			auto&& mp2 = geometry->vertices[triangles[i + 1]];
			auto&& mp3 = geometry->vertices[triangles[i + 2]];

			QVector3D faceNormal = computeFaceNormal({ mp1.position, mp2.position, mp3.position });

			mp1.normal += faceNormal;
			mp2.normal += faceNormal;
			mp3.normal += faceNormal;
		});

	tbb::parallel_for(0, int(geometry->vertices.size()), [&](int i)
		{
			geometry->vertices[i].normal.normalize();
		});

    spawn<DSharedMeshMarker<PosNrm>>(geometry, GL_TRIANGLES);
}