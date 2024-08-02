//==============================================================
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================

#if defined(_WIN32)
#define NOMINMAX
#endif

#include <string.h>

#include <iostream>
#include <memory>

#include "api/gtpin_api.h"
#include "instcount.hpp"
#include "knob_parser.h"
#include "utils.h"

using namespace gtpin;
using namespace gtpin_prof;

// Tool specific implementation of Writer and Control /////////////////////////

class InstCountWriter : public InstCountWriterBase {
 public:
  InstCountWriter() = default;
  virtual ~InstCountWriter() = default;
  void Write(const ApplicationDataSPtr res) const final {
    std::cerr << "[INFO] : [ Instruction count "
              << "| SIMD active lanes count"
              << " ] total for all invocations" << std::endl;
    for (const auto& kernelDataPair : res->GetKernels()) {
      auto kernelData = std::dynamic_pointer_cast<InstCountKernelData>(kernelDataPair.second);
      PTI_ASSERT(kernelData != nullptr);
      std::cerr << "=== " << kernelData->GetKernelName() << "(runs "
                << kernelData->GetInvocations().size() << " times";
      size_t collectedTimes = 0;
      for (const auto& invocationDataPair : kernelData->GetInvocations()) {
        if (invocationDataPair.second->IsCollected()) collectedTimes++;
      }
      if (collectedTimes != kernelData->GetInvocations().size())
        std::cout << ", collected " << collectedTimes << " times";
      std::cerr << ") ===\n";

      size_t resultsNum = kernelData->GetResultsNum();
      for (size_t tileId = 0; tileId < kernelData->GetCollectedTilesNum(); tileId++) {
        if (kernelData->GetCollectedTilesNum() > 1) {
          std::cerr << "--- Tile #" << tileId << " of " << kernelData->GetCollectedTilesNum()
                    << " collected" << std::endl;
        }

        std::vector<size_t> instCount(resultsNum);
        std::vector<size_t> simdCount(resultsNum);
        size_t maxInstCount = 0;
        size_t maxSimdCount = 0;

        for (size_t idx = 0; idx < resultsNum; idx++) {
          for (const auto& invocationDataPair : kernelData->GetInvocations()) {
            auto invocationData =
                std::dynamic_pointer_cast<InstCountInvocationData>(invocationDataPair.second);
            PTI_ASSERT(invocationData != nullptr);
            auto resultData = std::dynamic_pointer_cast<InstCountResultData>(
                invocationData->GetResultData(tileId, idx));
            PTI_ASSERT(resultData != nullptr);
            instCount[idx] += resultData->instructionCounter;
            simdCount[idx] += resultData->simdActiveLaneCounter;
          }
          maxInstCount = std::max(maxInstCount, instCount[idx]);
          maxSimdCount = std::max(maxSimdCount, simdCount[idx]);
        }
        std::string maxInstCountStr = std::to_string(maxInstCount);
        std::string maxSimdCountStr = std::to_string(maxSimdCount);

        auto assembly = kernelData->GetOrigAsm();
        auto resultDataCommon = kernelData->GetResultDataCommon();
        gtpin::BblId bblId = -1;
        for (size_t idx = 0; idx < resultsNum; idx++) {
          auto rdc = std::dynamic_pointer_cast<InstCountResultDataCommon>(resultDataCommon[idx]);
          PTI_ASSERT(rdc != nullptr);
          if (bblId != rdc->bblId) {
            bblId = rdc->bblId;
            std::cerr << "///  Basic block #" << bblId << "\n";
          }

          InstructionOffset offset = rdc->offset;
          std::cerr << "[" << std::dec << std::setw(maxInstCountStr.size() + 1) << instCount[idx];
          if (maxSimdCount > 0) {
            std::cerr << "|" << std::dec << std::setw(maxSimdCountStr.size() + 1) << simdCount[idx];
          }
          std::cerr << "] 0x" << std::setw(6) << std::hex << std::setfill('0') << offset
                    << std::setfill(' ') << std::dec << " : ";
          if (assembly.size() > idx)
            std::cerr << assembly[idx].GetAsmLineOrig();
          else
            std::cerr << " no assembly";
          std::cerr << std::endl;
        }
      }
    }
  }
};

