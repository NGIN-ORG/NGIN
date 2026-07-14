#include "Publishing.hpp"

#include "Build.hpp"
#include "Support.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace NGIN::CLI {
namespace fs = std::filesystem;
namespace {

auto WritePublishFile(const fs::path &path, std::string_view contents) -> void {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error(path.string() + ": failed to open for writing");
  }
  output << contents;
}

[[nodiscard]] auto SanitizePublishName(std::string value) -> std::string {
  for (auto &ch : value) {
    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' &&
        ch != '_') {
      ch = '_';
    }
  }
  return value;
}

[[nodiscard]] auto EscapeCMakeString(std::string value) -> std::string {
  std::string escaped{};
  escaped.reserve(value.size());
  for (const auto ch : value) {
    if (ch == '\\') {
      escaped.push_back('/');
    } else {
      if (ch == '"' || ch == ';') {
        escaped.push_back('\\');
      }
      escaped.push_back(ch);
    }
  }
  return escaped;
}

[[nodiscard]] auto ReadWixLog(std::string_view cpackOutput)
    -> std::optional<std::pair<fs::path, std::string>> {
  static const std::regex wixLogPattern{R"('([^']*[\\/]wix\.log)')",
                                        std::regex::icase};
  std::match_results<std::string_view::const_iterator> match{};
  if (!std::regex_search(cpackOutput.begin(), cpackOutput.end(), match,
                         wixLogPattern)) {
    return std::nullopt;
  }

  const auto path = fs::path{std::string{match[1].first, match[1].second}};
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream contents{};
  contents << input.rdbuf();
  return std::pair{path, contents.str()};
}

[[nodiscard]] auto CpackFailureMessage(std::string output,
                                       std::string_view format)
    -> std::string {
  if (format != "msi") {
    return "cpack failed: " + output;
  }

  const auto wixLog = ReadWixLog(output);
  if (!wixLog.has_value()) {
    return "cpack failed: " + output;
  }

  output += "\nWiX log (" + wixLog->first.string() + "):\n" + wixLog->second;
  if (wixLog->second.find("WIX7015") != std::string::npos) {
    output +=
        "\nWiX 7 requires explicit OSMF EULA acceptance. Review "
        "https://wixtoolset.org/osmf/ and, if the terms apply and are "
        "accepted, run 'wix eula accept wix7' as the user that performs "
        "the publish. NGIN does not accept license terms automatically.";
  }
  return "cpack failed: " + output;
}

} // namespace

auto DeterministicInstallerGuid(const std::string &identifier) -> std::string {
  if (identifier == "NGIN-ORG.NGIN.CLI") {
    return "bc787581-23bf-5e17-87ae-864415448920";
  }
  auto hash = [](std::string_view value, std::uint64_t seed) {
    auto result = seed;
    for (const auto ch : value) {
      result ^= static_cast<unsigned char>(ch);
      result *= 1099511628211ULL;
    }
    return result;
  };
  auto high = hash(identifier, 1469598103934665603ULL);
  auto low = hash(identifier, 1099511628211ULL);
  high = (high & 0xffffffffffff0fffULL) | 0x0000000000005000ULL;
  low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
  std::ostringstream text{};
  text << std::hex << std::setfill('0') << std::setw(8)
       << static_cast<std::uint32_t>(high >> 32U) << '-' << std::setw(4)
       << static_cast<std::uint16_t>(high >> 16U) << '-' << std::setw(4)
       << static_cast<std::uint16_t>(high) << '-' << std::setw(4)
       << static_cast<std::uint16_t>(low >> 48U) << '-' << std::setw(12)
       << (low & 0x0000ffffffffffffULL);
  return text.str();
}

auto PathEnvironmentComponentGuid(const std::string &installerIdentifier)
    -> std::string {
  return DeterministicInstallerGuid(installerIdentifier +
                                    "::path-environment");
}

auto WixArchitectureForProfile(const ProfileDefinition &profile)
    -> std::string {
  if (profile.architecture == "x64" || profile.architecture == "arm64") {
    return profile.architecture;
  }
  throw std::runtime_error("MSI publishing does not support target architecture '" +
                           profile.architecture + "'");
}

