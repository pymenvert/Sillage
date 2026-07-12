#pragma once

#include "core/types.h"
#include "eval/metrics.h"
#include "sim/simulator.h"

#include <functional>
#include <string>
#include <vector>

namespace sillage {

// Acceptance gates a scenario must satisfy (docs/03 §10). Values <= 0 for
// idSwitchesMax mean "must be exactly zero"; NaN-free floats, -1 disables.
struct ScenarioGates {
    int idSwitchesMax = 0;
    float idf1Min = -1.0f;
    float motaMin = -1.0f;
    float falsePositiveRateMax = -1.0f; // fp / totalGt
};

struct Scenario {
    std::string name;
    Simulator::Params sim;
    std::vector<SensorPose> sensors;
    float durationSeconds = 30.0f;
    ScenarioGates gates;
    // Optional fault injection: sensors whose frames are dropped after a time.
    float sensorDropTime = -1.0f;
    SensorId droppedSensor = 0;
    // Quarantined scenarios run and report but do not fail CI: the gate is a
    // target not yet reached, tracked as an open M1 work item. Silence is not
    // an option; neither is a red CI nobody trusts.
    bool quarantined = false;
};

// The regression library. Every entry runs in CI; a failed gate fails the build.
std::vector<Scenario> scenarioLibrary();

struct ScenarioOutcome {
    Scenario scenario;
    MotResult metrics;
    bool passed = false;
    std::string failureReason;
    float avgTickUs = 0.0f; // pipeline cost, microseconds per tick
    float maxTickUs = 0.0f;
};

// Runs one scenario headless at full speed. Deterministic.
// debugTrace prints track births/deaths with ground truth context (docs/09:
// answering "why did it lose him?" must never require a debugger).
ScenarioOutcome runScenario(const Scenario& scenario, float tickHz = 60.0f,
                            bool debugTrace = false);

} // namespace sillage
