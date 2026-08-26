#include "MetaGenEmitter.hpp"
#include "MetaGenInspection.hpp"
#include "MetaGenScanner.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
namespace fs = std::filesystem;
using namespace NGIN::Reflection::MetaGen;

auto Require(const bool condition, const std::string_view message) -> void {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

[[nodiscard]] auto Context(const fs::path &output = {}) -> MetaGenContext {
  MetaGenContext context{};
  context.projectName = "Scanner.Tests";
  context.profileName = "Debug";
  context.compiler = "test-compiler";
  context.projectDir = "/project";
  context.sourceFiles = {"/project/Test.hpp"};
  if (!output.empty()) {
    context.outputs = {output};
  }
  return context;
}

constexpr std::string_view Source = R"cpp(# 1 "/project/Test.hpp"
namespace Demo {
[[ngin::reflect("Name = \"Demo::Thing\", Attributes = (Serialize::Required = true)")]]
struct Thing : Base {
  int visible;
  [[ngin::ignore("")]] int transientValue;
private:
  [[ngin::generated_body("")]] template <class> friend struct GeneratedDescriptor;
  [[ngin::field("Name = \"secret\", Attributes = (Editor::Category = \"State\")")]] int secret_;
public:
  [[ngin::property("Name = \"value\"")]] int GetValue() const noexcept;
  [[ngin::property("Name = \"value\"")]] void SetValue(int value) &;
  [[ngin::method("Name = \"apply\"")]] void Apply(int amount) const & noexcept;
  [[ngin::ctor("injectable")]] explicit Thing([[ngin::dependency("Name = \"primary\", Optional = true")]] Service service);
};
[[ngin::reflect("Name = \"Demo::Mode\"")]]
enum class Mode { Ready, [[ngin::ignore("")]] Hidden, [[ngin::enum_value("Name = \"active\"")]] Active };
}
)cpp";

auto ScannerModelTest() -> void {
  const ScanResult scanned = ScanPreprocessedSource(Source, Context());
  if (!scanned.diagnostics.empty()) {
    for (const std::string &diagnostic : scanned.diagnostics)
      std::cerr << diagnostic << '\n';
  }
  Require(scanned.diagnostics.empty(),
          "supported scanner corpus has no diagnostics");
  Require(scanned.model.version == ReflectionModelVersion,
          "model version is explicit");
  Require(scanned.model.types.size() == 2, "record and enum are discovered");

  const ReflectedType &record = scanned.model.types[0];
  Require(record.cppQualifiedName == "Demo::Thing",
          "namespace qualification is retained");
  Require(record.reflectionName == "Demo::Thing",
          "custom type name is retained");
  Require(record.hasGeneratedBody, "generated body marker is retained");
  Require(record.bases.size() == 1 && record.bases[0].cppType == "Demo::Base",
          "relative public base is qualified");
  Require(record.fields.size() == 2,
          "public fields are automatic and ignored fields are excluded");
  Require(record.fields[0].cppName == "visible" &&
              record.fields[0].reflectionName == "visible",
          "automatic public field keeps its name");
  Require(record.fields[1].reflectionName == "secret" &&
              record.fields[1].attributes.contains("Editor::Category"),
          "private customized field and metadata are retained");
  Require(record.properties.size() == 1 && record.properties[0].getter &&
              record.properties[0].setter,
          "property accessors are paired");
  Require(record.methods.size() == 1 && record.methods[0].isConst &&
              record.methods[0].isLValueQualified &&
              record.methods[0].isNoexcept,
          "method qualifiers are retained");
  Require(
      record.constructors.size() == 1 && record.constructors[0].injectable &&
          record.constructors[0].parameters.size() == 1 &&
          record.constructors[0].parameters[0].dependencyName == "primary" &&
          record.constructors[0].parameters[0].optional,
      "injectable constructor dependency metadata is retained");
  Require(record.attributes.contains("Serialize::Required"),
          "unknown namespaced type metadata is retained");

  const ReflectedType &enumeration = scanned.model.types[1];
  Require(enumeration.kind == TypeKind::Enum &&
              enumeration.enumValues.size() == 2,
          "enum values are automatic and NGIN_IGNORE is honored");
  Require(enumeration.enumValues[1].reflectionName == "active",
          "enum value customization is retained");

  const std::string json = SerializeReflectionModel(scanned.model);
  Require(json.find("\"version\":1") != std::string::npos &&
              json.find("Editor::Category") != std::string::npos,
          "model JSON is versioned and preserves metadata");

  const std::string explanation =
      ExplainReflectionModel(scanned.model, "Demo::Thing");
  Require(explanation.find("included public field") != std::string::npos &&
              explanation.find("expression=static_cast<void (Demo::Thing::*) "
                               "(int) const & noexcept>") !=
                  std::string::npos &&
              explanation.find("cache-key:") != std::string::npos,
          "explain output reports inclusion reasons, member expressions, and "
          "cache keys");
}

