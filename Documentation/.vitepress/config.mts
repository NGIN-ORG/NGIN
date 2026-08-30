import { defineConfig } from "vitepress";
import { rawMarkdownPlugin } from "./config/raw-markdown";

function normalizeBase(value: string | undefined): string {
  if (!value || value === "/") {
    return "/";
  }
  return `/${value.replace(/^\/+|\/+$/g, "")}/`;
}

const siteBase = normalizeBase(process.env.NGIN_DOCS_BASE);

const baseSidebar = [
  { text: "NGIN.Base", link: "/libraries/base" },
  { text: "Quick start", link: "/libraries/base/quick-start" },
  {
    text: "Foundation",
    collapsed: false,
    items: [
      { text: "Foundation map", link: "/libraries/base/foundation" },
      {
        text: "Exceptions and results",
        link: "/libraries/base/exceptions-results",
        collapsed: true,
        items: [
          { text: "Choosing a shape", link: "/libraries/base/results/choosing-a-shape" },
          { text: "Expected and Optional", link: "/libraries/base/results/expected-optional" },
          { text: "Error boundaries", link: "/libraries/base/results/error-boundaries" },
          { text: "Results API", link: "/reference/cpp/base/results" }
        ]
      },
      {
        text: "Meta and hashing",
        link: "/libraries/base/meta-hashing",
        collapsed: true,
        items: [
          { text: "Type and symbol identity", link: "/libraries/base/meta/identity" },
          { text: "Traits", link: "/libraries/base/meta/traits" },
          { text: "Choosing a hash", link: "/libraries/base/hashing/choosing-a-hash" },
          { text: "Meta and Hashing API", link: "/reference/cpp/base/meta-hashing" }
        ]
      },
      {
        text: "Utilities",
        link: "/libraries/base/utilities",
        collapsed: true,
        items: [
          { text: "Any", link: "/libraries/base/utilities/any" },
          { text: "Callable", link: "/libraries/base/utilities/callable" },
          { text: "Interning and symbols", link: "/libraries/base/utilities/interning-symbols" },
          { text: "Utilities API", link: "/reference/cpp/base/utilities" }
        ]
      }
    ]
  },
  {
    text: "Concurrency",
    collapsed: false,
    items: [
      { text: "Overview", link: "/libraries/base/async-execution" },
      {
        text: "Async",
        link: "/libraries/base/async",
        collapsed: false,
        items: [
          { text: "First operation", link: "/libraries/base/async/first-task" },
          { text: "Errors and completions", link: "/libraries/base/async/errors" },
          { text: "Cancellation", link: "/libraries/base/async/cancellation" },
          { text: "Combining tasks", link: "/libraries/base/async/composition" },
          { text: "Contexts and schedulers", link: "/libraries/base/async/runtime" },
          { text: "Async generators", link: "/libraries/base/async/generators" },
          { text: "Async API reference", link: "/reference/cpp/base/async/" }
        ]
      },
      {
        text: "Execution",
        link: "/libraries/base/execution",
        collapsed: true,
        items: [
          { text: "First scheduler", link: "/libraries/base/execution/first-scheduler" },
          { text: "Choosing a scheduler", link: "/libraries/base/execution/choosing-scheduler" },
          { text: "Submitting work", link: "/libraries/base/execution/submitting-work" },
          { text: "Threads and fibers", link: "/libraries/base/execution/threads-fibers" },
          { text: "Shutdown and lifetimes", link: "/libraries/base/execution/shutdown-lifetimes" },
          { text: "Execution API", link: "/reference/cpp/base/execution" }
        ]
      },
      {
        text: "Synchronization",
        link: "/libraries/base/synchronization",
        collapsed: true,
        items: [
          { text: "Choosing a primitive", link: "/libraries/base/synchronization/choosing-primitive" },
          { text: "Mutexes and guards", link: "/libraries/base/synchronization/mutexes-guards" },
          { text: "Semaphores and conditions", link: "/libraries/base/synchronization/semaphores-conditions" },
          { text: "Correctness and lifetime", link: "/libraries/base/synchronization/correctness-lifetime" },
          { text: "Sync API", link: "/reference/cpp/base/sync/" }
        ]
      }
    ]
  },
  {
    text: "Data and memory",
    collapsed: false,
    items: [
      { text: "Overview", link: "/libraries/base/memory-containers" },
      {
        text: "Memory",
        link: "/libraries/base/memory",
        collapsed: true,
        items: [
          { text: "Choosing an allocator", link: "/libraries/base/memory/choosing-allocator" },
          { text: "Arenas and pools", link: "/libraries/base/memory/arenas-pools" },
          { text: "Ownership", link: "/libraries/base/memory/ownership" },
          { text: "Composition and diagnostics", link: "/libraries/base/memory/composition-diagnostics" },
          { text: "Memory API", link: "/reference/cpp/base/memory-containers" }
        ]
      },
      {
        text: "Containers",
        link: "/libraries/base/containers",
        collapsed: true,
        items: [
          { text: "Vectors and strings", link: "/libraries/base/containers/vector-string" },
          { text: "Flat hash tables", link: "/libraries/base/containers/flat-hash" },
          { text: "Concurrent hash maps", link: "/libraries/base/containers/concurrent-hash-map" },
          { text: "Invalidation and allocators", link: "/libraries/base/containers/invalidation-allocators" },
          { text: "Containers API", link: "/reference/cpp/base/containers/" }
        ]
      },
      {
        text: "Serialization",
        link: "/libraries/base/serialization",
        collapsed: true,
        items: [
          { text: "First document", link: "/libraries/base/serialization/first-document" },
          { text: "Ownership and limits", link: "/libraries/base/serialization/ownership-limits" },
          { text: "JSON", link: "/libraries/base/serialization/json" },
          { text: "XML", link: "/libraries/base/serialization/xml" },
          { text: "Streaming", link: "/libraries/base/serialization/streaming" },
          { text: "Serialization API", link: "/reference/cpp/base/serialization" }
        ]
      },
    ]
  },
  {
    text: "System and network",
    collapsed: false,
    items: [
      { text: "Overview", link: "/libraries/base/io-networking" },
      {
        text: "I/O",
        link: "/libraries/base/io",
        collapsed: true,
        items: [
          { text: "Paths and filesystems", link: "/libraries/base/io/paths-filesystems" },
          { text: "Files and directories", link: "/libraries/base/io/files-directories" },
          { text: "Atomic writes", link: "/libraries/base/io/atomic-writes" },
          { text: "Async I/O", link: "/libraries/base/io/async-io" },
          { text: "Processes and libraries", link: "/libraries/base/io/processes-libraries" },
          { text: "I/O API", link: "/reference/cpp/base/io" }
        ]
      },
      {
        text: "Networking",
        link: "/libraries/base/networking",
        collapsed: true,
        items: [
          { text: "Addresses and resolution", link: "/libraries/base/networking/addresses-resolution" },
          { text: "Manual sockets", link: "/libraries/base/networking/manual-sockets" },
          { text: "Coroutine driver", link: "/libraries/base/networking/async-driver" },
          { text: "Transport and framing", link: "/libraries/base/networking/transports-framing" },
          { text: "TLS", link: "/libraries/base/networking/tls" },
          { text: "Networking API", link: "/reference/cpp/base/networking" }
        ]
      },
      {
        text: "Cryptography",
        link: "/libraries/base/cryptography",
        collapsed: true,
        items: [
          { text: "Providers and errors", link: "/libraries/base/cryptography/providers-errors" },
          { text: "Random and secrets", link: "/libraries/base/cryptography/random-secrets" },
          { text: "Encoding and keys", link: "/libraries/base/cryptography/encoding-keys" },
          { text: "Hash, MAC, and KDF", link: "/libraries/base/cryptography/hash-mac-kdf" },
          { text: "Authenticated encryption", link: "/libraries/base/cryptography/authenticated-encryption" },
          { text: "Asymmetric and certificates", link: "/libraries/base/cryptography/asymmetric-certificates" },
          { text: "Crypto API", link: "/reference/cpp/base/crypto" }
        ]
      }
    ]
  },
  {
    text: "Text and computing",
    collapsed: true,
    items: [
      { text: "Overview", link: "/libraries/base/text-math-time" },
      {
        text: "Text",
        link: "/libraries/base/text",
        collapsed: true,
        items: [
          { text: "BasicString", link: "/libraries/base/text/basic-string" },
          { text: "Unicode", link: "/libraries/base/text/unicode" },
          { text: "Text API", link: "/reference/cpp/base/text" }
        ]
      },
      {
        text: "Math and units",
        link: "/libraries/base/math-units",
        collapsed: true,
        items: [
          { text: "Vectors and matrices", link: "/libraries/base/math/linear-algebra" },
          { text: "Geometry and transforms", link: "/libraries/base/math/geometry-transforms" },
          { text: "Big numbers", link: "/libraries/base/math/big-numbers" },
          { text: "Dimensioned units", link: "/libraries/base/math/units" },
          { text: "Math and Units API", link: "/reference/cpp/base/math-units" }
        ]
      },
      {
        text: "Time",
        link: "/libraries/base/time",
        collapsed: true,
        items: [
          { text: "Monotonic time", link: "/libraries/base/time/monotonic-time" },
          { text: "Deadlines and sleep", link: "/libraries/base/time/deadlines-sleep" },
          { text: "Time API", link: "/reference/cpp/base/time" }
        ]
      },
      {
        text: "SIMD",
        link: "/libraries/base/simd",
        collapsed: true,
        items: [
          { text: "Vectors and backends", link: "/libraries/base/simd/vectors-backends" },
          { text: "Scans and correctness", link: "/libraries/base/simd/scans-correctness" },
          { text: "SIMD API", link: "/reference/cpp/base/simd" }
        ]
      }
    ]
  },
  { text: "C++ API reference", link: "/reference/cpp/base/" }
];

