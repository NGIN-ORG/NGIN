import { defineConfig } from "vitepress";
import { rawMarkdownPlugin } from "./config/raw-markdown";
import { referenceLinks } from "../scripts/reference-links.mjs";
import { libraries } from "../doxygen/libraries.mjs";
import { doxygenServerPlugin } from "../scripts/doxygen-server.mjs";

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
        ]
      },
      {
        text: "Time",
        link: "/libraries/base/time",
        collapsed: true,
        items: [
          { text: "Monotonic time", link: "/libraries/base/time/monotonic-time" },
          { text: "Deadlines and sleep", link: "/libraries/base/time/deadlines-sleep" },
        ]
      },
      {
        text: "SIMD",
        link: "/libraries/base/simd",
        collapsed: true,
        items: [
          { text: "Vectors and backends", link: "/libraries/base/simd/vectors-backends" },
          { text: "Scans and correctness", link: "/libraries/base/simd/scans-correctness" },
        ]
      }
    ]
  },
];

const coreSidebar = [
  { text: "NGIN.Core", link: "/libraries/core" },
  { text: "Quick start", link: "/libraries/core/quick-start" },
  { text: "Application lifecycle", link: "/libraries/core/application-lifecycle" },
  { text: "Services and scopes", link: "/libraries/core/services" },
  { text: "Dependency injection", link: "/libraries/core/dependency-injection" },
  { text: "Modules and plugins", link: "/libraries/core/modules-plugins" },
  { text: "Configuration and events", link: "/libraries/core/configuration-events" },
];

const reflectionSidebar = [
  { text: "NGIN.Reflection", link: "/libraries/reflection" },
  { text: "Quick start", link: "/libraries/reflection/quick-start" },
  { text: "Registration model", link: "/libraries/reflection/registration" },
  { text: "MetaGen", link: "/libraries/reflection/metagen" },
  { text: "Modules and lifetimes", link: "/libraries/reflection/modules-lifetimes" },
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
    config(md) {
      md.use(referenceLinks, { base: siteBase });
    },
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
      {
        text: "C++ API",
        items: libraries.map(({ id, name }) => ({
          text: name, link: `/reference/doxygen/${id}/index.html`, target: "_self"
        }))
      },
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
    plugins: [doxygenServerPlugin({ base: siteBase }), rawMarkdownPlugin({ base: siteBase })]
  }
});
