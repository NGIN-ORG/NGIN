#include "Build.hpp"

#include "Authoring.hpp"
#include "Diagnostics.hpp"
#include "Overlay.hpp"
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
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace NGIN::CLI
{
    auto ResolveToolPath(const std::string &tool, const std::optional<fs::path> &searchRoot)
        -> std::optional<ToolResolution>;

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
            if (const auto *overrideRoot = std::getenv("NGIN_BUNDLED_TOOLS_ROOT");
                overrideRoot != nullptr && std::string_view(overrideRoot).size() > 0)
            {
                roots.emplace_back(overrideRoot);
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
            std::sort(directories.begin(), directories.end(), [](const fs::path &left, const fs::path &right) {
                return left.filename().string() > right.filename().string();
            });
            return directories;
        }

        [[nodiscard]] auto FindBundledToolPath(const std::string &tool, const std::optional<fs::path> &searchRoot)
            -> std::optional<ToolResolution>
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

        [[nodiscard]] auto ResolveGeneratedBuildPaths(const ResolvedLaunch &resolved, const fs::path &outputDir)
            -> GeneratedBuildPaths
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
            return resolved.workspace.has_value() ? resolved.workspace->path.parent_path()
                                                  : resolved.project.path.parent_path();
        }

        [[nodiscard]] auto ResolveOutputDir(const ResolvedLaunch &resolved, const std::optional<fs::path> &outputPath)
            -> fs::path
        {
            if (outputPath.has_value())
            {
                return fs::absolute(*outputPath);
            }

            return ResolveBuildRoot(resolved) / ".ngin" / "build" / resolved.project.name / resolved.profile.name;
        }

        [[nodiscard]] auto ProviderStateRoot(const ResolvedLaunch &resolved, std::string_view providerName) -> fs::path
        {
            return ResolveBuildRoot(resolved) / ".ngin" / "providers" / SanitizeIdentifier(std::string{providerName}) /
                   SanitizeIdentifier(resolved.profile.name);
        }

        [[nodiscard]] auto ProviderMetadataPath(const ResolvedLaunch &resolved, std::string_view providerName) -> fs::path
        {
            return ProviderStateRoot(resolved, providerName) / "ngin-provider.xml";
        }

        struct ProviderRestoreMetadata
        {
            std::string provider{};
            std::string kind{};
            fs::path toolchainFile{};
            fs::path installRoot{};
            fs::path prefixPath{};
        };

        [[nodiscard]] auto LoadProviderRestoreMetadata(const ResolvedLaunch &resolved,
                                                       std::string_view providerName,
                                                       DiagnosticReport &report) -> std::optional<ProviderRestoreMetadata>
        {
            const auto metadataPath = ProviderMetadataPath(resolved, providerName);
            if (!fs::exists(metadataPath))
            {
                AddError(report, "package provider '" + std::string{providerName} +
                                     "' has not been restored. Run `ngin restore` before configure/build.");
                return std::nullopt;
            }
            const auto loaded = LoadXml(metadataPath);
            const auto *rootElement = loaded.document.Root();
            if (rootElement == nullptr || rootElement->name != "ProviderRestore")
            {
                AddError(report, metadataPath.string() + ": expected ProviderRestore root element");
                return std::nullopt;
            }
            return ProviderRestoreMetadata{
                .provider = Attribute(*rootElement, "Provider").value_or(std::string{providerName}),
                .kind = Attribute(*rootElement, "Kind").value_or(""),
                .toolchainFile = Attribute(*rootElement, "ToolchainFile").value_or(""),
                .installRoot = Attribute(*rootElement, "InstallRoot").value_or(""),
                .prefixPath = Attribute(*rootElement, "PrefixPath").value_or(""),
            };
        }

        [[nodiscard]] auto CMakePackageName(const PackageManifest &manifest) -> std::string
        {
            return manifest.build.cmakePackage.empty() ? manifest.name : manifest.build.cmakePackage;
        }

        [[nodiscard]] auto PackageExposesSelectedExecutable(const PackageManifest &manifest,
                                                            const std::optional<ExecutableArtifact> &selectedExecutable)
            -> bool
        {
            if (!selectedExecutable.has_value())
            {
                return false;
            }
            return std::any_of(manifest.artifacts.executables.begin(), manifest.artifacts.executables.end(),
                               [&](const ExecutableArtifact &artifact) {
                                   return artifact.exported && artifact.name == selectedExecutable->name;
                               });
        }

        [[nodiscard]] auto ReplaceAll(std::string text, const std::string &needle, const std::string &replacement)
            -> std::string
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

        [[nodiscard]] auto ExpandProjectVariables(const std::string &input, const ProjectManifest &project,
                                                  const std::optional<WorkspaceManifest> &workspace) -> std::string
        {
            auto expanded =
                ReplaceAll(input, "${ProjectDir}", fs::weakly_canonical(project.path.parent_path()).string());
            const auto workspaceDir =
                workspace.has_value() ? workspace->path.parent_path() : project.path.parent_path();
            expanded = ReplaceAll(expanded, "${WorkspaceDir}", workspaceDir.string());
            expanded = ReplaceAll(expanded, "${ProjectName}", project.name);
            expanded = ReplaceAll(expanded, "${ProjectTarget}", project.output.target);
            return expanded;
        }

        [[nodiscard]] auto ResolveProjectPathValue(const std::string &input, const ProjectManifest &project,
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

        [[nodiscard]] auto ResolvePackagePathValue(const std::string &input, const fs::path &manifestPath) -> fs::path
        {
            fs::path path{input};
            if (path.is_relative())
            {
                path = manifestPath.parent_path() / path;
            }
            return path.lexically_normal();
        }

        [[nodiscard]] auto IsCompiledSourceExtension(const fs::path &path) -> bool
        {
            const auto ext = Lower(path.extension().string());
            return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".m" || ext == ".mm";
        }

        [[nodiscard]] auto IsHeaderSourceExtension(const fs::path &path) -> bool
        {
            const auto ext = Lower(path.extension().string());
            return ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx" || ext == ".ipp" || ext == ".inl";
        }

        [[nodiscard]] auto IsTargetSourceExtension(const fs::path &path) -> bool
        {
            return IsCompiledSourceExtension(path) || IsHeaderSourceExtension(path);
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

        [[nodiscard]] auto IsBuildInputKind(const InputDeclaration &input) -> bool
        {
            return input.kind == "Source" || (input.kind == "Generated" && input.role == "Source");
        }

        [[nodiscard]] auto SelectedBuildInputRoots(const ProjectManifest &project, const ProfileDefinition &profile)
            -> std::vector<InputDeclaration>
        {
            std::vector<InputDeclaration> roots{};
            for (const auto &input : project.inputs)
            {
                if (IsBuildInputKind(input) && input.mode == "Directory" && !input.path.empty() &&
                    SelectionMatches(project, input.selectors, profile))
                {
                    roots.push_back(input);
                }
            }
            return roots;
        }

        [[nodiscard]] auto IsStagedResolvedInput(const ResolvedInput &input) -> bool
        {
            return input.kind == "Config" || input.kind == "Content" || input.kind == "Asset" ||
                   (input.kind == "Generated" && (input.role == "Content" || input.role == "Asset" ||
                                                  !input.target.empty() || !input.targetRoot.empty()));
        }

        [[nodiscard]] auto StagedResolvedInputKind(const ResolvedInput &input) -> std::string
        {
            if (input.kind == "Config")
            {
                return "config-input";
            }
            if (input.kind == "Asset")
            {
                return "asset";
            }
            if (input.kind == "Content")
            {
                return input.contentKind.empty() ? "content" : input.contentKind;
            }
            if (input.kind == "Generated")
            {
                if (input.role == "Asset")
                {
                    return "asset";
                }
                if (input.role == "Content")
                {
                    return input.contentKind.empty() ? "content" : input.contentKind;
                }
                return "generated";
            }
            return Lower(input.kind);
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

        [[nodiscard]] auto PackageNeedsBuildIntegration(const ResolvedPackage &package,
                                                        const std::optional<ExecutableArtifact> &selectedExecutable)
            -> bool
        {
            if (Lower(package.manifest.build.backend) != "cmake")
            {
                return false;
            }
            const auto hasLibraries = std::any_of(
                package.manifest.artifacts.libraries.begin(), package.manifest.artifacts.libraries.end(),
                [](const LibraryArtifact &artifact) { return artifact.exported && !artifact.target.empty(); });
            return hasLibraries || PackageExposesSelectedExecutable(package.manifest, selectedExecutable);
        }

        [[nodiscard]] auto HasArtifactTargetsToBuild(const ResolvedLaunch &resolved) -> bool
        {
            if (resolved.selectedExecutable.has_value() && !resolved.selectedExecutable->target.empty())
            {
                return true;
            }
            return std::any_of(resolved.libraries.begin(), resolved.libraries.end(),
                               [](const LibraryArtifact &artifact) {
                                   return !artifact.target.empty() && Lower(artifact.linkage) != "interface" &&
                                          Lower(artifact.origin) != "imported" &&
                                          Lower(artifact.origin) != "prebuilt";
                               });
        }

        [[nodiscard]] auto CollectGeneratedProjectSources(const std::optional<WorkspaceManifest> &workspace,
                                                          const ProjectManifest &project,
                                                          const ProfileDefinition &profile, DiagnosticReport &report)
            -> std::vector<fs::path>
        {
            std::vector<fs::path> sources{};
            std::set<fs::path> unique{};
            std::size_t compiledSourceCount = 0;

            auto addSource = [&](const fs::path &candidate) {
                const auto normalized = candidate.lexically_normal();
                if (!fs::exists(normalized))
                {
                    AddError(report,
                             "project '" + project.name + "' source file '" + normalized.string() + "' does not exist");
                    return;
                }
                if (!fs::is_regular_file(normalized))
                {
                    AddError(report,
                             "project '" + project.name + "' source path '" + normalized.string() + "' is not a file");
                    return;
                }
                if (!IsTargetSourceExtension(normalized))
                {
                    AddError(report, "project '" + project.name + "' source file '" + normalized.string() +
                                         "' has an unsupported extension");
                    return;
                }
                if (unique.insert(normalized).second)
                {
                    sources.push_back(normalized);
                    if (IsCompiledSourceExtension(normalized))
                    {
                        ++compiledSourceCount;
                    }
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
                std::vector<fs::path> excludedRoots{};
                std::vector<fs::path> excludedFiles{};
                for (const auto &input : project.inputs)
                {
                    if (!IsBuildInputKind(input))
                    {
                        continue;
                    }
                    if (SelectionMatches(project, input.selectors, profile) || input.path.empty())
                    {
                        continue;
                    }
                    if (input.mode == "Directory")
                    {
                        excludedRoots.push_back(ResolveProjectPathValue(input.path, project, workspace));
                    }
                    else if (input.mode == "File")
                    {
                        excludedFiles.push_back(ResolveProjectPathValue(input.path, project, workspace));
                    }
                }

                auto isExcluded = [&](const fs::path &candidate) {
                    const auto normalized = candidate.lexically_normal();
                    for (const auto &root : excludedRoots)
                    {
                        if (IsPathWithinDirectory(normalized, root))
                        {
                            return true;
                        }
                    }
                    return std::find(excludedFiles.begin(), excludedFiles.end(), normalized) != excludedFiles.end();
                };

                auto addRootSources = [&](const InputDeclaration &root) {
                    const auto sourceRoot = ResolveProjectPathValue(root.path, project, workspace);
                    if (!fs::exists(sourceRoot))
                    {
                        AddError(report, "project '" + project.name + "' source root '" + sourceRoot.string() +
                                             "' does not exist");
                        return;
                    }
                    if (!fs::is_directory(sourceRoot))
                    {
                        AddError(report, "project '" + project.name + "' source root '" + sourceRoot.string() +
                                             "' is not a directory");
                        return;
                    }
                    for (const auto &entry : fs::recursive_directory_iterator(sourceRoot))
                    {
                        if (!entry.is_regular_file() || isExcluded(entry.path()))
                        {
                            continue;
                        }
                        const auto normalized = entry.path().lexically_normal();
                        const auto relativePath = normalized.lexically_relative(sourceRoot);
                        if (!root.includePatterns.empty())
                        {
                            if (!IsTargetSourceExtension(normalized) ||
                                !AnyGlobMatches(root.includePatterns, relativePath))
                            {
                                continue;
                            }
                        }
                        else if (root.role == "Header")
                        {
                            if (!IsHeaderSourceExtension(normalized))
                            {
                                continue;
                            }
                        }
                        else if (!IsCompiledSourceExtension(normalized))
                        {
                            continue;
                        }
                        if (!root.excludePatterns.empty() && AnyGlobMatches(root.excludePatterns, relativePath))
                        {
                            continue;
                        }
                        addSource(entry.path());
                    }
                };

                for (const auto &input : project.inputs)
                {
                    if (!IsBuildInputKind(input) || !SelectionMatches(project, input.selectors, profile))
                    {
                        continue;
                    }
                    if (input.mode == "Glob")
                    {
                        const auto projectDir = project.path.parent_path();
                        auto globRoot = projectDir;
                        if (!input.basePath.empty())
                        {
                            const fs::path base{input.basePath};
                            globRoot = (base.is_absolute() ? base : projectDir / base).lexically_normal();
                        }
                        if (!fs::exists(globRoot) || !fs::is_directory(globRoot))
                        {
                            AddError(report, "project '" + project.name + "' input glob base path '" +
                                                 globRoot.string() + "' does not exist");
                            continue;
                        }
                        for (const auto &entry : fs::recursive_directory_iterator(globRoot))
                        {
                            if (!entry.is_regular_file())
                            {
                                continue;
                            }
                            const auto relativePath = entry.path().lexically_relative(globRoot);
                            if (!AnyGlobMatches(input.includePatterns, relativePath) ||
                                (!input.excludePatterns.empty() && AnyGlobMatches(input.excludePatterns, relativePath)))
                            {
                                continue;
                            }
                            addSource(entry.path());
                        }
                    }
                    else if (input.mode == "Directory")
                    {
                        addRootSources(input);
                    }
                    else if (input.mode == "File")
                    {
                        addSource(ResolveProjectPathValue(input.path, project, workspace));
                    }
                }
            }

            std::sort(sources.begin(), sources.end());
            if (!sources.empty() && compiledSourceCount == 0)
            {
                AddError(report, "project '" + project.name + "' generated build resolved no compilable source files");
            }
            return sources;
        }

        [[nodiscard]] auto ResolveGeneratedDir(const ResolvedLaunch &resolved, const fs::path &outputDir) -> fs::path
        {
            return outputDir / ".ngin" / "generated" / resolved.project.name / resolved.profile.name;
        }

        [[nodiscard]] auto ExpandGeneratorMacros(std::string value, const ResolvedLaunch &resolved,
                                                 const fs::path &outputDir, const fs::path &projectDir,
                                                 const fs::path &generatorContextPath = {}) -> std::string
        {
            value = ReplaceAll(value, "$(ProjectDir)", projectDir.string());
            value = ReplaceAll(value, "$(ProjectName)", resolved.project.name);
            value = ReplaceAll(value, "$(ProfileName)", resolved.profile.name);
            value = ReplaceAll(value, "$(GeneratedDir)", ResolveGeneratedDir(resolved, outputDir).string());
            value = ReplaceAll(value, "$(OutputDir)", outputDir.string());
            if (!generatorContextPath.empty())
            {
                value = ReplaceAll(value, "$(GeneratorContext)", generatorContextPath.string());
            }
            return value;
        }

        [[nodiscard]] auto ResolveGeneratorPath(const std::string &value, const ResolvedLaunch &resolved,
                                                const ResolvedGenerator &generator, const fs::path &outputDir,
                                                const fs::path &baseDir, const fs::path &generatorContextPath = {})
            -> fs::path
        {
            fs::path path{
                ExpandGeneratorMacros(value, resolved, outputDir, generator.ownerDirectory, generatorContextPath)};
            if (path.is_relative())
            {
                path = baseDir / path;
            }
            return path.lexically_normal();
        }

        [[nodiscard]] auto GeneratorContextPath(const ResolvedLaunch &resolved, const ResolvedGenerator &generator,
                                                const fs::path &outputDir) -> fs::path
        {
            return outputDir / ".ngin" / "generator-context" / resolved.project.name / resolved.profile.name /
                   (SanitizeIdentifier(generator.declaration.name) + ".ngingen.xml");
        }

        [[nodiscard]] auto SelectedGeneratorOutputs(const ResolvedLaunch &resolved, const ResolvedGenerator &generator)
            -> std::vector<InputDeclaration>
        {
            std::vector<InputDeclaration> outputs{};
            for (auto output : generator.declaration.outputs)
            {
                if (SelectionMatches(generator.conditions, output.selectors, resolved.profile))
                {
                    outputs.push_back(std::move(output));
                }
            }
            return outputs;
        }

        [[nodiscard]] auto GeneratedOutputTarget(const ResolvedInput &input, const fs::path &generatedDir) -> fs::path
        {
            if (!input.target.empty())
            {
                return fs::path(input.target).lexically_normal();
            }
            auto fileName = input.absoluteSourcePath.filename();
            if (!input.targetRoot.empty())
            {
                return (fs::path(input.targetRoot) / fileName).lexically_normal();
            }
            auto relative = input.absoluteSourcePath.lexically_relative(generatedDir);
            const auto relativeText = relative.string();
            if (relative.empty() || relativeText == "." || relativeText.starts_with(".."))
            {
                relative = fileName;
            }
            return relative.lexically_normal();
        }

        auto AddGeneratedOutputInput(ResolvedLaunch &resolved, const ResolvedGenerator &generator,
                                     const InputDeclaration &output, const fs::path &absoluteOutput,
                                     const fs::path &generatedDir) -> void
        {
            ResolvedInput input{};
            input.ownerKind = "generator";
            input.ownerName = generator.declaration.name;
            input.ownerDirectory = generator.ownerDirectory;
            input.manifestPath = generator.manifestPath;
            input.declaringScope = output.declaringScope;
            input.setName = output.setName;
            input.name = output.name;
            input.kind = output.kind;
            input.role = output.role;
            input.source = absoluteOutput.string();
            input.mode = "File";
            input.visibility = output.visibility;
            input.target = output.target;
            input.targetRoot = output.targetRoot;
            input.basePath = output.basePath;
            input.contentKind = output.contentKind;
            input.required = output.required;
            input.absoluteSourcePath = absoluteOutput;
            input.metadata = output.metadata;
            if (IsStagedResolvedInput(input))
            {
                input.stagedRelativePath = GeneratedOutputTarget(input, generatedDir);
            }
            resolved.inputs.push_back(std::move(input));
        }

        [[nodiscard]] auto FindProjectUnitForGenerator(const ResolvedLaunch &resolved,
                                                       const ResolvedGenerator &generator)
            -> const ResolvedProjectUnit *
        {
            if (generator.ownerKind == "project")
            {
                for (const auto &unit : resolved.projectUnits)
                {
                    if (fs::weakly_canonical(unit.project.path) == fs::weakly_canonical(generator.manifestPath))
                    {
                        return &unit;
                    }
                }
            }
            return resolved.projectUnits.empty() ? nullptr : &resolved.projectUnits.back();
        }

        [[nodiscard]] auto ResolvePackageTool(const ResolvedLaunch &resolved, const ResolvedGenerator &generator,
                                              DiagnosticReport &report) -> std::optional<ToolDeclaration>
        {
            const auto packageName =
                generator.declaration.packageName.empty() ? generator.packageName : generator.declaration.packageName;
            if (packageName.empty())
            {
                AddError(report, "generator '" + generator.declaration.name + "' references tool '" +
                                     generator.declaration.toolName + "' without a package");
                return std::nullopt;
            }
            const auto packageIt =
                std::find_if(resolved.orderedPackages.begin(), resolved.orderedPackages.end(),
                             [&](const ResolvedPackage &package) { return package.manifest.name == packageName; });
            if (packageIt == resolved.orderedPackages.end())
            {
                AddError(report, "generator '" + generator.declaration.name + "' references unknown package '" +
                                     packageName + "'");
                return std::nullopt;
            }
            const auto toolIt = std::find_if(
                packageIt->manifest.tools.begin(), packageIt->manifest.tools.end(), [&](const ToolDeclaration &tool) {
                    return tool.name == generator.declaration.toolName &&
                           SelectionMatches(packageIt->manifest.conditions, tool.selectors, resolved.profile);
                });
            if (toolIt == packageIt->manifest.tools.end())
            {
                AddError(report, "generator '" + generator.declaration.name + "' references unknown package tool '" +
                                     packageName + "::" + generator.declaration.toolName + "'");
                return std::nullopt;
            }
            return *toolIt;
        }

        [[nodiscard]] auto ResolveGeneratorTool(const ResolvedLaunch &resolved, const ResolvedGenerator &generator,
                                                const fs::path &outputDir, DiagnosticReport &report)
            -> std::optional<ToolResolution>
        {
            if (generator.declaration.hasInlineTool)
            {
                const auto &tool = generator.declaration.inlineTool;
                if (!SelectionMatches(generator.conditions, tool.selectors, resolved.profile))
                {
                    AddError(report, "generator '" + generator.declaration.name +
                                         "' inline tool is not selected for profile '" + resolved.profile.name + "'");
                    return std::nullopt;
                }
                return ToolResolution{
                    .path =
                        ResolveGeneratorPath(tool.executable, resolved, generator, outputDir, generator.ownerDirectory),
                    .source = "inline",
                };
            }

            if (generator.declaration.toolName.empty())
            {
                AddError(report, "generator '" + generator.declaration.name + "' does not declare Tool");
                return std::nullopt;
            }
            const auto tool = ResolvePackageTool(resolved, generator, report);
            if (!tool.has_value())
            {
                return std::nullopt;
            }
            fs::path base = generator.providerRoot.empty() ? generator.packageDirectory : generator.providerRoot;
            auto resolvedTool = ToolResolution{
                .path = ResolveGeneratorPath(tool->executable, resolved, generator, outputDir, base),
                .source = "package",
            };
            if (!fs::exists(resolvedTool.path) && tool->executable == "bin/ngin-metagen")
            {
                const auto packageName = generator.declaration.packageName.empty() ? generator.packageName
                                                                                   : generator.declaration.packageName;
                const auto workspaceBuildTool =
                    ResolveBuildRoot(resolved) / "build" / "dev" / "Packages" / packageName / "ngin-metagen";
                if (fs::exists(workspaceBuildTool))
                {
                    resolvedTool.path = workspaceBuildTool;
                    resolvedTool.source = "workspace-build";
                }
#if defined(_WIN32)
                else if (fs::exists(workspaceBuildTool.string() + ".exe"))
                {
                    resolvedTool.path = workspaceBuildTool.string() + ".exe";
                    resolvedTool.source = "workspace-build";
                }
#endif
            }
            return resolvedTool;
        }

        auto ValidateGeneratorInputs(const ResolvedLaunch &resolved, const ResolvedGenerator &generator,
                                     const fs::path &outputDir, DiagnosticReport &report) -> void
        {
            for (const auto &input : generator.declaration.inputs)
            {
                if (!SelectionMatches(generator.conditions, input.selectors, resolved.profile) || !input.required ||
                    input.path.empty())
                {
                    continue;
                }
                const auto path =
                    ResolveGeneratorPath(input.path, resolved, generator, outputDir, generator.ownerDirectory);
                if (!fs::exists(path))
                {
                    AddError(report, "generator '" + generator.declaration.name + "' input '" + input.path +
                                         "' does not exist");
                }
            }
        }

        auto WriteGeneratorContext(const ResolvedLaunch &resolved, const ResolvedGenerator &generator,
                                   const ResolvedProjectUnit &unit, const std::vector<InputDeclaration> &outputs,
                                   const std::vector<fs::path> &absoluteOutputs, const fs::path &outputDir,
                                   const fs::path &contextPath, DiagnosticReport &report) -> void
        {
            fs::create_directories(contextPath.parent_path());
            std::ofstream out(contextPath);
            if (!out)
            {
                AddError(report, "generator '" + generator.declaration.name + "' failed to write context file '" +
                                     contextPath.string() + "'");
                return;
            }

            auto sources = CollectGeneratedProjectSources(resolved.workspace, unit.project, unit.profile, report);
            if (report.HasErrors())
            {
                return;
            }

            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
            out << "<GeneratorContext SchemaVersion=\"1\"\n";
            out << "                  Generator=\"" << EscapeXml(generator.declaration.name) << "\"\n";
            out << "                  Project=\"" << EscapeXml(unit.project.name) << "\"\n";
            out << "                  Profile=\"" << EscapeXml(unit.profile.name) << "\"\n";
            out << "                  Platform=\"" << EscapeXml(unit.profile.platform) << "\"\n";
            out << "                  BuildType=\"" << EscapeXml(unit.profile.buildType) << "\"\n";
            out << "                  OperatingSystem=\"" << EscapeXml(unit.profile.operatingSystem) << "\"\n";
            out << "                  Architecture=\"" << EscapeXml(unit.profile.architecture) << "\"\n";
            out << "                  Environment=\"" << EscapeXml(unit.profile.environmentName) << "\"\n";
            out << "                  ProjectDir=\"" << EscapeXml(unit.project.path.parent_path().string()) << "\"\n";
            out << "                  OutputDir=\"" << EscapeXml(outputDir.string()) << "\"\n";
            out << "                  GeneratedDir=\"" << EscapeXml(ResolveGeneratedDir(resolved, outputDir).string())
                << "\"\n";
            out << "                  LanguageStandard=\""
                << EscapeXml(unit.project.build.languageStandard.empty() ? std::string{"23"}
                                                                         : unit.project.build.languageStandard)
                << "\">\n";

            out << "  <Sources>\n";
            for (const auto &source : sources)
            {
                out << "    <File Path=\"" << EscapeXml(source.string()) << "\"";
                if (IsHeaderSourceExtension(source))
                {
                    out << " Role=\"Header\"";
                }
                else
                {
                    out << " Role=\"Source\"";
                }
                out << " />\n";
            }
            out << "  </Sources>\n";

            out << "  <IncludeDirectories>\n";
            out << "    <IncludeDirectory Path=\"" << EscapeXml(unit.project.path.parent_path().string()) << "\" />\n";
            for (const auto &sourceRoot : SelectedBuildInputRoots(unit.project, unit.profile))
            {
                const auto includeDir = ResolveProjectPathValue(sourceRoot.path, unit.project, resolved.workspace);
                out << "    <IncludeDirectory Path=\"" << EscapeXml(includeDir.string()) << "\" Visibility=\""
                    << EscapeXml(sourceRoot.visibility) << "\" />\n";
            }
            for (const auto &setting : EffectiveBuildSettings(unit.project, unit.profile,
                                                              unit.project.build.includeDirectories, "IncludePath"))
            {
                const auto includeDir = ResolveProjectPathValue(setting.value, unit.project, resolved.workspace);
                out << "    <IncludeDirectory Path=\"" << EscapeXml(includeDir.string()) << "\" Visibility=\""
                    << EscapeXml(setting.visibility) << "\" />\n";
            }
            std::set<fs::path> packageIncludeDirs{};
            const auto workspaceRoot = ResolveBuildRoot(resolved);
            for (const auto &package : resolved.orderedPackages)
            {
                const std::array candidates{
                    package.sourceDirectory / "include",
                    package.manifest.path.parent_path() / "include",
                    workspaceRoot / "Dependencies" / "NGIN" / package.manifest.name / "include",
                    workspaceRoot / "Packages" / package.manifest.name / "include",
                };
                for (const auto &candidate : candidates)
                {
                    std::error_code error;
                    if (fs::exists(candidate, error) && fs::is_directory(candidate, error))
                    {
                        packageIncludeDirs.insert(candidate.lexically_normal());
                    }
                }
            }
            for (const auto &includeDir : packageIncludeDirs)
            {
                out << "    <IncludeDirectory Path=\"" << EscapeXml(includeDir.string())
                    << "\" Visibility=\"Public\" Source=\"package\" />\n";
            }
            for (const auto &feature : resolved.selectedPackageFeatures)
            {
                const auto packageIt = std::find_if(
                    resolved.orderedPackages.begin(), resolved.orderedPackages.end(),
                    [&](const ResolvedPackage &package) { return package.manifest.name == feature.packageName; });
                if (packageIt == resolved.orderedPackages.end())
                {
                    continue;
                }
                for (const auto &setting : feature.build.includeDirectories)
                {
                    if (!SelectionMatches(packageIt->manifest.conditions, setting.selectors, unit.profile))
                    {
                        continue;
                    }
                    const auto includeDir = ResolvePackagePathValue(setting.value, feature.manifestPath);
                    out << "    <IncludeDirectory Path=\"" << EscapeXml(includeDir.string()) << "\" Visibility=\""
                        << EscapeXml(setting.visibility) << "\" Package=\"" << EscapeXml(feature.packageName)
                        << "\" />\n";
                }
            }
            out << "  </IncludeDirectories>\n";

            out << "  <CompileDefinitions>\n";
            for (const auto &setting :
                 EffectiveBuildSettings(unit.project, unit.profile, unit.project.build.compileDefinitions, "Define"))
            {
                out << "    <Definition Value=\""
                    << EscapeXml(ExpandProjectVariables(setting.value, unit.project, resolved.workspace))
                    << "\" Visibility=\"" << EscapeXml(setting.visibility) << "\" />\n";
            }
            for (const auto &feature : resolved.selectedPackageFeatures)
            {
                const auto packageIt = std::find_if(
                    resolved.orderedPackages.begin(), resolved.orderedPackages.end(),
                    [&](const ResolvedPackage &package) { return package.manifest.name == feature.packageName; });
                if (packageIt == resolved.orderedPackages.end())
                {
                    continue;
                }
                for (const auto &setting : feature.build.compileDefinitions)
                {
                    if (SelectionMatches(packageIt->manifest.conditions, setting.selectors, unit.profile))
                    {
                        out << "    <Definition Value=\"" << EscapeXml(setting.value) << "\" Visibility=\""
                            << EscapeXml(setting.visibility) << "\" Package=\"" << EscapeXml(feature.packageName)
                            << "\" />\n";
                    }
                }
            }
            out << "  </CompileDefinitions>\n";

            out << "  <CompileOptions>\n";
            for (const auto &setting :
                 EffectiveBuildSettings(unit.project, unit.profile, unit.project.build.compileOptions, "CompileOption"))
            {
                out << "    <Option Value=\""
                    << EscapeXml(ExpandProjectVariables(setting.value, unit.project, resolved.workspace))
                    << "\" Visibility=\"" << EscapeXml(setting.visibility) << "\" />\n";
            }
            for (const auto &feature : resolved.selectedPackageFeatures)
            {
                const auto packageIt = std::find_if(
                    resolved.orderedPackages.begin(), resolved.orderedPackages.end(),
                    [&](const ResolvedPackage &package) { return package.manifest.name == feature.packageName; });
                if (packageIt == resolved.orderedPackages.end())
                {
                    continue;
                }
                for (const auto &setting : feature.build.compileOptions)
                {
                    if (SelectionMatches(packageIt->manifest.conditions, setting.selectors, unit.profile))
                    {
                        out << "    <Option Value=\"" << EscapeXml(setting.value) << "\" Visibility=\""
                            << EscapeXml(setting.visibility) << "\" Package=\"" << EscapeXml(feature.packageName)
                            << "\" />\n";
                    }
                }
            }
            out << "  </CompileOptions>\n";

            out << "  <Outputs>\n";
            for (std::size_t index = 0; index < outputs.size(); ++index)
            {
                out << "    <Generated Role=\"" << EscapeXml(outputs[index].role) << "\" Path=\""
                    << EscapeXml(absoluteOutputs[index].string()) << "\" />\n";
            }
            out << "  </Outputs>\n";
            out << "</GeneratorContext>\n";
        }

        [[nodiscard]] auto RunGeneratorProcess(const std::string &generatorName, const fs::path &executable,
                                               const std::vector<std::string> &arguments,
                                               const std::optional<fs::path> &workingDirectory,
                                               const BuildExecutionOptions &options) -> int
        {
            const auto name = "Generator " + generatorName;
            const auto started = std::chrono::steady_clock::now();
            ProcessResult result{};
            bool emittedStreamOutput = false;
            if (options.backendOutput == BackendOutputMode::Stream && options.events == nullptr)
            {
                result.exitCode = RunProcess(executable, arguments, workingDirectory);
            }
            else
            {
                const auto callback =
                    options.events != nullptr && options.backendOutput == BackendOutputMode::Stream
                        ? std::function<void(std::string_view)>{[&](std::string_view text) {
                              emittedStreamOutput = true;
                              options.events->Emit(CliEventType::BackendOutput,
                                                   EventData{}
                                                       .AddString("phase", "generate")
                                                       .AddString("stream", "combined")
                                                       .AddString("text", std::string{text}));
                          }}
                        : std::function<void(std::string_view)>{};
                result = RunProcessCapture(executable, arguments, workingDirectory, callback);
            }
            const auto finished = std::chrono::steady_clock::now();
            const auto duration =
                static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count());
            if (options.events != nullptr && result.exitCode == 0 &&
                options.backendOutput == BackendOutputMode::Stream && !emittedStreamOutput && !result.output.empty())
            {
                options.events->Emit(CliEventType::BackendOutput, EventData{}
                                                                      .AddString("phase", "generate")
                                                                      .AddString("stream", "combined")
                                                                      .AddString("text", result.output));
            }
            if (options.events != nullptr && result.exitCode != 0 &&
                options.backendOutput != BackendOutputMode::Silent &&
                options.backendOutput != BackendOutputMode::Stream && !result.output.empty())
            {
                options.events->Emit(CliEventType::BackendOutput, EventData{}
                                                                  .AddString("phase", "generate")
                                                                  .AddString("stream", "combined")
                                                                  .AddString("text", result.output));
            }
            if (options.backendSteps != nullptr)
            {
                options.backendSteps->push_back(BackendStepResult{
                    .name = name,
                    .exitCode = result.exitCode,
                    .durationMilliseconds = duration,
                    .output =
                        options.backendOutput == BackendOutputMode::Silent ? std::string{} : std::move(result.output),
                });
            }
            return result.exitCode;
        }

        auto ExecuteGenerators(ResolvedLaunch &resolved, const fs::path &outputDir, DiagnosticReport &report,
                               const BuildExecutionOptions &options) -> void
        {
            const auto generatedDir = ResolveGeneratedDir(resolved, outputDir);
            for (const auto &generator : resolved.generators)
            {
                const auto outputs = SelectedGeneratorOutputs(resolved, generator);
                if (outputs.empty())
                {
                    AddError(report, "generator '" + generator.declaration.name + "' resolved no selected outputs");
                    continue;
                }
                ValidateGeneratorInputs(resolved, generator, outputDir, report);
                const auto tool = ResolveGeneratorTool(resolved, generator, outputDir, report);
                if (!tool.has_value())
                {
                    continue;
                }

                std::vector<fs::path> absoluteOutputs{};
                for (const auto &output : outputs)
                {
                    absoluteOutputs.push_back(
                        ResolveGeneratorPath(output.path, resolved, generator, outputDir, generatedDir));
                }

                if (generator.declaration.kind == "Command")
                {
                    const auto *unit = FindProjectUnitForGenerator(resolved, generator);
                    if (unit == nullptr)
                    {
                        AddError(report,
                                 "generator '" + generator.declaration.name + "' could not resolve owning project");
                        continue;
                    }
                    const auto contextPath = GeneratorContextPath(resolved, generator, outputDir);
                    WriteGeneratorContext(resolved, generator, *unit, outputs, absoluteOutputs, outputDir, contextPath,
                                          report);
                    if (report.HasErrors())
                    {
                        continue;
                    }
                    if (!fs::exists(tool->path))
                    {
                        AddError(report, "generator '" + generator.declaration.name + "' executable '" +
                                             tool->path.string() + "' does not exist");
                        continue;
                    }
                    std::vector<std::string> arguments{};
                    for (const auto &argument : generator.declaration.arguments)
                    {
                        if (!SelectionMatches(generator.conditions, argument.selectors, resolved.profile))
                        {
                            continue;
                        }
                        if (!argument.value.empty())
                        {
                            arguments.push_back(ExpandGeneratorMacros(argument.value, resolved, outputDir,
                                                                      generator.ownerDirectory, contextPath));
                        }
                        else
                        {
                            arguments.push_back(ResolveGeneratorPath(argument.path, resolved, generator, outputDir,
                                                                     generator.ownerDirectory, contextPath)
                                                    .string());
                        }
                    }
                    if (RunGeneratorProcess(generator.declaration.name, tool->path, arguments,
                                            generator.ownerDirectory, options) != 0)
                    {
                        AddError(report, "generator '" + generator.declaration.name + "' command failed");
                    }
                }
                else
                {
                    AddError(report, "unsupported generator kind '" + generator.declaration.kind + "'");
                }
                if (report.HasErrors())
                {
                    continue;
                }
                for (std::size_t index = 0; index < outputs.size(); ++index)
                {
                    const auto &absoluteOutput = absoluteOutputs[index];
                    if (!fs::exists(absoluteOutput))
                    {
                        if (outputs[index].required)
                        {
                            AddError(report, "generator '" + generator.declaration.name +
                                                 "' did not produce declared output '" + absoluteOutput.string() + "'");
                        }
                        continue;
                    }
                    AddGeneratedOutputInput(resolved, generator, outputs[index], absoluteOutput, generatedDir);
                }
            }
        }

        auto AddGeneratedOutputSources(const ResolvedLaunch &resolved, const ResolvedProjectUnit &unit,
                                       std::vector<fs::path> &sources) -> void
        {
            std::set<fs::path> existing(sources.begin(), sources.end());
            for (const auto &input : resolved.inputs)
            {
                if (input.kind != "Generated" || input.role != "Source" || input.ownerKind != "generator")
                {
                    continue;
                }
                const auto generatorIt = std::find_if(
                    resolved.generators.begin(), resolved.generators.end(),
                    [&](const ResolvedGenerator &generator) { return generator.declaration.name == input.ownerName; });
                if (generatorIt == resolved.generators.end())
                {
                    continue;
                }
                if (generatorIt->ownerKind == "project" &&
                    fs::weakly_canonical(generatorIt->manifestPath) != fs::weakly_canonical(unit.project.path))
                {
                    continue;
                }
                if (generatorIt->ownerKind != "project" &&
                    fs::weakly_canonical(unit.project.path) != fs::weakly_canonical(resolved.project.path))
                {
                    continue;
                }
                if (existing.insert(input.absoluteSourcePath).second)
                {
                    sources.push_back(input.absoluteSourcePath);
                }
            }
        }

        auto EmitTargetChecks(std::ostream &out, const PackageManifest &manifest) -> void
        {
            for (const auto &artifact : manifest.artifacts.libraries)
            {
                if (artifact.exported && !artifact.target.empty())
                {
                    out << "if(NOT TARGET \"" << EscapeCMake(artifact.target) << "\")\n";
                    out << "  message(FATAL_ERROR \"package '" << EscapeCMake(manifest.name) << "' expected target '"
                        << EscapeCMake(artifact.target) << "'\")\n";
                    out << "endif()\n";
                }
            }
            for (const auto &artifact : manifest.artifacts.executables)
            {
                if (artifact.exported && !artifact.target.empty())
                {
                    out << "if(NOT TARGET \"" << EscapeCMake(artifact.target) << "\")\n";
                    out << "  message(FATAL_ERROR \"package '" << EscapeCMake(manifest.name) << "' expected target '"
                        << EscapeCMake(artifact.target) << "'\")\n";
                    out << "endif()\n";
                }
            }
        }

        auto EmitPackageBuildOptions(std::ostream &out, const PackageBuildDescriptor &build) -> void
        {
            for (const auto &option : build.options)
            {
                const auto lowerValue = Lower(option.value);
                const auto cacheType =
                    lowerValue == "on" || lowerValue == "off" || lowerValue == "true" || lowerValue == "false"
                        ? "BOOL"
                        : "STRING";
                out << "set(" << option.name << " \"" << EscapeCMake(option.value) << "\" CACHE " << cacheType
                    << " \"\" FORCE)\n";
            }
        }

        [[nodiscard]] auto WriteGeneratedBuildProject(const ResolvedLaunch &resolved, const fs::path &outputDir,
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
                    AddError(report, "duplicate build target '" + library.target + "' in artifacts '" +
                                         targetProviders.at(library.target) + "' and '" + library.name + "'");
                }
            }
            for (const auto &executable : resolved.executables)
            {
                if (!executable.target.empty() && !targetProviders.emplace(executable.target, executable.name).second)
                {
                    AddError(report, "duplicate build target '" + executable.target + "' in artifacts '" +
                                         targetProviders.at(executable.target) + "' and '" + executable.name + "'");
                }
            }

            std::unordered_map<std::string, const ResolvedProjectUnit *> projectByPath{};
            for (const auto &unit : resolved.projectUnits)
            {
                projectByPath.emplace(fs::weakly_canonical(unit.project.path).string(), &unit);

                const auto buildMode = ProjectBuildMode(unit.project);
                if (!IsSupportedProjectBuildMode(buildMode))
                {
                    AddError(report,
                             "project '" + unit.project.name + "' uses unsupported build mode '" + buildMode + "'");
                    continue;
                }
                if (Lower(unit.project.build.backend) != "cmake")
                {
                    AddError(report, "project '" + unit.project.name + "' uses unsupported build backend '" +
                                         unit.project.build.backend + "'");
                    continue;
                }
                if (buildMode == "Generated")
                {
                    if (Lower(unit.project.build.language) != "cxx")
                    {
                        AddError(report, "project '" + unit.project.name +
                                             "' generated build currently supports only Language=\"CXX\"");
                        continue;
                    }
                    auto sources =
                        CollectGeneratedProjectSources(resolved.workspace, unit.project, unit.profile, report);
                    if (sources.empty() && unit.project.productKind != "External")
                    {
                        AddError(report,
                                 "project '" + unit.project.name + "' generated build resolved no source files");
                        continue;
                    }
                    for (const auto &source : sources)
                    {
                        if (IsCompiledSourceExtension(source))
                        {
                            languages.insert(SourceLanguageFor(source));
                        }
                    }
                    AddGeneratedOutputSources(resolved, unit, sources);
                    for (const auto &source : sources)
                    {
                        if (IsCompiledSourceExtension(source))
                        {
                            languages.insert(SourceLanguageFor(source));
                        }
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
                    AddError(report, "package '" + package.manifest.name + "' uses unsupported build backend '" +
                                         package.manifest.build.backend + "'");
                    continue;
                }

                const auto mode = EffectivePackageBuildMode(package);
                if (mode.empty())
                {
                    AddError(report,
                             "package '" + package.manifest.name + "' does not define a usable CMake integration mode");
                    continue;
                }

                if (mode == "FindPackage")
                {
                    const auto packageId = CMakePackageName(package.manifest);
                    if (!package.manifest.build.provider.empty())
                    {
                        (void)LoadProviderRestoreMetadata(resolved, package.manifest.build.provider, report);
                        if (report.HasErrors())
                        {
                            continue;
                        }
                    }
                    const auto alreadyFound = !addedPackageKeys.insert("find:" + packageId).second;
                    if (!alreadyFound)
                    {
                        out << "find_package(\"" << EscapeCMake(packageId) << "\" CONFIG QUIET)\n";
                    }
                    if (!package.manifest.artifacts.libraries.empty() &&
                        !package.manifest.artifacts.libraries.front().target.empty())
                    {
                        out << "if(NOT TARGET \"" << EscapeCMake(package.manifest.artifacts.libraries.front().target)
                            << "\")\n";
                    }
                    else if (!package.manifest.artifacts.executables.empty() &&
                             !package.manifest.artifacts.executables.front().target.empty())
                    {
                        out << "if(NOT TARGET \"" << EscapeCMake(package.manifest.artifacts.executables.front().target)
                            << "\")\n";
                    }
                    else
                    {
                        out << "if(TRUE)\n";
                    }
                    if (!alreadyFound)
                    {
                        out << "  find_package(\"" << EscapeCMake(packageId) << "\" QUIET)\n";
                    }
                    out << "endif()\n";
                    EmitTargetChecks(out, package.manifest);
                    continue;
                }

                if (mode == "AddSubdirectory")
                {
                    const auto sourceDir =
                        package.sourceDirectory.empty() ? package.manifest.path.parent_path() : package.sourceDirectory;
                    const auto cmakeLists = sourceDir / "CMakeLists.txt";
                    if (!fs::exists(cmakeLists))
                    {
                        AddError(report, "package '" + package.manifest.name + "' requires a CMake project at '" +
                                             cmakeLists.string() + "'");
                        continue;
                    }
                    const auto key = "subdir:" + sourceDir.string();
                    if (!addedPackageKeys.insert(key).second)
                    {
                        continue;
                    }
                    EmitPackageBuildOptions(out, package.manifest.build);
                    out << "add_subdirectory(\"" << ToCMakePath(sourceDir) << "\" \"${CMAKE_BINARY_DIR}/pkg_"
                        << SanitizeIdentifier(package.manifest.name) << "\" EXCLUDE_FROM_ALL)\n";
                    EmitTargetChecks(out, package.manifest);
                    continue;
                }

                if (mode == "Manual")
                {
                    const auto packageDir = fs::weakly_canonical(package.manifest.path.parent_path());
                    const auto cmakeLists = packageDir / "CMakeLists.txt";
                    if (!fs::exists(cmakeLists))
                    {
                        AddError(report, "package '" + package.manifest.name +
                                             "' requires a manual CMake wrapper at '" + cmakeLists.string() + "'");
                        continue;
                    }
                    const auto key = "manual:" + packageDir.string();
                    if (!addedPackageKeys.insert(key).second)
                    {
                        continue;
                    }
                    EmitPackageBuildOptions(out, package.manifest.build);
                    out << "add_subdirectory(\"" << ToCMakePath(packageDir) << "\" \"${CMAKE_BINARY_DIR}/pkg_"
                        << SanitizeIdentifier(package.manifest.name) << "\" EXCLUDE_FROM_ALL)\n";
                    EmitTargetChecks(out, package.manifest);
                    continue;
                }

                AddError(report, "package '" + package.manifest.name + "' uses unsupported CMake integration mode '" +
                                     mode + "'");
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
                        AddError(report, "project '" + unit.project.name + "' requires a manual CMakeLists.txt at '" +
                                             cmakeLists.string() + "'");
                        continue;
                    }
                    out << "add_subdirectory(\"" << ToCMakePath(projectDir) << "\" \"${CMAKE_BINARY_DIR}/proj_"
                        << SanitizeIdentifier(unit.project.name) << "\")\n";
                    continue;
                }

                const auto kind = Lower(unit.project.output.kind);
                const auto targetName = unit.project.output.target;
                const auto &sources = generatedSourcesByProject.at(unit.project.name);

                if (unit.project.productKind == "External")
                {
                    out << "if(NOT TARGET \"" << EscapeCMake(targetName) << "\")\n";
                    out << "  add_library(\"" << EscapeCMake(targetName) << "\" INTERFACE IMPORTED)\n";
                    out << "endif()\n";
                }
                else if (kind == "executable")
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
                    AddError(report, "project '" + unit.project.name + "' output kind '" + unit.project.output.kind +
                                         "' is not supported by generated CMake");
                    continue;
                }
                if (unit.project.productKind != "External")
                {
                    for (const auto &source : sources)
                    {
                        out << "  \"" << ToCMakePath(source) << "\"\n";
                    }
                    out << ")\n";
                    out << "set_target_properties(\"" << EscapeCMake(targetName) << "\" PROPERTIES CXX_STANDARD "
                        << EscapeCMake(unit.project.build.languageStandard)
                        << " CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)\n";
                }

                for (const auto &sourceRoot : SelectedBuildInputRoots(unit.project, unit.profile))
                {
                    const auto includeDir = ResolveProjectPathValue(sourceRoot.path, unit.project, resolved.workspace);
                    out << "target_include_directories(\"" << EscapeCMake(targetName) << "\" "
                        << ToCMakeVisibility(sourceRoot.visibility) << " \"" << ToCMakePath(includeDir) << "\")\n";
                }
                for (const auto &setting : EffectiveBuildSettings(unit.project, unit.profile,
                                                                  unit.project.build.includeDirectories, "IncludePath"))
                {
                    const auto includeDir = ResolveProjectPathValue(setting.value, unit.project, resolved.workspace);
                    out << "target_include_directories(\"" << EscapeCMake(targetName) << "\" "
                        << ToCMakeVisibility(setting.visibility) << " \"" << ToCMakePath(includeDir) << "\")\n";
                }
                for (const auto &setting : EffectiveBuildSettings(unit.project, unit.profile,
                                                                  unit.project.build.compileDefinitions, "Define"))
                {
                    out << "target_compile_definitions(\"" << EscapeCMake(targetName) << "\" "
                        << ToCMakeVisibility(setting.visibility) << " \""
                        << EscapeCMake(ExpandProjectVariables(setting.value, unit.project, resolved.workspace))
                        << "\")\n";
                }
                for (const auto &setting : EffectiveBuildSettings(unit.project, unit.profile,
                                                                  unit.project.build.compileOptions, "CompileOption"))
                {
                    out << "target_compile_options(\"" << EscapeCMake(targetName) << "\" "
                        << ToCMakeVisibility(setting.visibility) << " \""
                        << EscapeCMake(ExpandProjectVariables(setting.value, unit.project, resolved.workspace))
                        << "\")\n";
                }
                for (const auto &setting :
                     EffectiveBuildSettings(unit.project, unit.profile, unit.project.build.linkOptions, "LinkOption"))
                {
                    out << "target_link_options(\"" << EscapeCMake(targetName) << "\" "
                        << ToCMakeVisibility(setting.visibility) << " \""
                        << EscapeCMake(ExpandProjectVariables(setting.value, unit.project, resolved.workspace))
                        << "\")\n";
                }
                for (const auto &feature : resolved.selectedPackageFeatures)
                {
                    const auto packageIt = std::find_if(
                        resolved.orderedPackages.begin(), resolved.orderedPackages.end(),
                        [&](const ResolvedPackage &package) { return package.manifest.name == feature.packageName; });
                    if (packageIt == resolved.orderedPackages.end())
                    {
                        continue;
                    }
                    for (const auto &setting : feature.build.includeDirectories)
                    {
                        if (!SelectionMatches(packageIt->manifest.conditions, setting.selectors, unit.profile))
                        {
                            continue;
                        }
                        const auto includeDir = ResolvePackagePathValue(setting.value, feature.manifestPath);
                        out << "target_include_directories(\"" << EscapeCMake(targetName) << "\" "
                            << ToCMakeVisibility(setting.visibility) << " \"" << ToCMakePath(includeDir) << "\")\n";
                    }
                    for (const auto &setting : feature.build.compileDefinitions)
                    {
                        if (!SelectionMatches(packageIt->manifest.conditions, setting.selectors, unit.profile))
                        {
                            continue;
                        }
                        out << "target_compile_definitions(\"" << EscapeCMake(targetName) << "\" "
                            << ToCMakeVisibility(setting.visibility) << " \"" << EscapeCMake(setting.value) << "\")\n";
                    }
                    for (const auto &setting : feature.build.compileOptions)
                    {
                        if (!SelectionMatches(packageIt->manifest.conditions, setting.selectors, unit.profile))
                        {
                            continue;
                        }
                        out << "target_compile_options(\"" << EscapeCMake(targetName) << "\" "
                            << ToCMakeVisibility(setting.visibility) << " \"" << EscapeCMake(setting.value) << "\")\n";
                    }
                    for (const auto &setting : feature.build.linkOptions)
                    {
                        if (!SelectionMatches(packageIt->manifest.conditions, setting.selectors, unit.profile))
                        {
                            continue;
                        }
                        out << "target_link_options(\"" << EscapeCMake(targetName) << "\" "
                            << ToCMakeVisibility(setting.visibility) << " \"" << EscapeCMake(setting.value) << "\")\n";
                    }
                }

                const auto linkVisibility = kind == "executable" ? "PRIVATE" : "PUBLIC";
                std::vector<PackageReference> packageRefs{};
                {
                    std::unordered_map<std::string, std::size_t> indexByName{};
                    const auto addPackageRef = [&](const PackageReference &reference) {
                        if (!SelectionMatches(unit.project, reference.selectors, unit.profile))
                        {
                            return;
                        }
                        if (const auto it = indexByName.find(reference.name); it != indexByName.end())
                        {
                            packageRefs[it->second] = reference;
                        }
                        else
                        {
                            indexByName[reference.name] = packageRefs.size();
                            packageRefs.push_back(reference);
                        }
                    };
                    for (const auto &reference : unit.project.packageRefs)
                    {
                        addPackageRef(reference);
                    }
                    if (unit.environment.has_value())
                    {
                        for (const auto &reference : unit.environment->packageRefs)
                        {
                            addPackageRef(reference);
                        }
                    }
                    for (const auto &reference : unit.profile.packageRefs)
                    {
                        addPackageRef(reference);
                    }
                    for (const auto &feature : resolved.selectedPackageFeatures)
                    {
                        const auto addResolvedPackageRef = [&](PackageReference reference) {
                            if (reference.name.empty())
                            {
                                return;
                            }
                            if (const auto it = indexByName.find(reference.name); it != indexByName.end())
                            {
                                packageRefs[it->second] = std::move(reference);
                            }
                            else
                            {
                                indexByName[reference.name] = packageRefs.size();
                                packageRefs.push_back(std::move(reference));
                            }
                        };
                        addResolvedPackageRef(PackageReference{.name = feature.packageName});
                        const auto packageIt =
                            std::find_if(resolved.orderedPackages.begin(), resolved.orderedPackages.end(),
                                         [&](const ResolvedPackage &package) {
                                             return package.manifest.name == feature.packageName;
                                         });
                        if (packageIt == resolved.orderedPackages.end())
                        {
                            continue;
                        }
                        for (const auto &reference : feature.packageRefs)
                        {
                            if (SelectionMatches(packageIt->manifest.conditions, reference.selectors, unit.profile))
                            {
                                addResolvedPackageRef(reference);
                            }
                        }
                    }
                }
                for (const auto &packageRef : packageRefs)
                {
                    const auto packageIt = std::find_if(
                        resolved.orderedPackages.begin(), resolved.orderedPackages.end(),
                        [&](const ResolvedPackage &package) { return package.manifest.name == packageRef.name; });
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

                std::vector<ProjectReference> projectRefs{};
                for (const auto &reference : unit.project.projectRefs)
                {
                    if (SelectionMatches(unit.project, reference.selectors, unit.profile))
                    {
                        projectRefs.push_back(reference);
                    }
                }
                if (unit.environment.has_value())
                {
                    for (const auto &reference : unit.environment->projectRefs)
                    {
                        if (SelectionMatches(unit.project, reference.selectors, unit.profile))
                        {
                            projectRefs.push_back(reference);
                        }
                    }
                }
                for (const auto &reference : unit.profile.projectRefs)
                {
                    if (SelectionMatches(unit.project, reference.selectors, unit.profile))
                    {
                        projectRefs.push_back(reference);
                    }
                }
                for (const auto &projectRef : projectRefs)
                {
                    const auto canonical = fs::weakly_canonical(projectRef.path).string();
                    const auto refIt = projectByPath.find(canonical);
                    if (refIt == projectByPath.end())
                    {
                        AddError(report, "project '" + unit.project.name + "' references unknown project '" +
                                             projectRef.path.string() + "'");
                        continue;
                    }
                    const auto *referencedUnit = refIt->second;
                    const auto referencedKind = Lower(referencedUnit->project.output.kind);
                    if (referencedKind != "staticlibrary" && referencedKind != "sharedlibrary")
                    {
                        AddError(report, "project '" + unit.project.name + "' references non-library project '" +
                                             referencedUnit->project.name + "'");
                        continue;
                    }
                    out << "target_link_libraries(\"" << EscapeCMake(targetName) << "\" " << linkVisibility << " \""
                        << EscapeCMake(referencedUnit->project.output.target) << "\")\n";
                }
            }
            if (report.HasErrors())
            {
                return false;
            }

            out << "add_custom_target(ngin_stage_artifacts)\n";

            auto emitStageTarget = [&](const std::string &artifactName, const std::string &targetName,
                                       const std::string &subdir, const bool copyFile) {
                const auto safeName = SanitizeIdentifier(artifactName);
                out << "if(NOT TARGET \"" << EscapeCMake(targetName) << "\")\n";
                out << "  message(FATAL_ERROR \"required build target '" << EscapeCMake(targetName)
                    << "' is not available\")\n";
                out << "endif()\n";
                out << "add_custom_target(stage_" << safeName;
                if (copyFile)
                {
                    out << "\n"
                        << "  COMMAND ${CMAKE_COMMAND} -E make_directory \"" << ToCMakePath(outputDir / subdir)
                        << "\"\n"
                        << "  COMMAND ${CMAKE_COMMAND} -E copy_if_different \"$<TARGET_FILE:" << targetName << ">\" \""
                        << ToCMakePath(outputDir / subdir) << "/$<TARGET_FILE_NAME:" << targetName << ">\"\n";
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
                const auto origin = Lower(library.origin);
                emitStageTarget(library.name, library.target, "lib",
                                Lower(library.linkage) != "interface" && origin != "imported");
            }
            if (resolved.selectedExecutable.has_value() && !resolved.selectedExecutable->target.empty() &&
                Lower(resolved.selectedExecutable->origin) != "prebuilt")
            {
                emitStageTarget(resolved.selectedExecutable->name, resolved.selectedExecutable->target, "bin", true);
            }

            return WriteTextFileIfChanged(generatedSourceDir / "CMakeLists.txt", content.str());
        }

        [[nodiscard]] auto RunBackendProcess(std::string name, const fs::path &executable,
                                             const std::vector<std::string> &arguments,
                                             const std::optional<fs::path> &workingDirectory,
                                             const BuildExecutionOptions &options) -> int
        {
            const auto started = std::chrono::steady_clock::now();
            const auto phase =
                Lower(name).find("configure") != std::string::npos ? std::string{"configure"} : std::string{"build"};
            if (options.events != nullptr)
            {
                options.events->Emit(CliEventType::PhaseStarted,
                                     EventData{}.AddString("phase", phase).AddString("label", name));
            }
            ProcessResult result{};
            bool emittedStreamOutput = false;
            if (options.backendOutput == BackendOutputMode::Stream && options.events == nullptr)
            {
                result.exitCode = RunProcess(executable, arguments, workingDirectory);
            }
            else
            {
                std::atomic_bool finished{false};
                std::thread progressThread{};
                if (options.interactiveProgress)
                {
                    progressThread = std::thread([&finished, &name, started]() {
                        constexpr std::array<char, 4> spinner{'|', '/', '-', '\\'};
                        std::size_t index = 0;
                        while (!finished.load(std::memory_order_relaxed))
                        {
                            const auto now = std::chrono::steady_clock::now();
                            const auto seconds =
                                std::chrono::duration_cast<std::chrono::seconds>(now - started).count();
                            std::cout << "\r  " << spinner[index++ % spinner.size()] << " " << name << " running "
                                      << seconds << "s" << std::flush;
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        }
                    });
                }
                try
                {
                    const auto callback =
                        options.events != nullptr && options.backendOutput == BackendOutputMode::Stream
                            ? std::function<void(std::string_view)>{[&](std::string_view text) {
                                  emittedStreamOutput = true;
                                  options.events->Emit(CliEventType::BackendOutput,
                                                       EventData{}
                                                           .AddString("phase", phase)
                                                           .AddString("stream", "combined")
                                                           .AddString("text", std::string{text}));
                              }}
                            : std::function<void(std::string_view)>{};
                    result = RunProcessCapture(executable, arguments, workingDirectory, callback);
                }
                catch (...)
                {
                    finished.store(true, std::memory_order_relaxed);
                    if (progressThread.joinable())
                    {
                        progressThread.join();
                        std::cout << "\r\033[2K" << std::flush;
                    }
                    throw;
                }
                finished.store(true, std::memory_order_relaxed);
                if (progressThread.joinable())
                {
                    progressThread.join();
                    std::cout << "\r\033[2K" << std::flush;
                }
            }
            const auto finished = std::chrono::steady_clock::now();
            const auto duration =
                static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count());
            if (options.events != nullptr && result.exitCode == 0 &&
                options.backendOutput == BackendOutputMode::Stream && !emittedStreamOutput && !result.output.empty())
            {
                options.events->Emit(CliEventType::BackendOutput, EventData{}
                                                                      .AddString("phase", phase)
                                                                      .AddString("stream", "combined")
                                                                      .AddString("text", result.output));
            }
            if (options.events != nullptr)
            {
                EventData data{};
                data.AddString("phase", phase).AddString("label", name).AddNumber("durationMs", duration);
                if (result.exitCode == 0)
                {
                    options.events->Emit(CliEventType::PhaseCompleted, std::move(data));
                }
                else
                {
                    data.AddNumber("exitCode", result.exitCode);
                    options.events->Emit(CliEventType::PhaseFailed, std::move(data));
                    if (options.backendOutput != BackendOutputMode::Silent &&
                        options.backendOutput != BackendOutputMode::Stream && !result.output.empty())
                    {
                        options.events->Emit(CliEventType::BackendOutput, EventData{}
                                                                              .AddString("phase", phase)
                                                                              .AddString("stream", "combined")
                                                                              .AddString("text", result.output));
                    }
                }
            }
            if (options.backendSteps != nullptr)
            {
                options.backendSteps->push_back(BackendStepResult{
                    .name = std::move(name),
                    .exitCode = result.exitCode,
                    .durationMilliseconds = duration,
                    .output =
                        options.backendOutput == BackendOutputMode::Silent ? std::string{} : std::move(result.output),
                });
            }
            return result.exitCode;
        }

        struct CMakeProviderInputs
        {
            fs::path vcpkgToolchainFile{};
            std::vector<fs::path> prefixPaths{};
        };

        [[nodiscard]] auto CollectCMakeProviderInputs(const ResolvedLaunch &resolved, DiagnosticReport &report)
            -> CMakeProviderInputs
        {
            CMakeProviderInputs inputs{};
            std::set<std::string> loadedProviders{};
            for (const auto &package : resolved.orderedPackages)
            {
                const auto &providerName = package.manifest.build.provider;
                if (providerName.empty() || !loadedProviders.insert(providerName).second)
                {
                    continue;
                }
                const auto metadata = LoadProviderRestoreMetadata(resolved, providerName, report);
                if (!metadata.has_value())
                {
                    continue;
                }
                if (metadata->kind == "Vcpkg")
                {
                    if (metadata->toolchainFile.empty())
                    {
                        AddError(report, "vcpkg provider '" + providerName +
                                             "' restore metadata does not declare ToolchainFile");
                        continue;
                    }
                    if (!inputs.vcpkgToolchainFile.empty() && inputs.vcpkgToolchainFile != metadata->toolchainFile)
                    {
                        AddError(report, "multiple vcpkg toolchain files are not supported in one generated build");
                        continue;
                    }
                    inputs.vcpkgToolchainFile = metadata->toolchainFile;
                    continue;
                }
                if (metadata->kind == "Conan")
                {
                    if (metadata->prefixPath.empty())
                    {
                        AddError(report, "Conan provider '" + providerName +
                                             "' restore metadata does not declare PrefixPath");
                        continue;
                    }
                    inputs.prefixPaths.push_back(metadata->prefixPath);
                    continue;
                }
                AddError(report, "package provider '" + providerName + "' has unsupported restore kind '" +
                                     metadata->kind + "'");
            }
            return inputs;
        }

        [[nodiscard]] auto ConfigureGeneratedBuild(const ResolvedLaunch &resolved, const fs::path &outputDir,
                                                   DiagnosticReport &report, const BuildExecutionOptions &options)
            -> std::optional<GeneratedBuildPaths>
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

            const auto buildType = resolved.profile.buildType.empty() ? "Debug" : resolved.profile.buildType;
            const auto cmakeCachePath = generatedPaths.buildDir / "CMakeCache.txt";
            const auto compileCommandsPath = generatedPaths.buildDir / "compile_commands.json";
            const auto toolSearchRoot = BuildToolSearchRoot(resolved);
            const auto cmakeTool = ResolveToolPath("cmake", toolSearchRoot);
            if (!cmakeTool.has_value())
            {
                AddError(report, "missing tool: cmake. Install CMake, set NGIN_CMAKE, or "
                                 "fetch bundled tools into Tools/ThirdParty/BuildTools.");
                return std::nullopt;
            }
            if (cmakeProjectChanged || !fs::exists(cmakeCachePath) || !fs::exists(compileCommandsPath))
            {
                const auto providerInputs = CollectCMakeProviderInputs(resolved, report);
                if (report.HasErrors())
                {
                    return std::nullopt;
                }
                std::vector<std::string> configureArguments{
                    "-S",
                    generatedPaths.sourceDir.string(),
                    "-B",
                    generatedPaths.buildDir.string(),
                    "-DCMAKE_BUILD_TYPE=" + buildType,
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                };
                if (!providerInputs.vcpkgToolchainFile.empty())
                {
                    configureArguments.push_back("-DCMAKE_TOOLCHAIN_FILE=" + providerInputs.vcpkgToolchainFile.string());
                }
                if (!providerInputs.prefixPaths.empty())
                {
                    std::ostringstream prefixPath{};
                    for (std::size_t index = 0; index < providerInputs.prefixPaths.size(); ++index)
                    {
                        if (index != 0)
                        {
                            prefixPath << ";";
                        }
                        prefixPath << providerInputs.prefixPaths[index].string();
                    }
                    configureArguments.push_back("-DCMAKE_PREFIX_PATH=" + prefixPath.str());
                }
                if (const auto ninjaPath = ResolveToolPath("ninja", toolSearchRoot).or_else([&toolSearchRoot]() {
                        return ResolveToolPath("ninja-build", toolSearchRoot);
                    });
                    ninjaPath.has_value())
                {
                    configureArguments.push_back("-G");
                    configureArguments.push_back("Ninja");
                    configureArguments.push_back("-DCMAKE_MAKE_PROGRAM=" + ninjaPath->path.string());
                }
                if (RunBackendProcess("CMake configure", cmakeTool->path, configureArguments, std::nullopt, options) !=
                    0)
                {
                    AddError(report, "failed to configure generated CMake build project for profile '" +
                                         resolved.profile.name + "' with build type '" + buildType + "'");
                    return std::nullopt;
                }
            }
            return generatedPaths;
        }

        auto BuildArtifacts(const ResolvedLaunch &resolved, const fs::path &outputDir, DiagnosticReport &report,
                            const BuildExecutionOptions &options) -> void
        {
            const auto generatedPaths = ConfigureGeneratedBuild(resolved, outputDir, report, options);
            if (!generatedPaths.has_value() || report.HasErrors())
            {
                return;
            }

            const auto buildType = resolved.profile.buildType.empty() ? "Debug" : resolved.profile.buildType;
            const auto toolSearchRoot = BuildToolSearchRoot(resolved);
            const auto cmakeTool = ResolveToolPath("cmake", toolSearchRoot);
            if (!cmakeTool.has_value())
            {
                AddError(report, "missing tool: cmake. Install CMake, set NGIN_CMAKE, or "
                                 "fetch bundled tools into Tools/ThirdParty/BuildTools.");
                return;
            }
            if (RunBackendProcess("CMake build", cmakeTool->path,
                                  {
                                      "--build",
                                      generatedPaths->buildDir.string(),
                                      "--config",
                                      buildType,
                                      "--target",
                                      "ngin_stage_artifacts",
                                  },
                                  std::nullopt, options) != 0)
            {
                AddError(report, "failed to build or stage artifacts for profile '" + resolved.profile.name +
                                     "' with build type '" + buildType + "'");
            }
        }

        auto CollectBuiltArtifactFiles(const fs::path &outputDir, std::map<fs::path, std::string> &collisions,
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
    } // namespace

    auto ResolveToolPath(const std::string &tool, const std::optional<fs::path> &searchRoot)
        -> std::optional<ToolResolution>
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

    auto ToolExists(const std::string &tool, const std::optional<fs::path> &searchRoot) -> bool
    {
        return ResolveToolPath(tool, searchRoot).has_value();
    }

    auto RunProcess(const fs::path &executable, const std::vector<std::string> &arguments,
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
        if (!CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr, FALSE, 0, nullptr, workingDirectoryPtr,
                            &startupInfo, &processInfo))
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

    auto RunProcessCapture(const fs::path &executable, const std::vector<std::string> &arguments,
                           const std::optional<fs::path> &workingDirectory,
                           const std::function<void(std::string_view)> &outputCallback) -> ProcessResult
    {
#if defined(_WIN32)
        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
        {
            throw std::runtime_error("failed to create process output pipe");
        }
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

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
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdOutput = writePipe;
        startupInfo.hStdError = writePipe;
        startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr, TRUE, 0, nullptr, workingDirectoryPtr,
                            &startupInfo, &processInfo))
        {
            CloseHandle(readPipe);
            CloseHandle(writePipe);
            throw std::runtime_error("failed to start process '" + executable.string() + "'");
        }

        CloseHandle(writePipe);
        std::string output{};
        std::array<char, 4096> buffer{};
        DWORD bytesRead = 0;
        while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) &&
               bytesRead > 0)
        {
            output.append(buffer.data(), bytesRead);
            if (outputCallback)
            {
                outputCallback(std::string_view{buffer.data(), static_cast<std::size_t>(bytesRead)});
            }
        }
        CloseHandle(readPipe);

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
        return ProcessResult{.exitCode = static_cast<int>(exitCode), .output = std::move(output)};
