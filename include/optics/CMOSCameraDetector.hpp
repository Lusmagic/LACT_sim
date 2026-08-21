#pragma once

#include "core/Vec3.hpp"


struct CMOSCameraHit
{
    // 是否真正被相机接受
    bool hit = false;

    // 是否进入圆形镜头入瞳
    bool hit_pupil = false;

    // 是否满足相机视场
    bool inside_fov = false;


    // 入瞳交点
    Vec3 pupil_point;


    // 入瞳局部坐标
    double pupil_u_m = 0.0;
    double pupil_v_m = 0.0;


    // 理想 CMOS 像面坐标
    double sensor_x_m = 0.0;
    double sensor_y_m = 0.0;


    // 水平/垂直入射视场角
    double angle_h_deg = 0.0;
    double angle_v_deg = 0.0;


    // 从光线起点到入瞳的传播距离
    double distance_m = 0.0;
};


class CMOSCameraDetector
{
public:

    CMOSCameraDetector(
        const Vec3& position,
        const Vec3& normal,
        double focal_length_m,
        double f_number,
        double fov_h_deg,
        double fov_v_deg
    );


    CMOSCameraHit intersect(
        const Vec3& origin,
        const Vec3& direction
    ) const;


    const Vec3& position() const
    {
        return position_;
    }


    const Vec3& normal() const
    {
        return normal_;
    }


    double focalLength() const
    {
        return focal_length_m_;
    }


    double fNumber() const
    {
        return f_number_;
    }


    double pupilDiameter() const
    {
        return pupil_diameter_m_;
    }


    double sensorWidth() const
    {
        return sensor_width_m_;
    }


    double sensorHeight() const
    {
        return sensor_height_m_;
    }


    double fovHorizontalDeg() const
    {
        return fov_h_deg_;
    }


    double fovVerticalDeg() const
    {
        return fov_v_deg_;
    }


private:

    // 相机镜头中心
    Vec3 position_;


    // 相机观察方向
    Vec3 normal_;


    // 相机局部水平和垂直轴
    Vec3 u_axis_;
    Vec3 v_axis_;


    // 镜头参数
    double focal_length_m_;
    double f_number_;


    // 圆形有效入瞳直径
    double pupil_diameter_m_;


    // FOV
    double fov_h_deg_;
    double fov_v_deg_;


    // 由焦距和FOV计算得到的理想 CMOS 尺寸
    double sensor_width_m_;
    double sensor_height_m_;
};