const coreSidebar = [
  { text: "NGIN.Core", link: "/libraries/core" },
  { text: "Quick start", link: "/libraries/core/quick-start" },
  { text: "Application lifecycle", link: "/libraries/core/application-lifecycle" },
  { text: "Services and scopes", link: "/libraries/core/services" },
  { text: "Dependency injection", link: "/libraries/core/dependency-injection" },
  { text: "Modules and plugins", link: "/libraries/core/modules-plugins" },
  { text: "Configuration and events", link: "/libraries/core/configuration-events" },
  { text: "C++ API reference", link: "/reference/cpp/core/" }
];

const reflectionSidebar = [
  { text: "NGIN.Reflection", link: "/libraries/reflection" },
  { text: "Quick start", link: "/libraries/reflection/quick-start" },
  { text: "Registration model", link: "/libraries/reflection/registration" },
  { text: "MetaGen", link: "/libraries/reflection/metagen" },
  { text: "Modules and lifetimes", link: "/libraries/reflection/modules-lifetimes" },
  { text: "C++ API reference", link: "/reference/cpp/reflection/" }
];

const ecsSidebar = [
  { text: "NGIN.ECS", link: "/libraries/ecs" },
  { text: "Quick start", link: "/libraries/ecs/quick-start" },
  { text: "World and entities", link: "/libraries/ecs/world-entities" },
  { text: "Queries and systems", link: "/libraries/ecs/queries-systems" },
  { text: "Simulation and scheduling", link: "/libraries/ecs/simulation-scheduling" },
  {
    text: "Detailed guides",
    collapsed: true,
    items: [
      { text: "All ECS guides", link: "/libraries/ecs/guides/" },
      { text: "Entities", link: "/libraries/ecs/guides/Entities" },
      { text: "Storage", link: "/libraries/ecs/guides/Storage" },
      { text: "Queries", link: "/libraries/ecs/guides/Queries" },
      { text: "Commands", link: "/libraries/ecs/guides/Commands" },
      { text: "Systems", link: "/libraries/ecs/guides/Systems" },
      { text: "Simulation", link: "/libraries/ecs/guides/Simulation" },
      { text: "Errors and threading", link: "/libraries/ecs/guides/ErrorsAndThreading" }
    ]
  },
  { text: "C++ API reference", link: "/reference/cpp/ecs/" }
];

