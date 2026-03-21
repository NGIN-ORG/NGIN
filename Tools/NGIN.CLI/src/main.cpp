// NGIN native CLI: XML manifests, package-first composition, no lockfile.
#include <NGIN/Serialization/Core/ParseError.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>

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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    using NGIN::Serialization::ParseError;
    using NGIN::Serialization::XmlDocument;
    using NGIN::Serialization::XmlElement;
    using NGIN::Serialization::XmlNode;
    using NGIN::Serialization::XmlParseOptions;
    using NGIN::Serialization::XmlParser;

    struct IssueReport
    {
        std::vector<std::string> errors{};
        std::vector<std::string> warnings{};
    };

    struct LoadedXml
    {
        std::string text{};
        XmlDocument document{0};
    };

    auto AddError(IssueReport &report, std::string message) -> void
    {
        report.errors.push_back(std::move(message));
    }

    auto AddWarning(IssueReport &report, std::string message) -> void
    {
        report.warnings.push_back(std::move(message));
    }

    [[nodiscard]] auto ReadText(const fs::path &path) -> std::string
    {
        std::ifstream input(path);
        if (!input)
        {
            throw std::runtime_error("failed to open file: " + path.string());
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    [[nodiscard]] auto ToString(const ParseError &error) -> std::string
    {
        return std::string(error.message.Data(), error.message.Size());
    }

    [[nodiscard]] auto LoadXml(const fs::path &path) -> LoadedXml
    {
        LoadedXml loaded{};
        loaded.text = ReadText(path);
        XmlParseOptions options{};
        options.decodeEntities = true;
        options.arenaBytes = std::max<NGIN::UIntSize>(16384, static_cast<NGIN::UIntSize>(loaded.text.size() * 8 + 4096));
        auto parsed = XmlParser::Parse(loaded.text, options);
        if (!parsed.HasValue())
        {
            throw std::runtime_error(path.string() + ": failed to parse XML: " + ToString(parsed.ErrorUnsafe()));
        }
        loaded.document = std::move(parsed.ValueUnsafe());
        return loaded;
    }

    [[nodiscard]] auto ChildElements(const XmlElement &node, std::string_view name = {}) -> std::vector<const XmlElement *>
    {
        std::vector<const XmlElement *> out;
        out.reserve(static_cast<std::size_t>(node.children.Size()));
        for (NGIN::UIntSize index = 0; index < node.children.Size(); ++index)
        {
            const auto &child = node.children[index];
            if (child.type != XmlNode::Type::Element || child.element == nullptr)
            {
                continue;
            }
            if (name.empty() || child.element->name == name)
            {
                out.push_back(child.element);
            }
        }
        return out;
    }

    [[nodiscard]] auto FindChild(const XmlElement &node, std::string_view name) -> const XmlElement *
    {
        for (NGIN::UIntSize index = 0; index < node.children.Size(); ++index)
        {
            const auto &child = node.children[index];
            if (child.type == XmlNode::Type::Element && child.element != nullptr && child.element->name == name)
            {
                return child.element;
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto Attribute(const XmlElement &node, std::string_view key) -> std::optional<std::string>
    {
        const auto *attr = node.FindAttribute(key);
        if (attr == nullptr)
        {
            return std::nullopt;
        }
        return std::string(attr->value);
    }

    [[nodiscard]] auto RequireAttribute(const XmlElement &node, std::string_view key, const fs::path &path) -> std::string
    {
        const auto value = Attribute(node, key);
        if (!value.has_value())
        {
            throw std::runtime_error(path.string() + ": missing required attribute '" + std::string(key) + "'");
        }
        return *value;
    }

    [[nodiscard]] auto BoolAttribute(const XmlElement &node, std::string_view key, bool defaultValue = false) -> bool
    {
        const auto value = Attribute(node, key);
        if (!value.has_value())
        {
            return defaultValue;
        }
        return *value == "true" || *value == "1" || *value == "yes";
    }

    [[nodiscard]] auto Lower(std::string value) -> std::string
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    [[nodiscard]] auto IsValidStartupStage(const std::string_view value) -> bool
    {
        return value == "Foundation" || value == "Platform" || value == "Services" || value == "Features" || value == "Presentation";
    }

    [[nodiscard]] auto StartupStageRank(const std::string_view value) -> int
    {
        if (value == "Foundation")
        {
            return 0;
        }
        if (value == "Platform")
        {
            return 1;
        }
        if (value == "Services")
        {
            return 2;
        }
        if (value == "Features")
        {
            return 3;
        }
        if (value == "Presentation")
        {
            return 4;
        }
        return 99;
    }

    [[nodiscard]] auto IsValidModuleFamily(const std::string_view value) -> bool
    {
        return value == "Base" || value == "Reflection" || value == "Core" || value == "Platform" || value == "Editor" || value == "Domain" || value == "App";
    }

    [[nodiscard]] auto IsValidHostProfile(const std::string_view value) -> bool
    {
        return value == "ConsoleApp" || value == "GuiApp" || value == "Game" || value == "Editor" || value == "Service" || value == "TestHost";
    }

    [[nodiscard]] auto IsSupportedBuildConfiguration(const std::string_view value) -> bool
    {
        return value == "Debug" || value == "Release" || value == "RelWithDebInfo" || value == "MinSizeRel";
    }

    [[nodiscard]] auto IsSupportedBuildVisibility(const std::string_view value) -> bool
    {
        return value == "Private" || value == "Public" || value == "Interface";
    }

    [[nodiscard]] auto IsSupportedProjectBuildMode(const std::string_view value) -> bool
    {
        return value == "Generated" || value == "Manual";
    }

    [[nodiscard]] auto IsSupportedPackageBuildMode(const std::string_view value) -> bool
    {
        return value == "Manual" || value == "FindPackage" || value == "AddSubdirectory";
    }

    [[nodiscard]] auto ParseSupportedHosts(const XmlElement &node, const fs::path &path) -> std::vector<std::string>
    {
        std::vector<std::string> supportedHosts{};
        if (const auto *section = FindChild(node, "SupportedHosts"))
        {
            for (const auto *host : ChildElements(*section, "Host"))
            {
                auto hostName = RequireAttribute(*host, "Name", path);
                if (!IsValidHostProfile(hostName))
                {
                    throw std::runtime_error(path.string() + ": unknown supported host '" + hostName + "'");
                }
                supportedHosts.push_back(std::move(hostName));
            }
        }
        return supportedHosts;
    }

    [[nodiscard]] auto EscapeXml(std::string_view input) -> std::string
    {
        std::string out;
        out.reserve(input.size());
        for (const char ch : input)
        {
            switch (ch)
            {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out.push_back(ch);
                break;
            }
        }
        return out;
    }

    [[nodiscard]] auto EscapeCMake(std::string_view input) -> std::string
    {
        std::string out;
        out.reserve(input.size());
        for (const char ch : input)
        {
            if (ch == '\\' || ch == '"')
            {
                out.push_back('\\');
            }
            out.push_back(ch);
        }
        return out;
    }

    [[nodiscard]] auto ToCMakePath(const fs::path &path) -> std::string
    {
        return EscapeCMake(path.generic_string());
    }

    auto ValidateSchemaVersion(const XmlElement &node, const fs::path &path) -> void
    {
        const auto schemaVersion = RequireAttribute(node, "SchemaVersion", path);
        if (schemaVersion != "2")
        {
            throw std::runtime_error(path.string() + ": unsupported SchemaVersion '" + schemaVersion + "' (expected '2')");
        }
    }

    [[nodiscard]] auto ResolveStartupStage(const XmlElement &node, const std::string_view defaultStage) -> std::string
    {
        if (const auto startupStage = Attribute(node, "StartupStage"); startupStage.has_value() && !startupStage->empty())
        {
            return *startupStage;
        }
        return std::string(defaultStage);
    }

    [[nodiscard]] auto NormalizePath(const fs::path &path) -> fs::path
    {
        std::error_code error;
        const auto normalized = fs::weakly_canonical(path, error);
        if (!error)
        {
            return normalized.lexically_normal();
        }
        return fs::absolute(path).lexically_normal();
    }

    [[nodiscard]] auto IsPathWithinDirectory(const fs::path &candidate, const fs::path &directory) -> bool
    {
        const auto normalizedCandidate = NormalizePath(candidate);
        const auto normalizedDirectory = NormalizePath(directory);

        auto candidateIt = normalizedCandidate.begin();
        auto directoryIt = normalizedDirectory.begin();
        for (; directoryIt != normalizedDirectory.end() && candidateIt != normalizedCandidate.end(); ++directoryIt, ++candidateIt)
        {
            if (*candidateIt != *directoryIt)
            {
                return false;
            }
        }
        return directoryIt == normalizedDirectory.end();
    }

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

    [[nodiscard]] auto ToolExists(const std::string &tool) -> bool
    {
        if (tool.empty())
        {
            return false;
        }

        const fs::path toolPath{tool};
        if (toolPath.is_absolute() || HasPathSeparator(tool))
        {
            for (const auto &candidate : SearchExecutableCandidates(toolPath))
            {
                if (IsExecutableCandidate(candidate))
                {
                    return true;
                }
            }
            return false;
        }

        const auto *pathValue = std::getenv("PATH");
        if (pathValue == nullptr)
        {
            return false;
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
                    return true;
                }
            }
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }

        return false;
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

    [[nodiscard]] auto RunProcess(
        const fs::path &executable,
        const std::vector<std::string> &arguments,
        const std::optional<fs::path> &workingDirectory = std::nullopt) -> int
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

    struct WorkspaceManifest
    {
        fs::path path{};
        std::string name{};
        std::string platformVersion{};
        std::vector<fs::path> packageSources{};
        std::unordered_map<std::string, fs::path> packageProviders{};
        std::vector<fs::path> projects{};
    };

    struct PackageDependency
    {
        std::string name{};
        std::string versionRange{};
        bool optional{false};
    };

    struct ContentFile
    {
        std::string source{};
        std::string kind{};
        std::string target{};
    };
    struct PackageBootstrapDescriptor
    {
        std::string mode{"BuilderHookV1"};
        std::string entryPoint{};
        bool autoApply{false};
    };

    struct LibraryArtifact
    {
        std::string name{};
        std::string target{};
        std::string linkage{};
        std::string origin{};
        bool exported{true};
    };

    struct ExecutableArtifact
    {
        std::string name{};
        std::string target{};
        std::string origin{};
        bool exported{true};
    };

    struct ArtifactDescriptor
    {
        std::vector<LibraryArtifact> libraries{};
        std::vector<ExecutableArtifact> executables{};
    };

    struct BuildSetting
    {
        std::string value{};
        std::string visibility{"Private"};
    };

    struct BuildVariable
    {
        std::string name{};
        std::string value{};
    };

    struct PackageBuildDescriptor
    {
        std::string backend{};
        std::string mode{};
        std::vector<BuildVariable> options{};
    };

    struct ProjectBuildDescriptor
    {
        std::string backend{"CMake"};
        std::string mode{"Generated"};
        std::string language{"CXX"};
        std::string languageStandard{"23"};
        std::vector<std::string> sources{};
        std::vector<BuildSetting> includeDirectories{};
        std::vector<BuildSetting> compileDefinitions{};
        std::vector<BuildSetting> compileOptions{};
        std::vector<BuildSetting> linkOptions{};
    };

    struct ModuleDescriptor
    {
        std::string name{};
        std::string family{};
        std::string type{};
        std::string startupStage{};
        std::string version{};
        std::string compatiblePlatformRange{};
        std::vector<std::string> platforms{};
        std::vector<std::string> required{};
        std::vector<std::string> optional{};
        std::vector<std::string> providesServices{};
        std::vector<std::string> requiresServices{};
        std::vector<std::string> capabilities{};
        std::vector<std::string> supportedHosts{};
        bool requiresReflection{false};
    };

    auto ValidateModuleDescriptor(const ModuleDescriptor &module, const fs::path &path) -> void
    {
        if (!IsValidModuleFamily(module.family))
        {
            throw std::runtime_error(path.string() + ": unknown module family '" + module.family + "'");
        }
        if (!IsValidStartupStage(module.startupStage))
        {
            throw std::runtime_error(path.string() + ": unknown startup stage '" + module.startupStage + "'");
        }
    }

    struct PluginDescriptor
    {
        std::string name{};
        std::vector<std::string> platforms{};
        std::vector<std::string> requiredModules{};
        std::vector<std::string> optionalModules{};
        bool optional{false};
    };

    struct PackageManifest
    {
        fs::path path{};
        std::string name{};
        std::string version{};
        std::string compatiblePlatformRange{};
        ArtifactDescriptor artifacts{};
        PackageBuildDescriptor build{};
        std::vector<std::string> platforms{};
        std::vector<PackageDependency> dependencies{};
        std::optional<PackageBootstrapDescriptor> bootstrap{};
        std::vector<ContentFile> contents{};
        std::vector<ModuleDescriptor> modules{};
        std::vector<PluginDescriptor> plugins{};
    };

    struct ResolvedConfigSource
    {
        std::string ownerProjectName{};
        fs::path ownerProjectDirectory{};
        std::string source{};
        fs::path absoluteSourcePath{};
        fs::path stagedRelativePath{};
    };
    struct ResolvedBootstrap
    {
        std::string packageName{};
        std::string mode{};
        std::string entryPoint{};
        bool autoApply{false};
    };

    struct PackageCatalogEntry
    {
        std::string name{};
        fs::path manifestPath{};
        fs::path providerRoot{};
    };

    struct PackageReference
    {
        std::string name{};
        std::string versionRange{};
        bool optional{false};
    };

    struct ProjectReference
    {
        fs::path path{};
        std::optional<std::string> configuration{};
    };

    struct OutputDefinition
    {
        std::string kind{};
        std::string name{};
        std::string target{};
    };

    struct RuntimeDefinition
    {
        std::vector<ModuleDescriptor> modules{};
        std::vector<std::string> enableModules{};
        std::vector<std::string> disableModules{};
        std::vector<std::string> enablePlugins{};
        std::vector<std::string> disablePlugins{};
    };

    struct ConfigurationDefinition
    {
        std::string name{};
        std::string buildConfiguration{"Debug"};
        std::string hostProfile{};
        std::string platform{"linux-x64"};
        bool enableReflection{false};
        std::string environmentName{};
        std::string workingDirectory{"."};
        std::optional<std::string> launchExecutable{};
        std::vector<ProjectReference> projectRefs{};
        std::vector<PackageReference> packageRefs{};
        std::vector<std::string> configSources{};
        std::vector<std::string> enableModules{};
        std::vector<std::string> disableModules{};
        std::vector<std::string> enablePlugins{};
        std::vector<std::string> disablePlugins{};
    };

    struct ProjectManifest
    {
        fs::path path{};
        std::string name{};
        std::string type{};
        std::string defaultConfiguration{};
        std::string hostProfile{};
        std::vector<std::string> sourceRoots{};
        OutputDefinition output{};
        ProjectBuildDescriptor build{};
        std::vector<ProjectReference> projectRefs{};
        std::vector<PackageReference> packageRefs{};
        std::vector<std::string> configSources{};
        RuntimeDefinition runtime{};
        std::vector<ConfigurationDefinition> configurations{};
    };

    struct ResolvedProjectUnit
    {
        ProjectManifest project{};
        ConfigurationDefinition configuration{};
    };

    struct ResolvedPackage
    {
        PackageManifest manifest{};
        std::string source{"catalog"};
        fs::path sourceDirectory{};
    };

    struct ResolvedLaunch
    {
        std::optional<WorkspaceManifest> workspace{};
        ProjectManifest project{};
        ConfigurationDefinition configuration{};
        std::vector<ResolvedProjectUnit> projectUnits{};
        std::vector<ResolvedConfigSource> configSources{};
        std::vector<ResolvedBootstrap> bootstraps{};
        std::vector<ResolvedPackage> orderedPackages{};
        std::map<std::string, std::set<std::string>> packageEdges{};
        std::vector<std::string> requiredModules{};
        std::vector<std::string> optionalModules{};
        std::map<std::string, std::set<std::string>> dependencyEdges{};
        std::vector<std::string> enabledPlugins{};
        std::vector<LibraryArtifact> libraries{};
        std::vector<ExecutableArtifact> executables{};
        std::optional<ExecutableArtifact> selectedExecutable{};
    };

    [[nodiscard]] auto WorkspaceFilePath(const fs::path &root) -> std::optional<fs::path>
    {
        if (!fs::exists(root))
        {
            return std::nullopt;
        }
        std::vector<fs::path> candidates;
        for (const auto &entry : fs::directory_iterator(root))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".ngin")
            {
                candidates.push_back(entry.path());
            }
        }
        if (candidates.empty())
        {
            return std::nullopt;
        }
        std::sort(candidates.begin(), candidates.end());
        return candidates.front();
    }
    [[nodiscard]] auto IsValidPackageBootstrapMode(const std::string_view value) -> bool
    {
        return value == "BuilderHookV1";
    }

    [[nodiscard]] auto RootDirFrom(const fs::path &start) -> std::optional<fs::path>
    {
        auto current = fs::weakly_canonical(start);
        if (fs::is_regular_file(current))
        {
            current = current.parent_path();
        }
        while (!current.empty())
        {
            if (WorkspaceFilePath(current).has_value())
            {
                return current;
            }
            if (current == current.parent_path())
            {
                break;
            }
            current = current.parent_path();
        }
        return std::nullopt;
    }

    [[nodiscard]] auto RootDir(const char *argv0) -> fs::path
    {
        if (const auto fromExe = RootDirFrom(fs::absolute(argv0)); fromExe.has_value())
        {
            return *fromExe;
        }
        if (const auto fromCwd = RootDirFrom(fs::current_path()); fromCwd.has_value())
        {
            return *fromCwd;
        }
        return fs::current_path();
    }

    [[nodiscard]] auto PlatformAliases(const std::string &platform) -> std::set<std::string>
    {
        const auto lower = Lower(platform);
        std::set<std::string> out{lower};
        const auto dash = lower.find('-');
        const auto primary = dash == std::string::npos ? lower : lower.substr(0, dash);
        out.insert(primary);
        if (primary.rfind("win", 0) == 0)
        {
            out.insert("windows");
        }
        if (primary == "darwin")
        {
            out.insert("macos");
        }
        return out;
    }

    [[nodiscard]] auto PlatformSupported(const std::string &targetPlatform, const std::vector<std::string> &declaredPlatforms) -> bool
    {
        const auto aliases = PlatformAliases(targetPlatform);
        for (const auto &candidate : declaredPlatforms)
        {
            if (aliases.contains(Lower(candidate)))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto DefaultArtifactOrigin(const PackageManifest &manifest) -> std::string
    {
        const auto mode = Lower(manifest.build.mode);
        if (mode == "findpackage")
        {
            return "Imported";
        }
        return "Built";
    }

    [[nodiscard]] auto EffectiveArtifactOrigin(const std::string &explicitOrigin, const PackageManifest &manifest) -> std::string
    {
        if (!explicitOrigin.empty())
        {
            return explicitOrigin;
        }
        return DefaultArtifactOrigin(manifest);
    }

    [[nodiscard]] auto ParseSemver(const std::string &text) -> std::optional<std::array<int, 3>>
    {
        std::array<int, 3> parts{};
        std::stringstream ss(text.substr(0, text.find('-')));
        std::string token;
        for (int index = 0; index < 3; ++index)
        {
            if (!std::getline(ss, token, '.'))
            {
                return std::nullopt;
            }
            if (token.empty() || !std::all_of(token.begin(), token.end(), [](unsigned char c)
                                              { return std::isdigit(c); }))
            {
                return std::nullopt;
            }
            parts[index] = std::stoi(token);
        }
        return parts;
    }

    [[nodiscard]] auto CompareSemver(const std::string &left, const std::string &right) -> int
    {
        const auto a = ParseSemver(left);
        const auto b = ParseSemver(right);
        if (!a.has_value() || !b.has_value())
        {
            return left.compare(right);
        }
        for (int index = 0; index < 3; ++index)
        {
            if ((*a)[index] < (*b)[index])
            {
                return -1;
            }
            if ((*a)[index] > (*b)[index])
            {
                return 1;
            }
        }
        return 0;
    }

    [[nodiscard]] auto VersionSatisfies(const std::string &version, const std::string &rangeText) -> bool
    {
        if (rangeText.empty())
        {
            return true;
        }
        std::stringstream stream(rangeText);
        std::string token;
        while (stream >> token)
        {
            std::string op{"="};
            std::string rhs{token};
            if (token.rfind(">=", 0) == 0 || token.rfind("<=", 0) == 0)
            {
                op = token.substr(0, 2);
                rhs = token.substr(2);
            }
            else if (!token.empty() && (token[0] == '>' || token[0] == '<' || token[0] == '='))
            {
                op = token.substr(0, 1);
                rhs = token.substr(1);
            }
            const auto cmp = CompareSemver(version, rhs);
            if (op == "=" && cmp != 0)
            {
                return false;
            }
            if (op == ">" && cmp <= 0)
            {
                return false;
            }
            if (op == ">=" && cmp < 0)
            {
                return false;
            }
            if (op == "<" && cmp >= 0)
            {
                return false;
            }
            if (op == "<=" && cmp > 0)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto TopologicalDependenciesFirst(
        const std::set<std::string> &nodes,
        const std::map<std::string, std::set<std::string>> &dependencyEdges) -> std::optional<std::vector<std::string>>
    {
        std::map<std::string, int> indegree{};
        std::map<std::string, std::set<std::string>> dependents{};
        for (const auto &node : nodes)
        {
            indegree[node] = 0;
        }
        for (const auto &node : nodes)
        {
            const auto it = dependencyEdges.find(node);
            if (it == dependencyEdges.end())
            {
                continue;
            }
            for (const auto &dep : it->second)
            {
                if (nodes.contains(dep))
                {
                    ++indegree[node];
                    dependents[dep].insert(node);
                }
            }
        }

        std::vector<std::string> queue;
        for (const auto &[node, deg] : indegree)
        {
            if (deg == 0)
            {
                queue.push_back(node);
            }
        }
        std::sort(queue.begin(), queue.end());
        std::vector<std::string> ordered;
        while (!queue.empty())
        {
            const auto current = queue.front();
            queue.erase(queue.begin());
            ordered.push_back(current);
            for (const auto &dep : dependents[current])
            {
                --indegree[dep];
                if (indegree[dep] == 0)
                {
                    queue.push_back(dep);
                    std::sort(queue.begin(), queue.end());
                }
            }
        }

        if (ordered.size() != nodes.size())
        {
            return std::nullopt;
        }
        return ordered;
    }

    [[nodiscard]] auto DetectCycles(
        const std::set<std::string> &nodes,
        const std::map<std::string, std::set<std::string>> &dependencyEdges) -> std::vector<std::string>
    {
        auto ordered = TopologicalDependenciesFirst(nodes, dependencyEdges);
        if (ordered.has_value())
        {
            return {};
        }
        return std::vector<std::string>(nodes.begin(), nodes.end());
    }

    auto LoadPackageBuildDescriptor(PackageBuildDescriptor &build, const XmlElement *buildElement, const fs::path &path) -> void;
    [[nodiscard]] auto LoadPackageManifest(const fs::path &path) -> PackageManifest;

    [[nodiscard]] auto LoadWorkspaceManifest(const fs::path &root) -> WorkspaceManifest
    {
        const auto path = WorkspaceFilePath(root);
        if (!path.has_value())
        {
            throw std::runtime_error(root.string() + ": no .ngin workspace file found");
        }
        const auto doc = LoadXml(*path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Workspace")
        {
            throw std::runtime_error(path->string() + ": root element must be <Workspace>");
        }
        ValidateSchemaVersion(*rootElement, *path);

        WorkspaceManifest workspace{};
        workspace.path = fs::weakly_canonical(*path);
        workspace.name = RequireAttribute(*rootElement, "Name", *path);
        workspace.platformVersion = Attribute(*rootElement, "PlatformVersion").value_or("0.1.0");

        const auto *packageSourcesNode = FindChild(*rootElement, "PackageSources");
        if (packageSourcesNode == nullptr)
        {
            throw std::runtime_error(path->string() + ": missing <PackageSources>");
        }
        for (const auto *child : ChildElements(*packageSourcesNode, "PackageSource"))
        {
            workspace.packageSources.push_back((workspace.path.parent_path() / RequireAttribute(*child, "Path", *path)).lexically_normal());
        }
        if (const auto *providersNode = FindChild(*rootElement, "PackageProviders"))
        {
            for (const auto *child : ChildElements(*providersNode, "PackageProvider"))
            {
                const auto name = RequireAttribute(*child, "Name", *path);
                const auto providerRoot = (workspace.path.parent_path() / RequireAttribute(*child, "Root", *path)).lexically_normal();
                workspace.packageProviders[name] = providerRoot;
            }
        }

        const auto *projectsNode = FindChild(*rootElement, "Projects");
        if (projectsNode == nullptr)
        {
            throw std::runtime_error(path->string() + ": missing <Projects>");
        }
        for (const auto *node : ChildElements(*projectsNode, "Project"))
        {
            workspace.projects.push_back((workspace.path.parent_path() / RequireAttribute(*node, "Path", *path)).lexically_normal());
        }

        return workspace;
    }

    [[nodiscard]] auto TryLoadWorkspaceManifest(const fs::path &root) -> std::optional<WorkspaceManifest>
    {
        if (!WorkspaceFilePath(root).has_value())
        {
            return std::nullopt;
        }
        return LoadWorkspaceManifest(root);
    }

    [[nodiscard]] auto DiscoverPackageSourceRoots(const fs::path &start) -> std::vector<fs::path>
    {
        std::vector<fs::path> roots;
        std::set<fs::path> unique;
        auto current = fs::weakly_canonical(fs::is_regular_file(start) ? start.parent_path() : start);
        while (true)
        {
            const auto candidate = current / "Packages";
            if (fs::exists(candidate) && fs::is_directory(candidate))
            {
                const auto normalized = candidate.lexically_normal();
                if (unique.insert(normalized).second)
                {
                    roots.push_back(normalized);
                }
            }
            if (current == current.parent_path())
            {
                break;
            }
            current = current.parent_path();
        }
        return roots;
    }

    [[nodiscard]] auto LoadPackageCatalog(
        const std::optional<WorkspaceManifest> &workspace,
        const fs::path &projectPath) -> std::unordered_map<std::string, PackageCatalogEntry>
    {
        std::unordered_map<std::string, PackageCatalogEntry> out;
        const auto packageRoots = workspace.has_value() ? workspace->packageSources : DiscoverPackageSourceRoots(projectPath);
        for (const auto &packageRoot : packageRoots)
        {
            if (!fs::exists(packageRoot))
            {
                continue;
            }
            for (const auto &entry : fs::recursive_directory_iterator(packageRoot))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".nginpkg")
                {
                    continue;
                }
                const auto manifestPath = fs::weakly_canonical(entry.path());
                const auto manifest = LoadPackageManifest(manifestPath);
                fs::path providerRoot{};
                if (workspace.has_value())
                {
                    if (const auto provider = workspace->packageProviders.find(manifest.name); provider != workspace->packageProviders.end())
                    {
                        providerRoot = provider->second;
                    }
                }
                out.emplace(manifest.name, PackageCatalogEntry{
                                               .name = manifest.name,
                                               .manifestPath = manifestPath,
                                               .providerRoot = providerRoot,
                                           });
            }
        }
        return out;
    }

    [[nodiscard]] auto LoadPackageManifest(const fs::path &path) -> PackageManifest
    {
        const auto doc = LoadXml(path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Package")
        {
            throw std::runtime_error(path.string() + ": root element must be <Package>");
        }
        ValidateSchemaVersion(*rootElement, path);
        PackageManifest package{};
        package.path = path;
        package.name = RequireAttribute(*rootElement, "Name", path);
        package.version = RequireAttribute(*rootElement, "Version", path);
        package.compatiblePlatformRange = Attribute(*rootElement, "CompatiblePlatformRange").value_or("");
        if (const auto *artifacts = FindChild(*rootElement, "Artifacts"))
        {
            if (const auto *libraries = FindChild(*artifacts, "Libraries"))
            {
                for (const auto *node : ChildElements(*libraries, "Library"))
                {
                    LibraryArtifact artifact{};
                    artifact.name = RequireAttribute(*node, "Name", path);
                    artifact.target = Attribute(*node, "Target").value_or("");
                    artifact.linkage = Attribute(*node, "Linkage").value_or("");
                    artifact.origin = Attribute(*node, "Origin").value_or("");
                    artifact.exported = !Attribute(*node, "Exported").has_value() || BoolAttribute(*node, "Exported", true);
                    package.artifacts.libraries.push_back(std::move(artifact));
                }
            }
            if (const auto *executables = FindChild(*artifacts, "Executables"))
            {
                for (const auto *node : ChildElements(*executables, "Executable"))
                {
                    ExecutableArtifact artifact{};
                    artifact.name = RequireAttribute(*node, "Name", path);
                    artifact.target = Attribute(*node, "Target").value_or("");
                    artifact.origin = Attribute(*node, "Origin").value_or("");
                    artifact.exported = !Attribute(*node, "Exported").has_value() || BoolAttribute(*node, "Exported", true);
                    package.artifacts.executables.push_back(std::move(artifact));
                }
            }
        }
        LoadPackageBuildDescriptor(package.build, FindChild(*rootElement, "Build"), path);
        if (const auto *platforms = FindChild(*rootElement, "Platforms"))
        {
            for (const auto *node : ChildElements(*platforms, "Platform"))
            {
                package.platforms.push_back(RequireAttribute(*node, "Name", path));
            }
        }
        if (const auto *deps = FindChild(*rootElement, "Dependencies"))
        {
            for (const auto *node : ChildElements(*deps, "Dependency"))
            {
                PackageDependency dependency{};
                dependency.name = RequireAttribute(*node, "Name", path);
                dependency.versionRange = Attribute(*node, "VersionRange").value_or("");
                dependency.optional = BoolAttribute(*node, "Optional");
                package.dependencies.push_back(std::move(dependency));
            }
        }
        if (const auto *bootstrap = FindChild(*rootElement, "Bootstrap"))
        {
            PackageBootstrapDescriptor descriptor{};
            descriptor.mode = RequireAttribute(*bootstrap, "Mode", path);
            if (!IsValidPackageBootstrapMode(descriptor.mode))
            {
                throw std::runtime_error(path.string() + ": unknown package bootstrap mode '" + descriptor.mode + "'");
            }
            descriptor.entryPoint = RequireAttribute(*bootstrap, "EntryPoint", path);
            descriptor.autoApply = BoolAttribute(*bootstrap, "AutoApply");
            package.bootstrap = std::move(descriptor);
        }
        if (const auto *contents = FindChild(*rootElement, "Contents"))
        {
            for (const auto *node : ChildElements(*contents, "File"))
            {
                ContentFile content{};
                content.source = RequireAttribute(*node, "Source", path);
                content.kind = Attribute(*node, "Kind").value_or("other");
                content.target = Attribute(*node, "Target").value_or("");
                package.contents.push_back(std::move(content));
            }
        }

        const auto *modules = FindChild(*rootElement, "Modules");
        if (modules == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Modules>");
        }
        for (const auto *node : ChildElements(*modules, "Module"))
        {
            ModuleDescriptor module{};
            module.name = RequireAttribute(*node, "Name", path);
            module.family = Attribute(*node, "Family").value_or("Core");
            module.type = Attribute(*node, "Type").value_or("Runtime");
            module.startupStage = ResolveStartupStage(*node, "Features");
            module.version = Attribute(*node, "Version").value_or("");
            module.compatiblePlatformRange = Attribute(*node, "CompatiblePlatformRange").value_or("");
            module.requiresReflection = BoolAttribute(*node, "ReflectionRequired");
            ValidateModuleDescriptor(module, path);
            module.supportedHosts = ParseSupportedHosts(*node, path);

            if (const auto *platforms = FindChild(*node, "Platforms"))
            {
                for (const auto *platform : ChildElements(*platforms, "Platform"))
                {
                    module.platforms.push_back(RequireAttribute(*platform, "Name", path));
                }
            }

            if (const auto *dependencies = FindChild(*node, "Dependencies"))
            {
                for (const auto *dep : ChildElements(*dependencies, "Dependency"))
                {
                    const auto name = RequireAttribute(*dep, "Name", path);
                    if (BoolAttribute(*dep, "Optional"))
                    {
                        module.optional.push_back(name);
                    }
                    else
                    {
                        module.required.push_back(name);
                    }
                }
            }

            if (const auto *providesServices = FindChild(*node, "ProvidesServices"))
            {
                for (const auto *service : ChildElements(*providesServices, "Service"))
                {
                    module.providesServices.push_back(RequireAttribute(*service, "Name", path));
                }
            }

            if (const auto *requiresServices = FindChild(*node, "RequiresServices"))
            {
                for (const auto *service : ChildElements(*requiresServices, "Service"))
                {
                    module.requiresServices.push_back(RequireAttribute(*service, "Name", path));
                }
            }

            if (const auto *capabilities = FindChild(*node, "Capabilities"))
            {
                for (const auto *capability : ChildElements(*capabilities, "Capability"))
                {
                    module.capabilities.push_back(RequireAttribute(*capability, "Name", path));
                }
            }

            package.modules.push_back(std::move(module));
        }

        if (const auto *plugins = FindChild(*rootElement, "Plugins"))
        {
            for (const auto *node : ChildElements(*plugins, "Plugin"))
            {
                PluginDescriptor plugin{};
                plugin.name = RequireAttribute(*node, "Name", path);
                plugin.optional = BoolAttribute(*node, "Optional");

                if (const auto *platforms = FindChild(*node, "Platforms"))
                {
                    for (const auto *platform : ChildElements(*platforms, "Platform"))
                    {
                        plugin.platforms.push_back(RequireAttribute(*platform, "Name", path));
                    }
                }

                if (const auto *modulesElement = FindChild(*node, "Modules"))
                {
                    if (const auto *required = FindChild(*modulesElement, "Required"))
                    {
                        for (const auto *dep : ChildElements(*required, "ModuleRef"))
                        {
                            plugin.requiredModules.push_back(RequireAttribute(*dep, "Name", path));
                        }
                    }
                    if (const auto *optional = FindChild(*modulesElement, "Optional"))
                    {
                        for (const auto *dep : ChildElements(*optional, "ModuleRef"))
                        {
                            plugin.optionalModules.push_back(RequireAttribute(*dep, "Name", path));
                        }
                    }
                }

                package.plugins.push_back(std::move(plugin));
            }
        }
        return package;
    }

    [[nodiscard]] auto ParseModuleDefinition(const XmlElement &node, const fs::path &path) -> ModuleDescriptor
    {
        ModuleDescriptor module{};
        module.name = RequireAttribute(node, "Name", path);
        module.family = Attribute(node, "Family").value_or("App");
        module.type = Attribute(node, "Type").value_or("Runtime");
        module.startupStage = ResolveStartupStage(node, "Features");
        module.version = Attribute(node, "Version").value_or("");
        module.compatiblePlatformRange = Attribute(node, "CompatiblePlatformRange").value_or("");
        module.requiresReflection = BoolAttribute(node, "ReflectionRequired");
        ValidateModuleDescriptor(module, path);
        module.supportedHosts = ParseSupportedHosts(node, path);

        if (const auto *platforms = FindChild(node, "Platforms"))
        {
            for (const auto *platform : ChildElements(*platforms, "Platform"))
            {
                module.platforms.push_back(RequireAttribute(*platform, "Name", path));
            }
        }

        if (const auto *dependencies = FindChild(node, "Dependencies"))
        {
            for (const auto *dep : ChildElements(*dependencies, "Dependency"))
            {
                const auto name = RequireAttribute(*dep, "Name", path);
                if (BoolAttribute(*dep, "Optional"))
                {
                    module.optional.push_back(name);
                }
                else
                {
                    module.required.push_back(name);
                }
            }
        }

        if (const auto *providesServices = FindChild(node, "ProvidesServices"))
        {
            for (const auto *service : ChildElements(*providesServices, "Service"))
            {
                module.providesServices.push_back(RequireAttribute(*service, "Name", path));
            }
        }

        if (const auto *requiresServices = FindChild(node, "RequiresServices"))
        {
            for (const auto *service : ChildElements(*requiresServices, "Service"))
            {
                module.requiresServices.push_back(RequireAttribute(*service, "Name", path));
            }
        }

        if (const auto *capabilities = FindChild(node, "Capabilities"))
        {
            for (const auto *capability : ChildElements(*capabilities, "Capability"))
            {
                module.capabilities.push_back(RequireAttribute(*capability, "Name", path));
            }
        }

        return module;
    }

    [[nodiscard]] auto ParseBuildSetting(
        const XmlElement &node,
        const fs::path &path,
        std::string_view valueAttribute) -> BuildSetting
    {
        BuildSetting setting{};
        setting.value = RequireAttribute(node, valueAttribute, path);
        setting.visibility = Attribute(node, "Visibility").value_or("Private");
        if (!IsSupportedBuildVisibility(setting.visibility))
        {
            throw std::runtime_error(path.string() + ": unknown build visibility '" + setting.visibility + "'");
        }
        return setting;
    }

    auto LoadProjectBuildDescriptor(ProjectBuildDescriptor &build, const XmlElement *buildElement, const fs::path &path) -> void
    {
        if (buildElement == nullptr)
        {
            return;
        }

        if (const auto backend = Attribute(*buildElement, "Backend"); backend.has_value() && !backend->empty())
        {
            build.backend = *backend;
        }
        if (const auto mode = Attribute(*buildElement, "Mode"); mode.has_value() && !mode->empty())
        {
            build.mode = *mode;
        }
        if (!IsSupportedProjectBuildMode(build.mode))
        {
            throw std::runtime_error(path.string() + ": unknown project build mode '" + build.mode + "'");
        }
        if (const auto language = Attribute(*buildElement, "Language"); language.has_value() && !language->empty())
        {
            build.language = *language;
        }
        if (const auto languageStandard = Attribute(*buildElement, "LanguageStandard"); languageStandard.has_value() && !languageStandard->empty())
        {
            build.languageStandard = *languageStandard;
        }

        if (const auto *sources = FindChild(*buildElement, "Sources"))
        {
            for (const auto *item : ChildElements(*sources, "Source"))
            {
                build.sources.push_back(RequireAttribute(*item, "Path", path));
            }
        }

        if (const auto *includeDirectories = FindChild(*buildElement, "IncludeDirectories"))
        {
            for (const auto *item : ChildElements(*includeDirectories, "IncludeDirectory"))
            {
                build.includeDirectories.push_back(ParseBuildSetting(*item, path, "Path"));
            }
        }

        if (const auto *compileDefinitions = FindChild(*buildElement, "CompileDefinitions"))
        {
            for (const auto *item : ChildElements(*compileDefinitions, "Definition"))
            {
                build.compileDefinitions.push_back(ParseBuildSetting(*item, path, "Value"));
            }
        }

        if (const auto *compileOptions = FindChild(*buildElement, "CompileOptions"))
        {
            for (const auto *item : ChildElements(*compileOptions, "Option"))
            {
                build.compileOptions.push_back(ParseBuildSetting(*item, path, "Value"));
            }
        }

        if (const auto *linkOptions = FindChild(*buildElement, "LinkOptions"))
        {
            for (const auto *item : ChildElements(*linkOptions, "Option"))
            {
                build.linkOptions.push_back(ParseBuildSetting(*item, path, "Value"));
            }
        }
    }

    auto LoadPackageBuildDescriptor(PackageBuildDescriptor &build, const XmlElement *buildElement, const fs::path &path) -> void
    {
        if (buildElement == nullptr)
        {
            return;
        }
        build.backend = Attribute(*buildElement, "Backend").value_or("");
        build.mode = Attribute(*buildElement, "Mode").value_or("");
        if (!build.mode.empty() && !IsSupportedPackageBuildMode(build.mode))
        {
            throw std::runtime_error(path.string() + ": unknown package build mode '" + build.mode + "'");
        }
        if (const auto *options = FindChild(*buildElement, "Options"))
        {
            for (const auto *item : ChildElements(*options, "Option"))
            {
                BuildVariable variable{};
                variable.name = RequireAttribute(*item, "Name", path);
                variable.value = RequireAttribute(*item, "Value", path);
                build.options.push_back(std::move(variable));
            }
        }
    }

    [[nodiscard]] auto LoadProjectManifest(const fs::path &path) -> ProjectManifest
    {
        const auto doc = LoadXml(path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Project")
        {
            throw std::runtime_error(path.string() + ": root element must be <Project>");
        }
        ValidateSchemaVersion(*rootElement, path);
        ProjectManifest project{};
        project.path = path;
        project.name = RequireAttribute(*rootElement, "Name", path);
        project.type = RequireAttribute(*rootElement, "Type", path);
        project.defaultConfiguration = RequireAttribute(*rootElement, "DefaultConfiguration", path);

        if (const auto *host = FindChild(*rootElement, "Host"))
        {
            project.hostProfile = Attribute(*host, "Profile").value_or("");
            if (!project.hostProfile.empty() && !IsValidHostProfile(project.hostProfile))
            {
                throw std::runtime_error(path.string() + ": unknown host profile '" + project.hostProfile + "'");
            }
        }

        if (const auto *sourceRoots = FindChild(*rootElement, "SourceRoots"))
        {
            for (const auto *node : ChildElements(*sourceRoots, "SourceRoot"))
            {
                project.sourceRoots.push_back(RequireAttribute(*node, "Path", path));
            }
        }

        const auto *output = FindChild(*rootElement, "Output");
        if (output == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Output>");
        }
        project.output.kind = RequireAttribute(*output, "Kind", path);
        project.output.name = RequireAttribute(*output, "Name", path);
        project.output.target = RequireAttribute(*output, "Target", path);

        LoadProjectBuildDescriptor(project.build, FindChild(*rootElement, "Build"), path);

        auto parseReferences = [&](const XmlElement &referencesElement, std::vector<ProjectReference> &projectRefs, std::vector<PackageReference> &packageRefs)
        {
            for (const auto *node : ChildElements(referencesElement, "Project"))
            {
                ProjectReference reference{};
                reference.path = (path.parent_path() / RequireAttribute(*node, "Path", path)).lexically_normal();
                if (const auto configuration = Attribute(*node, "Configuration"); configuration.has_value() && !configuration->empty())
                {
                    reference.configuration = *configuration;
                }
                projectRefs.push_back(std::move(reference));
            }
            for (const auto *node : ChildElements(referencesElement, "Package"))
            {
                PackageReference packageReference{};
                packageReference.name = RequireAttribute(*node, "Name", path);
                packageReference.versionRange = Attribute(*node, "Version").value_or(Attribute(*node, "VersionRange").value_or(""));
                packageReference.optional = BoolAttribute(*node, "Optional");
                packageRefs.push_back(std::move(packageReference));
            }
        };

        if (const auto *references = FindChild(*rootElement, "References"))
        {
            parseReferences(*references, project.projectRefs, project.packageRefs);
        }

        if (const auto *config = FindChild(*rootElement, "ConfigSources"))
        {
            for (const auto *item : ChildElements(*config, "Config"))
            {
                project.configSources.push_back(RequireAttribute(*item, "Source", path));
            }
        }

        if (const auto *runtime = FindChild(*rootElement, "Runtime"))
        {
            if (const auto *modules = FindChild(*runtime, "Modules"))
            {
                for (const auto *node : ChildElements(*modules, "Module"))
                {
                    project.runtime.modules.push_back(ParseModuleDefinition(*node, path));
                }
            }
            if (const auto *enableModules = FindChild(*runtime, "EnableModules"))
            {
                for (const auto *node : ChildElements(*enableModules, "ModuleRef"))
                {
                    project.runtime.enableModules.push_back(RequireAttribute(*node, "Name", path));
                }
            }
            if (const auto *disableModules = FindChild(*runtime, "DisableModules"))
            {
                for (const auto *node : ChildElements(*disableModules, "ModuleRef"))
                {
                    project.runtime.disableModules.push_back(RequireAttribute(*node, "Name", path));
                }
            }
            if (const auto *enablePlugins = FindChild(*runtime, "EnablePlugins"))
            {
                for (const auto *node : ChildElements(*enablePlugins, "PluginRef"))
                {
                    project.runtime.enablePlugins.push_back(RequireAttribute(*node, "Name", path));
                }
            }
            if (const auto *disablePlugins = FindChild(*runtime, "DisablePlugins"))
            {
                for (const auto *node : ChildElements(*disablePlugins, "PluginRef"))
                {
                    project.runtime.disablePlugins.push_back(RequireAttribute(*node, "Name", path));
                }
            }
        }

        const auto *configurationsNode = FindChild(*rootElement, "Configurations");
        if (configurationsNode == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Configurations>");
        }
        for (const auto *node : ChildElements(*configurationsNode, "Configuration"))
        {
            ConfigurationDefinition configuration{};
            configuration.name = RequireAttribute(*node, "Name", path);
            configuration.buildConfiguration = Attribute(*node, "BuildConfiguration").value_or("Debug");
            configuration.hostProfile = Attribute(*node, "HostProfile").value_or(project.hostProfile);
            configuration.platform = Attribute(*node, "Platform").value_or("linux-x64");
            if (!IsSupportedBuildConfiguration(configuration.buildConfiguration))
            {
                throw std::runtime_error(path.string() + ": unknown build configuration '" + configuration.buildConfiguration + "'");
            }
            if (!configuration.hostProfile.empty() && !IsValidHostProfile(configuration.hostProfile))
            {
                throw std::runtime_error(path.string() + ": unknown host profile '" + configuration.hostProfile + "'");
            }
            configuration.enableReflection = BoolAttribute(*node, "EnableReflection");
            configuration.environmentName = Attribute(*node, "Environment").value_or("");
            configuration.workingDirectory = Attribute(*node, "WorkingDirectory").value_or(".");

            if (const auto *launch = FindChild(*node, "Launch"))
            {
                if (const auto executable = Attribute(*launch, "Executable"); executable.has_value() && !executable->empty())
                {
                    configuration.launchExecutable = *executable;
                }
            }
            if (const auto *config = FindChild(*node, "ConfigSources"))
            {
                for (const auto *item : ChildElements(*config, "Config"))
                {
                    configuration.configSources.push_back(RequireAttribute(*item, "Source", path));
                }
            }
            if (const auto *references = FindChild(*node, "References"))
            {
                parseReferences(*references, configuration.projectRefs, configuration.packageRefs);
            }
            if (const auto *modules = FindChild(*node, "EnableModules"))
            {
                for (const auto *item : ChildElements(*modules, "ModuleRef"))
                {
                    configuration.enableModules.push_back(RequireAttribute(*item, "Name", path));
                }
            }
            if (const auto *modules = FindChild(*node, "DisableModules"))
            {
                for (const auto *item : ChildElements(*modules, "ModuleRef"))
                {
                    configuration.disableModules.push_back(RequireAttribute(*item, "Name", path));
                }
            }
            if (const auto *plugins = FindChild(*node, "EnablePlugins"))
            {
                for (const auto *item : ChildElements(*plugins, "PluginRef"))
                {
                    configuration.enablePlugins.push_back(RequireAttribute(*item, "Name", path));
                }
            }
            if (const auto *plugins = FindChild(*node, "DisablePlugins"))
            {
                for (const auto *item : ChildElements(*plugins, "PluginRef"))
                {
                    configuration.disablePlugins.push_back(RequireAttribute(*item, "Name", path));
                }
            }
            project.configurations.push_back(std::move(configuration));
        }
        return project;
    }

    [[nodiscard]] auto FindProjectFile(const fs::path &start) -> std::optional<fs::path>
    {
        auto current = fs::weakly_canonical(start);
        while (true)
        {
            std::vector<fs::path> candidates;
            if (fs::exists(current))
            {
                for (const auto &entry : fs::directory_iterator(current))
                {
                    if (entry.is_regular_file() && entry.path().extension() == ".nginproj")
                    {
                        candidates.push_back(entry.path());
                    }
                }
            }
            if (!candidates.empty())
            {
                std::sort(candidates.begin(), candidates.end());
                return candidates.front();
            }
            if (current == current.parent_path())
            {
                break;
            }
            current = current.parent_path();
        }
        return std::nullopt;
    }

    [[nodiscard]] auto ResolveProjectPath(const std::optional<std::string> &explicitPath) -> fs::path
    {
        if (explicitPath.has_value())
        {
            return fs::weakly_canonical(*explicitPath);
        }
        if (const auto discovered = FindProjectFile(fs::current_path()); discovered.has_value())
        {
            return *discovered;
        }
        throw std::runtime_error("no project manifest specified and no .nginproj file found in the current directory tree");
    }

    [[nodiscard]] auto ConfigurationByName(const ProjectManifest &project, const std::optional<std::string> &configurationName) -> const ConfigurationDefinition &
    {
        const auto desired = configurationName.value_or(project.defaultConfiguration);
        for (const auto &configuration : project.configurations)
        {
            if (configuration.name == desired)
            {
                return configuration;
            }
        }
        throw std::runtime_error("unknown configuration '" + desired + "'");
    }

    auto MergePackageReferences(std::vector<PackageReference> &target, const std::vector<PackageReference> &source) -> void
    {
        std::unordered_map<std::string, std::size_t> indexByName;
        for (std::size_t index = 0; index < target.size(); ++index)
        {
            indexByName[target[index].name] = index;
        }
        for (const auto &reference : source)
        {
            if (const auto it = indexByName.find(reference.name); it != indexByName.end())
            {
                target[it->second] = reference;
                continue;
            }
            indexByName[reference.name] = target.size();
            target.push_back(reference);
        }
    }

    auto MergeStringSelection(std::set<std::string> &enabled, const std::vector<std::string> &add, const std::vector<std::string> &remove) -> void
    {
        for (const auto &name : add)
        {
            enabled.insert(name);
        }
        for (const auto &name : remove)
        {
            enabled.erase(name);
        }
    }

    auto CollectProjectClosure(
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration,
        std::vector<ResolvedProjectUnit> &ordered,
        std::set<fs::path> &visiting,
        std::set<fs::path> &visited,
        IssueReport &report) -> void
    {
        const auto canonicalPath = fs::weakly_canonical(project.path);
        if (visited.contains(canonicalPath))
        {
            return;
        }
        if (!visiting.insert(canonicalPath).second)
        {
            AddError(report, "project reference cycle detected at '" + canonicalPath.string() + "'");
            return;
        }

        auto collectReference = [&](const ProjectReference &reference)
        {
            const auto referencedPath = fs::weakly_canonical(reference.path);
            if (!fs::exists(referencedPath))
            {
                AddError(report, "project reference '" + referencedPath.string() + "' does not exist");
                return;
            }
            const auto referencedProject = LoadProjectManifest(referencedPath);
            std::optional<std::string> selectedConfiguration = reference.configuration;
            if (!selectedConfiguration.has_value())
            {
                const auto it = std::find_if(
                    referencedProject.configurations.begin(),
                    referencedProject.configurations.end(),
                    [&](const ConfigurationDefinition &candidate)
                    { return candidate.name == configuration.name; });
                if (it != referencedProject.configurations.end())
                {
                    selectedConfiguration = configuration.name;
                }
            }
            const auto &referencedConfiguration = ConfigurationByName(referencedProject, selectedConfiguration);
            CollectProjectClosure(referencedProject, referencedConfiguration, ordered, visiting, visited, report);
        };

        for (const auto &reference : project.projectRefs)
        {
            collectReference(reference);
        }
        for (const auto &reference : configuration.projectRefs)
        {
            collectReference(reference);
        }

        visiting.erase(canonicalPath);
        visited.insert(canonicalPath);
        ordered.push_back(ResolvedProjectUnit{
            .project = project,
            .configuration = configuration,
        });
    }

    [[nodiscard]] auto ResolvePackages(
        const std::optional<WorkspaceManifest> &workspace,
        const std::vector<ResolvedProjectUnit> &projectUnits,
        const std::unordered_map<std::string, PackageCatalogEntry> &catalog,
        const std::string &targetPlatform,
        IssueReport &report) -> std::vector<ResolvedPackage>
    {
        std::vector<PackageReference> combinedRefs{};
        for (const auto &unit : projectUnits)
        {
            MergePackageReferences(combinedRefs, unit.project.packageRefs);
            MergePackageReferences(combinedRefs, unit.configuration.packageRefs);
        }
        std::unordered_map<std::string, ResolvedPackage> resolved;
        std::map<std::string, std::set<std::string>> edges{};
        std::vector<PackageReference> queue = combinedRefs;
        std::vector<std::string> parents(queue.size(), "");

        std::size_t index = 0;
        while (index < queue.size())
        {
            const auto ref = queue[index];
            const auto requiredBy = parents[index];
            ++index;

            const auto itCatalog = catalog.find(ref.name);
            if (itCatalog == catalog.end())
            {
                const auto message = "package '" + ref.name + "' could not be resolved";
                if (ref.optional)
                {
                    AddWarning(report, message);
                }
                else
                {
                    AddError(report, requiredBy.empty() ? message : message + " (required by '" + requiredBy + "')");
                }
                continue;
            }

            if (resolved.contains(ref.name))
            {
                if (!requiredBy.empty())
                {
                    edges[requiredBy].insert(ref.name);
                }
                continue;
            }

            auto manifest = LoadPackageManifest(fs::weakly_canonical(itCatalog->second.manifestPath));
            if (manifest.name != ref.name)
            {
                AddError(report, "package '" + ref.name + "' resolved to manifest for '" + manifest.name + "'");
                continue;
            }
            if (!ref.versionRange.empty() && !VersionSatisfies(manifest.version, ref.versionRange))
            {
                const auto message = "package '" + ref.name + "' version " + manifest.version + " does not satisfy '" + ref.versionRange + "'";
                if (ref.optional)
                {
                    AddWarning(report, message);
                }
                else
                {
                    AddError(report, message);
                }
                continue;
            }
            if (!manifest.platforms.empty() && !PlatformSupported(targetPlatform, manifest.platforms))
            {
                const auto message = "package '" + ref.name + "' is not supported on platform '" + targetPlatform + "'";
                if (ref.optional)
                {
                    AddWarning(report, message);
                }
                else
                {
                    AddError(report, message);
                }
                continue;
            }
            const auto platformVersion = workspace.has_value() ? workspace->platformVersion : "0.1.0";
            if (!manifest.compatiblePlatformRange.empty() && !VersionSatisfies(platformVersion, manifest.compatiblePlatformRange))
            {
                AddError(report, "package '" + ref.name + "' compatible platform range does not include platform version '" + platformVersion + "'");
                continue;
            }

            for (const auto &content : manifest.contents)
            {
                const auto resolvedPath = manifest.path.parent_path() / content.source;
                if (!fs::exists(resolvedPath))
                {
                    AddError(report, "package '" + ref.name + "' content file '" + content.source + "' does not exist");
                }
            }

            if (!requiredBy.empty())
            {
                edges[requiredBy].insert(ref.name);
            }
            edges[ref.name];
            for (const auto &dep : manifest.dependencies)
            {
                queue.push_back({dep.name, dep.versionRange, dep.optional});
                parents.push_back(ref.name);
                edges[ref.name].insert(dep.name);
            }

            const auto sourceDirectory = itCatalog->second.providerRoot.empty()
                                             ? manifest.path.parent_path()
                                             : itCatalog->second.providerRoot;
            resolved.emplace(ref.name, ResolvedPackage{
                                           .manifest = std::move(manifest),
                                           .source = itCatalog->second.providerRoot.empty() ? "manifest" : "provider",
                                           .sourceDirectory = sourceDirectory,
                                       });
        }

        if (!report.errors.empty())
        {
            return {};
        }

        std::set<std::string> nodes;
        for (const auto &[name, _] : resolved)
        {
            nodes.insert(name);
        }
        if (const auto cycles = DetectCycles(nodes, edges); !cycles.empty())
        {
            AddError(report, "package graph contains dependency cycle(s)");
            return {};
        }
        const auto orderedNames = TopologicalDependenciesFirst(nodes, edges);
        if (!orderedNames.has_value())
        {
            AddError(report, "package graph could not be ordered");
            return {};
        }

        std::vector<ResolvedPackage> ordered;
        for (const auto &name : *orderedNames)
        {
            ordered.push_back(resolved.at(name));
        }
        return ordered;
    }

    auto ResolveArtifacts(
        const std::vector<ResolvedProjectUnit> &projectUnits,
        const std::vector<ResolvedPackage> &orderedPackages,
        const ProjectManifest &rootProject,
        const ConfigurationDefinition &rootConfiguration,
        IssueReport &report,
        std::vector<LibraryArtifact> &librariesOut,
        std::vector<ExecutableArtifact> &executablesOut,
        std::optional<ExecutableArtifact> &selectedExecutableOut) -> void
    {
        std::unordered_map<std::string, std::string> libraryProviders;
        std::unordered_map<std::string, std::string> executableProviders;

        for (const auto &unit : projectUnits)
        {
            const auto kind = Lower(unit.project.output.kind);
            if (kind == "staticlibrary" || kind == "sharedlibrary")
            {
                LibraryArtifact artifact{};
                artifact.name = unit.project.output.name;
                artifact.target = unit.project.output.target;
                artifact.linkage = kind == "sharedlibrary" ? "Shared" : "Static";
                artifact.origin = "Built";
                if (const auto it = libraryProviders.find(artifact.name); it != libraryProviders.end())
                {
                    AddError(report, "duplicate library artifact '" + artifact.name + "' in projects '" + it->second + "' and '" + unit.project.name + "'");
                    continue;
                }
                libraryProviders.emplace(artifact.name, unit.project.name);
                librariesOut.push_back(std::move(artifact));
            }
            else if (kind == "executable")
            {
                ExecutableArtifact artifact{};
                artifact.name = unit.project.output.name;
                artifact.target = unit.project.output.target;
                artifact.origin = "Built";
                if (const auto it = executableProviders.find(artifact.name); it != executableProviders.end())
                {
                    AddError(report, "duplicate executable artifact '" + artifact.name + "' in projects '" + it->second + "' and '" + unit.project.name + "'");
                    continue;
                }
                executableProviders.emplace(artifact.name, unit.project.name);
                executablesOut.push_back(std::move(artifact));
            }
        }

        for (const auto &package : orderedPackages)
        {
            for (auto artifact : package.manifest.artifacts.libraries)
            {
                if (!artifact.exported)
                {
                    continue;
                }
                artifact.origin = EffectiveArtifactOrigin(artifact.origin, package.manifest);
                if (artifact.origin.empty())
                {
                    AddError(report, "package '" + package.manifest.name + "' library artifact '" + artifact.name + "' does not declare an origin and it could not be inferred");
                    continue;
                }
                if (const auto it = libraryProviders.find(artifact.name); it != libraryProviders.end())
                {
                    AddError(report, "duplicate library artifact '" + artifact.name + "' in '" + it->second + "' and package '" + package.manifest.name + "'");
                    continue;
                }
                libraryProviders.emplace(artifact.name, package.manifest.name);
                librariesOut.push_back(std::move(artifact));
            }

            for (auto artifact : package.manifest.artifacts.executables)
            {
                if (!artifact.exported)
                {
                    continue;
                }
                artifact.origin = EffectiveArtifactOrigin(artifact.origin, package.manifest);
                if (artifact.origin.empty())
                {
                    AddError(report, "package '" + package.manifest.name + "' executable artifact '" + artifact.name + "' does not declare an origin and it could not be inferred");
                    continue;
                }
                if (const auto it = executableProviders.find(artifact.name); it != executableProviders.end())
                {
                    AddError(report, "duplicate executable artifact '" + artifact.name + "' in '" + it->second + "' and package '" + package.manifest.name + "'");
                    continue;
                }
                executableProviders.emplace(artifact.name, package.manifest.name);
                executablesOut.push_back(std::move(artifact));
            }
        }

        const auto rootKind = Lower(rootProject.output.kind);
        if (!rootConfiguration.launchExecutable.has_value() && rootKind == "executable")
        {
            for (const auto &executable : executablesOut)
            {
                if (executable.name == rootProject.output.name)
                {
                    selectedExecutableOut = executable;
                    return;
                }
            }
        }

        if (!rootConfiguration.launchExecutable.has_value())
        {
            if (executablesOut.size() == 1)
            {
                selectedExecutableOut = executablesOut.front();
            }
            else if (executablesOut.size() > 1)
            {
                AddError(report, "configuration '" + rootConfiguration.name + "' resolves multiple executable artifacts; add <Launch Executable=\"...\" /> to select one");
            }
            return;
        }

        const auto desired = *rootConfiguration.launchExecutable;
        for (const auto &executable : executablesOut)
        {
            if (executable.name == desired)
            {
                selectedExecutableOut = executable;
                return;
            }
        }
        AddError(report, "configuration '" + rootConfiguration.name + "' selects executable '" + desired + "' but no project or package exposes it");
    }

    auto ResolveLaunch(
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration,
        IssueReport &report) -> std::optional<ResolvedLaunch>
    {
        const auto workspaceRoot = RootDirFrom(project.path.parent_path());
        const auto workspace = workspaceRoot.has_value() ? TryLoadWorkspaceManifest(*workspaceRoot) : std::nullopt;
        const auto packageCatalog = LoadPackageCatalog(workspace, project.path);

        std::vector<ResolvedProjectUnit> projectUnits{};
        std::set<fs::path> visiting{};
        std::set<fs::path> visited{};
        CollectProjectClosure(project, configuration, projectUnits, visiting, visited, report);
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        auto orderedPackages = ResolvePackages(workspace, projectUnits, packageCatalog, configuration.platform, report);
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        std::unordered_map<std::string, std::set<std::string>> providersByModule;
        std::unordered_map<std::string, std::set<std::string>> providersByPlugin;
        std::unordered_map<std::string, ModuleDescriptor> modules;
        std::unordered_map<std::string, PluginDescriptor> plugins;

        for (const auto &unit : projectUnits)
        {
            for (const auto &module : unit.project.runtime.modules)
            {
                if (!module.platforms.empty() && !PlatformSupported(configuration.platform, module.platforms))
                {
                    AddError(report, "project '" + unit.project.name + "' provides module '" + module.name + "' that is not supported on platform '" + configuration.platform + "'");
                    continue;
                }
                if (const auto providerIt = providersByModule.find(module.name); providerIt != providersByModule.end() && !providerIt->second.empty())
                {
                    AddError(report, "duplicate module declaration for '" + module.name + "' in '" + *providerIt->second.begin() + "' and project '" + unit.project.name + "'");
                    continue;
                }
                modules.emplace(module.name, module);
                providersByModule[module.name].insert(unit.project.name);
            }
        }

        for (const auto &package : orderedPackages)
        {
            for (const auto &module : package.manifest.modules)
            {
                if (!module.platforms.empty() && !PlatformSupported(configuration.platform, module.platforms))
                {
                    AddError(report, "package '" + package.manifest.name + "' provides module '" + module.name + "' that is not supported on platform '" + configuration.platform + "'");
                    continue;
                }
                if (const auto providerIt = providersByModule.find(module.name); providerIt != providersByModule.end() && !providerIt->second.empty())
                {
                    AddError(report, "duplicate module declaration for '" + module.name + "' in '" + *providerIt->second.begin() + "' and package '" + package.manifest.name + "'");
                    continue;
                }
                modules.emplace(module.name, module);
                providersByModule[module.name].insert(package.manifest.name);
            }
            for (const auto &plugin : package.manifest.plugins)
            {
                if (!plugin.platforms.empty() && !PlatformSupported(configuration.platform, plugin.platforms))
                {
                    AddError(report, "package '" + package.manifest.name + "' provides plugin '" + plugin.name + "' that is not supported on platform '" + configuration.platform + "'");
                    continue;
                }
                if (const auto providerIt = providersByPlugin.find(plugin.name); providerIt != providersByPlugin.end() && !providerIt->second.empty())
                {
                    AddError(report, "duplicate plugin declaration for '" + plugin.name + "' in packages '" + *providerIt->second.begin() + "' and '" + package.manifest.name + "'");
                    continue;
                }
                plugins.emplace(plugin.name, plugin);
                providersByPlugin[plugin.name].insert(package.manifest.name);
            }
        }
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        std::set<std::string> directModules{};
        for (const auto &unit : projectUnits)
        {
            MergeStringSelection(directModules, unit.project.runtime.enableModules, unit.project.runtime.disableModules);
            MergeStringSelection(directModules, unit.configuration.enableModules, unit.configuration.disableModules);
        }
        std::set<std::string> directPlugins{};
        for (const auto &[pluginName, plugin] : plugins)
        {
            if (!plugin.optional)
            {
                directPlugins.insert(pluginName);
            }
        }
        for (const auto &unit : projectUnits)
        {
            MergeStringSelection(directPlugins, unit.project.runtime.enablePlugins, unit.project.runtime.disablePlugins);
            MergeStringSelection(directPlugins, unit.configuration.enablePlugins, unit.configuration.disablePlugins);
        }

        for (const auto &module : directModules)
        {
            if (!modules.contains(module))
            {
                AddError(report, "configuration '" + configuration.name + "' references unknown module '" + module + "'");
                continue;
            }
            if (!providersByModule.contains(module))
            {
                AddError(report, "configuration '" + configuration.name + "' enables module '" + module + "' but no active project or package provides it");
            }
        }
        for (const auto &plugin : directPlugins)
        {
            if (!plugins.contains(plugin))
            {
                AddError(report, "configuration '" + configuration.name + "' references unknown plugin '" + plugin + "'");
                continue;
            }
            if (!providersByPlugin.contains(plugin))
            {
                AddError(report, "configuration '" + configuration.name + "' enables plugin '" + plugin + "' but no active package provides it");
                continue;
            }
            const auto &descriptor = plugins.at(plugin);
            for (const auto &module : descriptor.requiredModules)
            {
                if (!providersByModule.contains(module))
                {
                    AddError(report, "plugin '" + plugin + "' requires module '" + module + "' but no active project or package provides it");
                }
            }
        }
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        std::set<std::string> requiredSet = directModules;
        std::set<std::string> optionalSet;
        for (const auto &plugin : directPlugins)
        {
            const auto &descriptor = plugins.at(plugin);
            for (const auto &module : descriptor.requiredModules)
            {
                requiredSet.insert(module);
            }
            for (const auto &module : descriptor.optionalModules)
            {
                if (providersByModule.contains(module) && !requiredSet.contains(module))
                {
                    optionalSet.insert(module);
                }
            }
        }
        std::vector<std::string> reqQueue(requiredSet.begin(), requiredSet.end());
        std::vector<std::string> optQueue(optionalSet.begin(), optionalSet.end());
        std::size_t reqIndex = 0;
        while (reqIndex < reqQueue.size())
        {
            const auto current = reqQueue[reqIndex++];
            const auto it = modules.find(current);
            if (it == modules.end())
            {
                AddError(report, "configuration '" + configuration.name + "' references unknown module '" + current + "'");
                continue;
            }
            const auto activeHostProfile = configuration.hostProfile.empty() ? "ConsoleApp" : configuration.hostProfile;
            if (!it->second.supportedHosts.empty() && std::find(it->second.supportedHosts.begin(), it->second.supportedHosts.end(), activeHostProfile) == it->second.supportedHosts.end())
            {
                AddError(report, "configuration '" + configuration.name + "' includes module '" + current + "' that does not support host profile '" + activeHostProfile + "'");
            }
            if (it->second.requiresReflection && !configuration.enableReflection)
            {
                AddError(report, "configuration '" + configuration.name + "' includes module '" + current + "' that requires reflection");
            }
            for (const auto &dep : it->second.required)
            {
                if (!providersByModule.contains(dep))
                {
                    AddError(report, "module '" + current + "' requires '" + dep + "' but no active project or package provides it");
                    continue;
                }
                if (!requiredSet.contains(dep))
                {
                    requiredSet.insert(dep);
                    reqQueue.push_back(dep);
                }
            }
            for (const auto &dep : it->second.optional)
            {
                if (!providersByModule.contains(dep))
                {
                    continue;
                }
                if (!requiredSet.contains(dep) && !optionalSet.contains(dep))
                {
                    optionalSet.insert(dep);
                    optQueue.push_back(dep);
                }
            }
        }
        std::size_t optIndex = 0;
        while (optIndex < optQueue.size())
        {
            const auto current = optQueue[optIndex++];
            if (requiredSet.contains(current))
            {
                continue;
            }
            const auto it = modules.find(current);
            if (it == modules.end())
            {
                continue;
            }
            for (const auto &dep : it->second.required)
            {
                if (!providersByModule.contains(dep))
                {
                    continue;
                }
                if (!requiredSet.contains(dep) && !optionalSet.contains(dep))
                {
                    optionalSet.insert(dep);
                    optQueue.push_back(dep);
                }
            }
            for (const auto &dep : it->second.optional)
            {
                if (!providersByModule.contains(dep))
                {
                    continue;
                }
                if (!requiredSet.contains(dep) && !optionalSet.contains(dep))
                {
                    optionalSet.insert(dep);
                    optQueue.push_back(dep);
                }
            }
        }

        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        std::set<std::string> allNodes = requiredSet;
        allNodes.insert(optionalSet.begin(), optionalSet.end());
        std::map<std::string, std::set<std::string>> depEdges;
        for (const auto &node : allNodes)
        {
            const auto &module = modules.at(node);
            for (const auto &dep : module.required)
            {
                if (allNodes.contains(dep))
                {
                    depEdges[node].insert(dep);
                }
            }
            for (const auto &dep : module.optional)
            {
                if (allNodes.contains(dep))
                {
                    depEdges[node].insert(dep);
                }
            }
        }
        for (const auto &[moduleName, dependencies] : depEdges)
        {
            const auto &module = modules.at(moduleName);
            const auto moduleRank = StartupStageRank(module.startupStage);
            for (const auto &dependencyName : dependencies)
            {
                const auto &dependency = modules.at(dependencyName);
                const auto dependencyRank = StartupStageRank(dependency.startupStage);
                if (moduleRank < dependencyRank)
                {
                    AddError(
                        report,
                        "module '" + moduleName + "' at startup stage '" + module.startupStage + "' depends on '" + dependencyName + "' at later startup stage '" + dependency.startupStage + "'");
                }
            }
        }
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        std::map<std::string, int> indegree{};
        std::map<std::string, std::set<std::string>> dependents{};
        for (const auto &node : allNodes)
        {
            indegree[node] = 0;
        }
        for (const auto &node : allNodes)
        {
            const auto it = depEdges.find(node);
            if (it == depEdges.end())
            {
                continue;
            }
            for (const auto &dep : it->second)
            {
                if (allNodes.contains(dep))
                {
                    ++indegree[node];
                    dependents[dep].insert(node);
                }
            }
        }

        auto compareModuleOrder = [&](const std::string &left, const std::string &right)
        {
            const auto leftRank = StartupStageRank(modules.at(left).startupStage);
            const auto rightRank = StartupStageRank(modules.at(right).startupStage);
            if (leftRank != rightRank)
            {
                return leftRank < rightRank;
            }
            return left < right;
        };

        std::vector<std::string> queue;
        for (const auto &[node, deg] : indegree)
        {
            if (deg == 0)
            {
                queue.push_back(node);
            }
        }
        std::sort(queue.begin(), queue.end(), compareModuleOrder);

        std::vector<std::string> orderedModules{};
        while (!queue.empty())
        {
            const auto current = queue.front();
            queue.erase(queue.begin());
            orderedModules.push_back(current);
            for (const auto &dep : dependents[current])
            {
                --indegree[dep];
                if (indegree[dep] == 0)
                {
                    queue.push_back(dep);
                    std::sort(queue.begin(), queue.end(), compareModuleOrder);
                }
            }
        }
        if (orderedModules.size() != allNodes.size())
        {
            AddError(report, "configuration closure contains cyclic module dependencies");
            return std::nullopt;
        }

        ResolvedLaunch resolved{};
        resolved.workspace = workspace;
        resolved.project = project;
        resolved.configuration = configuration;
        resolved.projectUnits = std::move(projectUnits);
        std::map<fs::path, std::string> configOwnersByDestination{};
        std::set<std::pair<std::string, std::string>> seenConfigDeclarations{};
        for (const auto &unit : resolved.projectUnits)
        {
            const auto ownerProjectDirectory = unit.project.path.parent_path();
            const auto collectConfigSources = [&](const std::vector<std::string> &configSources)
            {
                for (const auto &source : configSources)
                {
                    const auto declarationKey = std::make_pair(unit.project.name, source);
                    if (!seenConfigDeclarations.insert(declarationKey).second)
                    {
                        continue;
                    }

                    ResolvedConfigSource configSource{};
                    configSource.ownerProjectName = unit.project.name;
                    configSource.ownerProjectDirectory = ownerProjectDirectory;
                    configSource.source = source;

                    const auto declaredPath = fs::path(source);
                    configSource.stagedRelativePath = declaredPath.is_absolute() ? declaredPath.filename() : declaredPath.lexically_normal();
                    configSource.absoluteSourcePath = declaredPath.is_absolute()
                                                          ? declaredPath.lexically_normal()
                                                          : (ownerProjectDirectory / declaredPath).lexically_normal();

                    if (const auto it = configOwnersByDestination.find(configSource.stagedRelativePath); it != configOwnersByDestination.end())
                    {
                        AddError(report, "config source destination collision at '" + configSource.stagedRelativePath.string() + "' between projects '" + it->second + "' and '" + unit.project.name + "'");
                        continue;
                    }
                    configOwnersByDestination[configSource.stagedRelativePath] = unit.project.name;
                    resolved.configSources.push_back(std::move(configSource));
                }
            };
            collectConfigSources(unit.project.configSources);
            collectConfigSources(unit.configuration.configSources);
        }
        if (!report.errors.empty())
        {
            return std::nullopt;
        }
        resolved.orderedPackages = std::move(orderedPackages);
        for (const auto &package : resolved.orderedPackages)
        {
            if (!package.manifest.bootstrap.has_value())
            {
                continue;
            }
            resolved.bootstraps.push_back(ResolvedBootstrap{
                .packageName = package.manifest.name,
                .mode = package.manifest.bootstrap->mode,
                .entryPoint = package.manifest.bootstrap->entryPoint,
                .autoApply = package.manifest.bootstrap->autoApply,
            });
        }
        for (const auto &package : resolved.orderedPackages)
        {
            resolved.packageEdges[package.manifest.name] = {};
            for (const auto &dep : package.manifest.dependencies)
            {
                resolved.packageEdges[package.manifest.name].insert(dep.name);
            }
        }
        resolved.enabledPlugins.assign(directPlugins.begin(), directPlugins.end());
        for (const auto &name : orderedModules)
        {
            if (requiredSet.contains(name))
            {
                resolved.requiredModules.push_back(name);
            }
            else if (optionalSet.contains(name))
            {
                resolved.optionalModules.push_back(name);
            }
        }
        resolved.dependencyEdges = std::move(depEdges);
        ResolveArtifacts(resolved.projectUnits, resolved.orderedPackages, resolved.project, resolved.configuration, report, resolved.libraries, resolved.executables, resolved.selectedExecutable);
        if (!report.errors.empty())
        {
            return std::nullopt;
        }
        return resolved;
    }

    auto PrintIssues(const IssueReport &report, const std::string &title) -> void
    {
        if (!report.errors.empty())
        {
            std::cout << "\n"
                      << title << " errors:\n";
            for (const auto &issue : report.errors)
            {
                std::cout << "  - " << issue << "\n";
            }
        }
        if (!report.warnings.empty())
        {
            std::cout << "\nWarnings:\n";
            for (const auto &issue : report.warnings)
            {
                std::cout << "  - " << issue << "\n";
            }
        }
    }

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
        IssueReport &report) -> std::vector<fs::path>
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

    auto EmitTargetChecks(std::ofstream &out, const PackageManifest &manifest) -> void
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

    auto EmitPackageBuildOptions(std::ofstream &out, const PackageBuildDescriptor &build) -> void
    {
        for (const auto &option : build.options)
        {
            const auto lowerValue = Lower(option.value);
            const auto cacheType = lowerValue == "on" || lowerValue == "off" || lowerValue == "true" || lowerValue == "false" ? "BOOL" : "STRING";
            out << "set(" << option.name << " \"" << EscapeCMake(option.value) << "\" CACHE " << cacheType << " \"\" FORCE)\n";
        }
    }

    auto WriteGeneratedBuildProject(const ResolvedLaunch &resolved, const fs::path &outputDir, IssueReport &report) -> std::optional<fs::path>
    {
        if (!HasArtifactTargetsToBuild(resolved))
        {
            return std::nullopt;
        }

        const auto generatedSourceDir = outputDir / ".ngin" / "cmake-src";
        const auto generatedBuildDir = outputDir / ".ngin" / "cmake-build";
        fs::create_directories(generatedSourceDir);
        fs::create_directories(generatedBuildDir);

        std::unordered_map<std::string, std::vector<fs::path>> generatedSourcesByProject;
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
                generatedSourcesByProject.emplace(unit.project.name, std::move(sources));
            }
        }
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        std::ofstream out(generatedSourceDir / "CMakeLists.txt");
        out << "cmake_minimum_required(VERSION 3.20)\n";
        out << "project(NGINGeneratedBuild LANGUAGES";
        for (const auto &language : languages)
        {
            out << " " << language;
        }
        out << ")\n";
        out << "set(CMAKE_SUPPRESS_REGENERATION ON)\n";

        std::unordered_set<std::string> addedPackageKeys;
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
        if (!report.errors.empty())
        {
            return std::nullopt;
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
            MergePackageReferences(packageRefs, unit.configuration.packageRefs);
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
        if (!report.errors.empty())
        {
            return std::nullopt;
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

        return generatedBuildDir;
    }

    auto BuildArtifacts(const ResolvedLaunch &resolved, const fs::path &outputDir, IssueReport &report) -> void
    {
        const auto generatedBuildDir = WriteGeneratedBuildProject(resolved, outputDir, report);
        if (!generatedBuildDir.has_value() || !report.errors.empty())
        {
            return;
        }

        const auto generatedSourceDir = outputDir / ".ngin" / "cmake-src";
        const auto buildConfiguration = resolved.configuration.buildConfiguration.empty() ? "Debug" : resolved.configuration.buildConfiguration;
        if (RunProcess(
                "cmake",
                {
                    "-S",
                    generatedSourceDir.string(),
                    "-B",
                    generatedBuildDir->string(),
                    "-DCMAKE_BUILD_TYPE=" + buildConfiguration,
                })
            != 0)
        {
            AddError(report, "failed to configure generated CMake build project for configuration '" + resolved.configuration.name + "' with build configuration '" + buildConfiguration + "'");
            return;
        }
        if (RunProcess(
                "cmake",
                {
                    "--build",
                    generatedBuildDir->string(),
                    "--config",
                    buildConfiguration,
                    "--target",
                    "ngin_stage_artifacts",
                })
            != 0)
        {
            AddError(report, "failed to build or stage artifacts for configuration '" + resolved.configuration.name + "' with build configuration '" + buildConfiguration + "'");
        }
    }

    auto CollectBuiltArtifactFiles(
        const fs::path &outputDir,
        std::map<fs::path, std::string> &collisions,
        IssueReport &report,
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

    struct ParsedArgs
    {
        std::optional<std::string> projectPath{};
        std::optional<std::string> configurationName{};
        std::optional<std::string> outputPath{};
        std::optional<std::string> targetDir{};
        std::optional<std::string> packageName{};
        std::vector<std::string> runArgs{};
    };

    auto ParseCommonArgs(int argc, char **argv, int startIndex) -> ParsedArgs
    {
        ParsedArgs args{};
        for (int index = startIndex; index < argc; ++index)
        {
            const std::string current = argv[index];
            if (current == "--project" && index + 1 < argc)
            {
                args.projectPath = argv[++index];
            }
            else if ((current == "--configuration" || current == "--config") && index + 1 < argc)
            {
                args.configurationName = argv[++index];
            }
            else if (current == "--")
            {
                for (int argIndex = index + 1; argIndex < argc; ++argIndex)
                {
                    args.runArgs.push_back(argv[argIndex]);
                }
                break;
            }
            else if ((current == "--output" || current == "--output-dir") && index + 1 < argc)
            {
                args.outputPath = argv[++index];
            }
            else if ((current == "--dependencies" || current == "--externals") && index + 1 < argc)
            {
                args.targetDir = argv[++index];
            }
            else if (current.rfind("--", 0) == 0)
            {
                throw std::runtime_error("unknown option: " + current);
            }
            else if (!args.packageName.has_value())
            {
                args.packageName = current;
            }
            else
            {
                throw std::runtime_error("unexpected argument: " + current);
            }
        }
        return args;
    }

    auto CmdList(const fs::path &root) -> int
    {
        const auto workspace = LoadWorkspaceManifest(root);
        std::cout << "Workspace: " << workspace.name << "\n";
        std::cout << "Projects:\n";
        for (const auto &projectPath : workspace.projects)
        {
            const auto project = LoadProjectManifest(projectPath);
            std::cout << "  - " << project.name << " [" << project.type << "] " << project.path.string() << "\n";
        }
        return 0;
    }

    auto CmdStatus(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)args;
        const auto workspace = LoadWorkspaceManifest(root);
        std::cout << "Workspace: " << workspace.name << "\n";
        std::cout << "  manifest: " << workspace.path.string() << "\n";
        std::cout << "  platform version: " << workspace.platformVersion << "\n";
        std::cout << "Package sources:\n";
        for (const auto &source : workspace.packageSources)
        {
            std::cout << "  - " << source.string() << (fs::exists(source) ? "" : " [missing]") << "\n";
        }
        std::cout << "Projects:\n";
        for (const auto &projectPath : workspace.projects)
        {
            std::cout << "  - " << projectPath.string() << (fs::exists(projectPath) ? "" : " [missing]") << "\n";
        }
        return 0;
    }

    auto CmdDoctor(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)args;
        int fail = 0;
        std::cout << "NGIN workspace doctor\n";
        std::cout << "  root: " << root << "\n";
        std::cout << "  workspace manifest: " << WorkspaceFilePath(root).value_or(root / "<missing>") << "\n";
        if (!ToolExists("git"))
        {
            std::cout << "[error] missing tool: git\n";
            fail = 1;
        }
        else
        {
            std::cout << "[ok] tool: git\n";
        }
        if (!ToolExists("cmake"))
        {
            std::cout << "[error] missing tool: cmake\n";
            fail = 1;
        }
        else
        {
            std::cout << "[ok] tool: cmake\n";
        }
        std::optional<WorkspaceManifest> workspace;
        try
        {
            workspace = LoadWorkspaceManifest(root);
            std::size_t projectsParsed = 0;
            for (const auto &projectPath : workspace->projects)
            {
                (void)LoadProjectManifest(projectPath);
                ++projectsParsed;
            }
            const auto catalog = LoadPackageCatalog(workspace, root);
            std::cout << "[ok] XML manifests parse\n";
            std::cout << "[ok] projects: " << projectsParsed << "\n";
            std::cout << "[ok] packages indexed: " << catalog.size() << "\n";
        }
        catch (const std::exception &ex)
        {
            std::cout << "[error] " << ex.what() << "\n";
            fail = 1;
        }

        if (!workspace.has_value())
        {
            std::cout << "\ndoctor result: FAIL\n";
            return 1;
        }
        for (const auto &source : workspace->packageSources)
        {
            if (!fs::exists(source))
            {
                std::cout << "[warn] package source missing: " << source.string() << "\n";
                fail = 1;
            }
        }
        std::cout << "\ndoctor result: " << (fail == 0 ? "PASS" : "FAIL") << "\n";
        return fail;
    }

    auto CmdPackageList(const fs::path &root) -> int
    {
        const auto workspace = LoadWorkspaceManifest(root);
        const auto catalog = LoadPackageCatalog(workspace, root);
        std::vector<std::string> names;
        for (const auto &[name, _] : catalog)
        {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        for (const auto &name : names)
        {
            const auto &entry = catalog.at(name);
            const auto manifest = LoadPackageManifest(entry.manifestPath);
            std::cout << manifest.name << " " << manifest.version << " " << entry.manifestPath.string();
            if (!entry.providerRoot.empty())
            {
                std::cout << " provider=" << entry.providerRoot.string();
            }
            std::cout << "\n";
        }
        return 0;
    }

    auto CmdPackageShow(const fs::path &root, const ParsedArgs &args) -> int
    {
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("package show requires a package name");
        }
        const auto workspace = LoadWorkspaceManifest(root);
        const auto catalog = LoadPackageCatalog(workspace, root);
        const auto it = catalog.find(*args.packageName);
        if (it == catalog.end())
        {
            throw std::runtime_error("unknown package '" + *args.packageName + "'");
        }
        const auto manifest = LoadPackageManifest(it->second.manifestPath);
        std::cout << "Package: " << manifest.name << "\n";
        std::cout << "  version: " << manifest.version << "\n";
        std::cout << "  manifest: " << manifest.path << "\n";
        std::cout << "  provider root: ";
        if (it->second.providerRoot.empty())
        {
            std::cout << "(none)\n";
        }
        else
        {
            std::cout << it->second.providerRoot << "\n";
        }
        std::cout << "  build backend: " << (manifest.build.backend.empty() ? "(none)" : manifest.build.backend) << "\n";
        std::cout << "  libraries: " << manifest.artifacts.libraries.size() << "\n";
        for (const auto &library : manifest.artifacts.libraries)
        {
            std::cout << "    - " << library.name;
            if (!library.target.empty())
            {
                std::cout << " target=" << library.target;
            }
            if (!library.linkage.empty())
            {
                std::cout << " linkage=" << library.linkage;
            }
            if (!library.origin.empty())
            {
                std::cout << " origin=" << library.origin;
            }
            if (!library.exported)
            {
                std::cout << " internal";
            }
            std::cout << "\n";
        }
        std::cout << "  executables: " << manifest.artifacts.executables.size() << "\n";
        for (const auto &executable : manifest.artifacts.executables)
        {
            std::cout << "    - " << executable.name;
            if (!executable.target.empty())
            {
                std::cout << " target=" << executable.target;
            }
            if (!executable.origin.empty())
            {
                std::cout << " origin=" << executable.origin;
            }
            if (!executable.exported)
            {
                std::cout << " internal";
            }
            std::cout << "\n";
        }
        std::cout << "  platforms:";
        if (manifest.platforms.empty())
        {
            std::cout << " (none)";
        }
        for (const auto &platform : manifest.platforms)
        {
            std::cout << " " << platform;
        }
        std::cout << "\n";
        std::cout << "  dependencies: " << manifest.dependencies.size() << "\n";
        for (const auto &dependency : manifest.dependencies)
        {
            std::cout << "    - " << dependency.name;
            if (!dependency.versionRange.empty())
            {
                std::cout << " " << dependency.versionRange;
            }
            if (dependency.optional)
            {
                std::cout << " optional";
            }
            std::cout << "\n";
        }
        std::cout << "  contents: " << manifest.contents.size() << "\n";
        for (const auto &content : manifest.contents)
        {
            std::cout << "    - " << content.source << " [" << content.kind << "]";
            if (!content.target.empty())
            {
                std::cout << " -> " << content.target;
            }
            std::cout << "\n";
        }
        std::cout << "  modules: " << manifest.modules.size() << "\n";
        for (const auto &module : manifest.modules)
        {
            std::cout << "    - " << module.name << " [" << module.type << "]";
            if (!module.required.empty())
            {
                std::cout << " requires:";
                for (const auto &dep : module.required)
                {
                    std::cout << " " << dep;
                }
            }
            if (!module.optional.empty())
            {
                std::cout << " optional:";
                for (const auto &dep : module.optional)
                {
                    std::cout << " " << dep;
                }
            }
            std::cout << "\n";
        }
        std::cout << "  plugins: " << manifest.plugins.size() << "\n";
        for (const auto &plugin : manifest.plugins)
        {
            std::cout << "    - " << plugin.name;
            if (plugin.optional)
            {
                std::cout << " optional";
            }
            if (!plugin.requiredModules.empty())
            {
                std::cout << " requires:";
                for (const auto &dep : plugin.requiredModules)
                {
                    std::cout << " " << dep;
                }
            }
            if (!plugin.optionalModules.empty())
            {
                std::cout << " optional-modules:";
                for (const auto &dep : plugin.optionalModules)
                {
                    std::cout << " " << dep;
                }
            }
            std::cout << "\n";
        }
        return 0;
    }

    auto CmdValidate(const fs::path &, const ParsedArgs &args) -> int
    {
        const auto project = LoadProjectManifest(ResolveProjectPath(args.projectPath));
        const auto &configuration = ConfigurationByName(project, args.configurationName);
        IssueReport report{};
        const auto resolved = ResolveLaunch(project, configuration, report);
        if (!resolved.has_value() || !report.errors.empty())
        {
            PrintIssues(report, "Validation");
            return 1;
        }
        std::cout << "Validated configuration: " << resolved->configuration.name << "\n";
        std::cout << "  project: " << resolved->project.name << "\n";
        std::cout << "  packages: " << resolved->orderedPackages.size() << "\n";
        std::cout << "  required modules: " << resolved->requiredModules.size() << "\n";
        std::cout << "  optional modules: " << resolved->optionalModules.size() << "\n";
        std::cout << "  libraries: " << resolved->libraries.size() << "\n";
        std::cout << "  executables: " << resolved->executables.size() << "\n";
        std::cout << "  selected executable: " << (resolved->selectedExecutable.has_value() ? resolved->selectedExecutable->name : "(none)") << "\n";
        PrintIssues(report, "Validation");
        return 0;
    }

    auto CmdGraph(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto project = LoadProjectManifest(ResolveProjectPath(args.projectPath));
        const auto &configuration = ConfigurationByName(project, args.configurationName);
        IssueReport report{};
        const auto resolved = ResolveLaunch(project, configuration, report);
        if (!resolved.has_value() || !report.errors.empty())
        {
            PrintIssues(report, "Graph");
            return 1;
        }
        std::cout << "Graph for configuration: " << resolved->configuration.name << "\n\nProjects:\n";
        for (const auto &unit : resolved->projectUnits)
        {
            std::cout << "  - " << unit.project.name << " [" << unit.configuration.name << "]\n";
        }
        std::cout << "\nPackages:\n";
        for (const auto &package : resolved->orderedPackages)
        {
            const auto &edges = resolved->packageEdges.at(package.manifest.name);
            std::cout << "  - " << package.manifest.name << " -> ";
            if (edges.empty())
            {
                std::cout << "(none)";
            }
            else
            {
                bool first = true;
                for (const auto &dep : edges)
                {
                    if (!first)
                    {
                        std::cout << ", ";
                    }
                    std::cout << dep;
                    first = false;
                }
            }
            std::cout << "\n";
        }
        std::cout << "\nModules:\n";
        for (const auto &[name, edges] : resolved->dependencyEdges)
        {
            std::cout << "  - " << name << " -> ";
            if (edges.empty())
            {
                std::cout << "(none)";
            }
            else
            {
                bool first = true;
                for (const auto &dep : edges)
                {
                    if (!first)
                    {
                        std::cout << ", ";
                    }
                    std::cout << dep;
                    first = false;
                }
            }
            std::cout << "\n";
        }
        std::cout << "\nArtifacts:\n";
        for (const auto &library : resolved->libraries)
        {
            std::cout << "  - library " << library.name;
            if (!library.target.empty())
            {
                std::cout << " target=" << library.target;
            }
            if (!library.linkage.empty())
            {
                std::cout << " linkage=" << library.linkage;
            }
            if (!library.origin.empty())
            {
                std::cout << " origin=" << library.origin;
            }
            std::cout << "\n";
        }
        for (const auto &executable : resolved->executables)
        {
            std::cout << "  - executable " << executable.name;
            if (!executable.target.empty())
            {
                std::cout << " target=" << executable.target;
            }
            if (!executable.origin.empty())
            {
                std::cout << " origin=" << executable.origin;
            }
            if (resolved->selectedExecutable.has_value() && resolved->selectedExecutable->name == executable.name)
            {
                std::cout << " selected";
            }
            std::cout << "\n";
        }
        PrintIssues(report, "Graph");
        return 0;
    }

    auto WriteLaunchManifest(const ResolvedLaunch &resolved, const fs::path &outputDir, const std::vector<std::tuple<std::string, fs::path, fs::path>> &staged) -> fs::path
    {
        const auto manifestPath = outputDir / (resolved.project.name + "." + resolved.configuration.name + ".nginlaunch");
        std::ofstream out(manifestPath);
        out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        out << "<LaunchManifest SchemaVersion=\"2\" Configuration=\"" << EscapeXml(resolved.configuration.name)
            << "\" Project=\"" << EscapeXml(resolved.project.name)
            << "\" Type=\"" << EscapeXml(resolved.project.type)
            << "\" BuildConfiguration=\"" << EscapeXml(resolved.configuration.buildConfiguration)
            << "\" HostProfile=\"" << EscapeXml(resolved.configuration.hostProfile.empty() ? "ConsoleApp" : resolved.configuration.hostProfile)
            << "\" Platform=\"" << EscapeXml(resolved.configuration.platform)
            << "\">\n";
        out << "  <Runtime Environment=\"" << EscapeXml(resolved.configuration.environmentName)
            << "\" WorkingDirectory=\"" << EscapeXml(resolved.configuration.workingDirectory)
            << "\" />\n";
        if (resolved.selectedExecutable.has_value())
        {
            out << "  <SelectedExecutable Name=\"" << EscapeXml(resolved.selectedExecutable->name)
                << "\" Target=\"" << EscapeXml(resolved.selectedExecutable->target)
                << "\" Origin=\"" << EscapeXml(resolved.selectedExecutable->origin)
                << "\" />\n";
        }
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

    struct GeneratedLaunchPaths
    {
        fs::path outputDir{};
        fs::path manifestPath{};
    };

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

    auto CleanupPreviousStage(const fs::path &outputDir, IssueReport &report) -> void
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

        std::error_code generatedError;
        fs::remove_all(outputDir / ".ngin", generatedError);
    }

    [[nodiscard]] auto BuildLaunch(const ParsedArgs &args, IssueReport &report) -> std::optional<GeneratedLaunchPaths>
    {
        const auto project = LoadProjectManifest(ResolveProjectPath(args.projectPath));
        const auto &configuration = ConfigurationByName(project, args.configurationName);
        if (!IsSupportedBuildConfiguration(configuration.buildConfiguration))
        {
            AddError(report, "unsupported build configuration '" + configuration.buildConfiguration + "' in configuration '" + configuration.name + "'. Expected one of: Debug, Release, RelWithDebInfo, MinSizeRel");
            return std::nullopt;
        }
        const auto resolved = ResolveLaunch(project, configuration, report);
        if (!resolved.has_value() || !report.errors.empty())
        {
            return std::nullopt;
        }
        const auto buildRoot = resolved->workspace.has_value() ? resolved->workspace->path.parent_path() : resolved->project.path.parent_path();
        const auto outputDir = args.outputPath.has_value()
                                   ? fs::absolute(*args.outputPath)
                                   : (buildRoot / ".ngin" / "build" / resolved->project.name / resolved->configuration.name);
        CleanupPreviousStage(outputDir, report);
        if (!report.errors.empty())
        {
            return std::nullopt;
        }
        fs::create_directories(outputDir);
        std::map<fs::path, std::string> collisions;
        std::vector<std::tuple<std::string, fs::path, fs::path>> staged;

        BuildArtifacts(*resolved, outputDir, report);
        if (!report.errors.empty())
        {
            return std::nullopt;
        }
        CollectBuiltArtifactFiles(outputDir, collisions, report, staged);
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        for (const auto &package : resolved->orderedPackages)
        {
            for (const auto &content : package.manifest.contents)
            {
                const auto source = package.manifest.path.parent_path() / content.source;
                const auto rel = content.target.empty() ? content.source : content.target;
                const auto dest = outputDir / rel;
                if (collisions.contains(dest))
                {
                    AddError(report, "build output collision at '" + rel + "' between packages '" + collisions[dest] + "' and '" + package.manifest.name + "'");
                    continue;
                }
                collisions[dest] = package.manifest.name;
                fs::create_directories(dest.parent_path());
                fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
                staged.emplace_back(content.kind, source, dest);
            }
        }
        for (const auto &config : resolved->configSources)
        {
            const auto source = config.absoluteSourcePath;
            if (!fs::exists(source))
            {
                AddError(report, "missing config source '" + config.source + "' declared by project '" + config.ownerProjectName + "'");
                continue;
            }
            const auto dest = outputDir / config.stagedRelativePath;
            if (collisions.contains(dest))
            {
                AddError(report, "build output collision at config source '" + config.stagedRelativePath.string() + "' declared by project '" + config.ownerProjectName + "'");
                continue;
            }
            collisions[dest] = "<config>";
            fs::create_directories(dest.parent_path());
            fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
            staged.emplace_back("config-source", source, dest);
        }
        if (!report.errors.empty())
        {
            return std::nullopt;
        }
        const auto manifestPath = WriteLaunchManifest(*resolved, outputDir, staged);
        return GeneratedLaunchPaths{
            .outputDir = outputDir,
            .manifestPath = manifestPath,
        };
    }

    auto CmdBuild(const fs::path &, const ParsedArgs &args) -> int
    {
        IssueReport report{};
        const auto built = BuildLaunch(args, report);
        if (!built.has_value() || !report.errors.empty())
        {
            PrintIssues(report, "Build");
            return 1;
        }
        const auto project = LoadProjectManifest(ResolveProjectPath(args.projectPath));
        const auto &configuration = ConfigurationByName(project, args.configurationName);
        const auto resolved = ResolveLaunch(project, configuration, report);
        std::cout << "Built configuration: " << configuration.name << "\n";
        std::cout << "  project: " << resolved->project.name << "\n";
        std::cout << "  output: " << built->outputDir << "\n";
        std::cout << "  launch manifest: " << built->manifestPath << "\n";
        std::cout << "  selected executable: " << (resolved->selectedExecutable.has_value() ? resolved->selectedExecutable->name : "(none)") << "\n";
        PrintIssues(report, "Build");
        return 0;
    }

    struct LaunchManifestSummary
    {
        fs::path manifestPath{};
        std::string configurationName{};
        std::string workingDirectory{"."};
        std::optional<std::string> selectedExecutable{};
    };

    [[nodiscard]] auto LoadLaunchManifestSummary(const fs::path &manifestPath) -> LaunchManifestSummary
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
        if (const auto *runtime = FindChild(*rootElement, "Runtime"))
        {
            summary.workingDirectory = Attribute(*runtime, "WorkingDirectory").value_or(".");
        }
        if (const auto *selectedExecutable = FindChild(*rootElement, "SelectedExecutable"))
        {
            summary.selectedExecutable = Attribute(*selectedExecutable, "Name").value_or("");
        }
        return summary;
    }

    auto CmdRun(const fs::path &, const ParsedArgs &args) -> int
    {
        IssueReport report{};
        const auto built = BuildLaunch(args, report);
        if (!built.has_value() || !report.errors.empty())
        {
            PrintIssues(report, "Run");
            return 1;
        }

        const auto summary = LoadLaunchManifestSummary(built->manifestPath);
        if (!summary.selectedExecutable.has_value() || summary.selectedExecutable->empty())
        {
            throw std::runtime_error("launch manifest does not declare a selected executable");
        }

        const auto executableName = *summary.selectedExecutable + (fs::exists(built->outputDir / "bin" / (*summary.selectedExecutable + ".exe")) ? ".exe" : "");
        const auto executablePath = built->outputDir / "bin" / executableName;
        if (!fs::exists(executablePath))
        {
            throw std::runtime_error("selected executable was not staged to '" + executablePath.string() + "'");
        }

        fs::path workingDirectory = summary.workingDirectory == "."
                                        ? built->outputDir
                                        : fs::absolute(built->outputDir / summary.workingDirectory);
        if (!fs::exists(workingDirectory))
        {
            workingDirectory = built->outputDir;
        }

        return RunProcess(executablePath, args.runArgs, workingDirectory);
    }

    auto PrintHelp() -> void
    {
        std::cout
            << "usage: ngin <command> [options]\n\n"
            << "Commands:\n"
            << "  workspace list\n"
            << "  workspace status\n"
            << "  workspace doctor\n"
            << "  validate [--project <file.nginproj>] [--configuration <name>]\n"
            << "  graph [--project <file.nginproj>] [--configuration <name>]\n"
            << "  build [--project <file.nginproj>] [--configuration <name>] [--output <dir>]\n"
            << "  run [--project <file.nginproj>] [--configuration <name>] [--output <dir>] [-- <args...>]\n"
            << "  package list\n"
            << "  package show <PackageName>\n";
    }
}

auto main(int argc, char **argv) -> int
{
    try
    {
        const auto root = RootDir(argv[0]);
        if (argc < 2)
        {
            PrintHelp();
            return 0;
        }
        const std::string command = argv[1];
        if (command == "workspace")
        {
            if (argc < 3)
            {
                throw std::runtime_error("workspace requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "list")
            {
                return CmdList(root);
            }
            if (subcommand == "status")
            {
                return CmdStatus(root, ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "doctor")
            {
                return CmdDoctor(root, ParseCommonArgs(argc, argv, 3));
            }
            throw std::runtime_error("unknown workspace subcommand '" + subcommand + "'");
        }
        if (command == "package")
        {
            if (argc < 3)
            {
                throw std::runtime_error("package requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "list")
            {
                return CmdPackageList(root);
            }
            if (subcommand == "show")
            {
                return CmdPackageShow(root, ParseCommonArgs(argc, argv, 3));
            }
            throw std::runtime_error("unknown package subcommand '" + subcommand + "'");
        }
        if (command == "list")
        {
            return CmdList(root);
        }
        if (command == "status")
        {
            return CmdStatus(root, ParseCommonArgs(argc, argv, 2));
        }
        if (command == "doctor")
        {
            return CmdDoctor(root, ParseCommonArgs(argc, argv, 2));
        }
        if (command == "validate")
        {
            return CmdValidate(root, ParseCommonArgs(argc, argv, 2));
        }
        if (command == "graph")
        {
            return CmdGraph(root, ParseCommonArgs(argc, argv, 2));
        }
        if (command == "build")
        {
            return CmdBuild(root, ParseCommonArgs(argc, argv, 2));
        }
        if (command == "run")
        {
            return CmdRun(root, ParseCommonArgs(argc, argv, 2));
        }

        throw std::runtime_error("unknown command '" + command + "'");
    }
    catch (const std::exception &ex)
    {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
