import { encodeXmlAttribute } from './manifestText';

export interface ProjectTemplate {
  manifest: string;
  files: Record<string, string>;
}

function cppName(value: string): string {
  const result = value.replace(/[^A-Za-z0-9_]/g, '_');
  return /^[0-9]/.test(result) ? `_${result}` : result;
}

export function createProjectTemplate(name: string, artifactKind: 'Executable' | 'Library'): ProjectTemplate {
  const escapedName = encodeXmlAttribute(name);
  const symbol = cppName(name);
  if (artifactKind === 'Library') {
    return {
      manifest: `<Library Name="${escapedName}" Kind="Static">\n  <Build>\n    <Source Include="src/**/*.cpp" />\n    <Header Include="include/**/*.hpp" Visibility="Public" />\n    <IncludeDirectory Path="include" Visibility="Public" />\n  </Build>\n</Library>\n`,
      files: {
        [`include/${symbol}/${symbol}.hpp`]: '#pragma once\n',
        [`src/${symbol}.cpp`]: `#include <${symbol}/${symbol}.hpp>\n`
      }
    };
  }
  return {
    manifest: `<Executable Name="${escapedName}">\n  <Build><Source Include="src/**/*.cpp" /></Build>\n</Executable>\n`,
    files: { 'src/main.cpp': '#include <iostream>\n\nint main() {\n  std::cout << "Hello from NGIN\\n";\n  return 0;\n}\n' }
  };
}
