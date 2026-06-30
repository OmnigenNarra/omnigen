#pragma once

#define GRID_SEGMENT_WIDTH 10000.0f
#define GRID_SEGMENT_WIDTH_SHADER "10000.0f"
#define GRID_SEGMENT_COUNT 50
#define GRID_SEGMENT_COUNT_SHADER "50"
#define GRID_HEIGHT -20.0
#define GRID_THICKNESS "100.0"

#define DOMAIN_INNER_MARGIN 100

#define OPENGL_SHADER_VER "460"

#define TEX_PER_MAT 4

// 1/sqrt3 in each component
#define LIGHT_VECTOR "vec3(0.57735026919f, -0.57735026919f, 0.57735026919f)"

#define WORLD_LOWEST_Y -100000.0f   // -1000m
#define WORLD_HIGHEST_Y 1000000.0f // 10000m

using IndexType = quint32;

constexpr float getMaxGridCoord()
{
    return GRID_SEGMENT_WIDTH * GRID_SEGMENT_COUNT;
}

#define STRINGIFY(a) #a
#define TODO(X) _Pragma(STRINGIFY(message("----------------------------------------------------------------- TODO: "##X##)))
