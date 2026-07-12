#include "eval/scenarios.h"

#include <gtest/gtest.h>

namespace sillage {
namespace {

// The regression contract of docs/03 §10: every scenario in the library must
// hold its gates. A tracker change that trades one scenario for another fails
// here, in CI, before it ships.
class ScenarioGate : public ::testing::TestWithParam<Scenario> {};

TEST_P(ScenarioGate, HoldsItsGates) {
    const ScenarioOutcome outcome = runScenario(GetParam());
    const MotResult& m = outcome.metrics;
    if (GetParam().quarantined && !outcome.passed) {
        GTEST_SKIP() << GetParam().name << " (quarantined): " << outcome.failureReason
                     << " (IDsw=" << m.idSwitches << " MOTA=" << m.mota << " IDF1=" << m.idf1
                     << ")";
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