auto EmitterTest() -> void {
  const ScanResult scanned = ScanPreprocessedSource(Source, Context());
  const fs::path root =
      fs::temp_directory_path() / "ngin-metagen-scanner-tests";
  std::error_code error{};
  fs::remove_all(root, error);
  const fs::path output = root / "generated/reflection.generated.cpp";
  const EmitResult emitted = EmitReflection(Context(output), scanned.model);
  Require(emitted.diagnostics.empty(), "emitter accepts the scanner model");
  Require(fs::is_regular_file(output), "aggregate source is emitted");
  std::ifstream aggregateInput(output);
  const std::string aggregate{std::istreambuf_iterator<char>{aggregateInput},
                              {}};
  Require(aggregate.find("RegisterGeneratedReflectionModule") !=
              std::string::npos,
          "module registration is aggregated automatically");

  const fs::path unit =
      root / "generated/reflection.headers" /
      ("Header_" + StableHash(scanned.model.headers[0].normalizedPath) +
       ".reflection.inc");
  std::ifstream unitInput(unit);
  const std::string generated{std::istreambuf_iterator<char>{unitInput}, {}};
  Require(generated.find("GeneratedDescriptor<Demo::Thing>") !=
              std::string::npos,
          "per-header generated descriptor is emitted");
  Require(generated.find(
              "static_cast<void (Demo::Thing::*) (int) const & noexcept>") !=
              std::string::npos,
          "method emission uses an exact qualified member-pointer cast");

  const fs::file_time_type timestamp = fs::last_write_time(output);
  const EmitResult repeated = EmitReflection(Context(output), scanned.model);
  Require(repeated.diagnostics.empty() &&
              fs::last_write_time(output) == timestamp,
          "unchanged output is not rewritten");
  fs::remove_all(root, error);
}

auto DiagnosticsTest() -> void {
  constexpr std::string_view source = R"cpp(# 1 "/project/Test.hpp"
[[ngin::reflect("")]] class PrivateType {
  [[ngin::field("")]] int value;
};
)cpp";
  const ScanResult scanned = ScanPreprocessedSource(source, Context());
  Require(scanned.diagnostics.size() == 1 &&
              scanned.diagnostics.front().find("NGIN_GENERATED_BODY") !=
                  std::string::npos &&
              scanned.diagnostics.front().find("/project/Test.hpp:2") !=
                  std::string::npos,
          "unsupported private access has a source-located remedy");

  constexpr std::string_view templateSource = R"cpp(# 1 "/project/Test.hpp"
template <class T> [[ngin::reflect("")]] struct OpenType { T value; };
)cpp";
  const ScanResult openTemplate =
      ScanPreprocessedSource(templateSource, Context());
  Require(openTemplate.model.types.empty() &&
              openTemplate.diagnostics.size() == 1 &&
              openTemplate.diagnostics.front().find("open class templates") !=
                  std::string::npos &&
              openTemplate.diagnostics.front().find("Describe<T>") !=
                  std::string::npos,
          "annotated open templates fail with an escape-hatch diagnostic");

  constexpr std::string_view manualSource = R"cpp(# 1 "/project/Test.hpp"
[[ngin::reflect("Manual = true")]] struct ManualType { int (*callback)(int); };
)cpp";
  const ScanResult manual = ScanPreprocessedSource(manualSource, Context());
  Require(manual.diagnostics.empty() && manual.model.types.size() == 1 &&
              manual.model.types[0].manualDescriptor &&
              manual.model.types[0].fields.empty() &&
              ExplainReflectionModel(manual.model, "ManualType")
                      .find("manual NginReflect/Describe<T>") !=
                  std::string::npos,
          "manual descriptors retain automatic module aggregation without "
          "parsing members");
}
} // namespace

auto main() -> int {
  ScannerModelTest();
  EmitterTest();
  DiagnosticsTest();
  std::cout << "NGIN.Reflection.MetaGen scanner tests passed\n";
  return 0;
}