const uiSidebar = [
  { text: "NGIN.UI", link: "/libraries/ui" },
  { text: "Quick start", link: "/libraries/ui/quick-start" },
  { text: "Composition and layout", link: "/libraries/ui/composition-layout" },
  { text: "Controls and input", link: "/libraries/ui/controls-input" },
  { text: "State and MVVM", link: "/libraries/ui/state-mvvm" },
  { text: "Styling and motion", link: "/libraries/ui/styling-motion" },
  { text: "Testing and accessibility", link: "/libraries/ui/testing-accessibility" },
  { text: "Backends and hosting", link: "/libraries/ui/backends-hosting" },
  {
    text: "Detailed guides",
    collapsed: true,
    items: [
      { text: "All UI guides", link: "/libraries/ui/guides/" },
      { text: "First window", link: "/libraries/ui/guides/ngin-ui-first-window" },
      { text: "Application model", link: "/libraries/ui/guides/ngin-ui-application-model" },
      { text: "Application composition", link: "/libraries/ui/guides/ngin-ui-application-composition" },
      { text: "Controls", link: "/libraries/ui/guides/ngin-ui-foundational-controls" },
      { text: "Collections and navigation", link: "/libraries/ui/guides/ngin-ui-collections-navigation" },
      { text: "MVVM", link: "/libraries/ui/guides/ngin-ui-mvvm" },
      { text: "Styling", link: "/libraries/ui/guides/ngin-ui-styling" },
      { text: "Motion", link: "/libraries/ui/guides/ngin-ui-motion" },
      { text: "Testing", link: "/libraries/ui/guides/ngin-ui-testing-and-release" },
      { text: "Backend authoring", link: "/libraries/ui/guides/ngin-ui-backend-authoring" },
      { text: "Troubleshooting", link: "/libraries/ui/guides/ngin-ui-troubleshooting" }
    ]
  },
  { text: "C++ API reference", link: "/reference/cpp/ui/" }
];

