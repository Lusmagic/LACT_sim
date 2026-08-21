#include "app/OpticalSimCommon.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace lact;

static constexpr double PI_LOCAL = 3.14159265358979323846;
static constexpr double DEG_TO_RAD_LOCAL = PI_LOCAL / 180.0;

// ============================================================
// Runtime configuration
// ============================================================

struct RuntimeConfig
{
    Vec3 cmos_position_local;
    Vec3 cmos_look_at_local;
    Vec3 cmos_up_reference_local;

    double cmos_focal_length_m = 0.0;
    double cmos_f_number = 0.0;
    double cmos_horizontal_fov_deg = 0.0;
    double cmos_vertical_fov_deg = 0.0;

    double cmos_body_diameter_m = 0.0;
    double cmos_housing_depth_m = 0.0;
    double cmos_base_width_m = 0.0;
    double cmos_base_exposed_height_m = 0.0;
    double cmos_base_left_inset_m = 0.0;
    double cmos_physical_hole_diameter_m = 0.0;
    double cmos_physical_hole_left_edge_distance_m = 0.0;

    bool first_hit_enabled = true;
    double first_hit_epsilon_m = 0.0;

    bool obstruction_runtime_enabled = true;
    bool obstruction_require_config = true;
    double obstruction_start_epsilon_m = 0.0;

    bool save_diffuse_mirror_csv = true;
    std::string diffuse_mirror_csv;

    bool save_diffuse_cmos_csv = true;
    std::string diffuse_cmos_csv;

    bool diagnostics_verbose = true;
    bool diagnostics_print_cmos_frame = true;
    bool diagnostics_machine_readable_summary = true;
};

struct CmosFrame
{
    Vec3 position;
    Vec3 optical_axis;
    Vec3 right_axis;
    Vec3 up_axis;
};

struct DetectorGeometry
{
    double body_radius = 0.0;
    double base_left = 0.0;
    double base_right = 0.0;
    double base_bottom = 0.0;
    double base_top = 0.0;
    double hole_center_x = 0.0;
    double hole_center_y = 0.0;
    double hole_radius = 0.0;
    double pupil_radius = 0.0;
};

struct InputColumns
{
    int x_m = -1;
    int y_m = -1;
    int z_m = -1;
    int dir_x = -1;
    int dir_y = -1;
    int dir_z = -1;
    int wavelength_nm = -1;
    int time_ns = -1;
    int weight = -1;
};

// ============================================================
// Runtime-config helpers
// ============================================================

static std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::unordered_map<std::string, std::string>
readRuntimeConfigFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs)
        throw std::runtime_error("Cannot open runtime config: " + path);

    std::unordered_map<std::string, std::string> cfg;
    std::string line;

    while (std::getline(ifs, line))
    {
        const std::size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos)
            line = line.substr(0, comment_pos);

        line = lact::trim(line);
        if (line.empty())
            continue;

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Invalid runtime config line: " + line);

        const std::string key = lact::trim(line.substr(0, eq));
        const std::string value = lact::trim(line.substr(eq + 1));

        if (key.empty())
            throw std::runtime_error("Empty runtime config key: " + line);

        cfg[key] = value;
    }

    return cfg;
}

static std::string requireRuntimeString(
    const std::unordered_map<std::string, std::string>& cfg,
    const std::string& key)
{
    const auto it = cfg.find(key);

    if (it == cfg.end())
        throw std::runtime_error("Missing runtime config key: " + key);

    const std::string value = lact::trim(it->second);

    if (value.empty())
        throw std::runtime_error("Empty runtime config value: " + key);

    return value;
}

static std::string getRuntimeString(
    const std::unordered_map<std::string, std::string>& cfg,
    const std::string& key,
    const std::string& default_value)
{
    const auto it = cfg.find(key);
    return it == cfg.end() ? default_value : lact::trim(it->second);
}

static double requireRuntimeDouble(
    const std::unordered_map<std::string, std::string>& cfg,
    const std::string& key)
{
    const std::string value = requireRuntimeString(cfg, key);

    try
    {
        std::size_t used = 0;
        const double result = std::stod(value, &used);

        if (used != value.size())
            throw std::runtime_error("");

        return result;
    }
    catch (...)
    {
        throw std::runtime_error(
            "Invalid double for runtime key '" + key + "': " + value
        );
    }
}

static bool parseBool(const std::string& value)
{
    const std::string v = toLower(lact::trim(value));

    if (v == "true" || v == "1" || v == "yes" || v == "on")
        return true;

    if (v == "false" || v == "0" || v == "no" || v == "off")
        return false;

    throw std::runtime_error("Invalid boolean value: " + value);
}

static bool getRuntimeBool(
    const std::unordered_map<std::string, std::string>& cfg,
    const std::string& key,
    bool default_value)
{
    const auto it = cfg.find(key);
    return it == cfg.end() ? default_value : parseBool(it->second);
}

static Vec3 parseVec3(const std::string& value)
{
    std::stringstream ss(value);
    std::string sx, sy, sz, extra;

    if (!std::getline(ss, sx, ',') ||
        !std::getline(ss, sy, ',') ||
        !std::getline(ss, sz, ',') ||
        std::getline(ss, extra, ','))
    {
        throw std::runtime_error("Invalid Vec3 value: " + value);
    }

    try
    {
        return Vec3(
            std::stod(lact::trim(sx)),
            std::stod(lact::trim(sy)),
            std::stod(lact::trim(sz))
        );
    }
    catch (...)
    {
        throw std::runtime_error("Invalid Vec3 value: " + value);
    }
}

// ============================================================
// Vec3 helpers
// ============================================================

