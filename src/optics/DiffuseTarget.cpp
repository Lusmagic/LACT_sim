#include "optics/DiffuseTarget.hpp"

#include <cmath>
#include <random>



DiffuseTarget::DiffuseTarget(
    const Vec3& position,
    const Vec3& normal,
    double reflectivity
)
:
position_(position),
normal_(normal.normalized()),
reflectivity_(reflectivity)
{

}



double DiffuseTarget::reflectivity() const
{
    return reflectivity_;
}



const Vec3& DiffuseTarget::position() const
{
    return position_;
}



const Vec3& DiffuseTarget::normal() const
{
    return normal_;
}



Vec3 DiffuseTarget::sampleLambertDirection(
    uint64_t seed
) const
{

    std::mt19937_64 rng(seed);


    std::uniform_real_distribution<double> uni(
        0.0,
        1.0
    );


    /*
       Lambert cosine distribution:

       p(theta)=cos(theta)

    */


    double r1 = uni(rng);

    double r2 = uni(rng);



    double theta =
        std::acos(
            std::sqrt(1.0-r1)
        );


    double phi =
        2.0*M_PI*r2;



    double x =
        std::sin(theta)
        *
        std::cos(phi);



    double y =
        std::sin(theta)
        *
        std::sin(phi);



    double z =
        std::cos(theta);



    /*
       建立局部坐标

       z方向 = normal

    */


    Vec3 w =
        normal_;


    Vec3 ref =
        (
            std::abs(w.z)<0.9
        )
        ?
        Vec3(0,0,1)
        :
        Vec3(1,0,0);



    Vec3 u =
        ref.cross(w).normalized();


    Vec3 v =
        w.cross(u).normalized();



    return
        (
            u*x
            +
            v*y
            +
            w*z
        )
        .normalized();

}
