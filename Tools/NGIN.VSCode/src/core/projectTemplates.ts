import { encodeXmlAttribute } from './manifestText';

export interface ProjectTemplate {
  manifest: string;
  files: Record<string, string>;
}

function cppName(value: string): string {
  const result = value.replace(/[^A-Za-z0-9_]/g, '_');
  return /^[0-9]/.test(result) ? `_${result}` : result;
}

export function createProjectTemplate(name: string, type: string): ProjectTemplate {
  const escapedName = encodeXmlAttribute(name);
  const symbol = cppName(name);
  if (type === 'External') {
    return { manifest: `<Project Name="${escapedName}" Type="External" />\n`, files: {} };
  }
  if (type === 'Library') {
    return {
      manifest: `<Project Name="${escapedName}" Type="Library" Linkage="Static">\n  <Build>\n    <Source Include="src/**/*.cpp" />\n    <Header Include="include/**/*.hpp" Visibility="Public" />\n    <IncludeDirectory Path="include" Visibility="Public" />\n  </Build>\n</Project>\n`,
      files: {
        [`include/${symbol}/${symbol}.hpp`]: '#pragma once\n',
        [`src/${symbol}.cpp`]: `#include <${symbol}/${symbol}.hpp>\n`
      }
    };
  }
  const launch = type === 'Application' || type === 'Tool'
    ? `\n  <Launch Name="default" Default="true"><Executable Product="${escapedName}" /></Launch>` : '';
  const testing = type === 'Benchmark' ? '\n  <Testing />' : '';
  const source = type === 'Plugin'
    ? `extern "C" void ${symbol}_load() {}\n`
    : '#include <iostream>\n\nint main() {\n  std::cout << "Hello from NGIN\\n";\n  return 0;\n}\n';
  return {
    manifest: `<Project Name="${escapedName}" Type="${encodeXmlAttribute(type)}">\n  <Build><Source Include="src/**/*.cpp" /></Build>${launch}${testing}\n</Project>\n`,
    files: { 'src/main.cpp': source }
  };
}
