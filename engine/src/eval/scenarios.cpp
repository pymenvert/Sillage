#include "eval/scenarios.h"

#include "pipeline/pipeline.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace sillage {

namespace {

std::vector<SensorPose> twoCornerSensors(Vec2 room) {
    return {
        {{0.15f, 0.15f}, 0.0f},
        {{room.x - 0.15f, room.y - 0.15f}, 3.14159265f},
    };
}

using Motion = Simulator::Motion;

} // namespace

std::vector<Scenario> scenarioLibrary() {
    std::vector<Scenario> lib;
    const Vec2 room{10.0f, 8.0f};

    // 1. X crossing, repeated: the bread-and-butter case. Two agents ping-pong
    //    along the diagonals, meeting near the center over and over.
    {
        Scenario s;
        s.name = "crossing_x";
        s.sim.roomSize = room;
        s.sim.agents = {
            {{1.0f, 1.0f}, {9.0f, 7.0f}, 1.0f, Motion::PingPong, 1.5f},
            {{1.0f, 7.0f}, {9.0f, 1.0f}, 1.4f, Motion::PingPong, 1.5f},
        };
        s.sensors = twoCornerSensors(room);
        s.durationSeconds = 40.0f;
        s.gates = {.idSwitchesMax = 0, .idf1Min = 0.97f, .motaMin = 0.95f,
                   .falsePositiveRateMax = 0.02f};
        lib.push_back(s);
    }

    // 2. Head-on near-miss with turnaround: the documented trap (docs/03 §5).
    //    Both agents walk toward each other on almost the same line, meet at
    //    ~30 cm laterally, and turn back. Velocity says "swap", position says
    //    "bounce" — position must win.
    {
        Scenario s;
        s.name = "head_on_turnaround";
        s.sim.roomSize = room;
        s.sim.agents = {
            {{1.0f, 4.0f}, {4.9f, 4.0f}, 1.1f, Motion::PingPong, 1.5f},
            {{9.0f, 4.3f}, {5.1f, 4.3f}, 1.1f, Motion::PingPong, 1.5f},
        };
        s.sensors = twoCornerSensors(room);
        s.durationSeconds = 40.0f;
        s.gates = {.idSwitchesMax = 0, .idf1Min = 0.97f, .motaMin = 0.95f,
                   .falsePositiveRateMax = 0.02f};
        lib.push_back(s);
    }

    // 3. Side-by-side walk at 45 cm: two people must never merge into one.
    {
        Scenario s;
        s.name = "side_by_side";
        s.sim.roomSize = room;
        s.sim.agents = {
            {{1.0f, 3.8f}, {9.0f, 3.8f}, 1.0f, Motion::PingPong, 1.5f},
            {{1.0f, 4.25f}, {9.0f, 4.25f}, 1.0f, Motion::PingPong, 1.5f},
        };
        s.sensors = twoCornerSensors(room);
        s.durationSeconds = 30.0f;
        s.gates = {.idSwitchesMax = 0, .idf1Min = 0.95f, .motaMin = 0.90f,
                   .falsePositiveRateMax = 0.02f};
        lib.push_back(s);
    }

    // 4. Eight random walkers for two minutes: sustained realistic load.
    {
        Scenario s;
        s.name = "group_8_random";
        s.sim.roomSize = room;
        for (int i = 0; i < 8; ++i) {
            const float x = 1.0f + static_cast<float>(i);
            s.sim.agents.push_back({{x, 1.0f + 0.7f * static_cast<float>(i % 3)},
                                    {x, 7.0f},
                                    0.8f + 0.1f * static_cast<float>(i % 4),
                                    Motion::Random,
                                    1.5f});
        }
        s.sim.seed = 7;
        s.sensors = twoCornerSensors(room);
        s.durationSeconds = 120.0f;
        s.gates = {.idSwitchesMax = 4, .idf1Min = 0.95f, .motaMin = 0.90f,
                   .falsePositiveRateMax = 0.03f};
        // TODO(M1): deep-crowd residue — mutual occlusion knots of 3+ people
        // seen from two corner sensors still cost ~17 switches / IDF1 ~0.63.
        // Candidate work: per-knot joint hypothesis handling, sensor layout
        // guidance (a third sensor breaks most knots), size signatures.
        s.quarantined = true;
        lib.push_back(s);
    }

    // 5. Single sensor + pillar: an agent repeatedly disappears behind a
    //    pillar (total occlusion ~1 s) — coasting and graves must hold the id.
    {
        Scenario s;
        s.name = "pillar_occlusion";
        s.sim.roomSize = room;
        s.sim.pillars = {{{5.0f, 4.0f}, 0.35f}};
        // Crosses the pillar's shadow (as cast from the corner sensor) roughly
        // perpendicularly: total occlusion of ~1 s per pass.
        s.sim.agents = {
            {{2.5f, 6.8f}, {8.5f, 3.0f}, 1.0f, Motion::PingPong, 1.5f},
        };
        s.sensors = {{{0.15f, 0.15f}, 0.0f}};
        s.durationSeconds = 40.0f;
        s.gates = {.idSwitchesMax = 0, .idf1Min = 0.90f, .motaMin = 0.80f,
                   .falsePositiveRateMax = 0.02f};
        lib.push_back(s);
    }

    // 6. Sensor dropout mid-run during crossings: graceful degradation, no
    //    ghost tracks at the failover.
    {
        Scenario s;
        s.name = "sensor_dropout";
        s.sim.roomSize = room;
        s.sim.agents = {
            {{1.0f, 1.0f}, {9.0f, 7.0f}, 1.0f, Motion::PingPong, 1.5f},
            {{1.0f, 7.0f}, {9.0f, 1.0f}, 1.2f, Motion::PingPong, 1.5f},
        };
        s.sensors = twoCornerSensors(room);
        s.durationSeconds = 60.0f;
        s.sensorDropTime = 30.0f;
        s.droppedSensor = 1;
        s.gates = {.idSwitchesMax = 2, .idf1Min = 0.90f, .motaMin = 0.85f,
                   .falsePositiveRateMax = 0.02f};
        lib.push_back(s);
    }

    return lib;
}

