#include "optics/CMOSCameraDetector.hpp"

#include <cmath>
#include <stdexcept>


namespace
{

constexpr double PI =
    3.14159265358979323846;


constexpr double EPS =
    1e-12;


double degToRad(double deg)
{
    return deg * PI / 180.0;
}


double radToDeg(double rad)
{
    return rad * 180.0 / PI;
}

}


CMOSCameraDetector::CMOSCameraDetector(
    const Vec3& position,
    const Vec3& normal,
    double focal_length_m,
    double f_number,
    double fov_h_deg,
    double fov_v_deg
)
:
position_(position),
normal_(normal.normalized()),
focal_length_m_(focal_length_m),
f_number_(f_number),
fov_h_deg_(fov_h_deg),
fov_v_deg_(fov_v_deg)
{
    if (normal.norm2() <= 0.0)
    {
        throw std::runtime_error(
            "CMOSCameraDetector: normal cannot be zero"
        );
    }


    if (focal_length_m_ <= 0.0)
    {
        throw std::runtime_error(
            "CMOSCameraDetector: focal_length_m must be > 0"
        );
    }


    if (f_number_ <= 0.0)
    {
        throw std::runtime_error(
            "CMOSCameraDetector: f_number must be > 0"
        );
    }


    if (fov_h_deg_ <= 0.0 ||
        fov_h_deg_ >= 180.0 ||
        fov_v_deg_ <= 0.0 ||
        fov_v_deg_ >= 180.0)
    {
        throw std::runtime_error(
            "CMOSCameraDetector: invalid FOV"
        );
    }


    // =====================================================
    // 1. 计算有效入瞳直径
    //
    // D = f / N
    // =====================================================

    pupil_diameter_m_ =
        focal_length_m_
        /
        f_number_;


    // =====================================================
    // 2. 根据焦距 + FOV 反算理想 CMOS 尺寸
    //
    // W = 2 f tan(FOV_H / 2)
    // H = 2 f tan(FOV_V / 2)
    // =====================================================

    sensor_width_m_ =
        2.0
        *
        focal_length_m_
        *
        std::tan(
            0.5 * degToRad(fov_h_deg_)
        );


    sensor_height_m_ =
        2.0
        *
        focal_length_m_
        *
        std::tan(
            0.5 * degToRad(fov_v_deg_)
        );


    // =====================================================
    // 3. 建立相机局部坐标系
    //
    // normal_ = 观察方向
    // u_axis_ = 水平
    // v_axis_ = 垂直
    // =====================================================

    Vec3 ref;


    if (std::abs(normal_.z) < 0.9)
    {
        ref =
            Vec3(0.0, 0.0, 1.0);
    }
    else
    {
        ref =
            Vec3(0.0, 1.0, 0.0);
    }


    u_axis_ =
        ref.cross(normal_).normalized();


    v_axis_ =
        normal_.cross(u_axis_).normalized();
}



CMOSCameraHit CMOSCameraDetector::intersect(
    const Vec3& origin,
    const Vec3& direction
) const
{
    CMOSCameraHit result;


    Vec3 d =
        direction.normalized();


    // =====================================================
    // 1. 判断是否从相机正面进入
    //
    // normal 指向相机"看过去"的方向。
    //
    // 如果 normal = +Z，
    // 从前方目标传播到相机的光应该大致为 -Z。
    //
    // 所以必须：
    //
    // d · normal < 0
    // =====================================================

    double facing =
        d.dot(normal_);


    if (facing >= 0.0)
    {
        return result;
    }


    // =====================================================
    // 2. 射线与镜头入瞳平面求交
    //
    // P(t) = origin + t d
    //
    // (P - position) · normal = 0
    // =====================================================

    double denom =
        d.dot(normal_);


    if (std::abs(denom) < EPS)
    {
        return result;
    }


    double t =
        (position_ - origin).dot(normal_)
        /
        denom;


    if (t <= 0.0)
    {
        return result;
    }


    Vec3 p =
        origin + d * t;


    Vec3 rel =
        p - position_;


    double u =
        rel.dot(u_axis_);


    double v =
        rel.dot(v_axis_);


    result.pupil_point =
        p;


    result.pupil_u_m =
        u;


    result.pupil_v_m =
        v;


    result.distance_m =
        t;


    // =====================================================
    // 3. 圆形有效入瞳判断
    // =====================================================

    double pupil_radius =
        0.5 * pupil_diameter_m_;


    double r2 =
        u*u + v*v;


    if (r2 >
        pupil_radius * pupil_radius)
    {
        return result;
    }


    result.hit_pupil =
        true;


    // =====================================================
    // 4. FOV 判断
    //
    // d 是 "目标 -> 相机"
    //
    // 相机观察方向要使用反向：
    //
    // view_dir = -d
    // =====================================================

    Vec3 view_dir =
        d * (-1.0);


    double forward =
        view_dir.dot(normal_);


    if (forward <= 0.0)
    {
        return result;
    }


    double horizontal =
        view_dir.dot(u_axis_);


    double vertical =
        view_dir.dot(v_axis_);


    double angle_h =
        std::atan2(
            horizontal,
            forward
        );


    double angle_v =
        std::atan2(
            vertical,
            forward
        );


    result.angle_h_deg =
        radToDeg(angle_h);


    result.angle_v_deg =
        radToDeg(angle_v);


    double half_fov_h =
        0.5 * degToRad(fov_h_deg_);


    double half_fov_v =
        0.5 * degToRad(fov_v_deg_);


    if (std::abs(angle_h) >
        half_fov_h)
    {
        return result;
    }


    if (std::abs(angle_v) >
        half_fov_v)
    {
        return result;
    }


    result.inside_fov =
        true;


    // =====================================================
    // 5. 理想针孔模型投影到 CMOS
    // =====================================================

    result.sensor_x_m =
        focal_length_m_
        *
        std::tan(angle_h);


    result.sensor_y_m =
        focal_length_m_
        *
        std::tan(angle_v);


    // =====================================================
    // 6. 再确认像点位于 CMOS 有效面积内
    // =====================================================

    if (std::abs(result.sensor_x_m) >
        0.5 * sensor_width_m_)
    {
        return result;
    }


    if (std::abs(result.sensor_y_m) >
        0.5 * sensor_height_m_)
    {
        return result;
    }


    result.hit =
        true;


    return result;
}
