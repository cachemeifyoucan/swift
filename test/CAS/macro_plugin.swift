// REQUIRES: swift_swift_parser

/// Test loading and external library through `-load-plugin-library`
/// TODO: switch this test case to use `-external-plugin-path`.

// RUN: %empty-directory(%t)
// RUN: %empty-directory(%t/plugins)
//
//== Build the plugin library
// RUN: %host-build-swift \
// RUN:   -swift-version 5 \
// RUN:   -emit-library \
// RUN:   -o %t/plugins/%target-library-name(MacroDefinition) \
// RUN:   -module-name=MacroDefinition \
// RUN:   %S/../Macros/Inputs/syntax_macro_definitions.swift \
// RUN:   -g -no-toolchain-stdlib-rpath

// RUN: %target-swift-frontend -scan-dependencies -module-name MyApp -module-cache-path %t/clang-module-cache -O \
// RUN:   -disable-implicit-string-processing-module-import -disable-implicit-concurrency-module-import \
// RUN:   %s -o %t/deps.json -swift-version 5 -cache-compile-job -cas-path %t/cas -load-plugin-library %t/plugins/%target-library-name(MacroDefinition)

// RUN: %S/Inputs/SwiftDepsExtractor.py %t/deps.json MyApp casFSRootID > %t/fs.casid
// RUN: llvm-cas -cas %t/cas -ls-tree-recursive @%t/fs.casid | %FileCheck %s --check-prefix=FS

// FS: MacroDefinition

// RUN: %S/Inputs/BuildCommandExtractor.py %t/deps.json clang:SwiftShims > %t/SwiftShims.cmd
// RUN: %swift_frontend_plain @%t/SwiftShims.cmd

// RUN: %S/Inputs/BuildCommandExtractor.py %t/deps.json Swift > %t/Swift.cmd
// RUN: %swift_frontend_plain @%t/Swift.cmd

// RUN: %S/Inputs/SwiftDepsExtractor.py %t/deps.json Swift moduleCacheKey | tr -d '\n' > %t/Swift.key
// RUN: %S/Inputs/SwiftDepsExtractor.py %t/deps.json clang:SwiftShims moduleCacheKey | tr -d '\n' > %t/Shims.key
// RUN: %S/Inputs/BuildCommandExtractor.py %t/deps.json MyApp > %t/MyApp.cmd

// RUN: echo "[{" > %/t/map.json
// RUN: echo "\"moduleName\": \"Swift\"," >> %/t/map.json
// RUN: echo "\"modulePath\": \"Swift.swiftmodule\"," >> %/t/map.json
// RUN: echo -n "\"moduleCacheKey\": " >> %/t/map.json
// RUN: cat %t/Swift.key >> %/t/map.json
// RUN: echo "," >> %/t/map.json
// RUN: echo "\"isFramework\": false" >> %/t/map.json
// RUN: echo "}," >> %/t/map.json
// RUN: echo "{" >> %/t/map.json
// RUN: echo "\"moduleName\": \"SwiftShims\"," >> %/t/map.json
// RUN: echo "\"clangModulePath\": \"SwiftShims.pcm\"," >> %/t/map.json
// RUN: echo -n "\"clangModuleCacheKey\": " >> %/t/map.json
// RUN: cat %t/Shims.key >> %/t/map.json
// RUN: echo "," >> %/t/map.json
// RUN: echo "\"isFramework\": false" >> %/t/map.json
// RUN: echo "}]" >> %/t/map.json
// RUN: llvm-cas --cas %t/cas --make-blob --data %t/map.json > %t/map.casid

// RUN: %target-swift-frontend \
// RUN:   -typecheck -verify -cache-compile-job -cas-path %t/cas \
// RUN:   -swift-version 5 -disable-implicit-swift-modules \
// RUN:   -load-plugin-library %t/plugins/%target-library-name(MacroDefinition) \
// RUN:   -disable-implicit-string-processing-module-import -disable-implicit-concurrency-module-import \
// RUN:   -module-name MyApp -explicit-swift-module-map-file @%t/map.casid \
// RUN:   %s @%t/MyApp.cmd

@attached(extension, conformances: P, names: named(requirement))
macro DelegatedConformance() = #externalMacro(module: "MacroDefinition", type: "DelegatedConformanceViaExtensionMacro")

protocol P {
  static func requirement()
}

struct Wrapped: P {
  static func requirement() {
    print("Wrapped.requirement")
  }
}

@DelegatedConformance
struct Generic<Element> {}

// CHECK: {"expandMacroResult":{"diagnostics":[],"expandedSource":"extension Generic: P where Element: P {\n  static func requirement() {\n    Element.requirement()\n  }\n}"}}

func requiresP(_ value: (some P).Type) {
  value.requirement()
}

requiresP(Generic<Wrapped>.self)

struct Outer {
  @DelegatedConformance
  struct Nested<Element> {}
}

// CHECK: {"expandMacroResult":{"diagnostics":[],"expandedSource":"extension Outer.Nested: P where Element: P {\n  static func requirement() {\n    Element.requirement()\n  }\n}"}}

requiresP(Outer.Nested<Wrapped>.self)