const logSidebar = [
  { text: "NGIN.Log", link: "/libraries/log" },
  { text: "Quick start", link: "/libraries/log/quick-start" },
  { text: "Records and formatting", link: "/libraries/log/records-formatting" },
  { text: "Sinks and production", link: "/libraries/log/sinks-production" },
  {
    text: "Detailed guides",
    collapsed: true,
    items: [
      { text: "All Log guides", link: "/libraries/log/guides/" },
      { text: "Production", link: "/libraries/log/guides/Production" },
      { text: "Sinks", link: "/libraries/log/guides/Sinks" },
      { text: "Performance", link: "/libraries/log/guides/Performance" },
      { text: "Architecture", link: "/libraries/log/guides/Architecture" }
    ]
  },
  { text: "C++ API reference", link: "/reference/cpp/log/" }
];

const apiSidebar = [
  { text: "API guides", link: "/api" },
  {
    text: "C++ libraries",
    collapsed: false,
    items: [
      { text: "NGIN.Base", link: "/api/base" },
      { text: "Async", link: "/api/base/async" },
      { text: "Execution and sync", link: "/api/base/execution" },
      { text: "Memory and containers", link: "/api/base/memory-containers" },
      { text: "I/O and processes", link: "/api/base/io" },
      { text: "Networking and TLS", link: "/api/base/networking" },
      { text: "Serialization", link: "/api/base/serialization" },
      { text: "Crypto", link: "/api/base/crypto" },
      { text: "Foundation APIs", link: "/api/base/foundation" },
      { text: "NGIN.Core", link: "/api/core" },
      { text: "NGIN.Reflection", link: "/api/reflection" },
      { text: "NGIN.ECS", link: "/api/ecs" },
      { text: "NGIN.UI", link: "/api/ui" },
      { text: "NGIN.Log", link: "/api/log" }
    ]
  },
  {
    text: "Developer interfaces",
    collapsed: false,
    items: [
      { text: "CLI commands", link: "/reference/cli" },
      { text: "Project manifest", link: "/reference/project-manifest" },
      { text: "Package manifest", link: "/reference/package-manifest" },
      { text: "Workspace manifest", link: "/reference/workspace-manifest" }
    ]
  }
];

