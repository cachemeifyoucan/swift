// REQUIRES: swift_swift_parser

// RUN: %empty-directory(%t)

// RUN: %target-swift-frontend -scan-dependencies -module-name MyApp -module-cache-path %t/clang-module-cache -O \
// RUN:   -disable-implicit-string-processing-module-import -disable-implicit-concurrency-module-import \
// RUN:   %s -o %t/deps.json -swift-version 5 -cache-compile-job -cas-path %t/cas -plugin-path %swift-plugin-dir

// RUN: %S/Inputs/SwiftDepsExtractor.py %t/deps.json MyApp casFSRootID > %t/fs.casid
// RUN: llvm-cas -cas %t/cas -ls-tree-recursive @%t/fs.casid | %FileCheck %s --check-prefix=FS

// FS: SwiftMacros

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
// RUN:   -plugin-path %swift-plugin-dir \
// RUN:   -disable-implicit-string-processing-module-import -disable-implicit-concurrency-module-import \
// RUN:   -module-name MyApp -explicit-swift-module-map-file @%t/map.casid \
// RUN:   %s @%t/MyApp.cmd

import Swift

@attached(member, names: named(RawValue), named(rawValue), named(`init`), arbitrary)
@attached(extension, conformances: OptionSet)
public macro OptionSet<RawType>() =
  #externalMacro(module: "SwiftMacros", type: "OptionSetMacro")

@OptionSet<UInt8>
struct ShippingOptions {
  private enum Options: Int {
    case nextDay
    case secondDay
    case priority
    case standard
  }

  static let express: ShippingOptions = [.nextDay, .secondDay]
  static let all: ShippingOptions = [.express, .priority, .standard]
}

let options = ShippingOptions.express
assert(options.contains(.nextDay))
assert(options.contains(.secondDay))
assert(!options.contains(.standard))

