// Each entry is an independent Doxygen project with its own search and indexes.
export const libraries = [
  { id: "base", dependencies: [], name: "NGIN.Base", input: "../Dependencies/NGIN/NGIN.Base/include", symbol: "NGIN::Async::Task", guide: "base" },
  { id: "core", dependencies: ["base", "reflection", "log"], name: "NGIN.Core", input: "../Packages/NGIN.Core/include", symbol: "NGIN::Core::ApplicationBuilder", guide: "core" },
  { id: "reflection", dependencies: ["base"], name: "NGIN.Reflection", input: "../Dependencies/NGIN/NGIN.Reflection/include", symbol: "NGIN::Reflection", guide: "reflection" },
  { id: "ecs", dependencies: ["base"], name: "NGIN.ECS", input: "../Dependencies/NGIN/NGIN.ECS/include", symbol: "NGIN::ECS", guide: "ecs" },
  { id: "ui", dependencies: ["base"], name: "NGIN.UI", input: "../Packages/NGIN.UI/include", symbol: "NGIN::UI", guide: "ui" },
  { id: "log", dependencies: ["base"], name: "NGIN.Log", input: "../Dependencies/NGIN/NGIN.Log/include", symbol: "NGIN::Log", guide: "log" },
  { id: "ui-hosting", dependencies: ["base", "core", "ui"], name: "NGIN.UI.Hosting", input: "../Packages/NGIN.UI.Hosting/include", symbol: "NGIN::UI::Hosting", guide: "supporting-packages" },
  { id: "ui-sdl3", dependencies: ["ui"], name: "NGIN.UI.Backend.SDL3", input: "../Packages/NGIN.UI.Backend.SDL3/include", symbol: "NGIN::UI::SDL3", guide: "supporting-packages" },
  { id: "ui-accessibility-windows", dependencies: ["ui"], name: "NGIN.UI.Accessibility.Windows", input: "../Packages/NGIN.UI.Accessibility.Windows/include", symbol: "NGIN::UI::Accessibility::Windows", guide: "supporting-packages" }
];
