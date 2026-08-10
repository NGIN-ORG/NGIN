#include "ManifestPaths.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto Utf8Path(const std::filesystem::path &path) -> std::string
        {
            const auto utf8 = path.generic_u8string();
            return {reinterpret_cast<const char *>(utf8.data()), utf8.size()};
        }

        auto AddPathError(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message,
                          const ManifestSourceRange &source) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                     .code = std::move(code),
                                                     .message = std::move(message),
                                                     .source = source});
        }

        [[nodiscard]] auto IsAbsolutePortable(const std::string_view value) -> bool
        {
            return value.starts_with('/') || value.starts_with("//") ||
                   (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) != 0 && value[1] == ':');
        }

        [[nodiscard]] auto IsWithin(const std::filesystem::path &candidate, const std::filesystem::path &root) -> bool
        {
            const auto relative = candidate.lexically_relative(root);
            return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
        }

        [[nodiscard]] auto RegexEscape(const char ch) -> std::string
        {
            static constexpr std::string_view special = R"(.^$|(){}+\)";
            return special.find(ch) == std::string_view::npos ? std::string(1, ch) : "\\" + std::string(1, ch);
        }

        [[nodiscard]] auto SegmentRegex(const std::string_view segment) -> std::optional<std::string>
        {
            std::string regex{};
            for (std::size_t index = 0; index < segment.size(); ++index)
            {
                const auto ch = segment[index];
                if (ch == '*')
                {
                    regex += "[^/]*";
                }
                else if (ch == '?')
                {
                    regex += "[^/]";
                }
                else if (ch == '[')
                {
                    const auto close = segment.find(']', index + 1);
                    if (close == std::string_view::npos || close == index + 1)
                    {
                        return std::nullopt;
                    }
                    regex += std::string(segment.substr(index, close - index + 1));
                    index = close;
                }
                else
                {
                    regex += RegexEscape(ch);
                }
            }
            return regex;
        }

        [[nodiscard]] auto GlobRegex(const std::string_view pattern) -> std::optional<std::regex>
        {
            std::vector<std::string> segments{};
            std::size_t start = 0;
            while (start <= pattern.size())
            {
                const auto slash = pattern.find('/', start);
                segments.emplace_back(
                    pattern.substr(start, slash == std::string_view::npos ? pattern.size() - start : slash - start));
                if (slash == std::string_view::npos) break;
                start = slash + 1;
            }
            std::string expression{"^"};
            for (std::size_t index = 0; index < segments.size(); ++index)
            {
                const auto &segment = segments[index];
                if (segment == "**")
                {
                    expression += index + 1 == segments.size() ? "(?:[^/]+(?:/[^/]+)*)?" : "(?:[^/]+/)*";
                    continue;
                }
                if (segment.find("**") != std::string::npos)
                {
                    return std::nullopt;
                }
                const auto converted = SegmentRegex(segment);
                if (!converted.has_value()) return std::nullopt;
                expression += *converted;
                if (index + 1 != segments.size()) expression += '/';
            }
            expression += '$';
            try
            {
                return std::regex(expression, std::regex::ECMAScript);
            }
            catch (const std::regex_error &)
            {
                return std::nullopt;
            }
        }

        [[nodiscard]] auto AsciiCaseFold(std::string value) -> std::string
        {
            std::ranges::transform(value, value.begin(),
                                   [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }
    } // namespace

    auto PortablePathResult::Succeeded() const -> bool { return value.has_value() && diagnostics.empty(); }
    auto GlobResult::Succeeded() const -> bool { return diagnostics.empty(); }

    auto NormalizePortablePath(const std::string_view authored, const PortablePathBase base,
                               const ManifestSourceRange &source) -> PortablePathResult
    {
        PortablePathResult result{};
        if (authored.empty())
        {
            AddPathError(result.diagnostics, "NGIN2007", "portable path cannot be empty", source);
            return result;
        }
        if (authored.find('\\') != std::string_view::npos)
        {
            AddPathError(result.diagnostics, "NGIN2007", "portable paths use '/' and reject '\\'", source);
            return result;
        }
        if (IsAbsolutePortable(authored))
        {
            AddPathError(result.diagnostics, "NGIN2007", "portable authored paths must be relative", source);
            return result;
        }

        // "." explicitly names the manifest directory. It is a useful and
        // unambiguous integration root or working directory, not an empty path.
        if (authored == ".")
        {
            result.value = PortablePath{.value = ".", .base = base};
            return result;
        }

        std::vector<std::string> segments{};
        std::size_t start = 0;
        while (start <= authored.size())
        {
            const auto slash = authored.find('/', start);
            const auto segment =
                authored.substr(start, slash == std::string_view::npos ? authored.size() - start : slash - start);
            if (segment.empty() || segment == ".")
            {
            }
            else if (segment == "..")
            {
                if (!segments.empty() && segments.back() != "..")
                {
                    segments.pop_back();
                }
                else
                {
                    segments.emplace_back(segment);
                }
            }
            else
            {
                segments.emplace_back(segment);
            }
            if (slash == std::string_view::npos) break;
            start = slash + 1;
        }
        if (segments.empty())
        {
            AddPathError(result.diagnostics, "NGIN2007", "portable path normalizes to an empty path", source);
            return result;
        }
        std::ostringstream normalized;
        for (std::size_t index = 0; index < segments.size(); ++index)
        {
            if (index != 0) normalized << '/';
            normalized << segments[index];
        }
        result.value = PortablePath{.value = normalized.str(), .base = base};
        return result;
    }

    auto ResolvePortablePath(const PortablePath &path, const std::filesystem::path &manifestDirectory,
                             const std::filesystem::path &workspaceRoot, const std::filesystem::path &allowedRoot,
                             const ManifestSourceRange &source) -> PortablePathResult
    {
        PortablePathResult result{};
        if (path.base == PortablePathBase::ActionOutput)
        {
            AddPathError(result.diagnostics, "NGIN2007",
                         "Action output paths resolve only against an ActionPlan output root", source);
            return result;
        }
        std::error_code error{};
        const auto base = path.base == PortablePathBase::Workspace ? workspaceRoot : manifestDirectory;
        const auto canonicalRoot = std::filesystem::weakly_canonical(allowedRoot, error);
        if (error)
        {
            AddPathError(result.diagnostics, "NGIN2007", "cannot resolve allowed path root: " + error.message(),
                         source);
            return result;
        }
        const auto candidate = std::filesystem::weakly_canonical(base / std::filesystem::path(path.value), error);
        if (error)
        {
            AddPathError(result.diagnostics, "NGIN2007", "cannot resolve authored path: " + error.message(), source);
            return result;
        }
        if (!IsWithin(candidate, canonicalRoot) && candidate != canonicalRoot)
        {
            AddPathError(result.diagnostics, "NGIN2007", "resolved path escapes its allowed root", source);
            return result;
        }
        const auto relative = Utf8Path(candidate.lexically_relative(canonicalRoot));
        std::error_code workspaceError{};
        const auto canonicalWorkspace = std::filesystem::weakly_canonical(workspaceRoot, workspaceError);
        result.value =
            PortablePath{.value = relative,
                         .base = !workspaceError && canonicalRoot == canonicalWorkspace ? PortablePathBase::Workspace
                                                                                        : PortablePathBase::Manifest};
        return result;
    }

    auto NormalizeStageDestination(const std::string_view authored, const ManifestSourceRange &source)
        -> PortablePathResult
    {
        auto result = NormalizePortablePath(authored, PortablePathBase::Manifest, source);
        if (result.Succeeded() && (result.value->value == ".." || result.value->value.starts_with("../")))
        {
            result.value.reset();
            AddPathError(result.diagnostics, "NGIN2007", "stage destination escapes the stage root", source);
        }
        return result;
    }

    auto GlobMatchesPortable(const std::string_view pattern, const std::string_view portablePath) -> bool
    {
        const auto regex = GlobRegex(pattern);
        return regex.has_value() && std::regex_match(portablePath.begin(), portablePath.end(), *regex);
    }

    auto ValidateTargetPathCaseCollisions(const std::span<const PortablePath> paths, const bool targetCaseInsensitive,
                                          const ManifestSourceRange &source) -> std::vector<ManifestDiagnostic>
    {
        std::vector<ManifestDiagnostic> diagnostics{};
        if (!targetCaseInsensitive) return diagnostics;
        std::map<std::string, std::string, std::less<>> identities{};
        for (const auto &path : paths)
        {
            const auto folded = AsciiCaseFold(path.value);
            if (const auto existing = identities.find(folded);
                existing != identities.end() && existing->second != path.value)
            {
                AddPathError(diagnostics, "NGIN2009",
                             "case-insensitive target path collision between '" + existing->second + "' and '" +
                                 path.value + "'",
                             source);
            }
            else
            {
                identities.emplace(folded, path.value);
            }
        }
        return diagnostics;
    }

    auto ExpandPortableGlob(const std::filesystem::path &root, const std::string_view pattern,
                            const bool targetCaseInsensitive, const ManifestSourceRange &source,
                            const bool allowSymlinks) -> GlobResult
    {
        GlobResult result{};
        const auto normalizedPattern = NormalizePortablePath(pattern, PortablePathBase::Manifest, source);
        if (!normalizedPattern.Succeeded() || normalizedPattern.value->value == ".." ||
            normalizedPattern.value->value.starts_with("../") || !GlobRegex(normalizedPattern.value->value).has_value())
        {
            result.diagnostics = normalizedPattern.diagnostics;
            if (result.diagnostics.empty())
            {
                AddPathError(result.diagnostics, "NGIN2008", "invalid portable glob pattern", source);
            }
            return result;
        }

        std::error_code error{};
        const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
        if (error || !std::filesystem::is_directory(canonicalRoot, error))
        {
            AddPathError(result.diagnostics, "NGIN2008", "glob root is not a readable directory", source);
            return result;
        }

        std::map<std::string, PortablePath, std::less<>> matches{};
        std::map<std::string, std::string, std::less<>> caseIdentities{};
        std::set<std::string, std::less<>> activeDirectories{};
        std::function<void(const std::filesystem::path &)> visit;
        visit = [&](const std::filesystem::path &directory) {
            std::error_code localError{};
            const auto canonicalDirectory = std::filesystem::canonical(directory, localError);
            if (localError)
            {
                AddPathError(result.diagnostics, "NGIN2008", "cannot resolve glob directory: " + localError.message(),
                             source);
                return;
            }
            const auto directoryIdentity = Utf8Path(canonicalDirectory);
            if (!activeDirectories.insert(directoryIdentity).second)
            {
                AddPathError(result.diagnostics, "NGIN2008", "symlink cycle encountered while expanding glob", source);
                return;
            }

            std::vector<std::filesystem::directory_entry> entries{};
            for (std::filesystem::directory_iterator iterator(directory, localError), end;
                 !localError && iterator != end; iterator.increment(localError))
            {
                entries.push_back(*iterator);
            }
            if (localError)
            {
                AddPathError(result.diagnostics, "NGIN2008", "cannot enumerate glob directory: " + localError.message(),
                             source);
                activeDirectories.erase(directoryIdentity);
                return;
            }
            std::ranges::sort(entries, [](const auto &left, const auto &right) {
                return Utf8Path(left.path().filename()) < Utf8Path(right.path().filename());
            });
            for (const auto &entry : entries)
            {
                if (entry.is_symlink(localError) && !allowSymlinks)
                {
                    AddPathError(result.diagnostics, "NGIN2008",
                                 "symlink encountered while workspace policy disallows symlinks", source);
                    continue;
                }
                const auto canonicalEntry = std::filesystem::canonical(entry.path(), localError);
                if (localError)
                {
                    AddPathError(result.diagnostics, "NGIN2008", "cannot resolve glob entry: " + localError.message(),
                                 source);
                    localError.clear();
                    continue;
                }
                if (!IsWithin(canonicalEntry, canonicalRoot))
                {
                    AddPathError(result.diagnostics, "NGIN2008", "symlink escapes the glob root", source);
                    continue;
                }
                if (entry.is_directory(localError))
                {
                    visit(entry.path());
                    continue;
                }
                if (!entry.is_regular_file(localError)) continue;
                const auto relative = Utf8Path(entry.path().lexically_relative(root));
                if (!GlobMatchesPortable(normalizedPattern.value->value, relative)) continue;
                const auto folded = targetCaseInsensitive ? AsciiCaseFold(relative) : relative;
                if (const auto existing = caseIdentities.find(folded);
                    existing != caseIdentities.end() && existing->second != relative)
                {
                    AddPathError(result.diagnostics, "NGIN2009",
                                 "case-insensitive target path collision between '" + existing->second + "' and '" +
                                     relative + "'",
                                 source);
                    continue;
                }
                caseIdentities[folded] = relative;
                matches[relative] = PortablePath{.value = relative, .base = PortablePathBase::Manifest};
            }
            activeDirectories.erase(directoryIdentity);
        };
        // Begin at the non-pattern directory prefix. Besides avoiding needless
        // traversal of large workspaces, this keeps an Examples-only discovery
        // declaration from inspecting unrelated dependency and build trees.
        auto visitRoot = root;
        const auto &normalizedText = normalizedPattern.value->value;
        const auto wildcard = normalizedText.find_first_of("*?[");
        const auto fixedPrefix = normalizedText.substr(0, wildcard);
        if (const auto slash = fixedPrefix.rfind('/'); slash != std::string::npos)
            visitRoot /= std::filesystem::path(fixedPrefix.substr(0, slash));
        if (std::filesystem::is_directory(visitRoot, error)) visit(visitRoot);
        for (auto &[_, match] : matches) result.matches.push_back(std::move(match));
        return result;
    }
} // namespace NGIN::CLI
