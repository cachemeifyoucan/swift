//===--- CompileJobCacheKey.cpp - compile cache key methods ---------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file contains utility methods for creating compile job cache keys.
//
//===----------------------------------------------------------------------===//

#include <swift/Frontend/CompileJobCacheKey.h>
#include <swift/Basic/Version.h>
#include <llvm/ADT/SmallString.h>
#include "llvm/CAS/HierarchicalTreeBuilder.h"
#include "llvm/CAS/ObjectStore.h"

using namespace swift;

// TODO: Rewrite this into CASNodeSchema.
llvm::Expected<llvm::cas::ObjectRef> swift::createCompileJobBaseCacheKey(
    llvm::cas::ObjectStore &CAS, ArrayRef<const char *> Args,
    llvm::Optional<llvm::cas::ObjectRef> CASFS) {
  SmallString<256> CommandLine;
  for (StringRef Arg : Args) {
    CommandLine.append(Arg);
    CommandLine.push_back(0);
  }

  llvm::cas::HierarchicalTreeBuilder Builder;
  Builder.push(
      llvm::cantFail(CAS.storeFromString(None, CommandLine)),
      llvm::cas::TreeEntry::Regular, "command-line");
  Builder.push(
      llvm::cantFail(CAS.storeFromString(None, "-cc1")),
      llvm::cas::TreeEntry::Regular, "computation");

  // FIXME: The version is maybe insufficient...
  Builder.push(
      llvm::cantFail(CAS.storeFromString(None, version::getSwiftFullVersion())),
      llvm::cas::TreeEntry::Regular, "version");

  if (CASFS)
    Builder.push(*CASFS, llvm::cas::TreeEntry::Tree, "filesystem");

  if (auto Out = Builder.create(CAS))
    return Out->getRef();
  else
    return Out.takeError();
}

llvm::Expected<llvm::cas::ObjectRef>
swift::createCompileJobCacheKeyForOutput(llvm::cas::ObjectStore &CAS,
                                         llvm::cas::ObjectRef BaseKey,
                                         StringRef OutputID) {
  return CAS.storeFromString({BaseKey}, OutputID);
}
