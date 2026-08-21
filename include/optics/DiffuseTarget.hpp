#pragma once

#include "core/Vec3.hpp"

#include <cstdint>


class DiffuseTarget
{

public:

    DiffuseTarget(
        const Vec3& position,
        const Vec3& normal,
        double reflectivity
    );


    /*
        Lambert半球随机方向
    */
    Vec3 sampleLambertDirection(
        uint64_t seed
    ) const;



    double reflectivity() const;


    const Vec3& position() const;


    const Vec3& normal() const;



private:

    Vec3 position_;

    Vec3 normal_;

    double reflectivity_;


};
