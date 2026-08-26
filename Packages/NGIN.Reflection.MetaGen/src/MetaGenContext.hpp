#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace NGIN::Reflection::MetaGen {
namespace fs = std::filesystem;

/// @brief Build context supplied to MetaGen by the NGIN generator protocol.
struct MetaGenContext {
  /// @brief Generator protocol identifier.
  std::string generator{};
  /// @brief Product name being generated.
  std::string projectName{};
  /// @brief Active NGIN profile name.
  std::string profileName{};
  /// @brief Resolved target platform.
  std::string platform{};
  /// @brief Resolved optimization mode.
  std::string optimization{};
  /// @brief Whether the target enables debug symbols.
  bool debugSymbols{true};
  /// @brief Whether the target enables link-time optimization.
  bool linkTimeOptimization{false};
  /// @brief Backend-specific build configuration name.
  std::string backendConfiguration{};
  /// @brief Target operating system.
  std::string operatingSystem{};
  /// @brief Target processor architecture.
  std::string architecture{};
  /// @brief Target environment or ABI variant.
  std::string environment{};
  /// @brief Compiler selected for preprocessing.
  std::string compiler{};
  /// @brief Reflection backend requested by the project.
  std::string backend{"scanner"};
  /// @brief Directory containing the authored project manifest.
  fs::path projectDir{};
  /// @brief Product output directory.
  fs::path outputDir{};
  /// @brief Directory reserved for generated sources.
  fs::path generatedDir{};
  /// @brief Directory containing `compile_commands.json`.
  fs::path compilationDatabaseDir{};
  /// @brief C++ language-standard number used by the target.
  std::string languageStandard{"23"};
  /// @brief Source files declared by the product.
  std::vector<fs::path> sourceFiles{};
  /// @brief Roots used to classify project-owned source locations.
  std::vector<fs::path> sourceRoots{};
  /// @brief Resolved compiler include search paths.
  std::vector<fs::path> includeDirectories{};
  /// @brief Resolved preprocessor definitions.
  std::vector<std::string> compileDefinitions{};
  /// @brief Resolved compiler options.
  std::vector<std::string> compileOptions{};
  /// @brief Arguments authored for the MetaGen invocation.
  std::vector<std::string> arguments{};
  /// @brief Named generator options supplied by the manifest.
  std::map<std::string, std::string, std::less<>> options{};
  /// @brief Files MetaGen is expected to produce.
  std::vector<fs::path> outputs{};
};

/// @brief Result returned to the MetaGen executable entry point.
struct MetaGenResult {
  /// @brief Whether the selected backend can run in this environment.
  bool available{true};
  /// @brief Authored output files written or confirmed unchanged.
  std::vector<fs::path> generatedFiles{};
  /// @brief Number of reflected types in the generated model.
  std::size_t reflectedTypeCount{0};
  /// @brief Actionable errors produced during generation.
  std::vector<std::string> diagnostics{};
};

/// @brief Read and validate a generator context XML document.
/// @param path Generator context document path.
/// @param diagnostics Receives validation and parsing errors.
/// @return Parsed context; diagnostics are non-empty when it is unusable.
[[nodiscard]] auto ReadContext(const fs::path &path,
                               std::vector<std::string> &diagnostics)
    -> MetaGenContext;

/// @brief Run preprocessing, scanning, and deterministic source emission.
[[nodiscard]] auto GenerateMetaData(const MetaGenContext &context)
    -> MetaGenResult;
} // namespace NGIN::Reflection::MetaGen