const cppApiSidebar = [
  { text: "C++ API reference", link: "/reference/cpp/" },
  {
    text: "NGIN.Base",
    collapsed: false,
    items: [
      { text: "Library index", link: "/reference/cpp/base/" },
      {
        text: "Async",
        collapsed: false,
        items: [
          { text: "Overview", link: "/reference/cpp/base/async/" },
          { text: "Task", link: "/reference/cpp/base/async/task" },
          { text: "Operation", link: "/reference/cpp/base/async/operation" },
          { text: "Completion", link: "/reference/cpp/base/async/completion" },
          { text: "TaskContext", link: "/reference/cpp/base/async/task-context" },
          { text: "Cancellation", link: "/reference/cpp/base/async/cancellation" },
          { text: "WhenAll / WhenAny", link: "/reference/cpp/base/async/combinators" },
          { text: "AsyncGenerator", link: "/reference/cpp/base/async/async-generator" }
        ]
      },
      {
        text: "Execution",
        collapsed: true,
        items: [
          { text: "Overview", link: "/reference/cpp/base/execution" },
          { text: "ExecutorRef", link: "/reference/cpp/base/execution/executor-ref" },
          { text: "WorkItem", link: "/reference/cpp/base/execution/work-item" },
          { text: "Schedulers", link: "/reference/cpp/base/execution/schedulers" },
          { text: "Thread", link: "/reference/cpp/base/execution/thread" },
          { text: "Fiber", link: "/reference/cpp/base/execution/fiber" }
        ]
      },
      {
        text: "Synchronization",
        collapsed: true,
        items: [
          { text: "Overview", link: "/reference/cpp/base/sync/" },
          { text: "Mutexes and guards", link: "/reference/cpp/base/sync/mutexes" },
          { text: "Semaphore", link: "/reference/cpp/base/sync/semaphore" },
          { text: "AtomicCondition", link: "/reference/cpp/base/sync/atomic-condition" }
        ]
      },
      {
        text: "Memory",
        collapsed: true,
        items: [
          { text: "Overview", link: "/reference/cpp/base/memory-containers" },
          { text: "Allocator references", link: "/reference/cpp/base/memory/allocator-references" },
          { text: "Concrete allocators", link: "/reference/cpp/base/memory/allocators" },
          { text: "Composition", link: "/reference/cpp/base/memory/composition" },
          { text: "Ownership helpers", link: "/reference/cpp/base/memory/ownership" }
        ]
      },
      {
        text: "Containers",
        collapsed: true,
        items: [
          { text: "Overview", link: "/reference/cpp/base/containers/" },
          { text: "Vector", link: "/reference/cpp/base/containers/vector" },
          { text: "String", link: "/reference/cpp/base/containers/string" },
          { text: "FlatHashMap", link: "/reference/cpp/base/containers/flat-hash-map" },
          { text: "ConcurrentHashMap", link: "/reference/cpp/base/containers/concurrent-hash-map" }
        ]
      },
      {
        text: "I/O and Processes",
        collapsed: true,
        items: [
          { text: "Overview", link: "/reference/cpp/base/io" },
          { text: "Paths and filesystems", link: "/reference/cpp/base/io/paths-filesystems" },
          { text: "Files and directories", link: "/reference/cpp/base/io/files-directories" },
          { text: "Async I/O", link: "/reference/cpp/base/io/async" },
          { text: "Processes and libraries", link: "/reference/cpp/base/io/processes-libraries" }
        ]
      },
      {
        text: "Networking and TLS",
        collapsed: true,
        items: [
          { text: "Overview", link: "/reference/cpp/base/networking" },
          { text: "Addresses and resolution", link: "/reference/cpp/base/networking/addresses-resolution" },
          { text: "Sockets", link: "/reference/cpp/base/networking/sockets" },
          { text: "NetworkDriver", link: "/reference/cpp/base/networking/network-driver" },
          { text: "Transport and framing", link: "/reference/cpp/base/networking/transport" },
          { text: "TLS", link: "/reference/cpp/base/networking/tls" }
        ]
      },
      {
        text: "Serialization",
        collapsed: true,
        items: [
          { text: "Overview", link: "/reference/cpp/base/serialization" },
          { text: "Parsing core", link: "/reference/cpp/base/serialization/parsing-core" },
          { text: "JSON", link: "/reference/cpp/base/serialization/json" },
          { text: "XML", link: "/reference/cpp/base/serialization/xml" },
          { text: "Events", link: "/reference/cpp/base/serialization/events" },
          { text: "Writers", link: "/reference/cpp/base/serialization/writers" }
        ]
      },
      {
        text: "Cryptography",
        collapsed: true,
        items: [
          { text: "Overview", link: "/reference/cpp/base/crypto" },
          { text: "Providers and errors", link: "/reference/cpp/base/crypto/providers-errors" },
          { text: "Random and secrets", link: "/reference/cpp/base/crypto/random-secrets" },
          { text: "Encoding and keys", link: "/reference/cpp/base/crypto/encoding-keys" },
          { text: "Hash, MAC, and KDF", link: "/reference/cpp/base/crypto/hash-mac-kdf" },
          { text: "AEAD", link: "/reference/cpp/base/crypto/aead" },
          { text: "Asymmetric and certificates", link: "/reference/cpp/base/crypto/asymmetric-certificates" }
        ]
      },
      {
        text: "Foundation",
        collapsed: true,
        items: [
          { text: "Overview", link: "/reference/cpp/base/foundation" },
          { text: "Primitives and platform", link: "/reference/cpp/base/foundation/primitives-platform" },
          { text: "Results and exceptions", link: "/reference/cpp/base/results" },
          { text: "Meta and hashing", link: "/reference/cpp/base/meta-hashing" },
          { text: "Utilities", link: "/reference/cpp/base/utilities" },
          { text: "Text and Unicode", link: "/reference/cpp/base/text" },
          { text: "Math and units", link: "/reference/cpp/base/math-units" },
          { text: "Time", link: "/reference/cpp/base/time" },
          { text: "SIMD", link: "/reference/cpp/base/simd" }
        ]
      }
    ]
  },
  {
    text: "Application libraries",
    collapsed: false,
    items: [
      { text: "NGIN.Core", link: "/reference/cpp/core/" },
      { text: "NGIN.Reflection", link: "/reference/cpp/reflection/" },
      { text: "NGIN.ECS", link: "/reference/cpp/ecs/" },
      { text: "NGIN.UI", link: "/reference/cpp/ui/" },
      { text: "NGIN.Log", link: "/reference/cpp/log/" }
    ]
  }
];

