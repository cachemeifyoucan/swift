//===--- CachingUtils.h -----------------------------------------*- C++ -*-===//
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

#ifndef SWIFT_FRONTEND_CACHINGUTILS_H
#define SWIFT_FRONTEND_CACHINGUTILS_H

#include "swift/Frontend/FrontendInputsAndOutputs.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/CAS/ActionCache.h"
#include "llvm/CAS/ObjectStore.h"
#include "llvm/CAS/CASReference.h"
#include "llvm/Support/VirtualOutputBackend.h"

namespace swift {
llvm::IntrusiveRefCntPtr<llvm::vfs::OutputBackend>
createSwiftCachingOutputBackend(
    llvm::cas::ObjectStore &CAS, llvm::cas::ActionCache &Cache,
    llvm::cas::ObjectRef BaseKey,
    const FrontendInputsAndOutputs &InputsAndOutputs);
}

#endif