#else
        int pipeFd[2]{};
        if (::pipe(pipeFd) != 0)
        {
            throw std::runtime_error("failed to create process output pipe");
        }

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
            ::close(pipeFd[0]);
            ::close(pipeFd[1]);
            throw std::runtime_error("failed to fork process '" + executable.string() + "'");
        }

        if (processId == 0)
        {
            ::close(pipeFd[0]);
            ::dup2(pipeFd[1], STDOUT_FILENO);
            ::dup2(pipeFd[1], STDERR_FILENO);
            ::close(pipeFd[1]);
            if (workingDirectory.has_value() && ::chdir(workingDirectory->c_str()) != 0)
            {
                std::_Exit(127);
            }
            ::execvp(executable.c_str(), argv.data());
            std::_Exit(errno == ENOENT ? 127 : 126);
        }

        ::close(pipeFd[1]);
        std::string output{};
        std::array<char, 4096> buffer{};
        while (true)
        {
            const auto bytesRead = ::read(pipeFd[0], buffer.data(), buffer.size());
            if (bytesRead > 0)
            {
                output.append(buffer.data(), static_cast<std::size_t>(bytesRead));
                if (outputCallback)
                {
                    outputCallback(std::string_view{buffer.data(), static_cast<std::size_t>(bytesRead)});
                }
                continue;
            }
            if (bytesRead < 0 && errno == EINTR)
            {
                continue;
            }
            break;
        }
        ::close(pipeFd[0]);

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
            return ProcessResult{.exitCode = WEXITSTATUS(status), .output = std::move(output)};
        }
        if (WIFSIGNALED(status))
        {
            return ProcessResult{.exitCode = 128 + WTERMSIG(status), .output = std::move(output)};
        }
        return ProcessResult{.exitCode = 1, .output = std::move(output)};