export default defineConfig({
  base: siteBase,
  title: "NGIN",
  description: "Build, compose, and run modern C++ applications.",
  lang: "en-US",
  cleanUrls: true,
  lastUpdated: true,
  head: [
    ["meta", { name: "theme-color", content: "#07111f" }],
    ["meta", { property: "og:type", content: "website" }],
    ["meta", { property: "og:site_name", content: "NGIN Documentation" }]
  ],
  markdown: {
    lineNumbers: true,
    theme: { light: "github-light", dark: "github-dark" }
  },
  themeConfig: {
    logo: { src: "/ngin-mark.svg", alt: "NGIN" },
    siteTitle: "NGIN Docs",
    nav: [
      { text: "Start", link: "/start" },
      { text: "Guides", link: "/guides" },
      { text: "Libraries", link: "/libraries" },
      { text: "C++ API", link: "/reference/cpp/" },
      { text: "Reference", link: "/reference" },
      { text: "Help", link: "/troubleshooting" }
    ],
    sidebar: {
      "/start": [
        { text: "Start here", link: "/start" },
        { text: "Install NGIN", link: "/start/installation" },
        { text: "Your first project", link: "/start/first-project" },
        { text: "How NGIN works", link: "/start/mental-model" },
        { text: "Choose your path", link: "/start/choose-your-path" }
      ],
      "/project-system": [
        { text: "Project system", link: "/project-system" },
        { text: "Projects", link: "/project-system/projects" },
        { text: "Packages", link: "/project-system/packages" },
        { text: "Workspaces", link: "/project-system/workspaces" },
        { text: "Composition Graph", link: "/project-system/composition-graph" },
        { text: "Build, stage, and run", link: "/project-system/build-stage-run" }
      ],
      "/guides": [
        { text: "Guides", link: "/guides" },
        {
          text: "Build a product",
          collapsed: false,
          items: [
            { text: "Create a project", link: "/start/first-project" },
            { text: "Project manifests", link: "/project-system/projects" },
            { text: "Use packages", link: "/project-system/packages" },
            { text: "Build, stage, and run", link: "/project-system/build-stage-run" }
          ]
        },
        {
          text: "Understand the system",
          items: [
            { text: "Workspaces", link: "/project-system/workspaces" },
            { text: "Composition Graph", link: "/project-system/composition-graph" },
            { text: "Daily CLI workflow", link: "/tools/cli-workflow" }
          ]
        },
        {
          text: "Use the libraries",
          items: [
            { text: "NGIN.Base", link: "/libraries/base/quick-start" },
            { text: "NGIN.Core", link: "/libraries/core/quick-start" },
            { text: "Reflection", link: "/libraries/reflection/quick-start" },
            { text: "ECS", link: "/libraries/ecs/quick-start" },
            { text: "UI", link: "/libraries/ui/quick-start" },
            { text: "Logging", link: "/libraries/log/quick-start" }
          ]
        }
      ],
      "/libraries/base": baseSidebar,
      "/libraries/core": coreSidebar,
      "/libraries/reflection": reflectionSidebar,
      "/libraries/ecs": ecsSidebar,
      "/libraries/ui": uiSidebar,
      "/libraries/log": logSidebar,
      "/libraries": [
        { text: "Libraries", link: "/libraries" },
        { text: "NGIN.Base", link: "/libraries/base" },
        { text: "NGIN.Core", link: "/libraries/core" },
        { text: "NGIN.Reflection", link: "/libraries/reflection" },
        { text: "NGIN.ECS", link: "/libraries/ecs" },
        { text: "NGIN.UI", link: "/libraries/ui" },
        { text: "NGIN.Log", link: "/libraries/log" },
        { text: "Supporting packages", link: "/libraries/supporting-packages" }
      ],
      "/tools": [
        { text: "Tools", link: "/tools" },
        { text: "NGIN CLI", link: "/tools/cli" },
        { text: "Daily CLI workflow", link: "/tools/cli-workflow" },
        { text: "VS Code", link: "/tools/vscode" },
        { text: "MetaGen", link: "/tools/metagen" },
        { text: "Analyzers and formatters", link: "/tools/tooling-packages" }
      ],
      "/api/base": apiSidebar,
      "/api": apiSidebar,
      "/reference/cpp": cppApiSidebar,
      "/reference": [
        { text: "Reference", link: "/reference" },
        { text: "Project manifest", link: "/reference/project-manifest" },
        { text: "Package manifest", link: "/reference/package-manifest" },
        { text: "Workspace manifest", link: "/reference/workspace-manifest" },
        { text: "CLI command map", link: "/reference/cli" },
        { text: "Documentation for AI", link: "/reference/ai-access" }
      ],
      "/troubleshooting": [
        { text: "Troubleshooting", link: "/troubleshooting" },
        { text: "Install and configure", link: "/troubleshooting/install-configure" },
        { text: "Build and stage", link: "/troubleshooting/build-stage" },
        { text: "Run and plugins", link: "/troubleshooting/run-plugins" },
        { text: "Get diagnostic data", link: "/troubleshooting/diagnostics" }
      ],
      "/contributing": [
        { text: "Contributing", link: "/contributing" },
        { text: "Documentation", link: "/contributing/documentation" },
        { text: "Architecture", link: "/contributing/architecture" }
      ]
    },
    search: {
      provider: "local",
      options: {
        detailedView: true,
        translations: {
          button: { buttonText: "Search NGIN", buttonAriaLabel: "Search NGIN documentation" }
        }
      }
    },
    outline: { level: [2, 3], label: "On this page" },
    editLink: {
      pattern: "https://github.com/NGIN-ORG/NGIN/edit/main/Documentation/:path",
      text: "Edit this page on GitHub"
    },
    lastUpdated: { text: "Updated" },
    docFooter: { prev: "Previous", next: "Next" },
    socialLinks: [{ icon: "github", link: "https://github.com/NGIN-ORG/NGIN" }],
    footer: {
      message: "Documentation for the experimental NGIN platform.",
      copyright: "Released under the Apache License 2.0."
    }
  },
  vite: {
    plugins: [rawMarkdownPlugin({ base: siteBase })]
  }
});
