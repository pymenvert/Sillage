#include "config/project.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace sillage {

namespace {

json::Value vec2Json(Vec2 v) {
    return json::Array{json::Value(static_cast<double>(v.x)),
                       json::Value(static_cast<double>(v.y))};
}

std::optional<Vec2> vec2From(const json::Value& v) {
    if (!v.isArray() || v.asArray().size() != 2) {
        return std::nullopt;
    }
    return Vec2{static_cast<float>(v.asArray()[0].asNumber()),
                static_cast<float>(v.asArray()[1].asNumber())};
}

// Strict port field: absent -> fallback, anything else must be an integer in
// [1, 65535]. The previous raw static_cast invited silent wrap-around — a
// typo'd 70000 became port 4464, -1 became 65535, and a string value silently
// took the fallback — and the engine then "worked" while sending to the wrong
// port, which on a show site reads as "the network is broken".
std::optional<uint16_t> readPort(const json::Value& v, uint16_t fallback, const char* what,
                                 std::string& error) {
    if (v.isNull()) {
        return fallback;
    }
    const double d = v.isNumber() ? v.asNumber() : -1.0;
    if (d < 1.0 || d > 65535.0 || d != std::floor(d)) {
        error = std::string(what) + " must be an integer between 1 and 65535";
        return std::nullopt;
    }
    return static_cast<uint16_t>(d);
}

} // namespace

json::Value ProjectConfig::toJson() const {
    json::Object root;
    root["version"] = json::Value(kVersion);
    root["room"] = vec2Json(roomSize);
    root["sim"] = json::Value(simEnabled);

    json::Array sensorArr;
    for (const SensorConfig& s : sensors) {
        json::Object o;
        o["type"] = json::Value(s.type);
        o["host"] = json::Value(s.host);
        o["port"] = json::Value(static_cast<double>(s.port));
        o["position"] = vec2Json(s.pose.position);
        o["theta"] = json::Value(static_cast<double>(s.pose.theta));
        // emplace_back, not push_back(Value(...)): constructing the Value in
        // place avoids a temporary whose variant GCC 13 cannot prove
        // initialized, which -Werror=maybe-uninitialized turns into a hard
        // build failure (see engine/CMakeLists.txt for the warning flags).
        sensorArr.emplace_back(std::move(o));
    }
    root["sensors"] = json::Value(std::move(sensorArr));

    json::Array zoneArr;
    for (const ZoneConfig& z : zones) {
        json::Object o;
        o["name"] = json::Value(z.name);
        json::Array poly;
        for (const Vec2 p : z.polygon) {
            poly.push_back(vec2Json(p));
        }
        o["polygon"] = json::Value(std::move(poly));
        zoneArr.emplace_back(std::move(o));
    }
    root["zones"] = json::Value(std::move(zoneArr));

    json::Object outputs;
    json::Object osc;
    osc["enabled"] = json::Value(oscEnabled);
    osc["host"] = json::Value(oscHost);
    osc["port"] = json::Value(static_cast<double>(oscPort));
    outputs["augmentaOsc"] = json::Value(std::move(osc));
    json::Object tuio;
    tuio["enabled"] = json::Value(tuioEnabled);
    tuio["host"] = json::Value(tuioHost);
    tuio["port"] = json::Value(static_cast<double>(tuioPort));
    outputs["tuio"] = json::Value(std::move(tuio));
    json::Object adm;
    adm["enabled"] = json::Value(admEnabled);
    adm["host"] = json::Value(admHost);
    adm["port"] = json::Value(static_cast<double>(admPort));
    adm["maxObjects"] = json::Value(static_cast<double>(admMaxObjects));
    outputs["admOsc"] = json::Value(std::move(adm));
    root["outputs"] = json::Value(std::move(outputs));

    json::Object conditioning;
    conditioning["predictionSeconds"] = json::Value(static_cast<double>(predictionSeconds));
    conditioning["smoothing"] = json::Value(smoothing);
    root["conditioning"] = json::Value(std::move(conditioning));

    return json::Value(std::move(root));
}

