#include "Build.hpp"

#include "Authoring.hpp"
#include "Diagnostics.hpp"
#include "MetaGen.hpp"
#include "Resolution.hpp"
#include "Support.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace NGIN::CLI
{
    auto ResolveToolPath(
        const std::string &tool,
        const std::optional<fs::path> &searchRoot) -> std::optional<ToolResolution>;

    namespace
    {
        [[nodiscard]] auto HasPathSeparator(const std::string &value) -> bool
        {
            return value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
        }

        [[nodiscard]] auto SearchExecutableCandidates(const fs::path &path) -> std::vector<fs::path>
        {
            std::vector<fs::path> candidates{};
#if defined(_WIN32)
            const auto pathext = std::getenv("PATHEXT");
            std::string pathextValue = pathext != nullptr ? pathext : ".COM;.EXE;.BAT;.CMD";
            std::vector<std::string> extensions{};
            std::size_t start = 0;
            while (start <= pathextValue.size())
            {
                const auto end = pathextValue.find(';', start);
                auto extension = pathextValue.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!extension.empty())
                {
                    extensions.push_back(Lower(extension));
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }

            const auto lowercaseExtension = Lower(path.extension().string());
            if (!lowercaseExtension.empty())
            {
                candidates.push_back(path);
                return candidates;
            }

            candidates.push_back(path);
            for (const auto &extension : extensions)
            {
                candidates.push_back(path.string() + extension);
            }
#else
            candidates.push_back(path);
#endif
            return candidates;
        }

        [[nodiscard]] auto IsExecutableCandidate(const fs::path &path) -> bool
        {
            std::error_code error;
            if (!fs::exists(path, error) || !fs::is_regular_file(path, error))
            {
                return false;
            }
#if defined(_WIN32)
            return true;
#else
            return ::access(path.c_str(), X_OK) == 0;
#endif
        }

        [[nodiscard]] auto ToolOverrideEnvironmentName(const std::string &tool) -> std::string
        {
            std::string name{"NGIN_"};
            for (const auto ch : tool)
            {
                if (std::isalnum(static_cast<unsigned char>(ch)))
                {
                    name.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
                }
                else
                {
                    name.push_back('_');
                }
            }
            return name;
        }

        [[nodiscard]] auto CurrentHostId() -> std::optional<std::string>
        {
#if defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
            return std::string{"windows-arm64"};
#elif defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
            return std::string{"windows-x86_64"};
#else
            return std::nullopt;
#endif
#elif defined(__linux__)
#if defined(__aarch64__)
            return std::string{"linux-aarch64"};
#elif defined(__x86_64__)
            return std::string{"linux-x86_64"};
#else
            return std::nullopt;
#endif
#else
            return std::nullopt;
#endif
        }

        [[nodiscard]] auto NativeExecutableName(const std::string &tool) -> std::string
        {
            std::string executable = tool == "ninja-build" ? "ninja" : tool;
#if defined(_WIN32)
            if (Lower(fs::path(executable).extension().string()) != ".exe")
            {
                executable += ".exe";
            }
#endif
            return executable;
        }

        [[nodiscard]] auto BundledToolRootCandidates(const std::optional<fs::path> &searchRoot) -> std::vector<fs::path>
        {
            std::vector<fs::path> roots{};
            if (const auto *overrideRoot = std::getenv("NGIN_THIRD_PARTY_TOOLS_ROOT");
                overrideRoot != nullptr && std::string_view(overrideRoot).size() > 0)
            {
                roots.emplace_back(overrideRoot);
            }
            if (const auto *legacyOverrideRoot = std::getenv("NGIN_BUNDLED_TOOLS_ROOT");
                legacyOverrideRoot != nullptr && std::string_view(legacyOverrideRoot).size() > 0)
            {
                roots.emplace_back(legacyOverrideRoot);
            }
            if (searchRoot.has_value())
            {
                roots.push_back(*searchRoot / "Tools" / "ThirdParty" / "BuildTools");
            }

            auto current = fs::current_path();
            while (!current.empty())
            {
                roots.push_back(current / "Tools" / "ThirdParty" / "BuildTools");
                const auto parent = current.parent_path();
                if (parent == current)
                {
                    break;
                }
                current = parent;
            }

            std::vector<fs::path> uniqueRoots{};
            for (const auto &root : roots)
            {
                const auto normalized = root.lexically_normal();
                if (std::find(uniqueRoots.begin(), uniqueRoots.end(), normalized) == uniqueRoots.end())
                {
                    uniqueRoots.push_back(normalized);
                }
            }
            return uniqueRoots;
        }

        [[nodiscard]] auto VersionDirectories(const fs::path &root) -> std::vector<fs::path>
        {
            std::vector<fs::path> directories{};
            std::error_code error;
            if (!fs::exists(root, error) || !fs::is_directory(root, error))
            {
                return directories;
            }
            for (const auto &entry : fs::directory_iterator(root, error))
            {
                if (error)
                {
                    break;
                }
                if (entry.is_directory(error))
                {
                    directories.push_back(entry.path());
                }
            }
            std::sort(
                directories.begin(),
                directories.end(),
                [](const fs::path &left, const fs::path &right)
                {
                    return left.filename().string() > right.filename().string();
                });
            return directories;
        }

        [[nodiscard]] auto FindBundledToolPath(
            const std::string &tool,
            const std::optional<fs::path> &searchRoot) -> std::optional<ToolResolution>
        {
            const auto host = CurrentHostId();
            if (!host.has_value())
            {
                return std::nullopt;
            }

            const auto executable = NativeExecutableName(tool);
            for (const auto &root : BundledToolRootCandidates(searchRoot))
            {
                std::vector<fs::path> candidates{};
                if (tool == "cmake")
                {
                    for (const auto &versionDir : VersionDirectories(root / "cmake"))
                    {
                        candidates.push_back(versionDir / *host / "bin" / executable);
                    }
                }
                else if (tool == "ninja" || tool == "ninja-build")
                {
                    for (const auto &versionDir : VersionDirectories(root / "ninja"))
                    {
                        candidates.push_back(versionDir / *host / executable);
                        candidates.push_back(versionDir / *host / "bin" / executable);
                    }
                }

                for (const auto &candidate : candidates)
                {
                    if (IsExecutableCandidate(candidate))
                    {
                        return ToolResolution{
                            .path = candidate,
                            .source = "bundled:" + *host,
                        };
                    }
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] auto FindPathToolPath(const std::string &tool) -> std::optional<fs::path>
        {
            if (tool.empty())
            {
                return std::nullopt;
            }

            const fs::path toolPath{tool};
            if (toolPath.is_absolute() || HasPathSeparator(tool))
            {
                for (const auto &candidate : SearchExecutableCandidates(toolPath))
                {
                    if (IsExecutableCandidate(candidate))
                    {
                        return candidate;
                    }
                }
                return std::nullopt;
            }

            const auto *pathValue = std::getenv("PATH");
            if (pathValue == nullptr)
            {
                return std::nullopt;
            }

#if defined(_WIN32)
            constexpr char separator = ';';
#else
            constexpr char separator = ':';
#endif

            std::string searchPath = pathValue;
            std::size_t start = 0;
            while (start <= searchPath.size())
            {
                const auto end = searchPath.find(separator, start);
                const auto entry = searchPath.substr(start, end == std::string::npos ? std::string::npos : end - start);
                const fs::path directory = entry.empty() ? fs::current_path() : fs::path(entry);
                for (const auto &candidate : SearchExecutableCandidates(directory / tool))
                {
                    if (IsExecutableCandidate(candidate))
                    {
                        return candidate;
                    }
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }

            return std::nullopt;
        }

        [[nodiscard]] auto BuildToolSearchRoot(const ResolvedLaunch &resolved) -> std::optional<fs::path>
        {
            if (resolved.workspace.has_value())
            {
                return resolved.workspace->path.parent_path();
            }
            if (!resolved.project.path.empty())
            {
                if (const auto root = RootDirFrom(resolved.project.path.parent_path()); root.has_value())
                {
                    return root;
                }
                return resolved.project.path.parent_path();
            }
            return std::nullopt;
        }

#if defined(_WIN32)
        [[nodiscard]] auto QuoteWindowsArgument(const std::wstring &value) -> std::wstring
        {
            if (value.empty())
            {
                return L"\"\"";
            }

            const auto needsQuotes = value.find_first_of(L" \t\n\v\"") != std::wstring::npos;
            if (!needsQuotes)
            {
                return value;
            }

            std::wstring quoted{L"\""};
            unsigned int backslashes = 0;
            for (const wchar_t ch : value)
            {
                if (ch == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (ch == L'"')
                {
                    quoted.append(backslashes * 2 + 1, L'\\');
                    quoted.push_back(L'"');
                    backslashes = 0;
                    continue;
                }
                if (backslashes > 0)
                {
                    quoted.append(backslashes, L'\\');
                    backslashes = 0;
                }
                quoted.push_back(ch);
            }
            if (backslashes > 0)
            {
                quoted.append(backslashes * 2, L'\\');
            }
            quoted.push_back(L'"');
            return quoted;
        }
#endif

        [[nodiscard]] auto SanitizeIdentifier(std::string value) -> std::string
        {
            for (auto &ch : value)
            {
                if (!std::isalnum(static_cast<unsigned char>(ch)))
                {
                    ch = '_';
                }
            }
            if (value.empty())
            {
                return "artifact";
            }
            return value;
        }

        [[nodiscard]] auto ReadTextFile(const fs::path &path) -> std::optional<std::string>
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                return std::nullopt;
            }

            std::ostringstream buffer{};
            buffer << input.rdbuf();
            return buffer.str();
        }

        [[nodiscard]] auto WriteTextFileIfChanged(const fs::path &path, std::string_view content) -> bool
        {
            if (const auto existing = ReadTextFile(path); existing.has_value() && *existing == content)
            {
                return false;
            }

            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            return true;
        }

        struct GeneratedBuildPaths
        {
            fs::path sourceDir;
            fs::path buildDir;
        };

        [[nodiscard]] auto ResolveGeneratedBuildPaths(const ResolvedLaunch &resolved, const fs::path &outputDir) -> GeneratedBuildPaths
        {
            (void)resolved;
            const auto cacheRoot = outputDir / ".ngin";
            return {
                .sourceDir = cacheRoot / "cmake-src",
                .buildDir = cacheRoot / "cmake-build",
            };
        }

        [[nodiscard]] auto ResolveBuildRoot(const ResolvedLaunch &resolved) -> fs::path
        {
            return resolved.workspace.has_value() ? resolved.workspace->path.parent_path() : resolved.project.path.parent_path();
        }

        [[nodiscard]] auto ResolveOutputDir(const ResolvedLaunch &resolved, const std::optional<fs::path> &outputPath) -> fs::path
        {
            if (outputPath.has_value())
            {
                return fs::absolute(*outputPath);
            }

            return ResolveBuildRoot(resolved) / ".ngin" / "build" / resolved.project.name / resolved.configuration.name;
        }

        [[nodiscard]] auto ResolveMetaGenOutputDir(
            const ResolvedLaunch &resolved,
            const ProjectManifest &project,
            const ConfigurationDefinition &configuration) -> fs::path
        {
            return ResolveBuildRoot(resolved) / ".ngin" / "metagen" / project.name / configuration.name;
        }

        [[nodiscard]] auto PackageExposesSelectedExecutable(const PackageManifest &manifest, const std::optional<ExecutableArtifact> &selectedExecutable) -> bool
        {
            if (!selectedExecutable.has_value())
            {
                return false;
            }
            return std::any_of(
                manifest.artifacts.executables.begin(),
                manifest.artifacts.executables.end(),
                [&](const ExecutableArtifact &artifact)
                { return artifact.exported && artifact.name == selectedExecutable->name; });
        }

        [[nodiscard]] auto ReplaceAll(std::string text, const std::string &needle, const std::string &replacement) -> std::string
        {
            if (needle.empty())
            {
                return text;
            }
            std::size_t offset = 0;
            while ((offset = text.find(needle, offset)) != std::string::npos)
            {
                text.replace(offset, needle.size(), replacement);
                offset += replacement.size();
            }
            return text;
        }

        [[nodiscard]] auto ExpandProjectVariables(
            const std::string &input,
            const ProjectManifest &project,
            const std::optional<WorkspaceManifest> &workspace) -> std::string
        {
            auto expanded = ReplaceAll(input, "${ProjectDir}", fs::weakly_canonical(project.path.parent_path()).string());
            const auto workspaceDir = workspace.has_value() ? workspace->path.parent_path() : project.path.parent_path();
            expanded = ReplaceAll(expanded, "${WorkspaceDir}", workspaceDir.string());
            expanded = ReplaceAll(expanded, "${ProjectName}", project.name);
            expanded = ReplaceAll(expanded, "${ProjectTarget}", project.output.target);
            return expanded;
        }

        [[nodiscard]] auto ResolveProjectPathValue(
            const std::string &input,
            const ProjectManifest &project,
            const std::optional<WorkspaceManifest> &workspace) -> fs::path
        {
            auto value = ExpandProjectVariables(input, project, workspace);
            fs::path path{value};
            if (path.is_relative())
            {
                path = project.path.parent_path() / path;
            }
            return path.lexically_normal();
        }

        [[nodiscard]] auto IsCompiledSourceExtension(const fs::path &path) -> bool
        {
            const auto ext = Lower(path.extension().string());
            return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".m" || ext == ".mm";
        }

        [[nodiscard]] auto SourceLanguageFor(const fs::path &path) -> std::string
        {
            const auto ext = Lower(path.extension().string());
            if (ext == ".c")
            {
                return "C";
            }
            if (ext == ".m")
            {
                return "OBJC";
            }
            if (ext == ".mm")
            {
                return "OBJCXX";
            }
            return "CXX";
        }

        [[nodiscard]] auto ProjectNeedsCMakeBuild(const ProjectManifest &project) -> bool
        {
            const auto kind = Lower(project.output.kind);
            return kind == "executable" || kind == "staticlibrary" || kind == "sharedlibrary";
        }

        [[nodiscard]] auto ProjectBuildMode(const ProjectManifest &project) -> std::string
        {
            return project.build.mode.empty() ? "Generated" : project.build.mode;
        }

        [[nodiscard]] auto ToCMakeVisibility(const std::string &visibility) -> std::string
        {
            if (visibility == "Public")
            {
                return "PUBLIC";
            }
            if (visibility == "Interface")
            {
                return "INTERFACE";
            }
            return "PRIVATE";
        }

        [[nodiscard]] auto EffectivePackageBuildMode(const ResolvedPackage &package) -> std::string
        {
            if (!package.manifest.build.mode.empty())
            {
                return package.manifest.build.mode;
            }
            if (fs::exists(package.sourceDirectory / "CMakeLists.txt"))
            {
                return "AddSubdirectory";
            }
            return "FindPackage";
        }

        [[nodiscard]] auto PackageNeedsBuildIntegration(const ResolvedPackage &package, const std::optional<ExecutableArtifact> &selectedExecutable) -> bool
        {
            if (Lower(package.manifest.build.backend) != "cmake")
            {
                return false;
            }
            const auto hasLibraries = std::any_of(
                package.manifest.artifacts.libraries.begin(),
                package.manifest.artifacts.libraries.end(),
                [](const LibraryArtifact &artifact)
                { return artifact.exported && !artifact.target.empty(); });
            return hasLibraries || PackageExposesSelectedExecutable(package.manifest, selectedExecutable);
        }

        [[nodiscard]] auto HasArtifactTargetsToBuild(const ResolvedLaunch &resolved) -> bool
        {
            if (resolved.selectedExecutable.has_value() && !resolved.selectedExecutable->target.empty())
            {
                return true;
            }
            return std::any_of(
                resolved.libraries.begin(),
                resolved.libraries.end(),
                [](const LibraryArtifact &artifact)
                {
                    return !artifact.target.empty() && Lower(artifact.linkage) != "interface" && Lower(artifact.origin) != "prebuilt";
                });
        }

        [[nodiscard]] auto CollectGeneratedProjectSources(
            const std::optional<WorkspaceManifest> &workspace,
            const ProjectManifest &project,
            DiagnosticReport &report) -> std::vector<fs::path>
        {
            std::vector<fs::path> sources{};
            std::set<fs::path> unique{};

            auto addSource = [&](const fs::path &candidate)
            {
                const auto normalized = candidate.lexically_normal();
                if (!fs::exists(normalized))
                {
                    AddError(report, "project '" + project.name + "' source file '" + normalized.string() + "' does not exist");
                    return;
                }
                if (!fs::is_regular_file(normalized))
                {
                    AddError(report, "project '" + project.name + "' source path '" + normalized.string() + "' is not a file");
                    return;
                }
                if (!IsCompiledSourceExtension(normalized))
                {
                    AddError(report, "project '" + project.name + "' source file '" + normalized.string() + "' has an unsupported extension");
                    return;
                }
                if (unique.insert(normalized).second)
                {
                    sources.push_back(normalized);
                }
            };

            if (!project.build.sources.empty())
            {
                for (const auto &item : project.build.sources)
                {
                    addSource(ResolveProjectPathValue(item, project, workspace));
                }
            }
            else
            {
                for (const auto &rootPath : project.sourceRoots)
                {
                    const auto sourceRoot = ResolveProjectPathValue(rootPath, project, workspace);
                    if (!fs::exists(sourceRoot))
                    {
                        AddError(report, "project '" + project.name + "' source root '" + sourceRoot.string() + "' does not exist");
                        continue;
                    }
                    if (!fs::is_directory(sourceRoot))
                    {
                        AddError(report, "project '" + project.name + "' source root '" + sourceRoot.string() + "' is not a directory");
                        continue;
                    }
                    for (const auto &entry : fs::recursive_directory_iterator(sourceRoot))
                    {
                        if (!entry.is_regular_file() || !IsCompiledSourceExtension(entry.path()))
                        {
                            continue;
                        }
                        if (unique.insert(entry.path()).second)
                        {
                            sources.push_back(entry.path());
                        }
                    }
                }
            }

            std::sort(sources.begin(), sources.end());
            return sources;
        }

        auto AddGeneratedMetaSources(
            const ResolvedLaunch &resolved,
            const ResolvedProjectUnit &unit,
            std::vector<fs::path> &sources,
            DiagnosticReport &report) -> void
        {
            if (!unit.project.build.metaGenEnabled)
            {
                return;
            }

            const auto outputDir = ResolveMetaGenOutputDir(resolved, unit.project, unit.configuration);
            const auto result = GenerateMetaData(ResolveBuildRoot(resolved), unit.project, unit.configuration, outputDir);
            if (!result.available)
            {
                AddError(
                    report,
                    "project '" + unit.project.name + "' enables MetaGen, but this ngin CLI was built without Clang support. Install LLVM/Clang development packages and configure with NGIN_CLI_ENABLE_METAGEN=ON.");
                return;
            }
            for (const auto &diagnostic : result.diagnostics)
            {
                AddError(report, "MetaGen for project '" + unit.project.name + "': " + diagnostic);
            }
            if (report.HasErrors())
            {
                return;
            }
            sources.insert(sources.end(), result.generatedFiles.begin(), result.generatedFiles.end());
        }

        auto EmitTargetChecks(std::ostream &out, const PackageManifest &manifest) -> void
        {
            for (const auto &artifact : manifest.artifacts.libraries)
            {
                if (artifact.exported && !artifact.target.empty())
                {
                    out << "if(NOT TARGET \"" << EscapeCMake(artifact.target) << "\")\n";
                    out << "  message(FATAL_ERROR \"package '" << EscapeCMake(manifest.name)
                        << "' expected target '" << EscapeCMake(artifact.target) << "'\")\n";
                    out << "endif()\n";
                }
            }
            for (const auto &artifact : manifest.artifacts.executables)
            {
                if (artifact.exported && !artifact.target.empty())
                {
                    out << "if(NOT TARGET \"" << EscapeCMake(artifact.target) << "\")\n";
                    out << "  message(FATAL_ERROR \"package '" << EscapeCMake(manifest.name)
                        << "' expected target '" << EscapeCMake(artifact.target) << "'\")\n";
                    out << "endif()\n";
                }
            }
        }

        auto EmitPackageBuildOptions(std::ostream &out, const PackageBuildDescriptor &build) -> void
        {
            for (const auto &option : build.options)
            {
                const auto lowerValue = Lower(option.value);
                const auto cacheType = lowerValue == "on" || lowerValue == "off" || lowerValue == "true" || lowerValue == "false" ? "BOOL" : "STRING";
                out << "set(" << option.name << " \"" << EscapeCMake(option.value) << "\" CACHE " << cacheType << " \"\" FORCE)\n";
            }
        }

        [[nodiscard]] auto WriteGeneratedBuildProject(
            const ResolvedLaunch &resolved,
            const fs::path &outputDir,
            const GeneratedBuildPaths &generatedPaths,
            DiagnosticReport &report) -> bool
        {
            if (!HasArtifactTargetsToBuild(resolved))
            {
                return false;
            }

            const auto &generatedSourceDir = generatedPaths.sourceDir;
            fs::create_directories(generatedSourceDir);
            fs::create_directories(generatedPaths.buildDir);

            std::unordered_map<std::string, std::vector<fs::path>> generatedSourcesByProject{};
            std::set<std::string> languages{"CXX"};
            std::unordered_map<std::string, std::string> targetProviders{};

            for (const auto &library : resolved.libraries)
            {
                if (!library.target.empty() && !targetProviders.emplace(library.target, library.name).second)
                {
                    AddError(report, "duplicate build target '" + library.target + "' in artifacts '" + targetProviders.at(library.target) + "' and '" + library.name + "'");
                }
            }
            for (const auto &executable : resolved.executables)
            {
                if (!executable.target.empty() && !targetProviders.emplace(executable.target, executable.name).second)
                {
                    AddError(report, "duplicate build target '" + executable.target + "' in artifacts '" + targetProviders.at(executable.target) + "' and '" + executable.name + "'");
                }
            }

            std::unordered_map<std::string, const ResolvedProjectUnit *> projectByPath{};
            for (const auto &unit : resolved.projectUnits)
            {
                projectByPath.emplace(fs::weakly_canonical(unit.project.path).string(), &unit);

                const auto buildMode = ProjectBuildMode(unit.project);
                if (!IsSupportedProjectBuildMode(buildMode))
                {
                    AddError(report, "project '" + unit.project.name + "' uses unsupported build mode '" + buildMode + "'");
                    continue;
                }
                if (unit.project.build.metaGenEnabled && buildMode != "Generated")
                {
                    AddError(report, "project '" + unit.project.name + "' enables MetaGen, which is currently supported only for generated build mode");
                    continue;
                }
                if (Lower(unit.project.build.backend) != "cmake")
                {
                    AddError(report, "project '" + unit.project.name + "' uses unsupported build backend '" + unit.project.build.backend + "'");
                    continue;
                }
                if (buildMode == "Generated")
                {
                    if (Lower(unit.project.build.language) != "cxx")
                    {
                        AddError(report, "project '" + unit.project.name + "' generated build currently supports only Language=\"CXX\"");
                        continue;
                    }
                    auto sources = CollectGeneratedProjectSources(resolved.workspace, unit.project, report);
                    if (sources.empty())
                    {
                        AddError(report, "project '" + unit.project.name + "' generated build resolved no source files");
                        continue;
                    }
                    for (const auto &source : sources)
                    {
                        languages.insert(SourceLanguageFor(source));
                    }
                    AddGeneratedMetaSources(resolved, unit, sources, report);
                    for (const auto &source : sources)
                    {
                        languages.insert(SourceLanguageFor(source));
                    }
                    generatedSourcesByProject.emplace(unit.project.name, std::move(sources));
                }
            }
            if (report.HasErrors())
            {
                return false;
            }

            std::ostringstream content{};
            auto &out = content;
            out << "cmake_minimum_required(VERSION 3.20)\n";
            out << "project(NGINGeneratedBuild LANGUAGES";
            for (const auto &language : languages)
            {
                out << " " << language;
            }
            out << ")\n";
            out << "set(CMAKE_SUPPRESS_REGENERATION ON)\n";

            std::unordered_set<std::string> addedPackageKeys{};
            for (const auto &package : resolved.orderedPackages)
            {
                if (!PackageNeedsBuildIntegration(package, resolved.selectedExecutable))
                {
                    continue;
                }
                if (Lower(package.manifest.build.backend) != "cmake")
                {
                    AddError(report, "package '" + package.manifest.name + "' uses unsupported build backend '" + package.manifest.build.backend + "'");
                    continue;
                }

                const auto mode = EffectivePackageBuildMode(package);
                if (mode.empty())
                {
                    AddError(report, "package '" + package.manifest.name + "' does not define a usable CMake integration mode");
                    continue;
                }

                if (mode == "FindPackage")
                {
                    const auto packageId = package.manifest.name;
                    if (!addedPackageKeys.insert("find:" + packageId).second)
                    {
                        continue;
                    }
                    out << "find_package(\"" << EscapeCMake(packageId) << "\" CONFIG QUIET)\n";
                    if (!package.manifest.artifacts.libraries.empty() && !package.manifest.artifacts.libraries.front().target.empty())
                    {
                        out << "if(NOT TARGET \"" << EscapeCMake(package.manifest.artifacts.libraries.front().target) << "\")\n";
                    }
                    else if (!package.manifest.artifacts.executables.empty() && !package.manifest.artifacts.executables.front().target.empty())
                    {
                        out << "if(NOT TARGET \"" << EscapeCMake(package.manifest.artifacts.executables.front().target) << "\")\n";
                    }
                    else
                    {
                        out << "if(TRUE)\n";
                    }
                    out << "  find_package(\"" << EscapeCMake(packageId) << "\" QUIET)\n";
                    out << "endif()\n";
                    EmitTargetChecks(out, package.manifest);
                    continue;
                }

                if (mode == "AddSubdirectory")
                {
                    const auto sourceDir = package.sourceDirectory.empty() ? package.manifest.path.parent_path() : package.sourceDirectory;
                    const auto cmakeLists = sourceDir / "CMakeLists.txt";
                    if (!fs::exists(cmakeLists))
                    {
                        AddError(report, "package '" + package.manifest.name + "' requires a CMake project at '" + cmakeLists.string() + "'");
                        continue;
                    }
                    const auto key = "subdir:" + sourceDir.string();
                    if (!addedPackageKeys.insert(key).second)
                    {
                        continue;
                    }
                    EmitPackageBuildOptions(out, package.manifest.build);
                    out << "add_subdirectory(\"" << ToCMakePath(sourceDir) << "\" \"${CMAKE_BINARY_DIR}/pkg_" << SanitizeIdentifier(package.manifest.name) << "\" EXCLUDE_FROM_ALL)\n";
                    EmitTargetChecks(out, package.manifest);
                    continue;
                }

                if (mode == "Manual")
                {
                    const auto packageDir = fs::weakly_canonical(package.manifest.path.parent_path());
                    const auto cmakeLists = packageDir / "CMakeLists.txt";
                    if (!fs::exists(cmakeLists))
                    {
                        AddError(report, "package '" + package.manifest.name + "' requires a manual CMake wrapper at '" + cmakeLists.string() + "'");
                        continue;
                    }
                    const auto key = "manual:" + packageDir.string();
                    if (!addedPackageKeys.insert(key).second)
                    {
                        continue;
                    }
                    EmitPackageBuildOptions(out, package.manifest.build);
                    out << "add_subdirectory(\"" << ToCMakePath(packageDir) << "\" \"${CMAKE_BINARY_DIR}/pkg_" << SanitizeIdentifier(package.manifest.name) << "\" EXCLUDE_FROM_ALL)\n";
                    EmitTargetChecks(out, package.manifest);
                    continue;
                }

                AddError(report, "package '" + package.manifest.name + "' uses unsupported CMake integration mode '" + mode + "'");
            }
            if (report.HasErrors())
            {
                return false;
            }

            for (const auto &unit : resolved.projectUnits)
            {
                if (!ProjectNeedsCMakeBuild(unit.project))
                {
                    continue;
                }

                const auto buildMode = ProjectBuildMode(unit.project);
                if (buildMode == "Manual")
                {
                    const auto projectDir = fs::weakly_canonical(unit.project.path.parent_path());
                    const auto cmakeLists = projectDir / "CMakeLists.txt";
                    if (!fs::exists(cmakeLists))
                    {
                        AddError(report, "project '" + unit.project.name + "' requires a manual CMakeLists.txt at '" + cmakeLists.string() + "'");
                        continue;
                    }
                    out << "add_subdirectory(\"" << ToCMakePath(projectDir) << "\" \"${CMAKE_BINARY_DIR}/proj_" << SanitizeIdentifier(unit.project.name) << "\")\n";
                    continue;
                }

                const auto kind = Lower(unit.project.output.kind);
                const auto targetName = unit.project.output.target;
                const auto &sources = generatedSourcesByProject.at(unit.project.name);

                if (kind == "executable")
                {
                    out << "add_executable(\"" << EscapeCMake(targetName) << "\"\n";
                }
                else if (kind == "staticlibrary")
                {
                    out << "add_library(\"" << EscapeCMake(targetName) << "\" STATIC\n";
                }
                else if (kind == "sharedlibrary")
                {
                    out << "add_library(\"" << EscapeCMake(targetName) << "\" SHARED\n";
                }
                else
                {
                    AddError(report, "project '" + unit.project.name + "' output kind '" + unit.project.output.kind + "' is not supported by generated CMake");
                    continue;
                }
                for (const auto &source : sources)
                {
                    out << "  \"" << ToCMakePath(source) << "\"\n";
                }
                out << ")\n";
                out << "set_target_properties(\"" << EscapeCMake(targetName) << "\" PROPERTIES CXX_STANDARD "
                    << EscapeCMake(unit.project.build.languageStandard)
                    << " CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)\n";

                for (const auto &sourceRoot : unit.project.sourceRoots)
                {
                    const auto includeDir = ResolveProjectPathValue(sourceRoot, unit.project, resolved.workspace);
                    out << "target_include_directories(\"" << EscapeCMake(targetName) << "\" PRIVATE \"" << ToCMakePath(includeDir) << "\")\n";
                }
                for (const auto &setting : unit.project.build.includeDirectories)
                {
                    const auto includeDir = ResolveProjectPathValue(setting.value, unit.project, resolved.workspace);
                    out << "target_include_directories(\"" << EscapeCMake(targetName) << "\" " << ToCMakeVisibility(setting.visibility)
                        << " \"" << ToCMakePath(includeDir) << "\")\n";
                }
                for (const auto &setting : unit.project.build.compileDefinitions)
                {
                    out << "target_compile_definitions(\"" << EscapeCMake(targetName) << "\" " << ToCMakeVisibility(setting.visibility)
                        << " \"" << EscapeCMake(ExpandProjectVariables(setting.value, unit.project, resolved.workspace)) << "\")\n";
                }
                for (const auto &setting : unit.project.build.compileOptions)
                {
                    out << "target_compile_options(\"" << EscapeCMake(targetName) << "\" " << ToCMakeVisibility(setting.visibility)
                        << " \"" << EscapeCMake(ExpandProjectVariables(setting.value, unit.project, resolved.workspace)) << "\")\n";
                }
                for (const auto &setting : unit.project.build.linkOptions)
                {
                    out << "target_link_options(\"" << EscapeCMake(targetName) << "\" " << ToCMakeVisibility(setting.visibility)
                        << " \"" << EscapeCMake(ExpandProjectVariables(setting.value, unit.project, resolved.workspace)) << "\")\n";
                }

                const auto linkVisibility = kind == "executable" ? "PRIVATE" : "PUBLIC";
                std::vector<PackageReference> packageRefs = unit.project.packageRefs;
                {
                    std::unordered_map<std::string, std::size_t> indexByName{};
                    for (std::size_t index = 0; index < packageRefs.size(); ++index)
                    {
                        indexByName[packageRefs[index].name] = index;
                    }
                    if (unit.environment.has_value())
                    {
                        for (const auto &reference : unit.environment->packageRefs)
                        {
                            if (const auto it = indexByName.find(reference.name); it != indexByName.end())
                            {
                                packageRefs[it->second] = reference;
                            }
                            else
                            {
                                indexByName[reference.name] = packageRefs.size();
                                packageRefs.push_back(reference);
                            }
                        }
                    }
                    for (const auto &reference : unit.configuration.packageRefs)
                    {
                        if (const auto it = indexByName.find(reference.name); it != indexByName.end())
                        {
                            packageRefs[it->second] = reference;
                        }
                        else
                        {
                            indexByName[reference.name] = packageRefs.size();
                            packageRefs.push_back(reference);
                        }
                    }
                }
                for (const auto &packageRef : packageRefs)
                {
                    const auto packageIt = std::find_if(
                        resolved.orderedPackages.begin(),
                        resolved.orderedPackages.end(),
                        [&](const ResolvedPackage &package)
                        { return package.manifest.name == packageRef.name; });
                    if (packageIt == resolved.orderedPackages.end())
                    {
                        continue;
                    }
                    for (const auto &library : packageIt->manifest.artifacts.libraries)
                    {
                        if (library.exported && !library.target.empty())
                        {
                            out << "target_link_libraries(\"" << EscapeCMake(targetName) << "\" " << linkVisibility
                                << " \"" << EscapeCMake(library.target) << "\")\n";
                        }
                    }
                }

                std::vector<ProjectReference> projectRefs = unit.project.projectRefs;
                if (unit.environment.has_value())
                {
                    projectRefs.insert(projectRefs.end(), unit.environment->projectRefs.begin(), unit.environment->projectRefs.end());
                }
                projectRefs.insert(projectRefs.end(), unit.configuration.projectRefs.begin(), unit.configuration.projectRefs.end());
                for (const auto &projectRef : projectRefs)
                {
                    const auto canonical = fs::weakly_canonical(projectRef.path).string();
                    const auto refIt = projectByPath.find(canonical);
                    if (refIt == projectByPath.end())
                    {
                        AddError(report, "project '" + unit.project.name + "' references unknown project '" + projectRef.path.string() + "'");
                        continue;
                    }
                    const auto *referencedUnit = refIt->second;
                    const auto referencedKind = Lower(referencedUnit->project.output.kind);
                    if (referencedKind != "staticlibrary" && referencedKind != "sharedlibrary")
                    {
                        AddError(report, "project '" + unit.project.name + "' references non-library project '" + referencedUnit->project.name + "'");
                        continue;
                    }
                    out << "target_link_libraries(\"" << EscapeCMake(targetName) << "\" " << linkVisibility
                        << " \"" << EscapeCMake(referencedUnit->project.output.target) << "\")\n";
                }
            }
            if (report.HasErrors())
            {
                return false;
            }

            out << "add_custom_target(ngin_stage_artifacts)\n";

            auto emitStageTarget = [&](const std::string &artifactName,
                                       const std::string &targetName,
                                       const std::string &subdir,
                                       const bool copyFile)
            {
                const auto safeName = SanitizeIdentifier(artifactName);
                out << "if(NOT TARGET \"" << EscapeCMake(targetName) << "\")\n";
                out << "  message(FATAL_ERROR \"required build target '" << EscapeCMake(targetName) << "' is not available\")\n";
                out << "endif()\n";
                out << "add_custom_target(stage_" << safeName;
                if (copyFile)
                {
                    out << "\n"
                        << "  COMMAND ${CMAKE_COMMAND} -E make_directory \"" << ToCMakePath(outputDir / subdir) << "\"\n"
                        << "  COMMAND ${CMAKE_COMMAND} -E copy_if_different \"$<TARGET_FILE:" << targetName << ">\" \"" << ToCMakePath(outputDir / subdir) << "/$<TARGET_FILE_NAME:" << targetName << ">\"\n";
                }
                out << "  DEPENDS \"" << EscapeCMake(targetName) << "\"\n";
                out << "  VERBATIM)\n";
                out << "add_dependencies(ngin_stage_artifacts stage_" << safeName << ")\n";
            };

            for (const auto &library : resolved.libraries)
            {
                if (library.target.empty() || Lower(library.origin) == "prebuilt")
                {
                    continue;
                }
                emitStageTarget(library.name, library.target, "lib", Lower(library.linkage) != "interface");
            }
            if (resolved.selectedExecutable.has_value() && !resolved.selectedExecutable->target.empty() && Lower(resolved.selectedExecutable->origin) != "prebuilt")
            {
                emitStageTarget(resolved.selectedExecutable->name, resolved.selectedExecutable->target, "bin", true);
            }

            return WriteTextFileIfChanged(generatedSourceDir / "CMakeLists.txt", content.str());
        }

        [[nodiscard]] auto ConfigureGeneratedBuild(const ResolvedLaunch &resolved, const fs::path &outputDir, DiagnosticReport &report) -> std::optional<GeneratedBuildPaths>
        {
            if (!HasArtifactTargetsToBuild(resolved))
            {
                return std::nullopt;
            }

            const auto generatedPaths = ResolveGeneratedBuildPaths(resolved, outputDir);
            const auto cmakeProjectChanged = WriteGeneratedBuildProject(resolved, outputDir, generatedPaths, report);
            if (report.HasErrors())
            {
                return std::nullopt;
            }

            const auto buildConfiguration = resolved.configuration.buildConfiguration.empty() ? "Debug" : resolved.configuration.buildConfiguration;
            const auto cmakeCachePath = generatedPaths.buildDir / "CMakeCache.txt";
            const auto compileCommandsPath = generatedPaths.buildDir / "compile_commands.json";
            const auto toolSearchRoot = BuildToolSearchRoot(resolved);
            const auto cmakeTool = ResolveToolPath("cmake", toolSearchRoot);
            if (!cmakeTool.has_value())
            {
                AddError(report, "missing tool: cmake. Install CMake, set NGIN_CMAKE, or fetch bundled tools into Tools/ThirdParty/BuildTools.");
                return std::nullopt;
            }
            if (cmakeProjectChanged || !fs::exists(cmakeCachePath) || !fs::exists(compileCommandsPath))
            {
                std::vector<std::string> configureArguments{
                    "-S",
                    generatedPaths.sourceDir.string(),
                    "-B",
                    generatedPaths.buildDir.string(),
                    "-DCMAKE_BUILD_TYPE=" + buildConfiguration,
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                };
                if (const auto ninjaPath = ResolveToolPath("ninja", toolSearchRoot).or_else([&toolSearchRoot]()
                                                                         { return ResolveToolPath("ninja-build", toolSearchRoot); });
                    ninjaPath.has_value())
                {
                    configureArguments.push_back("-G");
                    configureArguments.push_back("Ninja");
                    configureArguments.push_back("-DCMAKE_MAKE_PROGRAM=" + ninjaPath->path.string());
                }
                if (RunProcess(
                        cmakeTool->path,
                        configureArguments) != 0)
                {
                    AddError(report, "failed to configure generated CMake build project for configuration '" + resolved.configuration.name + "' with build configuration '" + buildConfiguration + "'");
                    return std::nullopt;
                }
            }
            return generatedPaths;
        }

        auto BuildArtifacts(const ResolvedLaunch &resolved, const fs::path &outputDir, DiagnosticReport &report) -> void
        {
            const auto generatedPaths = ConfigureGeneratedBuild(resolved, outputDir, report);
            if (!generatedPaths.has_value() || report.HasErrors())
            {
                return;
            }

            const auto buildConfiguration = resolved.configuration.buildConfiguration.empty() ? "Debug" : resolved.configuration.buildConfiguration;
            const auto toolSearchRoot = BuildToolSearchRoot(resolved);
            const auto cmakeTool = ResolveToolPath("cmake", toolSearchRoot);
            if (!cmakeTool.has_value())
            {
                AddError(report, "missing tool: cmake. Install CMake, set NGIN_CMAKE, or fetch bundled tools into Tools/ThirdParty/BuildTools.");
                return;
            }
            if (RunProcess(
                    cmakeTool->path,
                    {
                        "--build",
                        generatedPaths->buildDir.string(),
                        "--config",
                        buildConfiguration,
                        "--target",
                        "ngin_stage_artifacts",
                    }) != 0)
            {
                AddError(report, "failed to build or stage artifacts for configuration '" + resolved.configuration.name + "' with build configuration '" + buildConfiguration + "'");
            }
        }

        auto CollectBuiltArtifactFiles(
            const fs::path &outputDir,
            std::map<fs::path, std::string> &collisions,
            DiagnosticReport &report,
            std::vector<std::tuple<std::string, fs::path, fs::path>> &staged) -> void
        {
            for (const auto &subdir : {std::string("bin"), std::string("lib")})
            {
                const auto base = outputDir / subdir;
                if (!fs::exists(base))
                {
                    continue;
                }
                for (const auto &entry : fs::recursive_directory_iterator(base))
                {
                    if (!entry.is_regular_file())
                    {
                        continue;
                    }
                    const auto dest = entry.path();
                    if (collisions.contains(dest))
                    {
                        AddError(report, "build output collision at '" + fs::relative(dest, outputDir).string() + "'");
                        continue;
                    }
                    collisions[dest] = "<artifact>";
                    staged.emplace_back(subdir == "bin" ? "executable" : "library", dest, dest);
                }
            }
        }

        auto PruneEmptyDirectories(fs::path path, const fs::path &stopAt) -> void
        {
            const auto normalizedStop = NormalizePath(stopAt);
            while (!path.empty() && NormalizePath(path) != normalizedStop)
            {
                std::error_code error;
                if (!fs::exists(path, error) || !fs::is_directory(path, error) || !fs::is_empty(path, error))
                {
                    break;
                }
                fs::remove(path, error);
                if (error)
                {
                    break;
                }
                path = path.parent_path();
            }
        }
    }

    auto ResolveToolPath(
        const std::string &tool,
        const std::optional<fs::path> &searchRoot) -> std::optional<ToolResolution>
    {
        const auto overrideName = ToolOverrideEnvironmentName(tool);
        if (const auto *overrideValue = std::getenv(overrideName.c_str());
            overrideValue != nullptr && std::string_view(overrideValue).size() > 0)
        {
            for (const auto &candidate : SearchExecutableCandidates(fs::path(overrideValue)))
            {
                if (IsExecutableCandidate(candidate))
                {
                    return ToolResolution{
                        .path = candidate,
                        .source = overrideName,
                    };
                }
            }
            return std::nullopt;
        }

        if (const auto bundled = FindBundledToolPath(tool, searchRoot); bundled.has_value())
        {
            return bundled;
        }
        if (const auto pathTool = FindPathToolPath(tool); pathTool.has_value())
        {
            return ToolResolution{
                .path = *pathTool,
                .source = "PATH",
            };
        }
        return std::nullopt;
    }

    auto ToolExists(
        const std::string &tool,
        const std::optional<fs::path> &searchRoot) -> bool
    {
        return ResolveToolPath(tool, searchRoot).has_value();
    }

    auto RunProcess(
        const fs::path &executable,
        const std::vector<std::string> &arguments,
        const std::optional<fs::path> &workingDirectory) -> int
    {
#if defined(_WIN32)
        std::wstring commandLine = QuoteWindowsArgument(executable.wstring());
        for (const auto &argument : arguments)
        {
            commandLine += L" ";
            commandLine += QuoteWindowsArgument(fs::path(argument).wstring());
        }

        std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
        commandBuffer.push_back(L'\0');

        std::wstring workingDirectoryValue{};
        const wchar_t *workingDirectoryPtr = nullptr;
        if (workingDirectory.has_value())
        {
            workingDirectoryValue = workingDirectory->wstring();
            workingDirectoryPtr = workingDirectoryValue.c_str();
        }

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessW(
                nullptr,
                commandBuffer.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                workingDirectoryPtr,
                &startupInfo,
                &processInfo))
        {
            throw std::runtime_error("failed to start process '" + executable.string() + "'");
        }

        WaitForSingleObject(processInfo.hProcess, INFINITE);
        DWORD exitCode = 1;
        if (!GetExitCodeProcess(processInfo.hProcess, &exitCode))
        {
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            throw std::runtime_error("failed to read exit code for process '" + executable.string() + "'");
        }

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return static_cast<int>(exitCode);
#else
        std::vector<std::string> argvStorage{};
        argvStorage.reserve(arguments.size() + 1);
        argvStorage.push_back(executable.string());
        argvStorage.insert(argvStorage.end(), arguments.begin(), arguments.end());

        std::vector<char *> argv{};
        argv.reserve(argvStorage.size() + 1);
        for (auto &value : argvStorage)
        {
            argv.push_back(value.data());
        }
        argv.push_back(nullptr);

        const auto processId = ::fork();
        if (processId < 0)
        {
            throw std::runtime_error("failed to fork process '" + executable.string() + "'");
        }

        if (processId == 0)
        {
            if (workingDirectory.has_value() && ::chdir(workingDirectory->c_str()) != 0)
            {
                std::_Exit(127);
            }
            ::execvp(executable.c_str(), argv.data());
            std::_Exit(errno == ENOENT ? 127 : 126);
        }

        int status = 0;
        while (::waitpid(processId, &status, 0) < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw std::runtime_error("failed to wait for process '" + executable.string() + "'");
        }

        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status))
        {
            return 128 + WTERMSIG(status);
        }
        return 1;
#endif
    }

    auto WriteLaunchManifest(
        const ResolvedLaunch &resolved,
        const fs::path &outputDir,
        const std::vector<std::tuple<std::string, fs::path, fs::path>> &staged) -> fs::path
    {
        const auto manifestPath = outputDir / (resolved.project.name + "." + resolved.configuration.name + ".nginlaunch");
        std::ofstream out(manifestPath);
        out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        out << "<LaunchManifest SchemaVersion=\"2\" Configuration=\"" << EscapeXml(resolved.configuration.name)
            << "\" Project=\"" << EscapeXml(resolved.project.name)
            << "\" Type=\"" << EscapeXml(resolved.project.type)
            << "\" BuildConfiguration=\"" << EscapeXml(resolved.configuration.buildConfiguration)
            << "\" OperatingSystem=\"" << EscapeXml(resolved.configuration.operatingSystem)
            << "\" Architecture=\"" << EscapeXml(resolved.configuration.architecture)
            << "\">\n";
        out << "  <Launch";
        if (resolved.selectedExecutable.has_value())
        {
            out << " Executable=\"" << EscapeXml(resolved.selectedExecutable->name)
                << "\" Target=\"" << EscapeXml(resolved.selectedExecutable->target)
                << "\" Origin=\"" << EscapeXml(resolved.selectedExecutable->origin) << "\"";
        }
        out << " WorkingDirectory=\"" << EscapeXml(resolved.configuration.launch.workingDirectory) << "\" />\n";
        out << "  <Environment Name=\"" << EscapeXml(resolved.configuration.environmentName) << "\">\n";
        out << "    <Variables>\n";
        for (const auto &variable : resolved.environmentVariables)
        {
            if (!variable.resolved)
            {
                continue;
            }
            out << "      <Variable Name=\"" << EscapeXml(variable.name) << "\"";
            if (variable.secret)
            {
                out << " Secret=\"true\"";
                if (!variable.fromEnvironment.empty())
                {
                    out << " FromEnvironment=\"" << EscapeXml(variable.fromEnvironment) << "\"";
                }
            }
            else
            {
                out << " Value=\"" << EscapeXml(variable.value) << "\"";
            }
            out << " />\n";
        }
        out << "    </Variables>\n";
        out << "    <Features>\n";
        for (const auto &feature : resolved.environmentFeatures)
        {
            out << "      <Feature Name=\"" << EscapeXml(feature.name)
                << "\" Enabled=\"" << (feature.enabled ? "true" : "false") << "\" />\n";
        }
        out << "    </Features>\n";
        out << "  </Environment>\n";
        out << "  <ConfigSources>\n";
        for (const auto &source : resolved.configSources)
        {
            out << "    <Config Source=\"" << EscapeXml(source.source)
                << "\" Project=\"" << EscapeXml(source.ownerProjectName)
                << "\" Destination=\"" << EscapeXml(source.stagedRelativePath.string())
                << "\" />\n";
        }
        out << "  </ConfigSources>\n";
        out << "  <Bootstraps>\n";
        for (const auto &bootstrap : resolved.bootstraps)
        {
            out << "    <Bootstrap Package=\"" << EscapeXml(bootstrap.packageName)
                << "\" Mode=\"" << EscapeXml(bootstrap.mode)
                << "\" EntryPoint=\"" << EscapeXml(bootstrap.entryPoint)
                << "\" AutoApply=\"" << (bootstrap.autoApply ? "true" : "false")
                << "\" />\n";
        }
        out << "  </Bootstraps>\n";
        out << "  <Packages>\n";
        for (const auto &package : resolved.orderedPackages)
        {
            out << "    <Package Name=\"" << EscapeXml(package.manifest.name) << "\" Version=\"" << EscapeXml(package.manifest.version) << "\" Source=\"" << EscapeXml(package.source) << "\">\n";
            for (const auto &content : package.manifest.contents)
            {
                const auto rel = content.target.empty() ? content.source : content.target;
                out << "      <Content Source=\"" << EscapeXml(content.source)
                    << "\" Kind=\"" << EscapeXml(content.kind)
                    << "\" Destination=\"" << EscapeXml(rel) << "\" />\n";
            }
            out << "    </Package>\n";
        }
        out << "  </Packages>\n";
        out << "  <Artifacts>\n";
        out << "    <Libraries>\n";
        for (const auto &library : resolved.libraries)
        {
            out << "      <Library Name=\"" << EscapeXml(library.name)
                << "\" Target=\"" << EscapeXml(library.target)
                << "\" Linkage=\"" << EscapeXml(library.linkage)
                << "\" Origin=\"" << EscapeXml(library.origin)
                << "\" />\n";
        }
        out << "    </Libraries>\n";
        out << "    <Executables>\n";
        for (const auto &executable : resolved.executables)
        {
            out << "      <Executable Name=\"" << EscapeXml(executable.name)
                << "\" Target=\"" << EscapeXml(executable.target)
                << "\" Origin=\"" << EscapeXml(executable.origin)
                << "\" />\n";
        }
        out << "    </Executables>\n";
        out << "  </Artifacts>\n";
        out << "  <Modules>\n";
        for (const auto &module : resolved.requiredModules)
        {
            out << "    <Module Name=\"" << EscapeXml(module) << "\" />\n";
        }
        for (const auto &module : resolved.optionalModules)
        {
            out << "    <Module Name=\"" << EscapeXml(module) << "\" Optional=\"true\" />\n";
        }
        out << "  </Modules>\n";
        out << "  <Plugins>\n";
        for (const auto &plugin : resolved.enabledPlugins)
        {
            out << "    <Plugin Name=\"" << EscapeXml(plugin) << "\" />\n";
        }
        out << "  </Plugins>\n";
        out << "  <StagedFiles>\n";
        for (const auto &[kind, source, destination] : staged)
        {
            out << "    <File Kind=\"" << EscapeXml(kind)
                << "\" Source=\"" << EscapeXml(source.string())
                << "\" Destination=\"" << EscapeXml(destination.string())
                << "\" RelativeDestination=\"" << EscapeXml(fs::relative(destination, outputDir).string()) << "\" />\n";
        }
        out << "  </StagedFiles>\n";
        out << "</LaunchManifest>\n";
        return manifestPath;
    }

    auto CleanupPreviousStage(const fs::path &outputDir, DiagnosticReport &report) -> void
    {
        if (!fs::exists(outputDir))
        {
            return;
        }
        if (!fs::is_directory(outputDir))
        {
            AddError(report, "output path '" + outputDir.string() + "' exists but is not a directory");
            return;
        }

        std::vector<fs::path> manifests{};
        for (const auto &entry : fs::directory_iterator(outputDir))
        {
            std::error_code error;
            if (!entry.is_regular_file(error))
            {
                continue;
            }
            if (entry.path().extension() == ".nginlaunch")
            {
                manifests.push_back(entry.path());
            }
        }

        for (const auto &manifestPath : manifests)
        {
            try
            {
                const auto document = LoadXml(manifestPath);
                const auto *rootElement = document.document.Root();
                if (rootElement == nullptr || rootElement->name != "LaunchManifest")
                {
                    throw std::runtime_error("root element must be <LaunchManifest>");
                }

                if (const auto *stagedFiles = FindChild(*rootElement, "StagedFiles"))
                {
                    for (const auto *file : ChildElements(*stagedFiles, "File"))
                    {
                        fs::path stagedPath{};
                        if (const auto relativeDestination = Attribute(*file, "RelativeDestination"); relativeDestination.has_value() && !relativeDestination->empty())
                        {
                            stagedPath = outputDir / fs::path(*relativeDestination);
                        }
                        else if (const auto destination = Attribute(*file, "Destination"); destination.has_value() && !destination->empty())
                        {
                            stagedPath = fs::path(*destination);
                        }
                        else
                        {
                            continue;
                        }

                        if (!IsPathWithinDirectory(stagedPath, outputDir))
                        {
                            AddWarning(report, "skipped cleanup for staged file outside output directory: '" + stagedPath.string() + "'");
                            continue;
                        }

                        std::error_code error;
                        if (fs::is_regular_file(stagedPath, error) || fs::is_symlink(stagedPath, error))
                        {
                            fs::remove(stagedPath, error);
                            if (!error)
                            {
                                PruneEmptyDirectories(stagedPath.parent_path(), outputDir);
                            }
                        }
                    }
                }

                std::error_code removeError;
                fs::remove(manifestPath, removeError);
            }
            catch (const std::exception &ex)
            {
                AddWarning(report, "failed to clean previous launch manifest '" + manifestPath.string() + "': " + ex.what());
            }
        }

    }

    auto CleanLaunch(
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration,
        const std::optional<fs::path> &outputPath) -> DiagnosticResult<fs::path>
    {
        DiagnosticResult<fs::path> result{};

        if (!IsSupportedBuildConfiguration(configuration.buildConfiguration))
        {
            AddError(
                result.diagnostics,
                "unsupported build configuration '" + configuration.buildConfiguration + "' in configuration '" + configuration.name + "'. Expected one of: Debug, Release, RelWithDebInfo, MinSizeRel");
            return result;
        }

        const auto resolved = ResolveLaunch(project, configuration);
        AppendDiagnostics(result.diagnostics, resolved.diagnostics);
        if (!resolved.value.has_value() || result.diagnostics.HasErrors())
        {
            return result;
        }

        const auto resolvedOutputDir = ResolveOutputDir(*resolved.value, outputPath);
        CleanupPreviousStage(resolvedOutputDir, result.diagnostics);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        const auto generatedPaths = ResolveGeneratedBuildPaths(*resolved.value, resolvedOutputDir);
        const auto cacheRoot = generatedPaths.buildDir.parent_path();
        std::error_code error;
        fs::remove_all(generatedPaths.sourceDir, error);
        error.clear();
        fs::remove_all(generatedPaths.buildDir, error);
        error.clear();
        fs::remove_all(ResolveMetaGenOutputDir(*resolved.value, project, configuration), error);
        PruneEmptyDirectories(cacheRoot, resolvedOutputDir);
        PruneEmptyDirectories(ResolveBuildRoot(*resolved.value) / ".ngin" / "metagen", ResolveBuildRoot(*resolved.value) / ".ngin");

        result.value = resolvedOutputDir;
        return result;
    }

    auto ConfigureLaunch(
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration,
        const std::optional<fs::path> &outputPath) -> DiagnosticResult<ConfiguredBuildPaths>
    {
        DiagnosticResult<ConfiguredBuildPaths> result{};

        if (!IsSupportedBuildConfiguration(configuration.buildConfiguration))
        {
            AddError(
                result.diagnostics,
                "unsupported build configuration '" + configuration.buildConfiguration + "' in configuration '" + configuration.name + "'. Expected one of: Debug, Release, RelWithDebInfo, MinSizeRel");
            return result;
        }

        const auto resolved = ResolveLaunch(project, configuration);
        AppendDiagnostics(result.diagnostics, resolved.diagnostics);
        if (!resolved.value.has_value() || result.diagnostics.HasErrors())
        {
            return result;
        }

        const auto resolvedOutputDir = ResolveOutputDir(*resolved.value, outputPath);
        fs::create_directories(resolvedOutputDir);

        ConfiguredBuildPaths configured{
            .outputDir = resolvedOutputDir,
        };

        const auto generatedPaths = ConfigureGeneratedBuild(*resolved.value, resolvedOutputDir, result.diagnostics);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }
        if (generatedPaths.has_value())
        {
            configured.buildDir = generatedPaths->buildDir;
            configured.compileCommandsPath = generatedPaths->buildDir / "compile_commands.json";
            configured.configured = true;
        }

        result.value = configured;
        return result;
    }

    auto BuildLaunch(
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration,
        const std::optional<fs::path> &outputPath) -> DiagnosticResult<GeneratedLaunchPaths>
    {
        DiagnosticResult<GeneratedLaunchPaths> result{};

        if (!IsSupportedBuildConfiguration(configuration.buildConfiguration))
        {
            AddError(
                result.diagnostics,
                "unsupported build configuration '" + configuration.buildConfiguration + "' in configuration '" + configuration.name + "'. Expected one of: Debug, Release, RelWithDebInfo, MinSizeRel");
            return result;
        }

        const auto resolved = ResolveLaunch(project, configuration);
        AppendDiagnostics(result.diagnostics, resolved.diagnostics);
        if (!resolved.value.has_value() || result.diagnostics.HasErrors())
        {
            return result;
        }

        const auto resolvedOutputDir = ResolveOutputDir(*resolved.value, outputPath);

        CleanupPreviousStage(resolvedOutputDir, result.diagnostics);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        fs::create_directories(resolvedOutputDir);
        std::map<fs::path, std::string> collisions{};
        std::vector<std::tuple<std::string, fs::path, fs::path>> staged{};

        BuildArtifacts(*resolved.value, resolvedOutputDir, result.diagnostics);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        CollectBuiltArtifactFiles(resolvedOutputDir, collisions, result.diagnostics, staged);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        for (const auto &package : resolved.value->orderedPackages)
        {
            for (const auto &content : package.manifest.contents)
            {
                const auto source = package.manifest.path.parent_path() / content.source;
                const auto rel = content.target.empty() ? content.source : content.target;
                const auto dest = resolvedOutputDir / rel;
                if (collisions.contains(dest))
                {
                    AddError(result.diagnostics, "build output collision at '" + rel + "' between packages '" + collisions[dest] + "' and '" + package.manifest.name + "'");
                    continue;
                }
                collisions[dest] = package.manifest.name;
                fs::create_directories(dest.parent_path());
                fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
                staged.emplace_back(content.kind, source, dest);
            }
        }
        for (const auto &config : resolved.value->configSources)
        {
            const auto source = config.absoluteSourcePath;
            if (!fs::exists(source))
            {
                AddError(result.diagnostics, "missing config source '" + config.source + "' declared by project '" + config.ownerProjectName + "'");
                continue;
            }
            const auto dest = resolvedOutputDir / config.stagedRelativePath;
            if (collisions.contains(dest))
            {
                AddError(result.diagnostics, "build output collision at config source '" + config.stagedRelativePath.string() + "' declared by project '" + config.ownerProjectName + "'");
                continue;
            }
            collisions[dest] = "<config>";
            fs::create_directories(dest.parent_path());
            fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
            staged.emplace_back("config-source", source, dest);
        }
        for (const auto &content : resolved.value->environmentContents)
        {
            const auto source = content.absoluteSourcePath;
            if (!fs::exists(source))
            {
                AddError(result.diagnostics, "missing environment content '" + content.source + "' declared by project '" + content.ownerProjectName + "'");
                continue;
            }
            const auto dest = resolvedOutputDir / content.stagedRelativePath;
            if (collisions.contains(dest))
            {
                AddError(result.diagnostics, "build output collision at environment content '" + content.stagedRelativePath.string() + "' declared by project '" + content.ownerProjectName + "'");
                continue;
            }
            collisions[dest] = "<environment>";
            fs::create_directories(dest.parent_path());
            fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
            staged.emplace_back(content.kind.empty() ? "environment-content" : content.kind, source, dest);
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        result.value = GeneratedLaunchPaths{
            .outputDir = resolvedOutputDir,
            .manifestPath = WriteLaunchManifest(*resolved.value, resolvedOutputDir, staged),
        };
        return result;
    }

    auto LoadLaunchManifestSummary(const fs::path &manifestPath) -> LaunchManifestSummary
    {
        const auto doc = LoadXml(manifestPath);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "LaunchManifest")
        {
            throw std::runtime_error(manifestPath.string() + ": root element must be <LaunchManifest>");
        }

        LaunchManifestSummary summary{};
        summary.manifestPath = manifestPath;
        summary.configurationName = RequireAttribute(*rootElement, "Configuration", manifestPath);
        if (const auto *launch = FindChild(*rootElement, "Launch"))
        {
            summary.workingDirectory = Attribute(*launch, "WorkingDirectory").value_or(".");
        }
        if (const auto *launch = FindChild(*rootElement, "Launch"))
        {
            summary.selectedExecutable = Attribute(*launch, "Executable").value_or("");
        }
        return summary;
    }
}
