#pragma once
#include "modern_box.hpp"
#include <cassert>
#include <cmath>

namespace k22 {
inline void check_modern_box() {
    using namespace modern_box;
    const std::array<Vec3,3> identity{{{1,0,0},{0,1,0},{0,0,1}}};
    const std::array<Vec3,3> turned{{{0,1,0},{-1,0,0},{0,0,1}}};
    auto close=[](float a,float b){assert(std::abs(a-b)<0.0001f);};
    const Vec3 a{-100,0,-100},b{0,0,100},c{100,0,-100};
    const Vec3 cornerA{0,0,0},cornerB{10,0,0},cornerC{0,10,0};
    for(bool bonus:{false,true}) {
        const auto straight=Query({0,5,0},identity,{3,1,1},{0,-10,0},0,bonus).triangle(a,b,c,a,b,c);
        const auto rotated=Query({0,5,0},turned,{3,1,1},{0,-10,0},0,bonus).triangle(a,b,c,a,b,c);
        assert(straight&&rotated);close(straight->fraction,.4f);close(rotated->fraction,.2f);
        close(rotated->position.y,0);close(rotated->normal.y,1);
        assert(!Query({0,-5,0},turned,{3,1,1},{0,10,0},0,bonus).triangle(a,b,c,a,b,c));
        const auto vertex=Query({-3,-3,3},identity,{1,1,1},{6,6,-6},0,bonus).triangle(cornerA,cornerB,cornerC,cornerA,cornerB,cornerC);
        assert(vertex);close(vertex->fraction,(3-1/std::sqrt(3.f))/6);
        close(vertex->position.x,0);close(vertex->position.y,0);close(vertex->position.z,0);
        close(vertex->normal.x,-1/std::sqrt(3.f));close(vertex->normal.z,1/std::sqrt(3.f));
        const auto stretched=Query({-3,-3,3},identity,{2,1,1},{6,6,-6},0,bonus).triangle(cornerA,cornerB,cornerC,cornerA,cornerB,cornerC);
        assert(stretched);close(stretched->fraction,7.f/18);
        close(stretched->normal.x,-1/std::sqrt(33.f));close(stretched->normal.y,-4/std::sqrt(33.f));close(stretched->normal.z,4/std::sqrt(33.f));
        const float correction=bonus?0.f:.0498046875f;
        const auto buffered=Query({0,5,0},turned,{3,1,1},{0,-10,0},correction,bonus).triangle(a,b,c,a,b,c);
        assert(buffered);close(buffered->scalar,2-correction);close(buffered->position.y,correction);
    }
    // A7 preserves signed initial penetration; A6 clamps its face interval to zero.
    const auto a6=Query({0,.5f,0},identity,{1,1,1},{0,-2,0},0,false).triangle(a,b,c,a,b,c);
    const auto a7=Query({0,.5f,0},identity,{1,1,1},{0,-2,0},0,true).triangle(a,b,c,a,b,c);
    assert(a6&&a7&&a6->initialOverlap&&a7->initialOverlap);
    close(a6->scalar,0);close(a7->scalar,-1);close(a7->fraction,0);
    // Initial vertex penetration uses A6's admitted negative root versus A7's
    // signed overlap and original face normal, visible to a corner mover.
    const auto corner6=Query({-.3f,-.3f,.9f},identity,{1,1,1},{0,0,-1},0,false).triangle(cornerA,cornerB,cornerC,cornerA,cornerB,cornerC);
    const auto corner7=Query({-.3f,-.3f,.9f},identity,{1,1,1},{0,0,-1},0,true).triangle(cornerA,cornerB,cornerC,cornerA,cornerB,cornerC);
    assert(corner6&&corner7&&corner6->initialOverlap&&corner7->initialOverlap);
    close(corner6->scalar,0);close(corner7->scalar,std::sqrt(.99f)-1);
    assert(corner6->normal.x<0&&corner6->normal.y<0);close(corner7->normal.z,1);
    // A7 retains the resource +Z face normal even when its pose rotates the
    // admitted face to +Y. Global/baked vertices and radial contacts differ.
    const Vec3 posedC{0,0,-10};
    const auto posedOverlap=Query({1,.5f,-1},identity,{1,1,1},{0,-2,0},0,true).triangle(cornerA,cornerB,posedC,cornerA,cornerB,cornerC);
    const auto bakedOverlap=Query({1,.5f,-1},identity,{1,1,1},{0,-2,0},0,true).triangle(cornerA,cornerB,posedC,cornerA,cornerB,posedC);
    const auto posedOrdinary=Query({1,3,-1},identity,{1,1,1},{0,-4,0},0,true).triangle(cornerA,cornerB,posedC,cornerA,cornerB,cornerC);
    const auto posedA6=Query({1,.5f,-1},identity,{1,1,1},{0,-2,0},0,false).triangle(cornerA,cornerB,posedC,cornerA,cornerB,cornerC);
    assert(posedOverlap&&bakedOverlap&&posedOrdinary&&posedA6);
    close(posedOverlap->scalar,-1);close(posedOverlap->normal.y,0);close(posedOverlap->normal.z,1);
    close(bakedOverlap->normal.y,1);close(bakedOverlap->normal.z,0);
    close(posedOrdinary->scalar,2);close(posedOrdinary->normal.y,1);close(posedOrdinary->normal.z,0);
    close(posedA6->scalar,0);close(posedA6->normal.y,1);close(posedA6->normal.z,0);
}
}