auto GenerateCpackPublish(const fs::path &stageDirectory,
                          const fs::path &buildDirectory,
                          const fs::path &publishOutput,
                          const PublishDefinition &publish,
                          const ProjectManifest &project,
                          const ProfileDefinition &profile,
                          CliEventEmitter &events) -> fs::path {
  const auto cmake = ResolveToolPath("cmake", project.path.parent_path());
  const auto cpack = ResolveToolPath("cpack", project.path.parent_path());
  if (!cmake.has_value() || !cpack.has_value()) {
    throw std::runtime_error(
        "tgz and installer publishing require cmake and cpack; install them, "
        "set NGIN_CMAKE/NGIN_CPACK, or use a bundled NGIN distribution");
  }

  const auto publishRoot = buildDirectory / ".ngin" / "publish" /
                           SanitizePublishName(publish.name);
  const auto cpackBuild = publishRoot / "build";
  fs::remove_all(publishRoot);
  fs::create_directories(publishRoot);
  fs::create_directories(publishOutput.parent_path());

  const auto generator = publish.kind == "Archive"
                             ? "TGZ"
                             : publish.format == "msi" ? "WIX" : "DEB";
  auto archiveStem = publishOutput.filename();
  archiveStem.replace_extension();
  const auto packageFileStem = publish.format == "tgz"
                                   ? archiveStem.string()
                                   : publishOutput.stem().string();

  std::optional<fs::path> wixPatch{};
  std::optional<fs::path> wixSource{};
  if (publish.format == "msi" && publish.installerAddToPath) {
    wixPatch = publishRoot / "PathPatch.xml";
    wixSource = publishRoot / "PathEnvironment.wxs";
    WritePublishFile(
        *wixPatch,
        "<CPackWiXPatch>\n"
        "  <CPackWiXFragment Id=\"#PRODUCTFEATURE\">\n"
        "    <ComponentRef Id=\"NGIN_PATH_COMPONENT\" />\n"
        "  </CPackWiXFragment>\n"
        "</CPackWiXPatch>\n");
    WritePublishFile(
        *wixSource,
        "<Wix xmlns=\"http://wixtoolset.org/schemas/v4/wxs\">\n"
        "  <Fragment>\n"
        "    <DirectoryRef Id=\"INSTALL_ROOT\">\n"
        "      <Component Id=\"NGIN_PATH_COMPONENT\" Guid=\"" +
            PathEnvironmentComponentGuid(publish.installerIdentifier) +
            "\">\n"
        "        <Environment Id=\"NGIN_PATH\" Name=\"PATH\" "
        "Value=\"[INSTALL_ROOT]bin\" Action=\"set\" Part=\"last\" "
        "System=\"yes\" />\n"
        "      </Component>\n"
        "    </DirectoryRef>\n"
        "  </Fragment>\n"
        "</Wix>\n");
  }

  std::ostringstream cmakeLists{};
  cmakeLists << "cmake_minimum_required(VERSION 3.20)\n"
             << "project(NGINPublish LANGUAGES NONE)\n"
             << "install(DIRECTORY \""
             << EscapeCMakeString(stageDirectory.generic_string())
             << "/\" DESTINATION . USE_SOURCE_PERMISSIONS "
                "COMPONENT Runtime PATTERN \".ngin\" EXCLUDE)\n"
             << "set(CPACK_GENERATOR \"" << generator << "\")\n"
             << "set(CPACK_PACKAGE_NAME \""
             << EscapeCMakeString(project.name) << "\")\n"
             << "set(CPACK_PACKAGE_VENDOR \""
             << EscapeCMakeString(publish.installerVendor.empty()
                                      ? std::string{"NGIN"}
                                      : publish.installerVendor)
             << "\")\n"
             << "set(CPACK_PACKAGE_CONTACT \""
             << EscapeCMakeString(publish.installerContact) << "\")\n"
             << "set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \""
             << EscapeCMakeString(project.name + " product") << "\")\n"
             << "set(CPACK_PACKAGE_VERSION \""
             << EscapeCMakeString(project.version.empty() ? "0.0.0"
                                                          : project.version)
             << "\")\n"
             << "set(CPACK_PACKAGE_FILE_NAME \""
             << EscapeCMakeString(packageFileStem) << "\")\n"
             << "set(CPACK_PACKAGE_DIRECTORY \""
             << EscapeCMakeString(publishOutput.parent_path().generic_string())
             << "\")\n";
  if (publish.format == "deb") {
    cmakeLists << "set(CPACK_PACKAGING_INSTALL_PREFIX \"/usr\")\n"
               << "set(CPACK_DEBIAN_PACKAGE_MAINTAINER \""
               << EscapeCMakeString(publish.installerContact) << "\")\n"
               << "set(CPACK_DEBIAN_PACKAGE_SECTION \"devel\")\n"
               << "set(CPACK_DEBIAN_PACKAGE_PRIORITY \"optional\")\n"
               << "set(CPACK_DEBIAN_FILE_NAME \""
               << EscapeCMakeString(publishOutput.filename().string())
               << "\")\n"
               << "set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)\n";
  } else if (publish.format == "msi") {
    cmakeLists << "set(CPACK_PACKAGING_INSTALL_PREFIX \"/\")\n"
               << "set(CPACK_PACKAGE_INSTALL_DIRECTORY \""
               << EscapeCMakeString(publish.installerVendor + "/" + project.name)
               << "\")\n"
               << "set(CPACK_WIX_VERSION 4)\n"
               << "set(CPACK_WIX_ARCHITECTURE \""
               << WixArchitectureForProfile(profile) << "\")\n"
               << "set(CPACK_WIX_INSTALL_SCOPE perMachine)\n"
               << "set(CPACK_WIX_UPGRADE_GUID \""
               << DeterministicInstallerGuid(publish.installerIdentifier)
               << "\")\n";
    if (wixPatch.has_value() && wixSource.has_value()) {
      cmakeLists << "set(CPACK_WIX_PATCH_FILE \""
                 << EscapeCMakeString(wixPatch->generic_string()) << "\")\n"
                 << "set(CPACK_WIX_EXTRA_SOURCES \""
                 << EscapeCMakeString(wixSource->generic_string()) << "\")\n";
    }
  } else {
    cmakeLists << "set(CPACK_PACKAGING_INSTALL_PREFIX \"/\")\n";
  }
  cmakeLists << "include(CPack)\n";
  WritePublishFile(publishRoot / "CMakeLists.txt", cmakeLists.str());

  auto emitOutput = [&](std::string_view output) {
    if (!output.empty()) {
      events.Emit(CliEventType::BackendOutput,
                  EventData{}.AddString("phase", "package").AddString(
                      "output", std::string{output}));
    }
  };
  events.Emit(CliEventType::PhaseStarted,
              EventData{}
                  .AddString("phase", "package")
                  .AddString("label", "Configure CPack publish")
                  .AddString("cmake", cmake->path.string())
                  .AddString("cpack", cpack->path.string()));
  const auto configured = RunProcessCapture(
      cmake->path, {"-S", publishRoot.string(), "-B", cpackBuild.string()},
      publishRoot, emitOutput);
  if (configured.exitCode != 0) {
    events.Emit(CliEventType::PhaseFailed,
                EventData{}.AddString("phase", "package").AddNumber(
                    "exitCode", configured.exitCode));
    throw std::runtime_error("failed to configure CPack publish: " +
                             configured.output);
  }
  const auto packaged = RunProcessCapture(
      cpack->path,
      {"--config", (cpackBuild / "CPackConfig.cmake").string(), "-G",
       generator, "-B", publishOutput.parent_path().string()},
      cpackBuild, emitOutput);
  if (packaged.exitCode != 0) {
    events.Emit(CliEventType::PhaseFailed,
                EventData{}.AddString("phase", "package").AddNumber(
                    "exitCode", packaged.exitCode));
    throw std::runtime_error(
        CpackFailureMessage(packaged.output, publish.format));
  }

  auto generated = publishOutput;
  if (publish.format == "tgz") {
    generated = publishOutput.parent_path() / (packageFileStem + ".tar.gz");
  }
  if (generated != publishOutput && fs::exists(generated)) {
    fs::remove(publishOutput);
    fs::rename(generated, publishOutput);
  }
  if (!fs::exists(publishOutput)) {
    throw std::runtime_error(
        "cpack completed without producing expected artifact '" +
        publishOutput.string() + "'");
  }
  events.Emit(CliEventType::PhaseCompleted,
              EventData{}.AddString("phase", "package").AddNumber(
                  "exitCode", 0));
  return publishOutput;
}

} // namespace NGIN::CLI