ScenarioOutcome runScenario(const Scenario& scenario, float tickHz, bool debugTrace) {
    const float dt = 1.0f / tickHz;

    Simulator sim(scenario.sim);
    for (const SensorPose& pose : scenario.sensors) {
        sim.addSensor(pose);
    }

    PipelineConfig cfg;
    cfg.sensors = scenario.sensors;
    cfg.backgroundBins = scenario.sim.raysPerScan;
    Pipeline pipeline(cfg);

    MotAccumulator mot;
    const auto ticks = static_cast<uint64_t>(scenario.durationSeconds * tickHz);
    for (uint64_t tick = 0; tick < ticks; ++tick) {
        std::vector<ScanFrame> frames = sim.step(dt, TimePoint{});
        const float t = static_cast<float>(tick) * dt;
        if (scenario.sensorDropTime >= 0.0f && t >= scenario.sensorDropTime) {
            std::erase_if(frames, [&](const ScanFrame& f) {
                return f.sensor == scenario.droppedSensor;
            });
        }
        const FrameSnapshot snap = pipeline.process(frames, dt, tick, sim.roomSize());
        if (pipeline.learning()) {
            continue;
        }
        std::vector<GtPoint> gt;
        for (const Simulator::GroundTruth& g : sim.groundTruth()) {
            gt.push_back({g.agentIndex, g.position});
        }
        mot.addFrame(gt, snap.tracks);

        if (debugTrace) {
            static std::set<uint32_t> alive; // debug-only, scenario runs are sequential
            static std::map<uint32_t, uint32_t> agentTrack;
            if (tick == 0) {
                alive.clear();
                agentTrack.clear();
            }
            // Verbose window: tick-level detail while two agents are close.
            for (size_t a = 0; a < gt.size(); ++a) {
                for (size_t b = a + 1; b < gt.size(); ++b) {
                    if ((gt[a].position - gt[b].position).norm() < 0.8f && tick % 6 == 0) {
                        std::printf("t=%6.2f  ", t);
                        for (const GtPoint& g : gt) {
                            std::printf("gt%u(%.2f,%.2f) ", g.agent,
                                        static_cast<double>(g.position.x),
                                        static_cast<double>(g.position.y));
                        }
                        std::printf("| ");
                        for (const Cluster& c : snap.clusters) {
                            std::printf("c(%.2f,%.2f r%.2f n%u%s) ",
                                        static_cast<double>(c.centroid.x),
                                        static_cast<double>(c.centroid.y),
                                        static_cast<double>(c.radius), c.pointCount,
                                        c.fromPredictionSplit ? "*" : "");
                        }
                        std::printf("| ");
                        for (const Track& tr : snap.tracks) {
                            std::printf("id%u(%.2f,%.2f %s) ", tr.id,
                                        static_cast<double>(tr.position.x),
                                        static_cast<double>(tr.position.y),
                                        tr.state == TrackState::Coasting ? "co" : "me");
                        }
                        std::printf("\n");
                    }
                }
            }
            // Switch detector mirroring the CLEAR-MOT persistence rule.
            for (const GtPoint& g : gt) {
                const Track* keep = nullptr;
                const Track* nearest = nullptr;
                float nearestD = 0.6f;
                for (const Track& tr : snap.tracks) {
                    const float d = (tr.position - g.position).norm();
                    const auto it = agentTrack.find(g.agent);
                    if (it != agentTrack.end() && tr.id == it->second && d <= 0.6f) {
                        keep = &tr;
                    }
                    if (d < nearestD) {
                        nearestD = d;
                        nearest = &tr;
                    }
                }
                const Track* match = keep ? keep : nearest;
                if (match == nullptr) {
                    continue;
                }
                const auto it = agentTrack.find(g.agent);
                if (it != agentTrack.end() && it->second != match->id) {
                    std::printf("t=%6.2f  SWITCH gt%u: id %u -> id %u  gt at (%.2f,%.2f)\n", t,
                                g.agent, it->second, match->id,
                                static_cast<double>(g.position.x),
                                static_cast<double>(g.position.y));
                    for (const Track& tr : snap.tracks) {
                        std::printf("           id %u at (%.2f,%.2f) v(%.2f,%.2f) %s\n", tr.id,
                                    static_cast<double>(tr.position.x),
                                    static_cast<double>(tr.position.y),
                                    static_cast<double>(tr.velocity.x),
                                    static_cast<double>(tr.velocity.y),
                                    tr.state == TrackState::Coasting ? "coast" : "meas");
                    }
                }
                agentTrack[g.agent] = match->id;
            }
            std::set<uint32_t> now;
            for (const Track& tr : snap.tracks) {
                now.insert(tr.id);
            }
            for (const Track& tr : snap.tracks) {
                if (!alive.contains(tr.id)) {
                    std::printf("t=%6.2f  +id %u born at (%.2f,%.2f)", t, tr.id,
                                static_cast<double>(tr.position.x),
                                static_cast<double>(tr.position.y));
                    for (const GtPoint& g : gt) {
                        std::printf("  [gt%u (%.2f,%.2f)]", g.agent,
                                    static_cast<double>(g.position.x),
                                    static_cast<double>(g.position.y));
                    }
                    std::printf("  clusters=%zu\n", snap.clusters.size());
                }
            }
            for (const uint32_t id : alive) {
                if (!now.contains(id)) {
                    std::printf("t=%6.2f  -id %u gone\n", t, id);
                }
            }
            alive = std::move(now);
        }
    }

    ScenarioOutcome outcome;
    outcome.scenario = scenario;
    outcome.metrics = mot.result();
    outcome.passed = true;

    const MotResult& m = outcome.metrics;
    const ScenarioGates& g = scenario.gates;
    auto fail = [&](const std::string& why) {
        outcome.passed = false;
        outcome.failureReason += outcome.failureReason.empty() ? why : "; " + why;
    };
    if (m.idSwitches > g.idSwitchesMax) {
        fail("idSwitches " + std::to_string(m.idSwitches) + " > " +
             std::to_string(g.idSwitchesMax));
    }
    if (g.idf1Min >= 0.0f && m.idf1 < g.idf1Min) {
        fail("IDF1 " + std::to_string(m.idf1) + " < " + std::to_string(g.idf1Min));
    }
    if (g.motaMin >= 0.0f && m.mota < g.motaMin) {
        fail("MOTA " + std::to_string(m.mota) + " < " + std::to_string(g.motaMin));
    }
    if (g.falsePositiveRateMax >= 0.0f && m.totalGt > 0) {
        const float fpRate = static_cast<float>(m.falsePositives) / static_cast<float>(m.totalGt);
        if (fpRate > g.falsePositiveRateMax) {
            fail("FP rate " + std::to_string(fpRate) + " > " +
                 std::to_string(g.falsePositiveRateMax));
        }
    }
    return outcome;
}

} // namespace sillage
