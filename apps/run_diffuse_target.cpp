#include "app/OpticalSimCommon.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace lact;

static constexpr double PI_LOCAL = 3.14159265358979323846;

// ============================================================
// Runtime configuration
// ============================================================

struct RuntimeConfig
{
    Vec3 target_center_global;
    Vec3 target_normal_global;
    double target_width_m = 0.0;
    double target_height_m = 0.0;
    double target_reflectivity = 0.0;
    std::uint64_t target_random_seed = 0;

    bool save_diffuse_csv = true;
    std::string diffuse_csv = "diffuse_hits.csv";

    bool diagnostics_verbose = true;
    bool diagnostics_machine_readable_summary = true;
};

struct TargetFrame
{
    Vec3 center;
    Vec3 normal;
    Vec3 right;
    Vec3 up;
};

struct InputColumns
{
    int surface_x = -1;
    int surface_y = -1;
    int surface_z = -1;
    int time_ns = -1;
    int wavelength_nm = -1;
    int weight = -1;
};

// ============================================================
// String helpers
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

        line = trim(line);

        if (line.empty())
            continue;

        const std::size_t eq = line.find('=');

        if (eq == std::string::npos)
            throw std::runtime_error("Invalid runtime config line: " + line);

        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

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

    if (trim(it->second).empty())
        throw std::runtime_error("Empty runtime config value: " + key);

    return trim(it->second);
}