#endif
    }

    auto WriteLaunchManifest(const ResolvedLaunch &resolved, const fs::path &outputDir,
                             const std::vector<std::tuple<std::string, fs::path, fs::path>> &staged) -> fs::path
    {
        const auto manifestPath = outputDir / (resolved.project.name + "." + resolved.profile.name + ".nginlaunch");
        std::ofstream out(manifestPath);
        out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        out << "<LaunchManifest SchemaVersion=\"3\" Profile=\"" << EscapeXml(resolved.profile.name) << "\" Project=\""
            << EscapeXml(resolved.project.name) << "\" Type=\"" << EscapeXml(resolved.project.type) << "\" BuildType=\""
            << EscapeXml(resolved.profile.buildType) << "\" Platform=\"" << EscapeXml(resolved.profile.platform)
            << "\" OperatingSystem=\"" << EscapeXml(resolved.profile.operatingSystem) << "\" Architecture=\""
            << EscapeXml(resolved.profile.architecture) << "\">\n";
        out << "  <Launch";
        if (resolved.selectedExecutable.has_value())
        {
            out << " Executable=\"" << EscapeXml(resolved.selectedExecutable->name) << "\" Target=\""
                << EscapeXml(resolved.selectedExecutable->target) << "\" Origin=\""
                << EscapeXml(resolved.selectedExecutable->origin) << "\"";
        }
        if (!resolved.profile.launch.name.empty())
        {
            out << " Name=\"" << EscapeXml(resolved.profile.launch.name) << "\"";
        }
        out << " WorkingDirectory=\"" << EscapeXml(resolved.profile.launch.workingDirectory) << "\"";
        if (!resolved.profile.launch.args.empty())
        {
            out << " Args=\"" << EscapeXml(resolved.profile.launch.args) << "\"";
        }
        out << " />\n";
        out << "  <Environment Name=\"" << EscapeXml(resolved.profile.environmentName) << "\">\n";
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
            out << "      <Feature Name=\"" << EscapeXml(feature.name) << "\" Enabled=\""
                << (feature.enabled ? "true" : "false") << "\" />\n";
        }
        out << "    </Features>\n";
        out << "  </Environment>\n";
        out << "  <Inputs>\n";
        for (const auto &input : resolved.inputs)
        {
            out << "    <Input Kind=\"" << EscapeXml(input.kind) << "\" Path=\"" << EscapeXml(input.source)
                << "\" OwnerKind=\"" << EscapeXml(input.ownerKind) << "\" Owner=\"" << EscapeXml(input.ownerName)
                << "\"";
            if (!input.name.empty())
            {
                out << " Name=\"" << EscapeXml(input.name) << "\"";
            }
            if (!input.role.empty())
            {
                out << " Role=\"" << EscapeXml(input.role) << "\"";
            }
            if (!input.mode.empty())
            {
                out << " Mode=\"" << EscapeXml(input.mode) << "\"";
            }
            if (!input.visibility.empty())
            {
                out << " Visibility=\"" << EscapeXml(input.visibility) << "\"";
            }
            if (!input.target.empty())
            {
                out << " Target=\"" << EscapeXml(input.target) << "\"";
            }
            if (!input.targetRoot.empty())
            {
                out << " TargetRoot=\"" << EscapeXml(input.targetRoot) << "\"";
            }
            if (!input.basePath.empty())
            {
                out << " BasePath=\"" << EscapeXml(input.basePath) << "\"";
            }
            if (!input.contentKind.empty())
            {
                out << " ContentKind=\"" << EscapeXml(input.contentKind) << "\"";
            }
            if (!input.stagedRelativePath.empty())
            {
                out << " Destination=\"" << EscapeXml(input.stagedRelativePath.generic_string()) << "\"";
            }
            out << " />\n";
        }
        out << "  </Inputs>\n";
        out << "  <Bootstraps>\n";
        for (const auto &bootstrap : resolved.bootstraps)
        {
            out << "    <Bootstrap Package=\"" << EscapeXml(bootstrap.packageName) << "\" Mode=\""
                << EscapeXml(bootstrap.mode) << "\" EntryPoint=\"" << EscapeXml(bootstrap.entryPoint)
                << "\" AutoApply=\"" << (bootstrap.autoApply ? "true" : "false") << "\" />\n";
        }
        out << "  </Bootstraps>\n";
        out << "  <Packages>\n";
        for (const auto &package : resolved.orderedPackages)
        {
            out << "    <Package Name=\"" << EscapeXml(package.manifest.name) << "\" Version=\""
                << EscapeXml(package.manifest.version) << "\" Source=\"" << EscapeXml(package.source) << "\" />\n";
        }
        out << "  </Packages>\n";
        out << "  <Artifacts>\n";
        out << "    <Libraries>\n";
        for (const auto &library : resolved.libraries)
        {
            out << "      <Library Name=\"" << EscapeXml(library.name) << "\" Target=\"" << EscapeXml(library.target)
                << "\" Linkage=\"" << EscapeXml(library.linkage) << "\" Origin=\"" << EscapeXml(library.origin)
                << "\" />\n";
        }
        out << "    </Libraries>\n";
        out << "    <Executables>\n";
        for (const auto &executable : resolved.executables)
        {
            out << "      <Executable Name=\"" << EscapeXml(executable.name) << "\" Target=\""
                << EscapeXml(executable.target) << "\" Origin=\"" << EscapeXml(executable.origin) << "\" />\n";
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
            out << "    <File Kind=\"" << EscapeXml(kind) << "\" Source=\"" << EscapeXml(source.string())
                << "\" Destination=\"" << EscapeXml(destination.generic_string()) << "\" RelativeDestination=\""
                << EscapeXml(fs::relative(destination, outputDir).generic_string()) << "\" />\n";
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
                        if (const auto relativeDestination = Attribute(*file, "RelativeDestination");
                            relativeDestination.has_value() && !relativeDestination->empty())
                        {
                            stagedPath = outputDir / fs::path(*relativeDestination);
                        }
                        else if (const auto destination = Attribute(*file, "Destination");
                                 destination.has_value() && !destination->empty())
                        {
                            stagedPath = fs::path(*destination);
                        }
                        else
                        {
                            continue;
                        }

                        if (!IsPathWithinDirectory(stagedPath, outputDir))
                        {
                            AddWarning(report, "skipped cleanup for staged file outside output directory: '" +
                                                   stagedPath.string() + "'");
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
                AddWarning(report,
                           "failed to clean previous launch manifest '" + manifestPath.string() + "': " + ex.what());
            }
        }
    }

    auto CleanLaunch(const ProjectManifest &project, const ProfileDefinition &profile,
                     const std::optional<fs::path> &outputPath) -> DiagnosticResult<fs::path>
    {
        DiagnosticResult<fs::path> result{};

        if (!IsSupportedBuildType(profile.buildType))
        {
            AddError(result.diagnostics, "unsupported build type '" + profile.buildType + "' in profile '" +
                                             profile.name +
                                             "'. Expected one of: Debug, Release, RelWithDebInfo, MinSizeRel");
            return result;
        }

        const auto resolved = ResolveLaunch(project, profile);
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
        fs::remove_all(ResolveGeneratedDir(*resolved.value, resolvedOutputDir), error);
        PruneEmptyDirectories(cacheRoot, resolvedOutputDir);

        result.value = resolvedOutputDir;
        return result;
    }

    auto ConfigureLaunch(const ProjectManifest &project, const ProfileDefinition &profile,
                         const std::optional<fs::path> &outputPath, const BuildExecutionOptions &options)
        -> DiagnosticResult<ConfiguredBuildPaths>
    {
        DiagnosticResult<ConfiguredBuildPaths> result{};

        if (!IsSupportedBuildType(profile.buildType))
        {
            AddError(result.diagnostics, "unsupported build type '" + profile.buildType + "' in profile '" +
                                             profile.name +
                                             "'. Expected one of: Debug, Release, RelWithDebInfo, MinSizeRel");
            return result;
        }

        auto resolvedResult = ResolveLaunch(project, profile);
        AppendDiagnostics(result.diagnostics, resolvedResult.diagnostics);
        if (!resolvedResult.value.has_value() || result.diagnostics.HasErrors())
        {
            return result;
        }

        auto resolved = std::move(*resolvedResult.value);
        const auto resolvedOutputDir = ResolveOutputDir(resolved, outputPath);
        fs::create_directories(resolvedOutputDir);
        ExecuteGenerators(resolved, resolvedOutputDir, result.diagnostics, options);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        ConfiguredBuildPaths configured{
            .outputDir = resolvedOutputDir,
        };

        const auto generatedPaths = ConfigureGeneratedBuild(resolved, resolvedOutputDir, result.diagnostics, options);
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

    auto BuildLaunch(const ProjectManifest &project, const ProfileDefinition &profile,
                     const std::optional<fs::path> &outputPath, const BuildExecutionOptions &options)
        -> DiagnosticResult<GeneratedLaunchPaths>
    {
        DiagnosticResult<GeneratedLaunchPaths> result{};

        if (!IsSupportedBuildType(profile.buildType))
        {
            AddError(result.diagnostics, "unsupported build type '" + profile.buildType + "' in profile '" +
                                             profile.name +
                                             "'. Expected one of: Debug, Release, RelWithDebInfo, MinSizeRel");
            return result;
        }

        auto resolvedResult = ResolveLaunch(project, profile);
        AppendDiagnostics(result.diagnostics, resolvedResult.diagnostics);
        if (!resolvedResult.value.has_value() || result.diagnostics.HasErrors())
        {
            return result;
        }

        auto resolved = std::move(*resolvedResult.value);
        const auto resolvedOutputDir = ResolveOutputDir(resolved, outputPath);

        CleanupPreviousStage(resolvedOutputDir, result.diagnostics);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        fs::create_directories(resolvedOutputDir);
        std::map<fs::path, std::string> collisions{};
        std::vector<std::tuple<std::string, fs::path, fs::path>> staged{};

        ExecuteGenerators(resolved, resolvedOutputDir, result.diagnostics, options);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        BuildArtifacts(resolved, resolvedOutputDir, result.diagnostics, options);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        CollectBuiltArtifactFiles(resolvedOutputDir, collisions, result.diagnostics, staged);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        for (const auto &content : resolved.inputs)
        {
            if (!IsStagedResolvedInput(content))
            {
                continue;
            }
            const auto source = content.absoluteSourcePath;
            if (!fs::exists(source))
            {
                AddError(result.diagnostics, "missing " + content.kind + " input '" + content.source +
                                                 "' declared by " + content.ownerKind + " '" + content.ownerName + "'");
                continue;
            }
            const auto dest = resolvedOutputDir / content.stagedRelativePath;
            if (collisions.contains(dest))
            {
                AddError(result.diagnostics, "build output collision at input '" + content.stagedRelativePath.generic_string() +
                                                 "' declared by " + content.ownerKind + " '" + content.ownerName + "'");
                continue;
            }
            collisions[dest] = content.ownerKind + ":" + content.ownerName;
            fs::create_directories(dest.parent_path());
            fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
            staged.emplace_back(StagedResolvedInputKind(content), source, dest);
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        result.value = GeneratedLaunchPaths{
            .outputDir = resolvedOutputDir,
            .manifestPath = WriteLaunchManifest(resolved, resolvedOutputDir, staged),
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
        summary.profileName = Attribute(*rootElement, "Profile").value_or("");
        if (summary.profileName.empty())
        {
            throw std::runtime_error(manifestPath.string() + ": launch manifest missing Profile");
        }
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
} // namespace NGIN::CLI
