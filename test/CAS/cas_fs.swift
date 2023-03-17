// RUN: %empty-directory(%t)
// RUN: mkdir -p %t/empty
// RUN: mkdir -p %t/cas

// RUN: llvm-cas --cas %t/cas --ingest --data %t/empty > %t/empty.casid
// RUN: not %target-swift-frontend -typecheck -enable-cas -cas-fs @%t/empty.casid -cas-path %t/cas %s 2>&1 | %FileCheck %s --check-prefix NO-INPUTS
// NO-INPUTS: error: error opening input file

// RUN: llvm-cas --cas %t/cas --ingest --data %s > %t/source.casid
// RUN: not %target-swift-frontend -typecheck -enable-cas -cas-fs @%t/source.casid -cas-path %t/cas %s 2>&1 | %FileCheck %s --check-prefix NO-RESOURCES
// NO-RESOURCES: error: unable to set working directory
// NO-RESOURCES: error: unable to load standard library

/// Ingest the resource directory to satisfy the file system requirement. Also switch CWD to resource dir. 
// RUN: llvm-cas --cas %t/cas --merge @%t/source.casid %test-resource-dir > %t/full.casid
// RUN: cd %test-resource-dir
// RUN: %target-swift-frontend -typecheck -enable-cas -cas-fs @%t/full.casid -cas-path %t/cas %s

func testFunc() {}