static std::string getRuntimeString(
    const std::unordered_map<std::string, std::string>& cfg,
    const std::string& key,
    const std::string& default_value)
{
    const auto it = cfg.find(key);

    if (it == cfg.end())
        return default_value;

    return trim(it->second);
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

static std::uint64_t requireRuntimeUInt64(
    const std::unordered_map<std::string, std::string>& cfg,
    const std::string& key)
{
    const std::string value = requireRuntimeString(cfg, key);

    try
    {
        std::size_t used = 0;
        const std::uint64_t result = std::stoull(value, &used);

        if (used != value.size())
            throw std::runtime_error("");

        return result;
    }
    catch (...)
    {
        throw std::runtime_error(
            "Invalid uint64 for runtime key '" + key + "': " + value
        );
    }
}

static bool parseBool(const std::string& value)
{
    const std::string v = toLower(trim(value));

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

    if (it == cfg.end())
        return default_value;

    return parseBool(it->second);
}

static Vec3 parseVec3(const std::string& value)
{
    std::stringstream ss(value);
    std::string sx, sy, sz, extra;

    if (!std::getline(ss, sx, ',') ||
        !std::getline(ss, sy, ',') ||
        !std::getline(ss, sz, ','))
    {
        throw std::runtime_error("Invalid Vec3 value: " + value);
    }

    if (std::getline(ss, extra, ','))
        throw std::runtime_error("Invalid Vec3 value: " + value);

    try
    {
        std::size_t ux = 0;
        std::size_t uy = 0;
        std::size_t uz = 0;

        const std::string tx = trim(sx);
        const std::string ty = trim(sy);
        const std::string tz = trim(sz);

        const double x = std::stod(tx, &ux);
        const double y = std::stod(ty, &uy);
        const double z = std::stod(tz, &uz);

        if (ux != tx.size() || uy != ty.size() || uz != tz.size())
            throw std::runtime_error("");

        return Vec3(x, y, z);
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
    return Vec3(
        a.x + b.x,
        a.y + b.y,
        a.z + b.z
    );
}

static Vec3 subVec(const Vec3& a, const Vec3& b)
{
    return Vec3(
        a.x - b.x,
        a.y - b.y,
        a.z - b.z
    );
}

static Vec3 scaleVec(const Vec3& a, double s)
{
    return Vec3(
        a.x * s,
        a.y * s,
        a.z * s
    );
}

static double dotVec(const Vec3& a, const Vec3& b)
{
    return
        a.x * b.x +
        a.y * b.y +
        a.z * b.z;
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

// ============================================================
// Runtime configuration
// ============================================================

static RuntimeConfig loadRuntimeConfig(const std::string& path)
{
    const auto cfg = readRuntimeConfigFile(path);
    RuntimeConfig out;

    out.target_center_global = parseVec3(
        requireRuntimeString(
            cfg,
            "target.center_global"
        )
    );

    out.target_normal_global = parseVec3(
        requireRuntimeString(
            cfg,
            "target.normal_global"
        )
    );

    out.target_width_m = requireRuntimeDouble(
        cfg,
        "target.width_m"
    );

    out.target_height_m = requireRuntimeDouble(
        cfg,
        "target.height_m"
    );

    out.target_reflectivity = requireRuntimeDouble(
        cfg,
        "target.reflectivity"
    );

    out.target_random_seed = requireRuntimeUInt64(
        cfg,
        "target.random_seed"
    );

    out.save_diffuse_csv = getRuntimeBool(
        cfg,
        "output.save_diffuse_csv",
        true
    );

    out.diffuse_csv = getRuntimeString(
        cfg,
        "output.diffuse_csv",
        "diffuse_hits.csv"
    );

    out.diagnostics_verbose = getRuntimeBool(
        cfg,
        "diagnostics.verbose",
        true
    );

    out.diagnostics_machine_readable_summary = getRuntimeBool(
        cfg,
        "diagnostics.machine_readable_summary",
        true
    );

    if (normVec(out.target_normal_global) <= 1.0e-15)
        throw std::runtime_error(
            "target.normal_global cannot be zero."
        );

    if (out.target_width_m <= 0.0)
        throw std::runtime_error(
            "target.width_m must be > 0."
        );

    if (out.target_height_m <= 0.0)
        throw std::runtime_error(
            "target.height_m must be > 0."
        );

    if (out.target_reflectivity < 0.0 ||
        out.target_reflectivity > 1.0)
    {
        throw std::runtime_error(
            "target.reflectivity must be in [0,1]."
        );
    }

    if (out.save_diffuse_csv && out.diffuse_csv.empty())
        throw std::runtime_error(
            "output.diffuse_csv cannot be empty."
        );

    return out;
}

// ============================================================
// Target frame
// ============================================================

static TargetFrame buildTargetFrame(
    const RuntimeConfig& runtime)
{
    TargetFrame frame;

    frame.center =
        runtime.target_center_global;

    frame.normal =
        normalizeVec(
            runtime.target_normal_global
        );

    Vec3 reference;

    if (std::abs(frame.normal.z) < 0.9)
        reference = Vec3(0.0, 0.0, 1.0);
    else
        reference = Vec3(0.0, 1.0, 0.0);

    frame.right = normalizeVec(
        crossVec(
            reference,
            frame.normal
        )
    );

    frame.up = normalizeVec(
        crossVec(
            frame.normal,
            frame.right
        )
    );

    return frame;
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
        fields.push_back(trim(field));

    return fields;
}

static int findColumn(
    const std::vector<std::string>& header,
    const std::string& name)
{
    for (std::size_t i = 0; i < header.size(); ++i)
    {
        if (trim(header[i]) == name)
            return static_cast<int>(i);
    }

    return -1;
}

static InputColumns findInputColumns(
    const std::vector<std::string>& header)
{
    InputColumns col;

    col.surface_x =
        findColumn(header, "surface_x");

    col.surface_y =
        findColumn(header, "surface_y");

    col.surface_z =
        findColumn(header, "surface_z");

    col.time_ns =
        findColumn(header, "time_ns");

    col.wavelength_nm =
        findColumn(header, "wavelength_nm");

    col.weight =
        findColumn(header, "weight");

    if (col.surface_x < 0 ||
        col.surface_y < 0 ||
        col.surface_z < 0)
    {
        throw std::runtime_error(
            "Input CSV must contain "
            "surface_x,surface_y,surface_z."
        );
    }

    if (col.time_ns < 0)
        throw std::runtime_error(
            "Input CSV must contain time_ns."
        );

    if (col.wavelength_nm < 0)
        throw std::runtime_error(
            "Input CSV must contain wavelength_nm."
        );

    if (col.weight < 0)
        throw std::runtime_error(
            "Input CSV must contain weight."
        );

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
        trim(fields[static_cast<std::size_t>(index)]);

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
            "Invalid CSV value for " +
            name + ": " + value
        );
    }
}

// ============================================================
// Target geometry
// ============================================================

static bool insideTarget(
    const Vec3& point,
    const TargetFrame& frame,
    const RuntimeConfig& runtime)
{
    const Vec3 relative =
        subVec(
            point,
            frame.center
        );

    const double u =
        dotVec(
            relative,
            frame.right
        );

    const double v =
        dotVec(
            relative,
            frame.up
        );

    return
        std::abs(u) <= 0.5 * runtime.target_width_m &&
        std::abs(v) <= 0.5 * runtime.target_height_m;
}

// ============================================================
// Lambert sampling
//
// p(theta,phi) = cos(theta) / pi
//
// Local coordinates:
//   x -> target.right
//   y -> target.up
//   z -> target.normal
// ============================================================

static Vec3 sampleLambertDirection(
    const TargetFrame& frame,
    std::mt19937_64& rng,
    std::uniform_real_distribution<double>& uniform01)
{
    const double u1 = uniform01(rng);
    const double u2 = uniform01(rng);

    const double r =
        std::sqrt(u1);

    const double phi =
        2.0 * PI_LOCAL * u2;

    const double local_x =
        r * std::cos(phi);

    const double local_y =
        r * std::sin(phi);

    const double local_z =
        std::sqrt(
            std::max(
                0.0,
                1.0 - u1
            )
        );

    Vec3 direction =
        addVec(
            addVec(
                scaleVec(
                    frame.right,
                    local_x
                ),
                scaleVec(
                    frame.up,
                    local_y
                )
            ),
            scaleVec(
                frame.normal,
                local_z
            )
        );

    return normalizeVec(direction);
}

// ============================================================
// Main
// ============================================================

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr
            << "Usage: run_diffuse_target "
            << "<target_hits.csv> "
            << "<simulation_fast_reflectivity.cfg>\n";

        return 2;
    }

    try
    {
        const std::string input_csv =
            argv[1];

        const std::string runtime_config =
            argv[2];

        const auto t_start =
            std::chrono::steady_clock::now();

        // ========================================================
        // 1. Runtime configuration
        // ========================================================

        const RuntimeConfig runtime =
            loadRuntimeConfig(
                runtime_config
            );

        const TargetFrame target =
            buildTargetFrame(
                runtime
            );

        // ========================================================
        // 2. Input
        // ========================================================

        std::ifstream ifs(
            input_csv
        );

        if (!ifs)
        {
            throw std::runtime_error(
                "Cannot open target hits CSV: " +
                input_csv
            );
        }

        std::string header_line;

        if (!std::getline(ifs, header_line))
        {
            throw std::runtime_error(
                "Target hits CSV is empty: " +
                input_csv
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

        // ========================================================
        // 3. Output
        // ========================================================

        std::ofstream ofs;

        if (runtime.save_diffuse_csv)
        {
            ofs.open(
                runtime.diffuse_csv
            );

            if (!ofs)
            {
                throw std::runtime_error(
                    "Cannot open diffuse output CSV: " +
                    runtime.diffuse_csv
                );
            }

            ofs
                << "x_m,y_m,z_m,"
                << "dir_x,dir_y,dir_z,"
                << "wavelength_nm,time_ns,weight\n";

            ofs << std::setprecision(17);
        }

        // ========================================================
        // 4. Random generators
        // ========================================================

        std::mt19937_64 rng(
            runtime.target_random_seed
        );

        std::uniform_real_distribution<double>
            uniform01(0.0, 1.0);

        // ========================================================
        // 5. Statistics
        // ========================================================

        std::uint64_t input_target_photons = 0;
        std::uint64_t invalid_rows = 0;
        std::uint64_t inside_target = 0;
        std::uint64_t outside_target = 0;
        std::uint64_t absorbed_by_target = 0;
        std::uint64_t reflected_by_target = 0;
        std::uint64_t diffuse_photons = 0;

        const auto t_trace_start =
            std::chrono::steady_clock::now();

        // ========================================================
        // 6. Photon loop
        // ========================================================

        std::string line;

        while (std::getline(ifs, line))
        {
            line = trim(line);

            if (line.empty())
                continue;

            const std::vector<std::string> fields =
                splitCsvLine(
                    line
                );

            try
            {
                const Vec3 position(
                    parseCsvDouble(
                        fields,
                        columns.surface_x,
                        "surface_x"
                    ),
                    parseCsvDouble(
                        fields,
                        columns.surface_y,
                        "surface_y"
                    ),
                    parseCsvDouble(
                        fields,
                        columns.surface_z,
                        "surface_z"
                    )
                );

                const double time_ns =
                    parseCsvDouble(
                        fields,
                        columns.time_ns,
                        "time_ns"
                    );

                const double wavelength_nm =
                    parseCsvDouble(
                        fields,
                        columns.wavelength_nm,
                        "wavelength_nm"
                    );

                const double weight =
                    parseCsvDouble(
                        fields,
                        columns.weight,
                        "weight"
                    );

                ++input_target_photons;

                // ------------------------------------------------
                // Target finite-size check
                // ------------------------------------------------

                if (
                    !insideTarget(
                        position,
                        target,
                        runtime
                    )
                )
                {
                    ++outside_target;
                    continue;
                }

                ++inside_target;

                // ------------------------------------------------
                // Target reflectivity Monte Carlo
                // ------------------------------------------------

                if (
                    uniform01(rng) >=
                    runtime.target_reflectivity
                )
                {
                    ++absorbed_by_target;
                    continue;
                }

                ++reflected_by_target;

                // ------------------------------------------------
                // Lambert diffuse reflection
                // ------------------------------------------------

                const Vec3 direction =
                    sampleLambertDirection(
                        target,
                        rng,
                        uniform01
                    );

                ++diffuse_photons;

                if (runtime.save_diffuse_csv)
                {
                    ofs
                        << position.x << ","
                        << position.y << ","
                        << position.z << ","
                        << direction.x << ","
                        << direction.y << ","
                        << direction.z << ","
                        << wavelength_nm << ","
                        << time_ns << ","
                        << weight
                        << "\n";
                }
            }
            catch (const std::exception&)
            {
                ++invalid_rows;
            }
        }

        const auto t_trace_done =
            std::chrono::steady_clock::now();

        // ========================================================
        // 7. Derived statistics
        // ========================================================

        const double target_geometric_fraction =
            input_target_photons > 0
                ? static_cast<double>(
                      inside_target
                  ) /
                  static_cast<double>(
                      input_target_photons
                  )
                : 0.0;

        const double target_reflectivity_survival_fraction =
            inside_target > 0
                ? static_cast<double>(
                      reflected_by_target
                  ) /
                  static_cast<double>(
                      inside_target
                  )
                : 0.0;

        const double final_diffuse_fraction =
            input_target_photons > 0
                ? static_cast<double>(
                      diffuse_photons
                  ) /
                  static_cast<double>(
                      input_target_photons
                  )
                : 0.0;

        const double trace_time_s =
            std::chrono::duration<double>(
                t_trace_done -
                t_trace_start
            ).count();

        const double total_time_s =
            std::chrono::duration<double>(
                t_trace_done -
                t_start
            ).count();

        // ========================================================
        // 8. Human-readable output
        // ========================================================

        std::cout
            << std::fixed
            << std::setprecision(8);

        if (runtime.diagnostics_verbose)
        {
            std::cout
                << "========================================\n"
                << "Diffuse Target Simulation\n"
                << "========================================\n";

            std::cout
                << "\n[Configuration]\n"
                << "  input_csv               : "
                << input_csv << "\n"
                << "  runtime_config          : "
                << runtime_config << "\n";

            std::cout
                << "\n[Target geometry]\n"
                << "  center_global           : ("
                << target.center.x << ", "
                << target.center.y << ", "
                << target.center.z << ")\n"
                << "  normal_global           : ("
                << target.normal.x << ", "
                << target.normal.y << ", "
                << target.normal.z << ")\n"
                << "  right_global            : ("
                << target.right.x << ", "
                << target.right.y << ", "
                << target.right.z << ")\n"
                << "  up_global               : ("
                << target.up.x << ", "
                << target.up.y << ", "
                << target.up.z << ")\n"
                << "  width_m                 : "
                << runtime.target_width_m << "\n"
                << "  height_m                : "
                << runtime.target_height_m << "\n"
                << "  reflectivity            : "
                << runtime.target_reflectivity << "\n"
                << "  random_seed             : "
                << runtime.target_random_seed << "\n";

            std::cout
                << "\n[Results]\n"
                << "  input_target_photons    : "
                << input_target_photons << "\n"
                << "  invalid_rows            : "
                << invalid_rows << "\n"
                << "  inside_target           : "
                << inside_target << "\n"
                << "  outside_target          : "
                << outside_target << "\n"
                << "  absorbed_by_target      : "
                << absorbed_by_target << "\n"
                << "  reflected_by_target     : "
                << reflected_by_target << "\n"
                << "  diffuse_photons         : "
                << diffuse_photons << "\n"
                << "  target_geometric_fraction: "
                << target_geometric_fraction << "\n"
                << "  target_reflectivity_survival_fraction: "
                << target_reflectivity_survival_fraction
                << "\n"
                << "  final_diffuse_fraction  : "
                << final_diffuse_fraction << "\n";

            std::cout
                << "\n[Output]\n"
                << "  save_diffuse_csv        : "
                << (
                    runtime.save_diffuse_csv
                        ? "true"
                        : "false"
                )
                << "\n"
                << "  diffuse_csv             : "
                << runtime.diffuse_csv << "\n";

            std::cout
                << "\n[Timing]\n"
                << "  trace_time_s            : "
                << trace_time_s << "\n"
                << "  total_time_s            : "
                << total_time_s << "\n";
        }

        // ========================================================
        // 9. Machine-readable summary
        // ========================================================

        if (runtime.diagnostics_machine_readable_summary)
        {
            std::cout
                << "\n"
                << "========================================\n"
                << "Machine-readable summary\n"
                << "========================================\n"
                << "input_target_photons="
                << input_target_photons << "\n"
                << "invalid_rows="
                << invalid_rows << "\n"
                << "inside_target="
                << inside_target << "\n"
                << "outside_target="
                << outside_target << "\n"
                << "absorbed_by_target="
                << absorbed_by_target << "\n"
                << "reflected_by_target="
                << reflected_by_target << "\n"
                << "diffuse_photons="
                << diffuse_photons << "\n"
                << "target_geometric_fraction="
                << target_geometric_fraction << "\n"
                << "target_reflectivity_survival_fraction="
                << target_reflectivity_survival_fraction
                << "\n"
                << "final_diffuse_fraction="
                << final_diffuse_fraction << "\n"
                << "target_center_global="
                << target.center.x << ","
                << target.center.y << ","
                << target.center.z << "\n"
                << "target_normal_global="
                << target.normal.x << ","
                << target.normal.y << ","
                << target.normal.z << "\n"
                << "target_width_m="
                << runtime.target_width_m << "\n"
                << "target_height_m="
                << runtime.target_height_m << "\n"
                << "target_reflectivity="
                << runtime.target_reflectivity << "\n"
                << "target_random_seed="
                << runtime.target_random_seed << "\n"
                << "diffuse_output_csv="
                << runtime.diffuse_csv << "\n";
        }

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr
            << "run_diffuse_target error: "
            << ex.what()
            << "\n";

        return 1;
    }
}
