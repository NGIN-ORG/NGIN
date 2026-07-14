#include "MetaGenContext.hpp"
#include "MetaGenCommon.hpp"

#include <clang-c/Index.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace NGIN::Reflection::MetaGen
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
            std::vector<std::string> sourceFileKeys{};
            std::vector<std::string> sourceRootKeys{};
            std::unordered_map<CXFile, bool> sourceMembership{};
            std::vector<fs::path> includeFiles{};
            std::vector<MetaGenType> types{};
            std::vector<std::string> diagnostics{};
        };

        [[nodiscard]] auto Lower(std::string value) -> std::string
        {
            std::transform(value.begin(),
                           value.end(),
                           value.begin(),
                           [](const unsigned char ch)
                           {
                               return static_cast<char>(std::tolower(ch));
                           });
            return value;
        }

        [[nodiscard]] auto NormalizedPathKey(const fs::path &path) -> std::string
        {
            std::error_code error;
            const auto canonical = fs::weakly_canonical(path, error);
            auto key = (error ? path.lexically_normal() : canonical).generic_string();
#ifdef _WIN32
            key = Lower(std::move(key));
#endif
            return key;
        }

        [[nodiscard]] auto IsPathWithinDirectory(std::string_view candidate, std::string_view directory) -> bool
        {
            if (candidate == directory)
            {
                return true;
            }
            return candidate.size() > directory.size() && candidate.starts_with(directory) &&
                   (directory.ends_with('/') || candidate[directory.size()] == '/');
        }

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

        [[nodiscard]] auto CursorFileHandle(CXCursor cursor) -> CXFile
        {
            CXSourceLocation location = clang_getCursorLocation(cursor);
            CXFile file{};
            clang_getSpellingLocation(location, &file, nullptr, nullptr, nullptr);
            return file;
        }

        [[nodiscard]] auto IsInSourceRoots(CXFile file, ScanContext &context) -> bool
        {
            if (const auto cached = context.sourceMembership.find(file); cached != context.sourceMembership.end())
            {
                return cached->second;
            }

            const auto fileKey = NormalizedPathKey(ClangString(clang_getFileName(file)).String());
            auto selected = std::find(context.sourceFileKeys.begin(), context.sourceFileKeys.end(), fileKey) !=
                            context.sourceFileKeys.end();
            if (!selected)
            {
                selected = std::any_of(context.sourceRootKeys.begin(),
                                       context.sourceRootKeys.end(),
                                       [&](const std::string &root)
                                       {
                                           return IsPathWithinDirectory(fileKey, root);
                                       });
            }
            context.sourceMembership.emplace(file, selected);
            return selected;
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

        [[nodiscard]] auto DirectAnnotations(CXCursor cursor) -> std::vector<Annotation>
        {
            std::vector<Annotation> annotations{};
            clang_visitChildren(
                cursor,
                [](CXCursor child, CXCursor, CXClientData data)
                {
                    if (clang_getCursorKind(child) == CXCursor_AnnotateAttr)
                    {
                        auto *out = static_cast<std::vector<Annotation> *>(data);
                        out->push_back(ParseAnnotation(CursorSpelling(child)));
                    }
                    return CXChildVisit_Continue;
                },
                &annotations);
            return annotations;
        }

        [[nodiscard]] auto FindAnnotation(const std::vector<Annotation> &annotations, std::string_view kind)
            -> const Annotation *
        {
            const auto it = std::find_if(annotations.begin(),
                                         annotations.end(),
                                         [&](const Annotation &annotation)
                                         {
                                             return annotation.kind == kind;
                                         });
            return it == annotations.end() ? nullptr : &*it;
        }

        [[nodiscard]] auto IsIgnored(const std::vector<Annotation> &annotations) -> bool
        {
            return FindAnnotation(annotations, "ignore") != nullptr;
        }

        [[nodiscard]] auto IsPublicOrUnspecified(CXCursor cursor) -> bool
        {
            const auto access = clang_getCXXAccessSpecifier(cursor);
            return access == CX_CXXInvalidAccessSpecifier || access == CX_CXXPublic;
        }

        [[nodiscard]] auto OptionOr(const Annotation *annotation, std::string_view key, std::string fallback)
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
                               const Annotation &annotation,
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
                    const auto file = CursorFileHandle(child);
                    if (file == nullptr)
                    {
                        return CXChildVisit_Recurse;
                    }
                    if (!IsInSourceRoots(file, *context))
                    {
                        return CXChildVisit_Continue;
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

        [[nodiscard]] auto BuildClangArguments(const MetaGenContext &context)
            -> std::vector<std::string>
        {
            std::vector<std::string> args{
                "-x",
                "c++",
                "-std=c++" + (context.languageStandard.empty() ? std::string("23") : context.languageStandard),
                "-DNGIN_METAGEN_SCAN=1",
            };

            auto include = [&](const fs::path &path)
            {
                args.push_back("-I" + path.string());
            };

            include(context.projectDir);
            for (const auto &rootPath : context.sourceRoots)
            {
                include(rootPath);
            }
            for (const auto &includeDirectory : context.includeDirectories)
            {
                include(includeDirectory);
            }
            for (const auto &definition : context.compileDefinitions)
            {
                args.push_back(definition.starts_with("-D") ? definition : "-D" + definition);
            }
            for (const auto &option : context.compileOptions)
            {
                args.push_back(option);
            }
            return args;
        }

        auto ScanSources(const MetaGenContext &metaGenContext, ScanContext &context) -> void
        {
            auto clangArgs = BuildClangArguments(metaGenContext);
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
                context.sourceMembership.clear();
                VisitTranslationUnit(clang_getTranslationUnitCursor(unit), context);
                clang_disposeTranslationUnit(unit);
            }
            clang_disposeIndex(index);
        }

        [[nodiscard]] auto EmitGeneratedCpp(const MetaGenContext &metaGenContext,
                                            const ScanContext &context) -> std::string
        {
            std::ostringstream out{};
            const auto functionName = "Register_" + SanitizeIdentifier(metaGenContext.projectName) + "_" +
                                      SanitizeIdentifier(metaGenContext.profileName) + "_Reflection";

            out << "// <auto-generated>\n";
            out << "// Generated by NGIN MetaGen. Do not edit by hand.\n";
            out << "// </auto-generated>\n\n";
            out << "#include <NGIN/Reflection/Reflection.hpp>\n\n";
            for (const auto &source : context.includeFiles)
            {
                out << "#include \"" << EscapeCppString(source.string()) << "\"\n";
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
                out << "      type.SetName(\"" << EscapeCppString(type.reflectionName) << "\");\n";
                if (type.kind == "enum")
                {
                    for (const auto &value : type.enumValues)
                    {
                        out << "      type.EnumValue(\"" << EscapeCppString(value.reflectionName) << "\", "
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
                            << EscapeCppString(field.reflectionName) << "\");\n";
                    }
                    for (const auto &property : type.properties)
                    {
                        out << "      type.Property<&" << type.cppName << "::" << property.getterName;
                        if (!property.setterName.empty())
                        {
                            out << ", &" << type.cppName << "::" << property.setterName;
                        }
                        out << ">(\"" << EscapeCppString(property.reflectionName) << "\");\n";
                    }
                    for (const auto &method : type.methods)
                    {
                        out << "      type.Method<&" << type.cppName << "::" << method.cppName << ">(\""
                            << EscapeCppString(method.reflectionName) << "\");\n";
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
            out << "      \"" << EscapeCppString(metaGenContext.projectName) << ".Reflection\",\n";
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

    auto GenerateMetaData(const MetaGenContext &metaGenContext) -> MetaGenResult
    {
        MetaGenResult result{};
        ScanContext context{};
        context.sourceFiles = metaGenContext.sourceFiles;
        context.sourceRoots = metaGenContext.sourceRoots;
        context.sourceFileKeys.reserve(context.sourceFiles.size());
        for (const auto &sourceFile : context.sourceFiles)
        {
            context.sourceFileKeys.push_back(NormalizedPathKey(sourceFile));
        }
        context.sourceRootKeys.reserve(context.sourceRoots.size());
        for (const auto &sourceRoot : context.sourceRoots)
        {
            context.sourceRootKeys.push_back(NormalizedPathKey(sourceRoot));
        }
        if (context.sourceFiles.empty())
        {
            result.diagnostics.push_back("project '" + metaGenContext.projectName + "' has no C++ source files to scan");
            return result;
        }
        if (metaGenContext.outputs.empty())
        {
            result.diagnostics.push_back("generator context declares no generated source outputs");
            return result;
        }

        ScanSources(metaGenContext, context);
        if (!context.diagnostics.empty())
        {
            result.diagnostics = std::move(context.diagnostics);
            return result;
        }

        const auto outputFile = metaGenContext.outputs.front();
        fs::create_directories(outputFile.parent_path());
        const auto generated = EmitGeneratedCpp(metaGenContext, context);
        std::ifstream existingInput(outputFile, std::ios::binary);
        const auto outputExists = static_cast<bool>(existingInput);
        std::ostringstream existing{};
        if (outputExists)
        {
            existing << existingInput.rdbuf();
        }
        if (!outputExists || existing.str() != generated)
        {
            std::ofstream out(outputFile, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                result.diagnostics.push_back("failed to write generated file '" + outputFile.string() + "'");
                return result;
            }
            out << generated;
        }

        result.generatedFiles.push_back(outputFile);
        result.reflectedTypeCount = context.types.size();
        return result;
    }
} // namespace NGIN::Reflection::MetaGen
