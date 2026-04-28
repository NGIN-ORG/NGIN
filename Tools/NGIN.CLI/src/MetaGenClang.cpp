#include "MetaGen.hpp"

#include "Authoring.hpp"
#include "MetaGenCommon.hpp"
#include "Support.hpp"

#include <clang-c/Index.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace NGIN::CLI
{
    namespace
    {
        struct ClangString
        {
            explicit ClangString(CXString value) noexcept : value(value) {}

            ~ClangString()
            {
                clang_disposeString(value);
            }

            ClangString(const ClangString &) = delete;
            auto operator=(const ClangString &) -> ClangString & = delete;

            [[nodiscard]] auto String() const -> std::string
            {
                const char *text = clang_getCString(value);
                return text != nullptr ? std::string(text) : std::string{};
            }

            CXString value{};
        };

        struct MetaGenMember
        {
            std::string cppName{};
            std::string reflectionName{};
            std::string kind{};
            std::vector<std::string> parameterTypes{};
        };

        struct MetaGenProperty
        {
            std::string reflectionName{};
            std::string getterName{};
            std::string setterName{};
        };

        struct MetaGenType
        {
            std::string cppName{};
            std::string reflectionName{};
            std::string kind{};
            fs::path declarationFile{};
            std::vector<MetaGenMember> fields{};
            std::vector<MetaGenProperty> properties{};
            std::vector<MetaGenMember> methods{};
            std::vector<MetaGenMember> constructors{};
            std::vector<MetaGenMember> enumValues{};
            std::vector<std::string> bases{};
        };

        struct RecordCollectContext
        {
            MetaGenType *type{};
            std::vector<std::string> *diagnostics{};
        };

        struct ScanContext
        {
            std::vector<fs::path> sourceFiles{};
            std::vector<fs::path> sourceRoots{};
            std::vector<fs::path> includeFiles{};
            std::vector<MetaGenType> types{};
            std::vector<std::string> diagnostics{};
        };

        [[nodiscard]] auto CursorSpelling(CXCursor cursor) -> std::string
        {
            return ClangString(clang_getCursorSpelling(cursor)).String();
        }

        [[nodiscard]] auto TypeSpelling(CXType type) -> std::string
        {
            return ClangString(clang_getTypeSpelling(type)).String();
        }

        [[nodiscard]] auto CursorFile(CXCursor cursor) -> fs::path
        {
            CXSourceLocation location = clang_getCursorLocation(cursor);
            CXFile file{};
            clang_getSpellingLocation(location, &file, nullptr, nullptr, nullptr);
            if (file == nullptr)
            {
                return {};
            }
            return fs::path(ClangString(clang_getFileName(file)).String()).lexically_normal();
        }

        [[nodiscard]] auto IsInSourceRoots(CXCursor cursor, const ScanContext &context) -> bool
        {
            const auto file = CursorFile(cursor);
            if (file.empty())
            {
                return false;
            }
            for (const auto &sourceFile : context.sourceFiles)
            {
                std::error_code error;
                if (fs::equivalent(file, sourceFile, error))
                {
                    return true;
                }
            }
            for (const auto &sourceRoot : context.sourceRoots)
            {
                if (IsPathWithinDirectory(file, sourceRoot))
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto QualifiedName(CXCursor cursor) -> std::string
        {
            std::vector<std::string> parts{};
            for (CXCursor current = cursor;
                 !clang_Cursor_isNull(current) && clang_getCursorKind(current) != CXCursor_TranslationUnit;
                 current = clang_getCursorSemanticParent(current))
            {
                const auto kind = clang_getCursorKind(current);
                if (kind == CXCursor_Namespace || kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl ||
                    kind == CXCursor_ClassTemplate || kind == CXCursor_EnumDecl)
                {
                    auto spelling = CursorSpelling(current);
                    if (!spelling.empty())
                    {
                        parts.push_back(std::move(spelling));
                    }
                }
            }
            std::reverse(parts.begin(), parts.end());
            std::ostringstream output{};
            for (std::size_t index = 0; index < parts.size(); ++index)
            {
                if (index != 0)
                {
                    output << "::";
                }
                output << parts[index];
            }
            return output.str();
        }

        [[nodiscard]] auto DirectAnnotations(CXCursor cursor) -> std::vector<MetaGen::Annotation>
        {
            std::vector<MetaGen::Annotation> annotations{};
            clang_visitChildren(
                cursor,
                [](CXCursor child, CXCursor, CXClientData data)
                {
                    if (clang_getCursorKind(child) == CXCursor_AnnotateAttr)
                    {
                        auto *out = static_cast<std::vector<MetaGen::Annotation> *>(data);
                        out->push_back(MetaGen::ParseAnnotation(CursorSpelling(child)));
                    }
                    return CXChildVisit_Continue;
                },
                &annotations);
            return annotations;
        }

        [[nodiscard]] auto FindAnnotation(const std::vector<MetaGen::Annotation> &annotations, std::string_view kind)
            -> const MetaGen::Annotation *
        {
            const auto it = std::find_if(annotations.begin(),
                                         annotations.end(),
                                         [&](const MetaGen::Annotation &annotation)
                                         {
                                             return annotation.kind == kind;
                                         });
            return it == annotations.end() ? nullptr : &*it;
        }

        [[nodiscard]] auto IsIgnored(const std::vector<MetaGen::Annotation> &annotations) -> bool
        {
            return FindAnnotation(annotations, "ignore") != nullptr;
        }

        [[nodiscard]] auto IsPublicOrUnspecified(CXCursor cursor) -> bool
        {
            const auto access = clang_getCXXAccessSpecifier(cursor);
            return access == CX_CXXInvalidAccessSpecifier || access == CX_CXXPublic;
        }

        [[nodiscard]] auto OptionOr(const MetaGen::Annotation *annotation, std::string_view key, std::string fallback)
            -> std::string
        {
            if (annotation == nullptr)
            {
                return fallback;
            }
            const auto it = annotation->options.find(std::string(key));
            return it == annotation->options.end() || it->second.empty() ? fallback : it->second;
        }

        [[nodiscard]] auto FindProperty(std::vector<MetaGenProperty> &properties, std::string_view name)
            -> MetaGenProperty *
        {
            const auto it = std::find_if(properties.begin(),
                                         properties.end(),
                                         [&](const MetaGenProperty &property)
                                         {
                                             return property.reflectionName == name;
                                         });
            return it == properties.end() ? nullptr : &*it;
        }

        [[nodiscard]] auto IsVoidType(CXType type) -> bool
        {
            return type.kind == CXType_Void;
        }

        auto AddPropertyMethod(MetaGenType &type,
                               CXCursor cursor,
                               const MetaGen::Annotation &annotation,
                               std::vector<std::string> &diagnostics) -> void
        {
            const auto cppName = CursorSpelling(cursor);
            const auto reflectionName = OptionOr(&annotation, "name", cppName);
            const auto parameterCount = clang_Cursor_getNumArguments(cursor);
            const auto returnsVoid = IsVoidType(clang_getCursorResultType(cursor));

            const bool isGetter = parameterCount == 0 && !returnsVoid;
            const bool isSetter = parameterCount == 1 && returnsVoid;
            if (!isGetter && !isSetter)
            {
                diagnostics.push_back("property method '" + type.cppName + "::" + cppName +
                                      "' must be a getter with no parameters and a non-void return, or a "
                                      "setter with one parameter and void return");
                return;
            }

            auto *property = FindProperty(type.properties, reflectionName);
            if (property == nullptr)
            {
                type.properties.push_back(MetaGenProperty{.reflectionName = reflectionName});
                property = &type.properties.back();
            }

            auto &slot = isGetter ? property->getterName : property->setterName;
            if (!slot.empty())
            {
                diagnostics.push_back("property '" + reflectionName + "' on type '" + type.cppName +
                                      "' has duplicate " + (isGetter ? std::string("getter") : std::string("setter")) +
                                      " annotations");
                return;
            }
            slot = cppName;
        }

        auto ValidateProperties(const MetaGenType &type, std::vector<std::string> &diagnostics) -> void
        {
            for (const auto &property : type.properties)
            {
                if (property.getterName.empty())
                {
                    diagnostics.push_back("property '" + property.reflectionName + "' on type '" + type.cppName +
                                          "' has a setter but no getter");
                }
            }
        }

        auto CollectRecordMembers(CXCursor recordCursor, MetaGenType &type, std::vector<std::string> &diagnostics)
            -> void
        {
            RecordCollectContext context{.type = &type, .diagnostics = &diagnostics};
            clang_visitChildren(
                recordCursor,
                [](CXCursor child, CXCursor, CXClientData data)
                {
                    auto *context = static_cast<RecordCollectContext *>(data);
                    auto *type = context->type;
                    const auto kind = clang_getCursorKind(child);
                    auto annotations = DirectAnnotations(child);
                    if (IsIgnored(annotations))
                    {
                        return CXChildVisit_Continue;
                    }

                    if (!IsPublicOrUnspecified(child))
                    {
                        return CXChildVisit_Continue;
                    }

                    if (kind == CXCursor_FieldDecl)
                    {
                        const auto *field = FindAnnotation(annotations, "field");
                        const auto *property = FindAnnotation(annotations, "property");
                        const auto cppName = CursorSpelling(child);
                        type->fields.push_back(MetaGenMember{
                            .cppName = cppName,
                            .reflectionName = OptionOr(field != nullptr ? field : property, "name", cppName),
                            .kind = "field",
                        });
                        return CXChildVisit_Continue;
                    }

                    if (kind == CXCursor_CXXMethod)
                    {
                        if (const auto *property = FindAnnotation(annotations, "property"))
                        {
                            AddPropertyMethod(*type, child, *property, *context->diagnostics);
                        }

                        const auto *method = FindAnnotation(annotations, "method");
                        if (method == nullptr)
                        {
                            return CXChildVisit_Continue;
                        }

                        const auto cppName = CursorSpelling(child);
                        type->methods.push_back(MetaGenMember{
                            .cppName = cppName,
                            .reflectionName = OptionOr(method, "name", cppName),
                            .kind = "method",
                        });
                        return CXChildVisit_Continue;
                    }

                    if (kind == CXCursor_Constructor && FindAnnotation(annotations, "ctor") != nullptr)
                    {
                        MetaGenMember ctor{};
                        ctor.kind = "ctor";
                        const int count = clang_Cursor_getNumArguments(child);
                        for (int index = 0; index < count; ++index)
                        {
                            CXCursor argument = clang_Cursor_getArgument(child, static_cast<unsigned>(index));
                            ctor.parameterTypes.push_back(TypeSpelling(clang_getCursorType(argument)));
                        }
                        type->constructors.push_back(std::move(ctor));
                        return CXChildVisit_Continue;
                    }

                    if (kind == CXCursor_CXXBaseSpecifier)
                    {
                        const CXCursor referenced = clang_getCursorReferenced(child);
                        auto baseName = QualifiedName(referenced);
                        if (baseName.empty())
                        {
                            baseName = TypeSpelling(clang_getCursorType(child));
                        }
                        type->bases.push_back(std::move(baseName));
                        return CXChildVisit_Continue;
                    }

                    return CXChildVisit_Continue;
                },
                &context);
            ValidateProperties(type, diagnostics);
        }

        auto CollectEnumValues(CXCursor enumCursor, MetaGenType &type) -> void
        {
            clang_visitChildren(
                enumCursor,
                [](CXCursor child, CXCursor, CXClientData data)
                {
                    if (clang_getCursorKind(child) != CXCursor_EnumConstantDecl)
                    {
                        return CXChildVisit_Continue;
                    }
                    auto *type = static_cast<MetaGenType *>(data);
                    const auto annotations = DirectAnnotations(child);
                    if (IsIgnored(annotations))
                    {
                        return CXChildVisit_Continue;
                    }
                    const auto cppName = CursorSpelling(child);
                    const auto *enumValue = FindAnnotation(annotations, "enum_value");
                    type->enumValues.push_back(MetaGenMember{
                        .cppName = cppName,
                        .reflectionName = OptionOr(enumValue, "name", cppName),
                        .kind = "enum_value",
                    });
                    return CXChildVisit_Continue;
                },
                &type);
        }

        [[nodiscard]] auto IsCompiledSourceExtension(const fs::path &path) -> bool;
        [[nodiscard]] auto IsHeaderSourceExtension(const fs::path &path) -> bool;

        auto VisitTranslationUnit(CXCursor cursor, ScanContext &context) -> void
        {
            clang_visitChildren(
                cursor,
                [](CXCursor child, CXCursor, CXClientData data)
                {
                    auto *context = static_cast<ScanContext *>(data);
                    const auto kind = clang_getCursorKind(child);
                    if (!IsInSourceRoots(child, *context))
                    {
                        return CXChildVisit_Recurse;
                    }

                    if (kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl || kind == CXCursor_EnumDecl)
                    {
                        const auto annotations = DirectAnnotations(child);
                        const auto *reflect = FindAnnotation(annotations, "reflect");
                        if (reflect != nullptr && !IsIgnored(annotations))
                        {
                            const auto declarationFile = CursorFile(child);
                            if (IsCompiledSourceExtension(declarationFile))
                            {
                                context->diagnostics.push_back("annotated type '" + QualifiedName(child) +
                                                               "' is declared in compiled source file '" +
                                                               declarationFile.string() +
                                                               "'. Move reflected types to an includable header for "
                                                               "MetaGen.");
                                return CXChildVisit_Continue;
                            }
                            if (!IsHeaderSourceExtension(declarationFile))
                            {
                                context->diagnostics.push_back(
                                    "annotated type '" + QualifiedName(child) + "' is declared in unsupported file '" +
                                    declarationFile.string() +
                                    "'. Move reflected types to a .h/.hpp/.hh/.hxx header for "
                                    "MetaGen.");
                                return CXChildVisit_Continue;
                            }
                            if (std::any_of(context->types.begin(),
                                            context->types.end(),
                                            [&](const MetaGenType &existing)
                                            {
                                                return existing.cppName == QualifiedName(child);
                                            }))
                            {
                                return CXChildVisit_Continue;
                            }

                            MetaGenType type{};
                            type.cppName = QualifiedName(child);
                            type.reflectionName = OptionOr(reflect, "name", type.cppName);
                            type.kind = kind == CXCursor_EnumDecl ? "enum" : "record";
                            type.declarationFile = declarationFile;
                            if (type.kind == "enum")
                            {
                                CollectEnumValues(child, type);
                            }
                            else
                            {
                                CollectRecordMembers(child, type, context->diagnostics);
                            }
                            if (std::none_of(context->includeFiles.begin(),
                                             context->includeFiles.end(),
                                             [&](const fs::path &includeFile)
                                             {
                                                 std::error_code error;
                                                 return fs::equivalent(includeFile, declarationFile, error);
                                             }))
                            {
                                context->includeFiles.push_back(declarationFile);
                            }
                            context->types.push_back(std::move(type));
                            return CXChildVisit_Continue;
                        }
                    }
                    return CXChildVisit_Recurse;
                },
                &context);
        }

        [[nodiscard]] auto IsCompiledSourceExtension(const fs::path &path) -> bool
        {
            const auto extension = Lower(path.extension().string());
            return extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx" ||
                   extension == ".m" || extension == ".mm";
        }

        [[nodiscard]] auto IsHeaderSourceExtension(const fs::path &path) -> bool
        {
            const auto extension = Lower(path.extension().string());
            return extension == ".h" || extension == ".hh" || extension == ".hpp" || extension == ".hxx";
        }

        [[nodiscard]] auto ResolveProjectPathValue(const std::string &value, const ProjectManifest &project) -> fs::path
        {
            fs::path path(value);
            if (path.is_relative())
            {
                path = project.path.parent_path() / path;
            }
            return path.lexically_normal();
        }

        [[nodiscard]] auto SelectorsMatch(const SelectorSet &selectors, const ConfigurationDefinition &configuration) -> bool
        {
            if (selectors.operatingSystem.has_value() && *selectors.operatingSystem != configuration.operatingSystem)
            {
                return false;
            }
            if (selectors.architecture.has_value() && *selectors.architecture != configuration.architecture)
            {
                return false;
            }
            if (selectors.buildConfiguration.has_value() && *selectors.buildConfiguration != configuration.buildConfiguration)
            {
                return false;
            }
            return true;
        }

        [[nodiscard]] auto CollectSourceFiles(const ProjectManifest &project, const ConfigurationDefinition &configuration) -> std::vector<fs::path>
        {
            std::set<fs::path> unique{};
            std::vector<fs::path> sources{};
            auto add = [&](const fs::path &candidate)
            {
                const auto normalized = candidate.lexically_normal();
                if (fs::exists(normalized) && fs::is_regular_file(normalized) &&
                    IsCompiledSourceExtension(normalized) && unique.insert(normalized).second)
                {
                    sources.push_back(normalized);
                }
            };

            if (!project.build.sources.empty())
            {
                for (const auto &source : project.build.sources)
                {
                    add(ResolveProjectPathValue(source, project));
                }
            }
            else if (project.sources.has_value())
            {
                std::vector<fs::path> excludedRoots{};
                std::vector<fs::path> excludedFiles{};
                auto collectExclusions = [&](const SourceGroup &group)
                {
                    for (const auto &root : group.roots)
                    {
                        if (!SelectorsMatch(root.selectors, configuration))
                        {
                            excludedRoots.push_back(ResolveProjectPathValue(root.path, project));
                        }
                    }
                    for (const auto &file : group.files)
                    {
                        if (!SelectorsMatch(file.selectors, configuration))
                        {
                            excludedFiles.push_back(ResolveProjectPathValue(file.path, project));
                        }
                    }
                };
                collectExclusions(project.sources->publicSources);
                collectExclusions(project.sources->privateSources);

                auto isExcluded = [&](const fs::path &candidate)
                {
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

                auto addRoot = [&](const SourceEntry &root)
                {
                    const auto sourceRoot = ResolveProjectPathValue(root.path, project);
                    if (!fs::exists(sourceRoot) || !fs::is_directory(sourceRoot))
                    {
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
                        if (!root.includePatterns.empty() && !AnyGlobMatches(root.includePatterns, relativePath))
                        {
                            continue;
                        }
                        if (!root.excludePatterns.empty() && AnyGlobMatches(root.excludePatterns, relativePath))
                        {
                            continue;
                        }
                        add(entry.path());
                    }
                };
                for (const auto &root : project.sources->publicSources.roots)
                {
                    if (SelectorsMatch(root.selectors, configuration))
                    {
                        addRoot(root);
                    }
                }
                for (const auto &root : project.sources->privateSources.roots)
                {
                    if (SelectorsMatch(root.selectors, configuration))
                    {
                        addRoot(root);
                    }
                }
                for (const auto &file : project.sources->publicSources.files)
                {
                    if (SelectorsMatch(file.selectors, configuration))
                    {
                        add(ResolveProjectPathValue(file.path, project));
                    }
                }
                for (const auto &file : project.sources->privateSources.files)
                {
                    if (SelectorsMatch(file.selectors, configuration))
                    {
                        add(ResolveProjectPathValue(file.path, project));
                    }
                }
            }
            else
            {
                for (const auto &root : project.sourceRoots)
                {
                    const auto sourceRoot = ResolveProjectPathValue(root, project);
                    if (!fs::exists(sourceRoot) || !fs::is_directory(sourceRoot))
                    {
                        continue;
                    }
                    for (const auto &entry : fs::recursive_directory_iterator(sourceRoot))
                    {
                        if (entry.is_regular_file())
                        {
                            add(entry.path());
                        }
                    }
                }
            }
            std::sort(sources.begin(), sources.end());
            return sources;
        }

        [[nodiscard]] auto CollectSourceRoots(const ProjectManifest &project, const ConfigurationDefinition &configuration) -> std::vector<fs::path>
        {
            std::vector<fs::path> roots{};
            for (const auto &root : project.sourceRoots)
            {
                roots.push_back(ResolveProjectPathValue(root, project));
            }
            if (project.sources.has_value())
            {
                for (const auto &root : project.sources->publicSources.roots)
                {
                    if (SelectorsMatch(root.selectors, configuration))
                    {
                        roots.push_back(ResolveProjectPathValue(root.path, project));
                    }
                }
                for (const auto &root : project.sources->privateSources.roots)
                {
                    if (SelectorsMatch(root.selectors, configuration))
                    {
                        roots.push_back(ResolveProjectPathValue(root.path, project));
                    }
                }
            }
            return roots;
        }

        [[nodiscard]] auto DefaultOutputDir(const ProjectManifest &project,
                                            const ConfigurationDefinition &configuration) -> fs::path
        {
            return fs::current_path() / ".ngin" / "metagen" / project.name / configuration.name;
        }

        [[nodiscard]] auto BuildClangArguments(const fs::path &root, const ProjectManifest &project, const ConfigurationDefinition &configuration)
            -> std::vector<std::string>
        {
            std::vector<std::string> args{
                "-x",
                "c++",
                "-std=c++" +
                    (project.build.languageStandard.empty() ? std::string("23") : project.build.languageStandard),
                "-DNGIN_METAGEN_SCAN=1",
            };

            auto include = [&](const fs::path &path)
            {
                args.push_back("-I" + path.string());
            };

            include(project.path.parent_path());
            for (const auto &rootPath : CollectSourceRoots(project, configuration))
            {
                include(rootPath);
            }
            include(root / "Dependencies/NGIN/NGIN.Reflection/include");
            include(root / "Dependencies/NGIN/NGIN.Base/include");
            include(root / "Packages/NGIN.Core/include");
            include(root / "Packages/NGIN.Reflection/include");
            include(root / "Packages/NGIN.Base/include");
            return args;
        }

        auto ScanSources(const fs::path &root,
                         const ProjectManifest &project,
                         const ConfigurationDefinition &configuration,
                         ScanContext &context) -> void
        {
            auto clangArgs = BuildClangArguments(root, project, configuration);
            std::vector<const char *> rawArgs{};
            rawArgs.reserve(clangArgs.size());
            for (const auto &arg : clangArgs)
            {
                rawArgs.push_back(arg.c_str());
            }

            CXIndex index = clang_createIndex(0, 0);
            for (const auto &source : context.sourceFiles)
            {
                CXTranslationUnit unit{};
                const auto error = clang_parseTranslationUnit2(index,
                                                               source.string().c_str(),
                                                               rawArgs.data(),
                                                               static_cast<int>(rawArgs.size()),
                                                               nullptr,
                                                               0,
                                                               CXTranslationUnit_SkipFunctionBodies,
                                                               &unit);
                if (error != CXError_Success || unit == nullptr)
                {
                    context.diagnostics.push_back("failed to parse source file '" + source.string() + "'");
                    continue;
                }
                VisitTranslationUnit(clang_getTranslationUnitCursor(unit), context);
                clang_disposeTranslationUnit(unit);
            }
            clang_disposeIndex(index);
        }

        [[nodiscard]] auto EmitGeneratedCpp(const ProjectManifest &project,
                                            const ConfigurationDefinition &configuration,
                                            const ScanContext &context) -> std::string
        {
            std::ostringstream out{};
            const auto functionName = "Register_" + MetaGen::SanitizeIdentifier(project.name) + "_" +
                                      MetaGen::SanitizeIdentifier(configuration.name) + "_Reflection";

            out << "// <auto-generated>\n";
            out << "// Generated by ngin metagen. Do not edit by hand.\n";
            out << "// </auto-generated>\n\n";
            out << "#include <NGIN/Reflection/Reflection.hpp>\n\n";
            for (const auto &source : context.includeFiles)
            {
                out << "#include \"" << MetaGen::EscapeCppString(source.string()) << "\"\n";
            }
            out << "\n";

            out << "namespace NGIN::Reflection\n";
            out << "{\n";
            for (const auto &type : context.types)
            {
                out << "  template <>\n";
                out << "  struct Describe<" << type.cppName << ">\n";
                out << "  {\n";
                out << "    static void Do(TypeBuilder<" << type.cppName << "> &type)\n";
                out << "    {\n";
                out << "      type.SetName(\"" << MetaGen::EscapeCppString(type.reflectionName) << "\");\n";
                if (type.kind == "enum")
                {
                    for (const auto &value : type.enumValues)
                    {
                        out << "      type.EnumValue(\"" << MetaGen::EscapeCppString(value.reflectionName) << "\", "
                            << type.cppName << "::" << value.cppName << ");\n";
                    }
                }
                else
                {
                    for (const auto &base : type.bases)
                    {
                        out << "      type.Base<" << base << ">();\n";
                    }
                    for (const auto &field : type.fields)
                    {
                        out << "      type.Field<&" << type.cppName << "::" << field.cppName << ">(\""
                            << MetaGen::EscapeCppString(field.reflectionName) << "\");\n";
                    }
                    for (const auto &property : type.properties)
                    {
                        out << "      type.Property<&" << type.cppName << "::" << property.getterName;
                        if (!property.setterName.empty())
                        {
                            out << ", &" << type.cppName << "::" << property.setterName;
                        }
                        out << ">(\"" << MetaGen::EscapeCppString(property.reflectionName) << "\");\n";
                    }
                    for (const auto &method : type.methods)
                    {
                        out << "      type.Method<&" << type.cppName << "::" << method.cppName << ">(\""
                            << MetaGen::EscapeCppString(method.reflectionName) << "\");\n";
                    }
                    for (const auto &constructor : type.constructors)
                    {
                        out << "      type.Constructor<";
                        for (std::size_t index = 0; index < constructor.parameterTypes.size(); ++index)
                        {
                            if (index != 0)
                            {
                                out << ", ";
                            }
                            out << constructor.parameterTypes[index];
                        }
                        out << ">();\n";
                    }
                }
                out << "    }\n";
                out << "  };\n\n";
            }
            out << "} // namespace NGIN::Reflection\n\n";

            out << "namespace\n";
            out << "{\n";
            out << "  bool RegisterGeneratedReflectionModule()\n";
            out << "  {\n";
            out << "    return NGIN::Reflection::EnsureModuleInitialized(\n";
            out << "      \"" << MetaGen::EscapeCppString(project.name) << ".Reflection\",\n";
            out << "      [](NGIN::Reflection::ModuleRegistration &module)\n";
            out << "      {\n";
            for (const auto &type : context.types)
            {
                out << "        module.RegisterType<" << type.cppName << ">();\n";
            }
            out << "      });\n";
            out << "  }\n\n";
            out << "  [[maybe_unused]] const bool g_registeredGeneratedReflectionModule "
                   "= RegisterGeneratedReflectionModule();\n";
            out << "} // namespace\n\n";

            out << "extern \"C\" bool " << functionName << "()\n";
            out << "{\n";
            out << "  return RegisterGeneratedReflectionModule();\n";
            out << "}\n";
            return out.str();
        }
    } // namespace

    auto GenerateMetaData(const fs::path &root,
                          const ProjectManifest &project,
                          const ConfigurationDefinition &configuration,
                          const fs::path &outputDir) -> MetaGenResult
    {
        (void)root;
        MetaGenResult result{};
        ScanContext context{};
        context.sourceFiles = CollectSourceFiles(project, configuration);
        context.sourceRoots = CollectSourceRoots(project, configuration);
        if (context.sourceFiles.empty())
        {
            result.diagnostics.push_back("project '" + project.name + "' has no C++ source files to scan");
            return result;
        }

        ScanSources(root, project, configuration, context);
        if (!context.diagnostics.empty())
        {
            result.diagnostics = std::move(context.diagnostics);
            return result;
        }

        fs::create_directories(outputDir);
        const auto outputFile = outputDir / (project.name + ".reflection.generated.cpp");
        const auto generated = EmitGeneratedCpp(project, configuration, context);
        std::ofstream out(outputFile);
        if (!out)
        {
            result.diagnostics.push_back("failed to write generated file '" + outputFile.string() + "'");
            return result;
        }
        out << generated;

        result.generatedFiles.push_back(outputFile);
        result.reflectedTypeCount = context.types.size();
        return result;
    }

    auto RunMetaGen(const fs::path &root,
                    const ProjectManifest &project,
                    const ConfigurationDefinition &configuration,
                    const std::optional<std::string> &outputPath) -> int
    {
        const auto outputDir =
            outputPath.has_value() ? fs::path(*outputPath) : DefaultOutputDir(project, configuration);
        auto result = GenerateMetaData(root, project, configuration, outputDir);
        if (!result.available || !result.diagnostics.empty())
        {
            for (const auto &diagnostic : result.diagnostics)
            {
                std::cerr << "error: " << diagnostic << "\n";
            }
            return 1;
        }

        for (const auto &generatedFile : result.generatedFiles)
        {
            std::cout << "generated: " << generatedFile.string() << "\n";
        }
        std::cout << "reflected types: " << result.reflectedTypeCount << "\n";
        return 0;
    }
} // namespace NGIN::CLI
