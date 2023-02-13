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

#include "CachingUtils.h"

#include "swift/Basic/FileTypes.h"
#include "swift/Frontend/CompileJobCacheKey.h"
#include "clang/Frontend/CompileJobCacheResult.h"
#include "llvm/Support/VirtualOutputFile.h"

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

  Optional<file_types::ID> getOutputFileType(StringRef Path) const {
    return None;
  }

  Expected<std::unique_ptr<OutputFileImpl>>
  createFileImpl(StringRef ResolvedPath,
                 Optional<OutputConfig> Config) override {
    return std::make_unique<SwiftCASOutputFile>(
        ResolvedPath, [&](StringRef Path, StringRef Bytes) -> Error {
          Optional<ObjectRef> BytesRef;
          if (Error E = CAS.storeFromString(None, Bytes).moveInto(BytesRef))
            return E;

          // FIXME: Using output path as the additional information for cache
          // key.
          auto CacheKey =
              createCompileJobCacheKeyForOutput(CAS, BaseKey, Path);
          if (!CacheKey)
            return CacheKey.takeError();

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

public:
  SwiftCASOutputBackend(ObjectStore &CAS, ActionCache &Cache, ObjectRef BaseKey,
                        const FrontendInputsAndOutputs &InputsAndOutputs)
      : CAS(CAS), Cache(Cache), BaseKey(BaseKey),
        InputsAndOutputs(InputsAndOutputs) {}

private:
  ObjectStore &CAS;
  ActionCache &Cache;
  ObjectRef BaseKey;
  const FrontendInputsAndOutputs &InputsAndOutputs;
};
}

llvm::IntrusiveRefCntPtr<llvm::vfs::OutputBackend>
swift::createSwiftCachingOutputBackend(
    llvm::cas::ObjectStore &CAS, llvm::cas::ActionCache &Cache,
    llvm::cas::ObjectRef BaseKey,
    const FrontendInputsAndOutputs &InputsAndOutputs) {
  return makeIntrusiveRefCnt<SwiftCASOutputBackend>(CAS, Cache, BaseKey,
                                                    InputsAndOutputs);
}