std::optional<ProjectConfig> ProjectConfig::fromJson(const json::Value& v, std::string& error) {
    if (!v.isObject()) {
        error = "config root must be an object";
        return std::nullopt;
    }
    ProjectConfig cfg;
    const int version = static_cast<int>(v["version"].asNumber(kVersion));
    if (version > kVersion) {
        error = "config version " + std::to_string(version) + " is newer than this engine";
        return std::nullopt;
    }
    if (const auto room = vec2From(v["room"])) {
        if (room->x < 1.0f || room->y < 1.0f) {
            error = "room must be at least 1x1 m";
            return std::nullopt;
        }
        cfg.roomSize = *room;
    }
    cfg.simEnabled = v["sim"].asBool(cfg.simEnabled);

    for (const json::Value& s : v["sensors"].asArray()) {
        SensorConfig sensor;
        sensor.type = s["type"].asString().empty() ? "hokuyo" : s["type"].asString();
        sensor.host = s["host"].asString();
        // Same per-type defaults as the CLI flags in main.cpp — a single
        // hardcoded 10940 here meant a file-configured SICK defaulted to the
        // Hokuyo port and never connected, while `--sick` worked.
        const uint16_t defaultPort = sensor.type == "sick"  ? 2112
                                     : sensor.type == "udp" ? 9911
                                                            : 10940;
        if (const auto port = readPort(s["port"], defaultPort, "sensor port", error)) {
            sensor.port = *port;
        } else {
            return std::nullopt;
        }
        if (const auto pos = vec2From(s["position"])) {
            sensor.pose.position = *pos;
        }
        sensor.pose.theta = static_cast<float>(s["theta"].asNumber());
        if (sensor.host.empty() && sensor.type != "udp") { // udp binds locally
            error = "sensor without host";
            return std::nullopt;
        }
        cfg.sensors.push_back(std::move(sensor));
    }

    for (const json::Value& z : v["zones"].asArray()) {
        ZoneConfig zone;
        zone.name = z["name"].asString();
        for (const json::Value& p : z["polygon"].asArray()) {
            if (const auto vertex = vec2From(p)) {
                zone.polygon.push_back(*vertex);
            }
        }
        if (zone.name.empty() || zone.polygon.size() < 3) {
            error = "zone '" + zone.name + "' needs a name and >= 3 vertices";
            return std::nullopt;
        }
        cfg.zones.push_back(std::move(zone));
    }

    const json::Value& osc = v["outputs"]["augmentaOsc"];
    cfg.oscEnabled = osc["enabled"].asBool(cfg.oscEnabled);
    if (!osc["host"].asString().empty()) {
        cfg.oscHost = osc["host"].asString();
    }
    if (const auto port = readPort(osc["port"], cfg.oscPort, "augmentaOsc port", error)) {
        cfg.oscPort = *port;
    } else {
        return std::nullopt;
    }
    const json::Value& tuio = v["outputs"]["tuio"];
    cfg.tuioEnabled = tuio["enabled"].asBool(cfg.tuioEnabled);
    if (!tuio["host"].asString().empty()) {
        cfg.tuioHost = tuio["host"].asString();
    }
    if (const auto port = readPort(tuio["port"], cfg.tuioPort, "tuio port", error)) {
        cfg.tuioPort = *port;
    } else {
        return std::nullopt;
    }
    const json::Value& adm = v["outputs"]["admOsc"];
    cfg.admEnabled = adm["enabled"].asBool(cfg.admEnabled);
    if (!adm["host"].asString().empty()) {
        cfg.admHost = adm["host"].asString();
    }
    if (const auto port = readPort(adm["port"], cfg.admPort, "admOsc port", error)) {
        cfg.admPort = *port;
    } else {
        return std::nullopt;
    }
    if (!adm["maxObjects"].isNull()) {
        const double n = adm["maxObjects"].isNumber() ? adm["maxObjects"].asNumber() : -1.0;
        // A negative value static_cast to uint32_t used to become ~4 billion
        // ADM objects; cap to a range that still covers every console.
        if (n < 1.0 || n > 4096.0 || n != std::floor(n)) {
            error = "admOsc maxObjects must be an integer between 1 and 4096";
            return std::nullopt;
        }
        cfg.admMaxObjects = static_cast<uint32_t>(n);
    }

    cfg.predictionSeconds =
        static_cast<float>(v["conditioning"]["predictionSeconds"].asNumber());
    cfg.smoothing = v["conditioning"]["smoothing"].asBool();
    return cfg;
}

bool ProjectConfig::save(const std::filesystem::path& file, std::string& error) const {
    const std::filesystem::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + tmp.string();
            return false;
        }
        out << toJson().serialize(2) << "\n";
        out.flush();
        // Verify the write actually succeeded (disk full, quota, I/O error)
        // BEFORE the rename — otherwise the good project file gets replaced by
        // a truncated one and the installation loses its configuration.
        if (!out.good()) {
            out.close();
            std::error_code rmErr;
            std::filesystem::remove(tmp, rmErr);
            error = "write failed (disk full?) for " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec); // atomic on the same volume
    if (ec) {
        std::error_code rmErr;
        std::filesystem::remove(tmp, rmErr);
        error = "rename failed: " + ec.message();
        return false;
    }
    return true;
}

std::optional<ProjectConfig> ProjectConfig::load(const std::filesystem::path& file,
                                                 std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot open " + file.string();
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const auto parsed = json::parse(buffer.str());
    if (!parsed.value) {
        error = "invalid JSON: " + parsed.error;
        return std::nullopt;
    }
    return fromJson(*parsed.value, error);
}

} // namespace sillage
