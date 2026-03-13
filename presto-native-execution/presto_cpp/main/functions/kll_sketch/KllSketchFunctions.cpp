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

#include "presto_cpp/main/functions/kll_sketch/KllSketchRegistration.h"

#include "DataSketches/kll_sketch.hpp"

#include "velox/expression/VectorFunction.h"
#include "velox/functions/Registerer.h"
#include "velox/vector/FlatVector.h"

namespace facebook::presto::functions {

namespace {

// Generic sketch_kll_quantile function template
template <typename SketchType, typename OutputType>
class KllSketchQuantileFunctionTyped : public velox::exec::VectorFunction {
 public:
  void apply(
      const velox::SelectivityVector& rows,
      std::vector<velox::VectorPtr>& args,
      const velox::TypePtr& outputType,
      velox::exec::EvalCtx& context,
      velox::VectorPtr& result) const override {
    VELOX_CHECK_GE(args.size(), 2);
    VELOX_CHECK_LE(args.size(), 3);

    auto sketchVector = args[0]->as<velox::SimpleVector<velox::StringView>>();
    auto rankVector = args[1]->as<velox::SimpleVector<double>>();
    auto inclusiveVector =
        args.size() == 3 ? args[2]->as<velox::SimpleVector<bool>>() : nullptr;

    context.ensureWritable(rows, outputType, result);

    rows.applyToSelected([&](velox::vector_size_t row) {
      if (sketchVector->isNullAt(row) || rankVector->isNullAt(row)) {
        result->setNull(row, true);
        return;
      }

      auto sketchData = sketchVector->valueAt(row);
      double rank = rankVector->valueAt(row);
      bool inclusive = inclusiveVector ? inclusiveVector->valueAt(row) : true;

      auto sketch = datasketches::kll_sketch<SketchType>::deserialize(
          sketchData.data(), sketchData.size());
      auto quantile = sketch.get_quantile(rank, inclusive);

      if constexpr (std::is_same_v<OutputType, velox::StringView>) {
        // For VARCHAR, need to copy string data
        result->as<velox::FlatVector<velox::StringView>>()->set(
            row, velox::StringView(quantile));
      } else {
        result->as<velox::FlatVector<OutputType>>()->set(row, quantile);
      }
    });
  }
};

// Factory functions for each type
template <typename SketchType, typename OutputType>
std::shared_ptr<velox::exec::VectorFunction> makeKllSketchQuantileTyped(
    const std::string& /*name*/,
    const std::vector<velox::exec::VectorFunctionArg>& /*inputArgs*/,
    const velox::core::QueryConfig& /*config*/) {
  return std::make_shared<
      KllSketchQuantileFunctionTyped<SketchType, OutputType>>();
}

} // namespace

void registerKllSketchFunctions(const std::string& prefix) {
  // Note: Using different function names for different types as a workaround
  // for Velox's limitation of only checking input types (not return types)
  // for function overloading.
  //
  // TODO: Once kllsketch is registered as a parametric type in Velox,
  // these can be unified under a single "sketch_kll_quantile" name.

  // Register DOUBLE variants: sketch_kll_quantile (default for backward
  // compatibility) Register both signatures together
  velox::exec::registerStatefulVectorFunction(
      prefix + "sketch_kll_quantile",
      {velox::exec::FunctionSignatureBuilder()
           .returnType("double")
           .argumentType("varbinary")
           .argumentType("double")
           .argumentType("boolean")
           .build(),
       velox::exec::FunctionSignatureBuilder()
           .returnType("double")
           .argumentType("varbinary")
           .argumentType("double")
           .build()},
      makeKllSketchQuantileTyped<double, double>);

  // Register BIGINT variants: sketch_kll_quantile_bigint
  velox::exec::registerStatefulVectorFunction(
      prefix + "sketch_kll_quantile_bigint",
      {velox::exec::FunctionSignatureBuilder()
           .returnType("bigint")
           .argumentType("varbinary")
           .argumentType("double")
           .argumentType("boolean")
           .build(),
       velox::exec::FunctionSignatureBuilder()
           .returnType("bigint")
           .argumentType("varbinary")
           .argumentType("double")
           .build()},
      makeKllSketchQuantileTyped<int64_t, int64_t>);

  // Register VARCHAR variants: sketch_kll_quantile_varchar
  velox::exec::registerStatefulVectorFunction(
      prefix + "sketch_kll_quantile_varchar",
      {velox::exec::FunctionSignatureBuilder()
           .returnType("varchar")
           .argumentType("varbinary")
           .argumentType("double")
           .argumentType("boolean")
           .build(),
       velox::exec::FunctionSignatureBuilder()
           .returnType("varchar")
           .argumentType("varbinary")
           .argumentType("double")
           .build()},
      makeKllSketchQuantileTyped<std::string, velox::StringView>);

  // Register BOOLEAN variants: sketch_kll_quantile_boolean
  velox::exec::registerStatefulVectorFunction(
      prefix + "sketch_kll_quantile_boolean",
      {velox::exec::FunctionSignatureBuilder()
           .returnType("boolean")
           .argumentType("varbinary")
           .argumentType("double")
           .argumentType("boolean")
           .build(),
       velox::exec::FunctionSignatureBuilder()
           .returnType("boolean")
           .argumentType("varbinary")
           .argumentType("double")
           .build()},
      makeKllSketchQuantileTyped<bool, bool>);
}

} // namespace facebook::presto::functions

// Made with Bob
