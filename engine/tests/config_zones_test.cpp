#include "config/project.h"
#include "core/json.h"
#include "io/ecosystem_outputs.h"
#include "logic/zones.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace sillage {
namespace {

// --- JSON ---------------------------------------------------------------------

TEST(Json, ParsesAndSerializesRoundtrip) {
    const std::string src =
        R"({"name":"salle \"A\"","n":-3.5,"ok":true,"nul":null,"arr":[1,2,[3]],"o":{"k":"v"}})";
    const auto parsed = json::parse(src);
    ASSERT_TRUE(parsed.value.has_value()) << parsed.error;
    const json::Value& v = *parsed.value;
    EXPECT_EQ(v["name"].asString(), "salle \"A\"");
    EXPECT_DOUBLE_EQ(v["n"].asNumber(), -3.5);
    EXPECT_TRUE(v["ok"].asBool());
    EXPECT_TRUE(v["nul"].isNull());
    EXPECT_EQ(v["arr"].asArray().size(), 3u);
    EXPECT_EQ(v["o"]["k"].asString(), "v");
    EXPECT_TRUE(v["missing"].isNull());

    // Serialized form must reparse identically.
    const auto again = json::parse(v.serialize(2));
    ASSERT_TRUE(again.value.has_value()) << again.error;
    EXPECT_EQ((*again.value)["name"].asString(), "salle \"A\"");
}

TEST(Json, RejectsMalformedInput) {
    EXPECT_FALSE(json::parse("{").value.has_value());
    EXPECT_FALSE(json::parse("{\"a\":}").value.has_value());
    EXPECT_FALSE(json::parse("[1,]").value.has_value());
    EXPECT_FALSE(json::parse("{} garbage").value.has_value());
    EXPECT_FALSE(json::parse("\"unterminated").value.has_value());
}

// --- Project config --------------------------------------------------------------

TEST(ProjectConfig, SaveLoadRoundtrip) {
    ProjectConfig cfg;
    cfg.roomSize = {12.0f, 9.0f};
    cfg.simEnabled = false;
    cfg.sensors.push_back({"hokuyo", "192.168.0.10", 10940, {{0.2f, 0.3f}, 0.785f}});
    cfg.zones.push_back({"scene", {{1, 1}, {5, 1}, {5, 4}, {1, 4}}});
    cfg.tuioEnabled = true;
    cfg.predictionSeconds = 0.08f;
    cfg.smoothing = true;

    const auto path = std::filesystem::temp_directory_path() / "sillage_cfg_test.json";
    std::string error;
    ASSERT_TRUE(cfg.save(path, error)) << error;

    const auto loaded = ProjectConfig::load(path, error);
    ASSERT_TRUE(loaded.has_value()) << error;
    EXPECT_FLOAT_EQ(loaded->roomSize.x, 12.0f);
    EXPECT_FALSE(loaded->simEnabled);
    ASSERT_EQ(loaded->sensors.size(), 1u);
    EXPECT_EQ(loaded->sensors[0].host, "192.168.0.10");
    EXPECT_NEAR(loaded->sensors[0].pose.theta, 0.785f, 1e-4f);
    ASSERT_EQ(loaded->zones.size(), 1u);
    EXPECT_EQ(loaded->zones[0].name, "scene");
    EXPECT_EQ(loaded->zones[0].polygon.size(), 4u);
    EXPECT_TRUE(loaded->tuioEnabled);
    EXPECT_NEAR(loaded->predictionSeconds, 0.08f, 1e-5f);
    EXPECT_TRUE(loaded->smoothing);
    std::filesystem::remove(path);
}

TEST(ProjectConfig, RejectsBrokenConfigs) {
    std::string error;
    const auto bad1 = json::parse(R"({"zones":[{"name":"z","polygon":[[1,1],[2,2]]}]})");
    EXPECT_FALSE(ProjectConfig::fromJson(*bad1.value, error).has_value()); // 2 vertices
    const auto bad2 = json::parse(R"({"sensors":[{"port":10940}]})");
    EXPECT_FALSE(ProjectConfig::fromJson(*bad2.value, error).has_value()); // no host
    const auto bad3 = json::parse(R"({"version":99})");
    EXPECT_FALSE(ProjectConfig::fromJson(*bad3.value, error).has_value()); // future version
}

// --- Zones ------------------------------------------------------------------------

Track trackAt(uint32_t id, float x, float y) {
    Track t;
    t.id = id;
    t.position = {x, y};
    return t;
}

TEST(Zones, EnterExitAndCounters) {
    ZoneEngine zones({{"stage", {{0, 0}, {4, 0}, {4, 4}, {0, 4}}}});

    auto events = zones.update({trackAt(1, 2.0f, 2.0f)}); // enters
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, ZoneEvent::Type::Enter);
    EXPECT_EQ(events[0].zone, "stage");
    EXPECT_EQ(events[0].trackId, 1u);

    events = zones.update({trackAt(1, 2.5f, 2.0f)}); // stays: no event
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(zones.status()[0].occupants, 1u);

    events = zones.update({trackAt(1, 6.0f, 2.0f)}); // leaves
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, ZoneEvent::Type::Exit);
    EXPECT_EQ(zones.status()[0].occupants, 0u);
    EXPECT_EQ(zones.status()[0].totalEntries, 1u);

    events = zones.update({trackAt(2, 1.0f, 1.0f)}); // someone else enters
    ASSERT_EQ(events.size(), 1u);
    events = zones.update({}); // track disappears entirely => exit
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, ZoneEvent::Type::Exit);
    EXPECT_EQ(zones.status()[0].totalEntries, 2u);
}

TEST(Zones, PointInConcavePolygon) {
    // L-shaped zone.
    const std::vector<Vec2> l = {{0, 0}, {4, 0}, {4, 2}, {2, 2}, {2, 4}, {0, 4}};
    EXPECT_TRUE(ZoneEngine::pointInPolygon({1, 3}, l));
    EXPECT_TRUE(ZoneEngine::pointInPolygon({3, 1}, l));
    EXPECT_FALSE(ZoneEngine::pointInPolygon({3, 3}, l)); // the notch
    EXPECT_FALSE(ZoneEngine::pointInPolygon({5, 1}, l));
}

// --- Output conditioning --------------------------------------------------------------

TEST(Conditioning, PredictionExtrapolatesAlongVelocity) {
    OutputConditioner cond({.predictionSeconds = 0.1f, .smoothing = false});
    Track t = trackAt(1, 2.0f, 3.0f);
    t.velocity = {1.0f, -0.5f};
    const auto out = cond.apply({t}, 1.0f / 60.0f);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_NEAR(out[0].position.x, 2.1f, 1e-5f);
    EXPECT_NEAR(out[0].position.y, 2.95f, 1e-5f);
}

TEST(Conditioning, SmoothingConvergesWithoutBias) {
    OutputConditioner cond({.predictionSeconds = 0.0f, .smoothing = true});
    // Constant position with alternating 2 cm noise: output variance shrinks.
    float maxLate = 0.0f;
    for (int i = 0; i < 240; ++i) {
        const float noisy = 1.0f + ((i % 2) != 0 ? 0.02f : -0.02f);
        const auto out = cond.apply({trackAt(1, noisy, 0.0f)}, 1.0f / 60.0f);
        if (i > 120) {
            maxLate = std::max(maxLate, std::abs(out[0].position.x - 1.0f));
        }
    }
    EXPECT_LT(maxLate, 0.01f); // noise halved at least
}

} // namespace
} // namespace sillage
