//===--- CachingUtils.cpp ---------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/Frontend/CachingUtils.h"

#include "swift/AST/DiagnosticsFrontend.h"
#include "swift/Basic/FileTypes.h"
#include "swift/Frontend/CompileJobCacheKey.h"
#include "clang/Frontend/CompileJobCacheResult.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CAS/BuiltinUnifiedCASDatabases.h"
#include "llvm/CAS/ObjectStore.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualOutputBackends.h"
#include "llvm/Support/VirtualOutputFile.h"

#define DEBUG_TYPE "cache-util"

using namespace swift;
using namespace llvm;
using namespace llvm::cas;
using namespace llvm::vfs;

namespace {
class SwiftCASOutputFile final : public OutputFileImpl {
public:
  Error keep() override { return OnKeep(Path, Bytes); }
  Error discard() override { return Error::success(); }
  raw_pwrite_stream &getOS() override { return OS; }

  using OnKeepType = llvm::unique_function<Error(StringRef, StringRef)>;
  SwiftCASOutputFile(StringRef Path, OnKeepType OnKeep)
      : Path(Path.str()), OS(Bytes), OnKeep(std::move(OnKeep)) {}

private:
  std::string Path;
  SmallString<16> Bytes;
  raw_svector_ostream OS;
  OnKeepType OnKeep;
};

class SwiftCASOutputBackend final : public OutputBackend {
  void anchor() override {}

protected:
  IntrusiveRefCntPtr<OutputBackend> cloneImpl() const override {
    return makeIntrusiveRefCnt<SwiftCASOutputBackend>(CAS, Cache, BaseKey,
                                                      InputsAndOutputs);
  }

  Expected<std::unique_ptr<OutputFileImpl>>
  createFileImpl(StringRef ResolvedPath,
                 Optional<OutputConfig> Config) override {
    return std::make_unique<SwiftCASOutputFile>(
        ResolvedPath, [&](StringRef Path, StringRef Bytes) -> Error {
          Optional<ObjectRef> BytesRef;
          if (Error E = CAS.storeFromString(None, Bytes).moveInto(BytesRef))
            return E;

          auto ProducingInput = OutputToInputMap.find(Path);
          assert(ProducingInput != OutputToInputMap.end() &&
                 "Unknown output file");

          auto InputFilename = ProducingInput->second.first.getFileName();
          auto OutputType = ProducingInput->second.second;

          auto CacheKey = createCompileJobCacheKeyForOutput(
              CAS, BaseKey, InputFilename, OutputType);
          if (!CacheKey)
            return CacheKey.takeError();

          LLVM_DEBUG(llvm::dbgs()
                         << "DEBUG: writing output \'" << Path << "\' type \'"
                         << file_types::getTypeName(OutputType) << "\' input \'"
                         << InputFilename << "\' hash \'"
                         << CAS.getID(*CacheKey).toString() << "\'\n";);

          // Use clang compiler job output for now.
          clang::cas::CompileJobCacheResult::Builder Builder;
          Builder.addOutput(
              clang::cas::CompileJobCacheResult::OutputKind::MainOutput,
              *BytesRef);
          auto Result = Builder.build(CAS);
          if (!Result)
            return Result.takeError();

          if (auto E = Cache.put(CAS.getID(*CacheKey), CAS.getID(*Result)))
            return E;

          return Error::success();
        });
  }

private:
  void initBackend(const FrontendInputsAndOutputs &InputsAndOutputs) {
    // FIXME: The output to input map might not be enough for example all the
    // outputs can be written to `-`, but the backend cannot distinguish which
    // input it actually comes from. Maybe the solution is just not to cache
    // any commands write output to `-`.
    file_types::ID mainOutputType = InputsAndOutputs.getOutputType();
    auto addInput = [&](const InputFile &Input) {
      OutputToInputMap.insert(
          {Input.outputFilename(), {Input, mainOutputType}});
      Input.getPrimarySpecificPaths()
          .SupplementaryOutputs.forEachSetOutputAndType(
              [&](const std::string &Out, file_types::ID ID) {
                OutputToInputMap.insert({Out, {Input, ID}});
              });
    };
    llvm::for_each(InputsAndOutputs.getAllInputs(), addInput);
  }

