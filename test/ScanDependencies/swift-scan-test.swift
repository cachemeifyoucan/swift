// REQUIRES: objc_interop
// RUN: %empty-directory(%t)
// RUN: split-file %s %t

// RUN: %target-swift-frontend -emit-module -o %t/Test.swiftmodule -module-name Test \
// RUN:   -disable-implicit-string-processing-module-import -disable-implicit-concurrency-module-import -parse-stdlib \
// RUN:   -import-objc-header %t/bridging.h \
// RUN:   %t/test.swift

// RUN: %swift-scan-test -action scan_dependency -- %target-swift-frontend -emit-module -module-name App \
// RUN:   -disable-implicit-string-processing-module-import -disable-implicit-concurrency-module-import -parse-stdlib \
// RUN:   -I %t %t/main.swift

// RUN: false

//--- bridging.h
void a(void);

//--- test.swift
public func b() {}

//--- main.swift
import Test
