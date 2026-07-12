#include "config/project.h"

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
        sensorArr.push_back(json::Value(std::move(o)));
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
        zoneArr.push_back(json::Value(std::move(o)));
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
        sensor.port = static_cast<uint16_t>(s["port"].asNumber(10940));
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
    cfg.oscPort = static_cast<uint16_t>(osc["port"].asNumber(cfg.oscPort));
    const json::Value& tuio = v["outputs"]["tuio"];
    cfg.tuioEnabled = tuio["enabled"].asBool(cfg.tuioEnabled);
    if (!tuio["host"].asString().empty()) {
        cfg.tuioHost = tuio["host"].asString();
    }
    cfg.tuioPort = static_cast<uint16_t>(tuio["port"].asNumber(cfg.tuioPort));
    const json::Value& adm = v["outputs"]["admOsc"];
    cfg.admEnabled = adm["enabled"].asBool(cfg.admEnabled);
    if (!adm["host"].asString().empty()) {
        cfg.admHost = adm["host"].asString();
    }
    cfg.admPort = static_cast<uint16_t>(adm["port"].asNumber(cfg.admPort));
    cfg.admMaxObjects = static_cast<uint32_t>(adm["maxObjects"].asNumber(cfg.admMaxObjects));

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
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec); // atomic on the same volume
    if (ec) {
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