  file_types::ID getOutputFileType(StringRef Path) const {
    return file_types::lookupTypeForExtension(llvm::sys::path::extension(Path));
  }

public:
  SwiftCASOutputBackend(ObjectStore &CAS, ActionCache &Cache, ObjectRef BaseKey,
                        const FrontendInputsAndOutputs &InputsAndOutputs)
      : CAS(CAS), Cache(Cache), BaseKey(BaseKey),
        InputsAndOutputs(InputsAndOutputs) {
    initBackend(InputsAndOutputs);
  }

private:
  ObjectStore &CAS;
  ActionCache &Cache;
  ObjectRef BaseKey;

  StringMap<std::pair<const InputFile &, file_types::ID>> OutputToInputMap;
  const FrontendInputsAndOutputs &InputsAndOutputs;
};
}

namespace swift {

llvm::IntrusiveRefCntPtr<llvm::vfs::OutputBackend>
createSwiftCachingOutputBackend(
    llvm::cas::ObjectStore &CAS, llvm::cas::ActionCache &Cache,
    llvm::cas::ObjectRef BaseKey,
    const FrontendInputsAndOutputs &InputsAndOutputs) {
  return makeIntrusiveRefCnt<SwiftCASOutputBackend>(CAS, Cache, BaseKey,
                                                    InputsAndOutputs);
}

std::string getDefaultSwiftCASPath() {
  SmallString<256> Path;
  if (!llvm::sys::path::cache_directory(Path))
    llvm::report_fatal_error("cannot get default cache directory");
  llvm::sys::path::append(Path, "swift-cache");

  return std::string(Path.data(), Path.size());
}

bool replayCachedCompilerOutputs(
    ObjectStore &CAS, ActionCache &Cache, ObjectRef BaseKey,
    DiagnosticEngine &Diag, const FrontendInputsAndOutputs &InputsAndOutputs) {
  clang::cas::CompileJobResultSchema Schema(CAS);
  bool CanReplayAllOutput = true;
  SmallVector<std::pair<std::string, llvm::cas::ObjectProxy>> OutputProxies;

  auto replayOutputFile = [&](StringRef InputName, file_types::ID OutputKind,
                              StringRef OutputPath) {
    auto OutputKey =
        createCompileJobCacheKeyForOutput(CAS, BaseKey, InputName, OutputKind);
    if (!OutputKey) {
      Diag.diagnose(SourceLoc(), diag::error_cas,
                    toString(OutputKey.takeError()));
      CanReplayAllOutput = false;
      return;
    }

    auto Lookup = Cache.get(CAS.getID(*OutputKey));
    if (!Lookup) {
      Diag.diagnose(SourceLoc(), diag::error_cas, toString(Lookup.takeError()));
      CanReplayAllOutput = false;
      return;
    }
    if (!*Lookup) {
      Diag.diagnose(SourceLoc(), diag::output_cache_miss, OutputPath);
      CanReplayAllOutput = false;
      return;
    }
    auto OutputRef = CAS.getReference(**Lookup);
    if (!OutputRef) {
      CanReplayAllOutput = false;
      return;
    }
    auto Result = Schema.load(*OutputRef);
    if (!Result) {
      Diag.diagnose(SourceLoc(), diag::error_cas, toString(Result.takeError()));
      CanReplayAllOutput = false;
      return;
    }
    auto MainOutput = Result->getOutput(
        clang::cas::CompileJobCacheResult::OutputKind::MainOutput);
    if (!MainOutput) {
      CanReplayAllOutput = false;
      return;
    }
    auto LoadedResult = CAS.getProxy(MainOutput->Object);
    if (!LoadedResult) {
      Diag.diagnose(SourceLoc(), diag::error_cas,
                    toString(LoadedResult.takeError()));
      CanReplayAllOutput = false;
      return;
    }

    OutputProxies.emplace_back(OutputPath.str(), *LoadedResult);
  };

  auto replayOutputFromInput = [&] (const InputFile &Input) {
    auto InputPath = Input.getFileName();
    replayOutputFile(InputPath, InputsAndOutputs.getOutputType(),
                     Input.outputFilename());
    Input.getPrimarySpecificPaths()
        .SupplementaryOutputs.forEachSetOutputAndType(
            [&](const std::string &File, file_types::ID ID) {
              replayOutputFile(InputPath, ID, File);
            });
  };

  llvm::for_each(InputsAndOutputs.getAllInputs(), replayOutputFromInput);

  if (!CanReplayAllOutput)
    return false;

  // Replay the result only when everything is resolved.
  // Use on disk output backend directly here to write to disk.
  llvm::vfs::OnDiskOutputBackend Backend;
  for (auto &Output : OutputProxies) {
    auto File = Backend.createFile(Output.first);
    if (!File) {
      Diag.diagnose(SourceLoc(), diag::error_opening_output, Output.first,
                    toString(File.takeError()));
      continue;
    }
    *File << Output.second.getData();
    if (auto E = File->keep()) {
      Diag.diagnose(SourceLoc(), diag::error_opening_output, Output.first,
                    toString(std::move(E)));
      continue;
    }
    Diag.diagnose(SourceLoc(), diag::replay_output, Output.first,
                  Output.second.getID().toString());
  }

  return true;
}

static Expected<std::unique_ptr<llvm::MemoryBuffer>>
loadCachedCompileResultFromCacheKeyImpl(ObjectStore &CAS, ActionCache &Cache,
                                        StringRef CacheKey,
                                        StringRef Filename) {
  auto ID = CAS.parseID(CacheKey);
  if (!ID)
    return ID.takeError();

  auto Result = Cache.get(*ID);
  if (!Result)
    return Result.takeError();
  if (!*Result)
    return nullptr;

  auto OutputRef = CAS.getReference(**Result);
  if (!OutputRef)
    return nullptr;
  clang::cas::CompileJobResultSchema Schema(CAS);
  auto CachedOutput = Schema.load(*OutputRef);

  if (!CachedOutput)
    return CachedOutput.takeError();

  auto Output = CachedOutput->getOutput(
      clang::cas::CompileJobCacheResult::OutputKind::MainOutput);
  if (!Output)
    return nullptr;

  auto Proxy = CAS.getProxy(Output->Object);
  if (!Proxy)
    return Proxy.takeError();

  return Proxy->getMemoryBuffer(Filename);
}

std::unique_ptr<llvm::MemoryBuffer>
loadCachedCompileResultFromCacheKey(ObjectStore &CAS, ActionCache &Cache,
                                    DiagnosticEngine &Diag, StringRef CacheKey,
                                    StringRef Filename) {
  auto Output =
      loadCachedCompileResultFromCacheKeyImpl(CAS, Cache, CacheKey, Filename);
  if (!Output) {
    Diag.diagnose(SourceLoc(), diag::error_cas, toString(Output.takeError()));
    return nullptr;
  }
  return std::move(*Output);
}

namespace cas {

CachingTool::CachingTool(StringRef Path) {
  auto DB = llvm::cas::createOnDiskUnifiedCASDatabases(Path);
  if (!DB) {
    llvm::errs() << "Failed to create CAS at " << Path << ": "
                 << toString(DB.takeError()) << "\n";
    return;
  }

  CAS = std::move(DB->first);
  Cache = std::move(DB->second);
}

std::string CachingTool::computeCacheKeyForPCH(ArrayRef<const char *> Args,
                                               StringRef HeaderPath) {
  auto BaseKey = createCompileJobBaseCacheKey(*CAS, Args);
  if (!BaseKey) {
    llvm::errs() << "Failed to create cache key: "
                 << toString(BaseKey.takeError()) << "\n";
    return "";
  }

  auto PCHKey = createCompileJobCacheKeyForOutput(*CAS, *BaseKey, HeaderPath,
                                                  file_types::ID::TY_PCH);
  if (!PCHKey) {
    llvm::errs() << "Failed to create cache key: "
                 << toString(PCHKey.takeError()) << "\n";
    return "";
  }

  return CAS->getID(*PCHKey).toString();
}

std::string CachingTool::storeContent(StringRef Content) {
  auto Result = CAS->storeFromString({}, Content);
  if (!Result) {
    llvm::errs() << "Failed to store to CAS: " << toString(Result.takeError())
                 << "\n";
    return "";
  }

  return CAS->getID(*Result).toString();
}

} // namespace cas
} // namespace swift
