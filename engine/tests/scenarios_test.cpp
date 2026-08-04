#include "eval/scenarios.h"

#include <gtest/gtest.h>

#include <cstdio>

namespace sillage {
namespace {

// The regression contract of docs/03 §10: every scenario in the library must
// hold its gates. A tracker change that trades one scenario for another fails
// here, in CI, before it ships.
class ScenarioGate : public ::testing::TestWithParam<Scenario> {};

TEST_P(ScenarioGate, HoldsItsGates) {
    const ScenarioOutcome outcome = runScenario(GetParam());
    const MotResult& m = outcome.metrics;
    if (GetParam().quarantined) {
        // Quarantine exempts the scenario from its aspirational gates, not
        // from regressing. The previous unconditional GTEST_SKIP meant this
        // scenario could go from 15 ID switches to 200 without a single red
        // pixel; the ceiling (today's numbers plus margin) turns that into a
        // CI failure while the M1 work item stays open.
        EXPECT_TRUE(outcome.ceilingHeld)
            << GetParam().name << " REGRESSED past its quarantine ceiling: "
            << outcome.ceilingReason << " (IDsw=" << m.idSwitches << " MOTA=" << m.mota
            << " IDF1=" << m.idf1 << " fp=" << m.falsePositives << ")";
        if (outcome.passed) {
            std::printf("[  NOTE  ] %s now passes its real gates — lift the quarantine.\n",
                        GetParam().name.c_str());
        }
        return;
    }
    EXPECT_TRUE(outcome.passed) << GetParam().name << ": " << outcome.failureReason
                                << " (IDsw=" << m.idSwitches << " MOTA=" << m.mota
                                << " IDF1=" << m.idf1 << " miss=" << m.misses
                                << " fp=" << m.falsePositives << ")";
}

INSTANTIATE_TEST_SUITE_P(TrackingRegression, ScenarioGate,
                         ::testing::ValuesIn(scenarioLibrary()),
                         [](const ::testing::TestParamInfo<Scenario>& info) {
                             return info.param.name;
                         });

} // namespace
} // namespace sillage