static gtpin::Knob<bool> knobPerTileCollection("per-tile-collection", false,
                                               "Collect data with tile granularity");
static gtpin::KnobVector<int> knobKernelRun("kernel-run", {}, "Kernel run to profile");
static gtpin::Knob<bool> knobDisableSimdCollection("disable-simd", false,
                                                   "Disable collection of SIMD active lanes");
class InstCountGTPinControl : public InstCountControl {
 public:
  using InstCountControl::InstCountControl;

  bool ShouldInstrument(const KernelBuildDescriptor& buildDescr) const final { return true; }
  bool EnablePerTileCollection(const KernelBuildDescriptor& buildDescr) const final {
    return knobPerTileCollection;
  }
  bool ShouldProfileEnqueue(const KernelExecDescriptor& execDescr) const final {
    if (!gtpin::IsKernelExecProfileEnabled(execDescr.gtExecDesc, execDescr.gpuPlatform))
      return false;

    if (!knobKernelRun.NumValues()) return true;

    for (uint32_t i = 0; i != knobKernelRun.NumValues(); ++i) {
      if (knobKernelRun.GetValue(i) == execDescr.runIdx) {
        return true;
      }
    }
    return false;
  }
  bool ShouldCollectSimdWidth() const final { return !knobDisableSimdCollection; }
};

// External Tool Interface ////////////////////////////////////////////////////

extern "C" PTI_EXPORT void Usage() {
  std::cout << "Usage: ./instcount";
#if defined(_WIN32)
  std::cout << "[.exe]";
#endif
  std::cout << " [options] <application> <args>" << std::endl;

  std::cout << "Options:" << std::endl;
  std::cout << "--disable-simd                 "
            << "Disable SIMD active lanes collection" << std::endl;
}

extern "C" PTI_EXPORT int ParseArgs(int argc, char* argv[]) {
  int app_index = 1;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--disable-simd") == 0) {
      utils::SetEnv("GIC_DisableSimd", "1");
      app_index++;
    } else if (strcmp(argv[i], "--version") == 0) {
#ifdef PTI_VERSION
      std::cout << TOSTRING(PTI_VERSION) << std::endl;
#endif
      return 0;
    } else {
      break;
    }
  }
  return app_index;
}

extern "C" PTI_EXPORT void SetToolEnv() {
  utils::SetEnv("ZE_ENABLE_TRACING_LAYER", "1");
  utils::SetEnv("ZET_ENABLE_PROGRAM_INSTRUMENTATION", "1");
}

// Internal Tool Interface ////////////////////////////////////////////////////

std::shared_ptr<InstCountWriter> writer;
std::shared_ptr<InstCountControl> control;
std::unique_ptr<InstCountGTPinProfiler> profiler;

void EnableProfiling() {
  PTI_ASSERT(profiler == nullptr);

  /// Apply options
  std::vector<const char*> args;
  std::string value;
  value = utils::GetEnv("GIC_DisableSimd");
  if (!value.empty() && value == "1") {
    args.push_back("--disable-simd");
  }
  ConfigureGTPin(args.size(), args.data());

  writer = std::make_shared<InstCountWriter>();
  control = std::make_shared<InstCountGTPinControl>();
  profiler = std::make_unique<InstCountGTPinProfiler>(writer, control);

  auto status = profiler->Start();
  if (PROF_STATUS::SUCCESS != status) {
    std::cerr << profiler->LastError() << std::endl;
  }
}

void DisableProfiling() {
  if (profiler == nullptr) {
    return;
  }
  PTI_ASSERT(profiler->Status() == PROF_STATUS::ACTIVE);

  auto status = profiler->Stop();
  if (PROF_STATUS::SUCCESS != status) {
    std::cerr << profiler->LastError() << std::endl;
  }
}

// GTPin loader interface allows to use library as a GTPin loader tool
EXPORT_C_FUNC PTI_EXPORT void GTPin_Entry(int argc, const char* argv[]) {
  ConfigureGTPin(argc, argv);
  // DisableProfiling() is registered as an atexit callback by PTI loader. No need to do it here
  EnableProfiling();
}