static Vec3 addVec(const Vec3& a, const Vec3& b)
{
    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static Vec3 subVec(const Vec3& a, const Vec3& b)
{
    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static Vec3 scaleVec(const Vec3& a, double s)
{
    return Vec3(a.x * s, a.y * s, a.z * s);
}

static double dotVec(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 crossVec(const Vec3& a, const Vec3& b)
{
    return Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

static double normVec(const Vec3& a)
{
    return std::sqrt(dotVec(a, a));
}

static Vec3 normalizeVec(const Vec3& a)
{
    const double n = normVec(a);

    if (n <= 1.0e-15)
        throw std::runtime_error("Cannot normalize zero vector.");

    return scaleVec(a, 1.0 / n);
}

static double distanceVec(const Vec3& a, const Vec3& b)
{
    return normVec(subVec(a, b));
}

// ============================================================
// Runtime configuration
// ============================================================

static RuntimeConfig loadRuntimeConfig(const std::string& path)
{
    const auto cfg = readRuntimeConfigFile(path);
    RuntimeConfig out;

    out.cmos_position_local =
        parseVec3(requireRuntimeString(cfg, "cmos.position_local"));
    out.cmos_look_at_local =
        parseVec3(requireRuntimeString(cfg, "cmos.look_at_local"));
    out.cmos_up_reference_local =
        parseVec3(requireRuntimeString(cfg, "cmos.up_reference_local"));

    out.cmos_focal_length_m =
        requireRuntimeDouble(cfg, "cmos.focal_length_m");
    out.cmos_f_number =
        requireRuntimeDouble(cfg, "cmos.f_number");
    out.cmos_horizontal_fov_deg =
        requireRuntimeDouble(cfg, "cmos.horizontal_fov_deg");
    out.cmos_vertical_fov_deg =
        requireRuntimeDouble(cfg, "cmos.vertical_fov_deg");

    out.cmos_body_diameter_m =
        requireRuntimeDouble(cfg, "cmos.body_diameter_m");
    out.cmos_housing_depth_m =
        requireRuntimeDouble(cfg, "cmos.housing_depth_m");
    out.cmos_base_width_m =
        requireRuntimeDouble(cfg, "cmos.base_width_m");
    out.cmos_base_exposed_height_m =
        requireRuntimeDouble(cfg, "cmos.base_exposed_height_m");
    out.cmos_base_left_inset_m =
        requireRuntimeDouble(cfg, "cmos.base_left_inset_m");
    out.cmos_physical_hole_diameter_m =
        requireRuntimeDouble(cfg, "cmos.physical_hole_diameter_m");
    out.cmos_physical_hole_left_edge_distance_m =
        requireRuntimeDouble(
            cfg,
            "cmos.physical_hole_left_edge_distance_m"
        );

    out.first_hit_enabled =
        getRuntimeBool(cfg, "first_hit.enabled", true);
    out.first_hit_epsilon_m =
        requireRuntimeDouble(cfg, "first_hit.epsilon_m");

    out.obstruction_runtime_enabled =
        getRuntimeBool(cfg, "obstruction.runtime_enabled", true);
    out.obstruction_require_config =
        getRuntimeBool(cfg, "obstruction.require_config", true);
    out.obstruction_start_epsilon_m =
        requireRuntimeDouble(cfg, "obstruction.start_epsilon_m");

    out.save_diffuse_mirror_csv =
        getRuntimeBool(cfg, "output.save_diffuse_mirror_csv", true);
    out.diffuse_mirror_csv =
        getRuntimeString(
            cfg,
            "output.diffuse_mirror_csv",
            "diffuse_mirror_hits.csv"
        );

    out.save_diffuse_cmos_csv =
        getRuntimeBool(cfg, "output.save_diffuse_cmos_csv", true);
    out.diffuse_cmos_csv =
        getRuntimeString(
            cfg,
            "output.diffuse_cmos_csv",
            "diffuse_cmos_hits.csv"
        );

    out.diagnostics_verbose =
        getRuntimeBool(cfg, "diagnostics.verbose", true);
    out.diagnostics_print_cmos_frame =
        getRuntimeBool(cfg, "diagnostics.print_cmos_frame", true);
    out.diagnostics_machine_readable_summary =
        getRuntimeBool(
            cfg,
            "diagnostics.machine_readable_summary",
            true
        );

    if (out.cmos_focal_length_m <= 0.0 ||
        out.cmos_f_number <= 0.0)
    {
        throw std::runtime_error("Invalid CMOS optical parameters.");
    }

    if (out.cmos_horizontal_fov_deg <= 0.0 ||
        out.cmos_horizontal_fov_deg >= 180.0 ||
        out.cmos_vertical_fov_deg <= 0.0 ||
        out.cmos_vertical_fov_deg >= 180.0)
    {
        throw std::runtime_error("Invalid CMOS FOV.");
    }

    if (out.cmos_body_diameter_m <= 0.0 ||
        out.cmos_housing_depth_m <= 0.0 ||
        out.cmos_base_width_m <= 0.0 ||
        out.cmos_base_exposed_height_m < 0.0 ||
        out.cmos_base_left_inset_m < 0.0 ||
        out.cmos_physical_hole_diameter_m <= 0.0 ||
        out.cmos_physical_hole_left_edge_distance_m < 0.0)
    {
        throw std::runtime_error("Invalid CMOS mechanical geometry.");
    }

    if (out.cmos_base_width_m > out.cmos_body_diameter_m)
    {
        throw std::runtime_error(
            "cmos.base_width_m cannot exceed cmos.body_diameter_m."
        );
    }

    if (out.first_hit_epsilon_m < 0.0 ||
        out.obstruction_start_epsilon_m < 0.0)
    {
        throw std::runtime_error("Epsilon values must be >= 0.");
    }

    return out;
}

// ============================================================
// Telescope local -> global
// ============================================================

static Vec3 localVectorToGlobal(
    const Vec3& local,
    const TelescopeFrame& frame)
{
    return Vec3(
        local.x * frame.x_axis.x +
            local.y * frame.y_axis.x +
            local.z * frame.z_axis.x,
        local.x * frame.x_axis.y +
            local.y * frame.y_axis.y +
            local.z * frame.z_axis.y,
        local.x * frame.x_axis.z +
            local.y * frame.y_axis.z +
            local.z * frame.z_axis.z
    );
}

static Vec3 localPointToGlobal(
    const Vec3& local,
    const TelescopeConfig& telescope,
    const TelescopeFrame& frame)
{
    const Vec3 offset =
        localVectorToGlobal(local, frame);

    return Vec3(
        telescope.position_m.x + offset.x,
        telescope.position_m.y + offset.y,
        telescope.position_m.z + offset.z
    );
}

// ============================================================
// CMOS coordinate frame
// ============================================================

static CmosFrame buildCmosFrame(
    const RuntimeConfig& runtime,
    const TelescopeConfig& telescope_cfg,
    const TelescopeFrame& telescope_frame)
{
    CmosFrame frame;

    frame.position =
        localPointToGlobal(
            runtime.cmos_position_local,
            telescope_cfg,
            telescope_frame
        );

    const Vec3 look_at =
        localPointToGlobal(
            runtime.cmos_look_at_local,
            telescope_cfg,
            telescope_frame
        );

    frame.optical_axis =
        normalizeVec(
            subVec(
                look_at,
                frame.position
            )
        );

    const Vec3 up_reference =
        normalizeVec(
            localVectorToGlobal(
                runtime.cmos_up_reference_local,
                telescope_frame
            )
        );

    Vec3 right =
        crossVec(
            frame.optical_axis,
            up_reference
        );

    if (normVec(right) < 1.0e-10)
        right = crossVec(
            frame.optical_axis,
            telescope_frame.x_axis
        );

    if (normVec(right) < 1.0e-10)
        right = crossVec(
            frame.optical_axis,
            telescope_frame.y_axis
        );

    if (normVec(right) < 1.0e-10)
        throw std::runtime_error(
            "Cannot construct CMOS coordinate frame."
        );

    frame.right_axis =
        normalizeVec(right);

    frame.up_axis =
        normalizeVec(
            crossVec(
                frame.right_axis,
                frame.optical_axis
            )
        );

    return frame;
}

// ============================================================
// Detector geometry
// ============================================================

static DetectorGeometry buildDetectorGeometry(
    const RuntimeConfig& runtime)
{
    DetectorGeometry g;

    g.body_radius =
        0.5 *
        runtime.cmos_body_diameter_m;

    g.base_left =
        -g.body_radius +
        runtime.cmos_base_left_inset_m;

    g.base_right =
        g.base_left +
        runtime.cmos_base_width_m;

    g.base_bottom =
        -g.body_radius -
        runtime.cmos_base_exposed_height_m;

    const double limiting_x =
        std::max(
            std::abs(g.base_left),
            std::abs(g.base_right)
        );

    if (limiting_x >= g.body_radius)
    {
        throw std::runtime_error(
            "CMOS base cannot be completely covered by circular body."
        );
    }

    g.base_top =
        -std::sqrt(
            g.body_radius * g.body_radius -
            limiting_x * limiting_x
        );

    g.hole_radius =
        0.5 *
        runtime.cmos_physical_hole_diameter_m;

    g.hole_center_x =
        -g.body_radius +
        runtime.cmos_physical_hole_left_edge_distance_m +
        g.hole_radius;

    g.hole_center_y = 0.0;

    g.pupil_radius =
        0.5 *
        runtime.cmos_focal_length_m /
        runtime.cmos_f_number;

    if (g.pupil_radius > g.hole_radius)
    {
        throw std::runtime_error(
            "Effective pupil is larger than physical receiver hole."
        );
    }

    const double hole_outer =
        std::abs(g.hole_center_x) +
        g.hole_radius;

    if (hole_outer > g.body_radius)
    {
        throw std::runtime_error(
            "Physical receiver hole lies outside circular detector body."
        );
    }

    return g;
}

static bool insideDetectorSilhouette(
    double x,
    double y,
    const DetectorGeometry& g)
{
    const bool inside_disk =
        x * x + y * y <=
        g.body_radius * g.body_radius;

    const bool inside_base =
        x >= g.base_left &&
        x <= g.base_right &&
        y >= g.base_bottom &&
        y <= g.base_top;

    return inside_disk || inside_base;
}

// ============================================================
// Detector 3-D intersection
//
// Front plane: local z = 0
// Detector extends toward -optical_axis.
// Cross section = circular body union base.
// ============================================================

static bool intersectDetectorGeometry(
    const Vec3& origin,
    const Vec3& direction,
    const CmosFrame& cmos,
    const RuntimeConfig& runtime,
    const DetectorGeometry& geometry,
    double& t_hit,
    Vec3& hit_point,
    bool& hit_front_face)
{
    const Vec3 rel =
        subVec(
            origin,
            cmos.position
        );

    const double ox =
        dotVec(
            rel,
            cmos.right_axis
        );

    const double oy =
        dotVec(
            rel,
            cmos.up_axis
        );

    const double oz =
        dotVec(
            rel,
            cmos.optical_axis
        );

    const double dx =
        dotVec(
            direction,
            cmos.right_axis
        );

    const double dy =
        dotVec(
            direction,
            cmos.up_axis
        );

    const double dz =
        dotVec(
            direction,
            cmos.optical_axis
        );

    const double depth =
        runtime.cmos_housing_depth_m;

    double best_t =
        std::numeric_limits<double>::infinity();

    bool best_front = false;

    auto validDepth =
        [&](double t)
    {
        const double z =
            oz + t * dz;

        return
            z <= 1.0e-9 &&
            z >= -depth - 1.0e-9;
    };

    auto tryFrontBack =
        [&](double t, bool front)
    {
        if (t <= 0.0 || t >= best_t)
            return;

        const double x =
            ox + t * dx;

        const double y =
            oy + t * dy;

        if (!insideDetectorSilhouette(
                x,
                y,
                geometry))
        {
            return;
        }

        best_t = t;
        best_front = front;
    };

    if (std::abs(dz) > 1.0e-15)
    {
        tryFrontBack(
            -oz / dz,
            true
        );

        tryFrontBack(
            (-depth - oz) / dz,
            false
        );
    }

    // --------------------------------------------------------
    // Circular side wall
    // --------------------------------------------------------

    const double A =
        dx * dx +
        dy * dy;

    const double B =
        2.0 *
        (ox * dx + oy * dy);

    const double C =
        ox * ox +
        oy * oy -
        geometry.body_radius *
        geometry.body_radius;

    if (A > 1.0e-15)
    {
        const double discriminant =
            B * B -
            4.0 * A * C;

        if (discriminant >= 0.0)
        {
            const double root =
                std::sqrt(discriminant);

            const double t1 =
                (-B - root) /
                (2.0 * A);

            const double t2 =
                (-B + root) /
                (2.0 * A);

            auto tryCircularSide =
                [&](double t)
            {
                if (t <= 0.0 ||
                    t >= best_t ||
                    !validDepth(t))
                {
                    return;
                }

                best_t = t;
                best_front = false;
            };

            tryCircularSide(t1);
            tryCircularSide(t2);
        }
    }

    // --------------------------------------------------------
    // Base side walls
    // --------------------------------------------------------

    auto tryBaseVerticalSide =
        [&](double x_side)
    {
        if (std::abs(dx) <= 1.0e-15)
            return;

        const double t =
            (x_side - ox) / dx;

        if (t <= 0.0 ||
            t >= best_t ||
            !validDepth(t))
        {
            return;
        }

        const double y =
            oy + t * dy;

        if (y < geometry.base_bottom ||
            y > geometry.base_top)
        {
            return;
        }

        best_t = t;
        best_front = false;
    };

    auto tryBaseHorizontalSide =
        [&](double y_side)
    {
        if (std::abs(dy) <= 1.0e-15)
            return;

        const double t =
            (y_side - oy) / dy;

        if (t <= 0.0 ||
            t >= best_t ||
            !validDepth(t))
        {
            return;
        }

        const double x =
            ox + t * dx;

        if (x < geometry.base_left ||
            x > geometry.base_right)
        {
            return;
        }

        best_t = t;
        best_front = false;
    };

    tryBaseVerticalSide(
        geometry.base_left
    );

    tryBaseVerticalSide(
        geometry.base_right
    );

    tryBaseHorizontalSide(
        geometry.base_bottom
    );

    if (!std::isfinite(best_t))
        return false;

    t_hit = best_t;
    hit_front_face = best_front;

    hit_point =
        addVec(
            origin,
            scaleVec(
                direction,
                best_t
            )
        );

    return true;
}

// ============================================================
// Physical receiver hole / effective pupil
// ============================================================

static void detectorFrontCoordinates(
    const Vec3& point,
    const CmosFrame& cmos,
    double& x,
    double& y)
{
    const Vec3 relative =
        subVec(
            point,
            cmos.position
        );

    x =
        dotVec(
            relative,
            cmos.right_axis
        );

    y =
        dotVec(
            relative,
            cmos.up_axis
        );
}

static bool insidePhysicalHole(
    const Vec3& point,
    const CmosFrame& cmos,
    const DetectorGeometry& geometry)
{
    double x = 0.0;
    double y = 0.0;

    detectorFrontCoordinates(
        point,
        cmos,
        x,
        y
    );

    const double dx =
        x -
        geometry.hole_center_x;

    const double dy =
        y -
        geometry.hole_center_y;

    return
        dx * dx +
        dy * dy <=
        geometry.hole_radius *
        geometry.hole_radius;
}

static bool insidePupil(
    const Vec3& point,
    const CmosFrame& cmos,
    const DetectorGeometry& geometry)
{
    double x = 0.0;
    double y = 0.0;

    detectorFrontCoordinates(
        point,
        cmos,
        x,
        y
    );

    const double dx =
        x -
        geometry.hole_center_x;

    const double dy =
        y -
        geometry.hole_center_y;

    return
        dx * dx +
        dy * dy <=
        geometry.pupil_radius *
        geometry.pupil_radius;
}

// ============================================================
// CMOS FOV
// ============================================================

static bool insideCmosFov(
    const Vec3& photon_direction,
    const CmosFrame& cmos,
    const RuntimeConfig& runtime)
{
    const Vec3 viewing_direction =
        normalizeVec(
            scaleVec(
                photon_direction,
                -1.0
            )
        );

    const double forward =
        dotVec(
            viewing_direction,
            cmos.optical_axis
        );

    if (forward <= 0.0)
        return false;

    const double horizontal =
        dotVec(
            viewing_direction,
            cmos.right_axis
        );

    const double vertical =
        dotVec(
            viewing_direction,
            cmos.up_axis
        );

    const double horizontal_angle =
        std::atan2(
            std::abs(horizontal),
            forward
        );

    const double vertical_angle =
        std::atan2(
            std::abs(vertical),
            forward
        );

    const double horizontal_limit =
        0.5 *
        runtime.cmos_horizontal_fov_deg *
        DEG_TO_RAD_LOCAL;

    const double vertical_limit =
        0.5 *
        runtime.cmos_vertical_fov_deg *
        DEG_TO_RAD_LOCAL;

    return
        horizontal_angle <= horizontal_limit &&
        vertical_angle <= vertical_limit;
}

// ============================================================
// CSV helpers
// ============================================================

static std::vector<std::string>
splitCsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;

    while (std::getline(ss, field, ','))
        fields.push_back(lact::trim(field));

    return fields;
}

static int findColumn(
    const std::vector<std::string>& header,
    const std::string& name)
{
    for (std::size_t i = 0; i < header.size(); ++i)
    {
        if (lact::trim(header[i]) == name)
            return static_cast<int>(i);
    }

    return -1;
}

static InputColumns findInputColumns(
    const std::vector<std::string>& header)
{
    InputColumns col;

    col.x_m =
        findColumn(header, "x_m");

    col.y_m =
        findColumn(header, "y_m");

    col.z_m =
        findColumn(header, "z_m");

    col.dir_x =
        findColumn(header, "dir_x");

    col.dir_y =
        findColumn(header, "dir_y");

    col.dir_z =
        findColumn(header, "dir_z");

    col.wavelength_nm =
        findColumn(
            header,
            "wavelength_nm"
        );

    col.time_ns =
        findColumn(
            header,
            "time_ns"
        );

    col.weight =
        findColumn(
            header,
            "weight"
        );

    if (col.x_m < 0 ||
        col.y_m < 0 ||
        col.z_m < 0 ||
        col.dir_x < 0 ||
        col.dir_y < 0 ||
        col.dir_z < 0 ||
        col.wavelength_nm < 0 ||
        col.time_ns < 0 ||
        col.weight < 0)
    {
        throw std::runtime_error(
            "Diffuse CSV must contain "
            "x_m,y_m,z_m,dir_x,dir_y,dir_z,"
            "wavelength_nm,time_ns,weight."
        );
    }

    return col;
}

static double parseCsvDouble(
    const std::vector<std::string>& fields,
    int index,
    const std::string& name)
{
    if (index < 0 ||
        static_cast<std::size_t>(index) >= fields.size())
    {
        throw std::runtime_error(
            "Missing CSV field: " + name
        );
    }

    const std::string value =
        lact::trim(
            fields[
                static_cast<std::size_t>(index)
            ]
        );

    try
    {
        std::size_t used = 0;

        const double result =
            std::stod(
                value,
                &used
            );

        if (used != value.size())
            throw std::runtime_error("");

        return result;
    }
    catch (...)
    {
        throw std::runtime_error(
            "Invalid CSV value for " +
            name + ": " + value
        );
    }
}

// ============================================================
// Main
// ============================================================

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr
            << "Usage: run_diffuse_mirror_detector "
            << "<diffuse_hits.csv> "
            << "<main_config.cfg> "
            << "<simulation_fast_reflectivity.cfg>\n";

        return 2;
    }

    try
    {
        const std::string diffuse_csv =
            argv[1];

        const std::string main_config =
            argv[2];

        const std::string runtime_config =
            argv[3];

        const RuntimeConfig runtime =
            loadRuntimeConfig(
                runtime_config
            );

        // ====================================================
        // Main optical configuration
        // ====================================================

        auto main_cfg =
            readKeyValueConfig(
                main_config
            );

        ComponentConfigPaths component_paths;

        auto cfg =
            expandConfig(
                main_cfg,
                main_config,
                component_paths
            );

        // ====================================================
        // Telescope
        // ====================================================

        TelescopeConfig telescope_cfg =
            buildTelescopeConfig(
                cfg
            );

        TelescopeFrame telescope_frame =
            buildTelescopeFrame(
                telescope_cfg
            );

        // ====================================================
        // Mirrors / output plane
        // ====================================================

        std::vector<MirrorFacet> facets =
            buildFacetsFromConfig(
                cfg
            );

        ErrorConfig error_cfg =
            buildErrorConfig(
                cfg
            );

        applyStructuralDeformation(
            facets,
            error_cfg,
            telescope_cfg
        );

        applyFacetErrors(
            facets,
            error_cfg
        );

        OutputPlane plane =
            buildOutputPlane(
                cfg
            );

        applyTelescopeFrame(
            facets,
            plane,
            telescope_frame
        );

        MirrorLayout mirrors =
            makeMirrorLayoutFromFacets(
                facets
            );

        // ====================================================
        // Optical tracer
        // ====================================================

        OpticalEfficiencyConfig efficiency_cfg =
            buildEfficiencyConfig(
                cfg
            );

        OpticalEfficiency efficiency(
            efficiency_cfg
        );

        PropagationConfig propagation_cfg =
            buildPropagationConfig(
                cfg
            );

        OpticalTracer tracer(
            propagation_cfg.speed_of_light_m_per_ns,
            error_cfg.reflect_direction_sigma_deg *
                DEG_TO_RAD_LOCAL,
            error_cfg.random_seed
        );

        // ====================================================
        // Obstruction
        // ====================================================

        ObstructionMask obstruction =
            buildObstructionMask(
                cfg
            );

        if (runtime.obstruction_runtime_enabled &&
            runtime.obstruction_require_config &&
            !obstruction.enabled)
        {
            throw std::runtime_error(
                "Runtime obstruction enabled, "
                "but obstruction config is disabled."
            );
        }

        const bool obstruction_enabled =
            runtime.obstruction_runtime_enabled &&
            obstruction.enabled;

        // ====================================================
        // Detector
        // ====================================================

        const CmosFrame cmos =
            buildCmosFrame(
                runtime,
                telescope_cfg,
                telescope_frame
            );

        const DetectorGeometry detector_geometry =
            buildDetectorGeometry(
                runtime
            );

        // ====================================================
        // Input diffuse CSV
        // ====================================================

        std::ifstream ifs(
            diffuse_csv
        );

        if (!ifs)
        {
            throw std::runtime_error(
                "Cannot open diffuse CSV: " +
                diffuse_csv
            );
        }

        std::string header_line;

        if (!std::getline(
                ifs,
                header_line))
        {
            throw std::runtime_error(
                "Diffuse CSV is empty: " +
                diffuse_csv
            );
        }

        const std::vector<std::string> header =
            splitCsvLine(
                header_line
            );

        const InputColumns columns =
            findInputColumns(
                header
            );

        // ====================================================
        // Outputs
        // ====================================================

        std::ofstream mirror_ofs;
        std::ofstream cmos_ofs;

        if (runtime.save_diffuse_mirror_csv)
        {
            mirror_ofs.open(
                runtime.diffuse_mirror_csv
            );

            if (!mirror_ofs)
            {
                throw std::runtime_error(
                    "Cannot open diffuse mirror CSV: " +
                    runtime.diffuse_mirror_csv
                );
            }

            mirror_ofs
                << "source_x_m,source_y_m,source_z_m,"
                << "dir_x,dir_y,dir_z,"
                << "mirror_x_m,mirror_y_m,mirror_z_m,"
                << "wavelength_nm,time_ns,weight\n";

            mirror_ofs
                << std::setprecision(17);
        }

        if (runtime.save_diffuse_cmos_csv)
        {
            cmos_ofs.open(
                runtime.diffuse_cmos_csv
            );

            if (!cmos_ofs)
            {
                throw std::runtime_error(
                    "Cannot open diffuse CMOS CSV: " +
                    runtime.diffuse_cmos_csv
                );
            }

            cmos_ofs
                << "source_x_m,source_y_m,source_z_m,"
                << "dir_x,dir_y,dir_z,"
                << "pupil_x_m,pupil_y_m,pupil_z_m,"
                << "wavelength_nm,time_ns,weight\n";

            cmos_ofs
                << std::setprecision(17);
        }

        // ====================================================
        // Statistics
        // ====================================================

        std::uint64_t total_diffuse_rays = 0;
        std::uint64_t invalid_rows = 0;

        std::uint64_t mirror_geometry_hits = 0;
        std::uint64_t detector_geometry_hits = 0;
        std::uint64_t both_geometry_hits = 0;
        std::uint64_t neither_geometry_hits = 0;

        std::uint64_t mirror_first = 0;
        std::uint64_t detector_first = 0;

        std::uint64_t blocked_target_to_mirror = 0;
        std::uint64_t final_mirror_hits = 0;

        std::uint64_t blocked_target_to_detector = 0;
        std::uint64_t detector_housing_first_hits = 0;
        std::uint64_t detector_side_or_back_hits = 0;
        std::uint64_t blocked_by_detector_front_housing = 0;
        std::uint64_t physical_hole_hits = 0;
        std::uint64_t pupil_hits = 0;
        std::uint64_t outside_fov = 0;
        std::uint64_t inside_fov = 0;
        std::uint64_t final_cmos_hits = 0;

        // ====================================================
        // Ray loop
        // ====================================================

        std::string line;

        while (std::getline(
            ifs,
            line))
        {
            line = lact::trim(line);

            if (line.empty())
                continue;

            const std::vector<std::string> fields =
                splitCsvLine(
                    line
                );

            try
            {
                Photon photon;

                photon.pos =
                    Vec3(
                        parseCsvDouble(
                            fields,
                            columns.x_m,
                            "x_m"
                        ),
                        parseCsvDouble(
                            fields,
                            columns.y_m,
                            "y_m"
                        ),
                        parseCsvDouble(
                            fields,
                            columns.z_m,
                            "z_m"
                        )
                    );

                photon.dir =
                    Vec3(
                        parseCsvDouble(
                            fields,
                            columns.dir_x,
                            "dir_x"
                        ),
                        parseCsvDouble(
                            fields,
                            columns.dir_y,
                            "dir_y"
                        ),
                        parseCsvDouble(
                            fields,
                            columns.dir_z,
                            "dir_z"
                        )
                    );

                photon.wavelength_nm =
                    parseCsvDouble(
                        fields,
                        columns.wavelength_nm,
                        "wavelength_nm"
                    );

                photon.time_ns =
                    parseCsvDouble(
                        fields,
                        columns.time_ns,
                        "time_ns"
                    );

                photon.weight =
                    parseCsvDouble(
                        fields,
                        columns.weight,
                        "weight"
                    );

                photon.normalizeDirection();

                ++total_diffuse_rays;

                // --------------------------------------------
                // Mirror geometry candidate
                // --------------------------------------------

                OpticalSurfaceHit mirror_hit =
                    tracer.traceToPlane(
                        photon,
                        mirrors,
                        plane,
                        efficiency
                    );

                const bool has_mirror =
                    mirror_hit.hit_mirror;

                double mirror_distance =
                    std::numeric_limits<double>::infinity();

                if (has_mirror)
                {
                    ++mirror_geometry_hits;

                    mirror_distance =
                        distanceVec(
                            photon.pos,
                            mirror_hit.mirror_point
                        );
                }

                // --------------------------------------------
                // Detector geometry candidate
                // --------------------------------------------

                double detector_distance =
                    std::numeric_limits<double>::infinity();

                Vec3 detector_hit_point;

                bool detector_hit_front_face =
                    false;

                const bool has_detector =
                    intersectDetectorGeometry(
                        photon.pos,
                        photon.dir,
                        cmos,
                        runtime,
                        detector_geometry,
                        detector_distance,
                        detector_hit_point,
                        detector_hit_front_face
                    );

                if (has_detector)
                    ++detector_geometry_hits;

                if (has_mirror &&
                    has_detector)
                {
                    ++both_geometry_hits;
                }

                if (!has_mirror &&
                    !has_detector)
                {
                    ++neither_geometry_hits;
                    continue;
                }

                // --------------------------------------------
                // First physical object
                // --------------------------------------------

                bool choose_mirror = false;
                bool choose_detector = false;

                if (has_mirror &&
                    !has_detector)
                {
                    choose_mirror = true;
                }
                else if (!has_mirror &&
                         has_detector)
                {
                    choose_detector = true;
                }
                else if (!runtime.first_hit_enabled)
                {
                    choose_mirror = true;
                }
                else if (
                    mirror_distance +
                    runtime.first_hit_epsilon_m <
                    detector_distance)
                {
                    choose_mirror = true;
                }
                else
                {
                    choose_detector = true;
                }

                // --------------------------------------------
                // Mirror first
                // --------------------------------------------

                if (choose_mirror)
                {
                    ++mirror_first;

                    bool blocked = false;

                    if (obstruction_enabled)
                    {
                        const Vec3 start =
                            addVec(
                                photon.pos,
                                scaleVec(
                                    photon.dir,
                                    runtime.obstruction_start_epsilon_m
                                )
                            );

                        blocked =
                            segmentBlockedByObstruction(
                                start,
                                mirror_hit.mirror_point,
                                obstruction,
                                &telescope_frame
                            );
                    }

                    if (blocked)
                    {
                        ++blocked_target_to_mirror;
                        continue;
                    }

                    ++final_mirror_hits;

                    if (runtime.save_diffuse_mirror_csv)
                    {
                        mirror_ofs
                            << photon.pos.x << ","
                            << photon.pos.y << ","
                            << photon.pos.z << ","
                            << photon.dir.x << ","
                            << photon.dir.y << ","
                            << photon.dir.z << ","
                            << mirror_hit.mirror_point.x << ","
                            << mirror_hit.mirror_point.y << ","
                            << mirror_hit.mirror_point.z << ","
                            << photon.wavelength_nm << ","
                            << photon.time_ns << ","
                            << photon.weight
                            << "\n";
                    }

                    continue;
                }

                // --------------------------------------------
                // Detector first
                // --------------------------------------------

                if (choose_detector)
                {
                    ++detector_first;

                    bool blocked = false;

                    if (obstruction_enabled)
                    {
                        const Vec3 start =
                            addVec(
                                photon.pos,
                                scaleVec(
                                    photon.dir,
                                    runtime.obstruction_start_epsilon_m
                                )
                            );

                        blocked =
                            segmentBlockedByObstruction(
                                start,
                                detector_hit_point,
                                obstruction,
                                &telescope_frame
                            );
                    }

                    if (blocked)
                    {
                        ++blocked_target_to_detector;
                        continue;
                    }

                    ++detector_housing_first_hits;

                    if (!detector_hit_front_face)
                    {
                        ++detector_side_or_back_hits;
                        continue;
                    }

                    if (!insidePhysicalHole(
                            detector_hit_point,
                            cmos,
                            detector_geometry))
                    {
                        ++blocked_by_detector_front_housing;
                        continue;
                    }

                    ++physical_hole_hits;

                    if (!insidePupil(
                            detector_hit_point,
                            cmos,
                            detector_geometry))
                    {
                        ++blocked_by_detector_front_housing;
                        continue;
                    }

                    ++pupil_hits;

                    if (!insideCmosFov(
                            photon.dir,
                            cmos,
                            runtime))
                    {
                        ++outside_fov;
                        continue;
                    }

                    ++inside_fov;
                    ++final_cmos_hits;

                    if (runtime.save_diffuse_cmos_csv)
                    {
                        cmos_ofs
                            << photon.pos.x << ","
                            << photon.pos.y << ","
                            << photon.pos.z << ","
                            << photon.dir.x << ","
                            << photon.dir.y << ","
                            << photon.dir.z << ","
                            << detector_hit_point.x << ","
                            << detector_hit_point.y << ","
                            << detector_hit_point.z << ","
                            << photon.wavelength_nm << ","
                            << photon.time_ns << ","
                            << photon.weight
                            << "\n";
                    }

                    continue;
                }
            }
            catch (const std::exception&)
            {
                ++invalid_rows;
            }
        }

        // ====================================================
        // Ratios
        // ====================================================

        const double mirror_geometry_ratio =
            total_diffuse_rays > 0
                ? static_cast<double>(
                      mirror_geometry_hits
                  ) /
                  static_cast<double>(
                      total_diffuse_rays
                  )
                : 0.0;

        const double detector_geometry_ratio =
            total_diffuse_rays > 0
                ? static_cast<double>(
                      detector_geometry_hits
                  ) /
                  static_cast<double>(
                      total_diffuse_rays
                  )
                : 0.0;

        const double mirror_obstruction_transmission =
            mirror_first > 0
                ? static_cast<double>(
                      final_mirror_hits
                  ) /
                  static_cast<double>(
                      mirror_first
                  )
                : 0.0;

        const double detector_obstruction_transmission =
            detector_first > 0
                ? static_cast<double>(
                      detector_first -
                      blocked_target_to_detector
                  ) /
                  static_cast<double>(
                      detector_first
                  )
                : 0.0;

        const double final_mirror_ratio =
            total_diffuse_rays > 0
                ? static_cast<double>(
                      final_mirror_hits
                  ) /
                  static_cast<double>(
                      total_diffuse_rays
                  )
                : 0.0;

        const double final_cmos_ratio =
            total_diffuse_rays > 0
                ? static_cast<double>(
                      final_cmos_hits
                  ) /
                  static_cast<double>(
                      total_diffuse_rays
                  )
                : 0.0;

        // ====================================================
        // Human-readable output
        // ====================================================

        std::cout
            << std::fixed
            << std::setprecision(8);

        if (runtime.diagnostics_verbose)
        {
            std::cout
                << "========================================\n"
                << "Diffuse light: Mirror / Detector simulation\n"
                << "========================================\n";

            if (runtime.diagnostics_print_cmos_frame)
            {
                std::cout
                    << "\n[Detector geometry]\n"
                    << "  position_local               : ("
                    << runtime.cmos_position_local.x << ", "
                    << runtime.cmos_position_local.y << ", "
                    << runtime.cmos_position_local.z << ")\n"
                    << "  look_at_local                : ("
                    << runtime.cmos_look_at_local.x << ", "
                    << runtime.cmos_look_at_local.y << ", "
                    << runtime.cmos_look_at_local.z << ")\n"
                    << "  position_global              : ("
                    << cmos.position.x << ", "
                    << cmos.position.y << ", "
                    << cmos.position.z << ")\n"
                    << "  optical_axis_global          : ("
                    << cmos.optical_axis.x << ", "
                    << cmos.optical_axis.y << ", "
                    << cmos.optical_axis.z << ")\n"
                    << "  right_axis_global            : ("
                    << cmos.right_axis.x << ", "
                    << cmos.right_axis.y << ", "
                    << cmos.right_axis.z << ")\n"
                    << "  up_axis_global               : ("
                    << cmos.up_axis.x << ", "
                    << cmos.up_axis.y << ", "
                    << cmos.up_axis.z << ")\n"
                    << "  body_diameter_m              : "
                    << 2.0 * detector_geometry.body_radius << "\n"
                    << "  housing_depth_m              : "
                    << runtime.cmos_housing_depth_m << "\n"
                    << "  base_left_m                  : "
                    << detector_geometry.base_left << "\n"
                    << "  base_right_m                 : "
                    << detector_geometry.base_right << "\n"
                    << "  base_bottom_m                : "
                    << detector_geometry.base_bottom << "\n"
                    << "  base_top_m                   : "
                    << detector_geometry.base_top << "\n"
                    << "  hole_center_x_m              : "
                    << detector_geometry.hole_center_x << "\n"
                    << "  hole_center_y_m              : "
                    << detector_geometry.hole_center_y << "\n"
                    << "  hole_diameter_m              : "
                    << 2.0 * detector_geometry.hole_radius << "\n"
                    << "  pupil_diameter_m             : "
                    << 2.0 * detector_geometry.pupil_radius << "\n";
            }

            std::cout
                << "\n[Results]\n"
                << "  total_diffuse_rays           : "
                << total_diffuse_rays << "\n"
                << "  invalid_rows                 : "
                << invalid_rows << "\n"
                << "  mirror_geometry_hits         : "
                << mirror_geometry_hits << "\n"
                << "  detector_geometry_hits       : "
                << detector_geometry_hits << "\n"
                << "  both_geometry_hits           : "
                << both_geometry_hits << "\n"
                << "  neither_geometry_hits        : "
                << neither_geometry_hits << "\n"
                << "  mirror_first                 : "
                << mirror_first << "\n"
                << "  detector_first               : "
                << detector_first << "\n"
                << "  blocked_target_to_mirror     : "
                << blocked_target_to_mirror << "\n"
                << "  final_mirror_hits            : "
                << final_mirror_hits << "\n"
                << "  blocked_target_to_detector   : "
                << blocked_target_to_detector << "\n"
                << "  detector_housing_first_hits  : "
                << detector_housing_first_hits << "\n"
                << "  detector_side_or_back_hits   : "
                << detector_side_or_back_hits << "\n"
                << "  blocked_by_front_housing     : "
                << blocked_by_detector_front_housing << "\n"
                << "  physical_hole_hits           : "
                << physical_hole_hits << "\n"
                << "  pupil_hits                   : "
                << pupil_hits << "\n"
                << "  outside_fov                  : "
                << outside_fov << "\n"
                << "  inside_fov                   : "
                << inside_fov << "\n"
                << "  final_cmos_hits              : "
                << final_cmos_hits << "\n"
                << "  mirror_geometry_ratio        : "
                << mirror_geometry_ratio << "\n"
                << "  detector_geometry_ratio      : "
                << detector_geometry_ratio << "\n"
                << "  mirror_obstruction_transmission: "
                << mirror_obstruction_transmission << "\n"
                << "  detector_obstruction_transmission: "
                << detector_obstruction_transmission << "\n"
                << "  final_mirror_ratio           : "
                << final_mirror_ratio << "\n"
                << "  final_cmos_ratio             : "
                << final_cmos_ratio << "\n";
        }

        // ====================================================
        // Machine-readable summary
        // ====================================================

        if (runtime.diagnostics_machine_readable_summary)
        {
            std::cout
                << "\n========================================\n"
                << "Machine-readable summary\n"
                << "========================================\n"
                << "total_diffuse_rays="
                << total_diffuse_rays << "\n"
                << "invalid_rows="
                << invalid_rows << "\n"
                << "mirror_geometry_hits="
                << mirror_geometry_hits << "\n"
                << "detector_geometry_hits="
                << detector_geometry_hits << "\n"
                << "both_geometry_hits="
                << both_geometry_hits << "\n"
                << "neither_geometry_hits="
                << neither_geometry_hits << "\n"
                << "mirror_first="
                << mirror_first << "\n"
                << "detector_first="
                << detector_first << "\n"
                << "blocked_target_to_mirror="
                << blocked_target_to_mirror << "\n"
                << "final_mirror_hits="
                << final_mirror_hits << "\n"
                << "blocked_target_to_detector="
                << blocked_target_to_detector << "\n"
                << "detector_housing_first_hits="
                << detector_housing_first_hits << "\n"
                << "detector_side_or_back_hits="
                << detector_side_or_back_hits << "\n"
                << "blocked_by_detector_front_housing="
                << blocked_by_detector_front_housing << "\n"
                << "physical_hole_hits="
                << physical_hole_hits << "\n"
                << "pupil_hits="
                << pupil_hits << "\n"
                << "outside_fov="
                << outside_fov << "\n"
                << "inside_fov="
                << inside_fov << "\n"
                << "final_cmos_hits="
                << final_cmos_hits << "\n"
                << "mirror_geometry_ratio="
                << mirror_geometry_ratio << "\n"
                << "detector_geometry_ratio="
                << detector_geometry_ratio << "\n"
                << "mirror_obstruction_transmission="
                << mirror_obstruction_transmission << "\n"
                << "detector_obstruction_transmission="
                << detector_obstruction_transmission << "\n"
                << "final_mirror_ratio="
                << final_mirror_ratio << "\n"
                << "final_cmos_ratio="
                << final_cmos_ratio << "\n"
                << "first_hit_exclusivity_enabled="
                << (runtime.first_hit_enabled ? 1 : 0) << "\n"
                << "obstruction_enabled="
                << (obstruction_enabled ? 1 : 0) << "\n"
                << "primitive_count="
                << obstruction.primitives.size() << "\n"
                << "detector_body_diameter_m="
                << 2.0 * detector_geometry.body_radius << "\n"
                << "detector_base_left_m="
                << detector_geometry.base_left << "\n"
                << "detector_base_right_m="
                << detector_geometry.base_right << "\n"
                << "detector_base_bottom_m="
                << detector_geometry.base_bottom << "\n"
                << "detector_base_top_m="
                << detector_geometry.base_top << "\n"
                << "detector_hole_center_x_m="
                << detector_geometry.hole_center_x << "\n"
                << "detector_hole_center_y_m="
                << detector_geometry.hole_center_y << "\n"
                << "detector_hole_diameter_m="
                << 2.0 * detector_geometry.hole_radius << "\n"
                << "detector_pupil_diameter_m="
                << 2.0 * detector_geometry.pupil_radius << "\n"
                << "mirror_output_csv="
                << runtime.diffuse_mirror_csv << "\n"
                << "cmos_output_csv="
                << runtime.diffuse_cmos_csv << "\n";
        }

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr
            << "run_diffuse_mirror_detector error: "
            << ex.what()
            << "\n";

        return 1;
    }
}
