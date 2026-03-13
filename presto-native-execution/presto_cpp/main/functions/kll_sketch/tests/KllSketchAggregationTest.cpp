/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "DataSketches/kll_sketch.hpp"

#include "presto_cpp/main/functions/kll_sketch/KllSketchRegistration.h"
#include "velox/exec/PlanNodeStats.h"
#include "velox/exec/tests/utils/PlanBuilder.h"
#include "velox/functions/lib/aggregates/tests/utils/AggregationTestBase.h"

using namespace facebook::velox;
using namespace facebook::velox::exec;
using namespace facebook::velox::exec::test;
using namespace facebook::velox::functions::aggregate::test;
using namespace datasketches;

namespace facebook::presto::functions::aggregate::test {
namespace {
class KllSketchQuantileTest : public AggregationTestBase {
 protected:
  void SetUp() override {
    folly::SingletonVault::singleton()->registrationComplete();
    AggregationTestBase::SetUp();
    presto::functions::aggregate::kll_sketch::registerAllKllSketchFunctions("");
  }

  // Helper to create a serialized KLL sketch from a vector of values
  template <typename T>
  std::string createSerializedSketch(const std::vector<T>& values) {
    datasketches::kll_sketch<T> sketch;
    for (const auto& value : values) {
      sketch.update(value);
    }

    auto serialized = sketch.serialize();
    return std::string(
        reinterpret_cast<const char*>(serialized.data()), serialized.size());
  }

  // Helper to test quantile function for numeric types
  template <typename T>
  T testQuantile(
      const std::vector<T>& values,
      double rank,
      bool inclusive = true,
      const std::string& funcName = "sketch_kll_quantile") {
    auto sketch = createSerializedSketch(values);
    auto input =
        makeRowVector({makeFlatVector<std::string>({sketch}, VARBINARY())});

    std::string query = fmt::format(
        "{}(c0, CAST({} AS DOUBLE){})",
        funcName,
        rank,
        inclusive ? "" : ", false");

    auto op = PlanBuilder().values({input}).project({query}).planNode();

    if constexpr (std::is_same_v<T, double>) {
      return readSingleValue(op).template value<TypeKind::DOUBLE>();
    } else if constexpr (std::is_same_v<T, int64_t>) {
      return readSingleValue(op).template value<TypeKind::BIGINT>();
    } else if constexpr (std::is_same_v<T, bool>) {
      return readSingleValue(op).template value<TypeKind::BOOLEAN>();
    }
  }

  // Helper to test quantile function for VARCHAR type
  std::string testQuantileString(
      const std::vector<std::string>& values,
      double rank,
      bool inclusive = true) {
    auto sketch = createSerializedSketch(values);
    auto input =
        makeRowVector({makeFlatVector<std::string>({sketch}, VARBINARY())});

    std::string query = fmt::format(
        "sketch_kll_quantile_varchar(c0, CAST({} AS DOUBLE){})",
        rank,
        inclusive ? "" : ", false");

    auto op = PlanBuilder().values({input}).project({query}).planNode();

    auto result = readSingleValue(op).template value<TypeKind::VARCHAR>();
    return std::string(result.data(), result.size());
  }
};

// ============================================================================
// Test Doubles - sketch_kll_quantile for DOUBLE type
// ============================================================================
TEST_F(KllSketchQuantileTest, testDoubles) {
  // Create sketch with values 0-99
  std::vector<double> values;
  for (int i = 0; i < 100; i++) {
    values.push_back(static_cast<double>(i));
  }

  // Test sketch_kll_quantile (default returns double)
  EXPECT_NEAR(testQuantile(values, 0.0), 0.0, 1.0);
  EXPECT_NEAR(testQuantile(values, 0.5), 49.5, 2.0);
  EXPECT_NEAR(testQuantile(values, 0.5, false), 49.5, 2.0);
  EXPECT_NEAR(testQuantile(values, 1.0), 99.0, 1.0);
}

// ============================================================================
// Test Ints - sketch_kll_quantile_bigint for BIGINT type
// ============================================================================
TEST_F(KllSketchQuantileTest, testInts) {
  // Create sketch with values 0-99
  std::vector<int64_t> values;
  for (int64_t i = 0; i < 100; i++) {
    values.push_back(i);
  }

  // Test sketch_kll_quantile_bigint
  EXPECT_NEAR(
      testQuantile(values, 0.0, true, "sketch_kll_quantile_bigint"), 0, 1);
  EXPECT_NEAR(
      testQuantile(values, 0.5, true, "sketch_kll_quantile_bigint"), 49, 2);
  EXPECT_NEAR(
      testQuantile(values, 0.5, false, "sketch_kll_quantile_bigint"), 49, 2);
  EXPECT_NEAR(
      testQuantile(values, 1.0, true, "sketch_kll_quantile_bigint"), 99, 1);
}

// ============================================================================
// Test Strings - sketch_kll_quantile_varchar for VARCHAR type
// ============================================================================
TEST_F(KllSketchQuantileTest, testStrings) {
  // Create sketch with letters a-z
  std::vector<std::string> values;
  for (char c = 'a'; c <= 'z'; c++) {
    values.push_back(std::string(1, c));
  }

  // Test sketch_kll_quantile_varchar
  auto q0 = testQuantileString(values, 0.0);
  auto q50 = testQuantileString(values, 0.5);
  auto q50_excl = testQuantileString(values, 0.5, false);
  auto q100 = testQuantileString(values, 1.0);

  EXPECT_EQ(q0, "a");
  EXPECT_TRUE(q50 == "m" || q50 == "n"); // Around middle
  EXPECT_TRUE(q50_excl == "m" || q50_excl == "n");
  EXPECT_EQ(q100, "z");
}

// ============================================================================
// Test Booleans - sketch_kll_quantile_boolean for BOOLEAN type
// ============================================================================
TEST_F(KllSketchQuantileTest, testBooleans) {
  // Create sketch with pattern: every 3rd value is true
  std::vector<bool> values;
  for (int i = 0; i < 100; i++) {
    values.push_back(i % 3 == 0);
  }

  // Test sketch_kll_quantile_boolean
  EXPECT_EQ(
      testQuantile(values, 0.0, true, "sketch_kll_quantile_boolean"), false);
  EXPECT_EQ(
      testQuantile(values, 0.5, true, "sketch_kll_quantile_boolean"), false);
  EXPECT_EQ(
      testQuantile(values, 0.7, true, "sketch_kll_quantile_boolean"), true);
  EXPECT_EQ(
      testQuantile(values, 1.0, true, "sketch_kll_quantile_boolean"), true);
}

} // namespace
} // namespace facebook::presto::functions::aggregate::test

// Made with Bob
