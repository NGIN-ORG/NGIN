import { encodeXmlAttribute } from './manifestText';

export type ProjectProductKind = 'Executable' | 'Static' | 'Shared' | 'Interface' | 'Plugin';
export type ProjectLayout = 'colocated' | 'split' | 'public-private' | 'custom';

export interface ProjectTemplate {
  manifest: string;
  files: Record<string, string>;
}

export interface CustomProjectLayout {
  sourceRoot: string;
  headerRoot: string;
}

function cppName(value: string): string {
  const result = value.replace(/[^A-Za-z0-9_]/g, '_');
  return /^[0-9]/.test(result) ? `_${result}` : result;
}

function executableTemplate(name: string, layout: ProjectLayout, custom?: CustomProjectLayout): ProjectTemplate {
  const sourceRoot = layout === 'colocated' ? '.'
    : layout === 'public-private' ? `Source/${name}/Private`
      : layout === 'custom' ? custom?.sourceRoot ?? 'Source'
        : 'src';
  const sourcePath = sourceRoot === '.' ? 'main.cpp' : `${sourceRoot}/main.cpp`;
  const pattern = sourceRoot === '.' ? '*.cpp' : `${sourceRoot}/**/*.cpp`;
  return {
    manifest: `<Executable Name="${encodeXmlAttribute(name)}">\n  <Build><Source Include="${encodeXmlAttribute(pattern)}" /></Build>\n</Executable>\n`,
    files: {
      [sourcePath]: '#include <iostream>\n\nint main() {\n  std::cout << "Hello from NGIN\\n";\n  return 0;\n}\n'
    }
  };
}

function libraryRoots(name: string, layout: ProjectLayout, custom?: CustomProjectLayout): { source: string; header: string } {
  if (layout === 'colocated') return { source: '.', header: '.' };
  if (layout === 'public-private') return { source: `Source/${name}/Private`, header: `Source/${name}/Public` };
  if (layout === 'custom') return { source: custom?.sourceRoot ?? 'Source', header: custom?.headerRoot ?? 'Include' };
  return { source: 'src', header: 'include' };
}

export function createProjectTemplate(
  name: string,
  productKind: ProjectProductKind | 'Library',
  layout: ProjectLayout = 'split',
  custom?: CustomProjectLayout
): ProjectTemplate {
  if (productKind === 'Executable') return executableTemplate(name, layout, custom);
  const libraryKind = productKind === 'Library' ? 'Static' : productKind;
  const escapedName = encodeXmlAttribute(name);
  const symbol = cppName(name);
  const roots = libraryRoots(symbol, layout, custom);
  const headerPath = roots.header === '.' ? `${symbol}.hpp`
    : layout === 'split' ? `${roots.header}/${symbol}/${symbol}.hpp`
      : `${roots.header}/${symbol}.hpp`;
  const sourcePath = roots.source === '.' ? `${symbol}.cpp` : `${roots.source}/${symbol}.cpp`;
  const headerPattern = roots.header === '.' ? '*.hpp' : `${roots.header}/**/*.hpp`;
  const sourcePattern = roots.source === '.' ? '*.cpp' : `${roots.source}/**/*.cpp`;
  const headerDirectory = roots.header;
  const build = libraryKind === 'Interface'
    ? `    <Header Include="${encodeXmlAttribute(headerPattern)}" Visibility="Interface" />\n`
      + `    <IncludeDirectory Path="${encodeXmlAttribute(headerDirectory)}" Visibility="Interface" />\n`
    : `    <Source Include="${encodeXmlAttribute(sourcePattern)}" />\n`
      + `    <Header Include="${encodeXmlAttribute(headerPattern)}" Visibility="Public" />\n`
      + `    <IncludeDirectory Path="${encodeXmlAttribute(headerDirectory)}" Visibility="Public" />\n`;
  const include = roots.header === '.' ? `#include "${symbol}.hpp"\n`
    : layout === 'split' ? `#include <${symbol}/${symbol}.hpp>\n`
      : `#include "${symbol}.hpp"\n`;
  return {
    manifest: `<Library Name="${escapedName}" Kind="${libraryKind}">\n  <Build>\n${build}  </Build>\n</Library>\n`,
    files: {
      [headerPath]: '#pragma once\n',
      ...(libraryKind === 'Interface' ? {} : { [sourcePath]: include })
    }
  };
}
