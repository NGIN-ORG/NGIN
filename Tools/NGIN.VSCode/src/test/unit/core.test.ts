import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import { tmpdir } from 'node:os';
import * as path from 'node:path';
import test from 'node:test';
import {
  createBrowseConfiguration,
  createSourceConfiguration,
  parseCompileCommands,
  selectCompileCommand,
  splitCommandLine
} from '../../core/compileCommands';
import { dependencyLockPath, lifecycleArguments } from '../../core/commandArguments';
import { parseActionDiagnostics } from '../../core/actionDiagnostics';
import { parseCliDiagnostics, parseCompilerDiagnostics } from '../../core/diagnostics';
import { projectsForFile } from '../../core/projectOwnership';
import { createNativeDebugConfiguration } from '../../core/debugConfiguration';
import { displayOptionValue, parseCompositionGraph } from '../../core/graph';
import { insertBuildItem, kindForPath, updateExactBuildItemPaths, updateProjectAttributes } from '../../core/manifestEdits';
import { parseAttributes, parseWorkspaceChoices, parseWorkspaceProjectRules } from '../../core/manifestText';
import { compileCommandsPath, contextKey, isWithin, projectOutputDirectory, safePathComponent } from '../../core/paths';
import { enumerateProjectFiles } from '../../core/projectFiles';
import type { CompositionGraph, NginContext, ProjectCandidate } from '../../model';

function graph(): CompositionGraph {
  return {
    kind: 'NGIN.CompositionGraph', state: 'resolved',
    product: { identity: 'App', name: 'App', type: 'Application', languageStandard: 'C++23' },
    selection: {
      identity: 'Selection', configuration: 'Debug', targetOperatingSystem: 'windows',
      targetArchitecture: 'x64', compiler: 'clang', debugSymbols: true
    },
    options: [], packages: [], exports: [], capabilityBindings: [], actions: [], plugins: [], contributions: [],
    buildItems: [
      { identity: 'Include:include', kind: 'IncludeDirectory', path: 'include' },
      { identity: 'Define:APP', kind: 'Define', path: 'APP=1' }
    ],
    launches: [], testing: null, publishes: [], edges: []
  };
}

test('workspace choices and project rules are read without creating a semantic model', () => {
  const source = `<Workspace Name="Demo">
  <Projects><Project Path="App/App.nginproj" /><Project Include="Examples/**/*.nginproj" Exclude="**/Old/**" /></Projects>
  <Configurations><Configuration Name="Debug" /><Configuration Name="Release" /></Configurations>
  <Targets><Target Name="host" OS="host" Architecture="host" /></Targets>
  <Toolchains><Toolchain Name="default" Compiler="default" Linker="default" /></Toolchains>
  <Defaults><Configuration Name="Release" /><Target Name="host" /><Toolchain Name="default" /></Defaults>
  <Presets><Preset Name="dev" Command="build"><Configuration Name="Debug" /></Preset></Presets>
</Workspace>`;
  const choices = parseWorkspaceChoices(source);
  assert.equal(choices.name, 'Demo');
  assert.deepEqual(choices.configurations, ['Debug', 'Release']);
  assert.equal(choices.defaults.configuration, 'Release');
  assert.deepEqual(choices.presets, [{ name: 'dev', command: 'build' }]);
  assert.deepEqual(parseWorkspaceProjectRules(source), [
    { path: 'App/App.nginproj', include: undefined, exclude: undefined },
    { path: undefined, include: 'Examples/**/*.nginproj', exclude: '**/Old/**' }
  ]);
});

test('XML attributes decode entities only after lexical extraction', () => {
  assert.deepEqual(parseAttributes(`Name="A&amp;B" Before='2.0'`), { Name: 'A&B', Before: '2.0' });
});

test('build membership insertion preserves the surrounding XML and ordering', () => {
  const source = `<Project Name="App" Type="Application">\n  <!-- retained -->\n  <Dependencies />\n  <Launch Name="Default" />\n</Project>\n`;
  const edit = insertBuildItem(source, 'Header', 'Include', 'include/App.hpp');
  assert.ok(edit);
  const updated = source.slice(0, edit.start) + edit.text + source.slice(edit.end);
  assert.match(updated, /<Dependencies \/>\n  <Build>\n    <Header Include="include\/App.hpp" \/>\n  <\/Build>\n  <Launch/);
  assert.match(updated, /<!-- retained -->/);
});

test('build membership insertion expands a self-closing Build element', () => {
  const source = `<Project Name="App" Type="Application">\n  <Build />\n</Project>`;
  const edit = insertBuildItem(source, 'Source', 'Remove', 'src/old.cpp');
  assert.ok(edit);
  const updated = source.slice(0, edit.start) + edit.text + source.slice(edit.end);
  assert.match(updated, /<Build>\n    <Source Remove="src\/old.cpp" \/>\n  <\/Build>/);
});

test('exact file rules can follow a rename without touching globs or comments', () => {
  const source = `<Build>\n  <Source Include="src/old.cpp" />\n  <Source Include="src/*.cpp" />\n  <!-- src/old.cpp -->\n</Build>`;
  const edits = updateExactBuildItemPaths(source, 'src/old.cpp', 'src/new.cpp');
  assert.equal(edits.length, 1);
  const edit = edits[0];
  const updated = source.slice(0, edit.start) + edit.text + source.slice(edit.end);
  assert.match(updated, /Include="src\/new.cpp"/);
  assert.match(updated, /Include="src\/\*\.cpp"/);
  assert.match(updated, /<!-- src\/old.cpp -->/);
  assert.equal(kindForPath('include/App.hpp'), 'Header');
  assert.equal(kindForPath('src/module.ixx'), 'CxxModule');
});

test('product editor updates, adds, and removes root attributes without rewriting children', () => {
  const source = `<Project Name="Old" Type="Application" Version="1.0.0">\n  <!-- keep -->\n  <Build />\n</Project>`;
  const edits = updateProjectAttributes(source, { Name: 'New', Type: 'Library', Version: undefined, Linkage: 'Static' })
    .sort((left, right) => right.start - left.start);
  let updated = source;
  for (const edit of edits) updated = updated.slice(0, edit.start) + edit.text + updated.slice(edit.end);
  assert.match(updated, /<Project Name="New" Type="Library" Linkage="Static">/);
  assert.doesNotMatch(updated, /Version=/);
  assert.match(updated, /<!-- keep -->/);
});

test('CLI diagnostics preserve Windows drive paths and hints', () => {
  const diagnostics = parseCliDiagnostics('C:\\work\\App.nginproj:12:4: error NGIN1001: unexpected element\n  hint: use Build\n');
  assert.deepEqual(diagnostics, [{
    path: 'C:\\work\\App.nginproj', line: 12, column: 4, severity: 'error', code: 'NGIN1001',
    message: 'unexpected element', hint: 'use Build'
  }]);
});

test('compiler diagnostics are parsed independently from manifest diagnostics', () => {
  const values = parseCompilerDiagnostics(
    'C:\\work\\main.cpp:4:7: warning: unused value [-Wunused-value]\n' +
    'C:\\work\\other.cpp(9,3): error C2065: undeclared identifier\n');
  assert.equal(values.length, 2);
  assert.equal(values[0].code, '-Wunused-value');
  assert.equal(values[1].code, 'C2065');
});

test('Composition Graph parsing rejects incomplete envelopes', () => {
  assert.equal(parseCompositionGraph(JSON.stringify(graph())).product.name, 'App');
  assert.throws(() => parseCompositionGraph('{}'), /unsupported or unresolved/);
  assert.throws(() => parseCompositionGraph('{'), /invalid graph JSON/);
  assert.equal(displayOptionValue('{"type":"Boolean","value":false}'), 'false');
});

test('compile command parsing handles quoted compiler paths and maps configuration', () => {
  const directory = path.resolve('project');
  const sourceFile = path.join(directory, 'src', 'main.cpp');
  const include = path.join(directory, 'include');
  const args = [`${path.join(directory, 'tool chain', 'clang++')}`, `-I${include}`, '-DAPP=1', '-std=c++23', '-c', sourceFile];
  const parsed = parseCompileCommands(JSON.stringify([{ directory, file: sourceFile, arguments: args }]));
  const configuration = createSourceConfiguration(parsed[0], graph());
  assert.equal(configuration.compilerPath, args[0]);
  assert.deepEqual(configuration.includePath, [include]);
  assert.deepEqual(configuration.defines, ['APP=1']);
  assert.equal(configuration.standard, 'c++23');
  assert.equal(configuration.intelliSenseMode, 'windows-clang-x64');
  assert.deepEqual(splitCommandLine(`"${args[0]}" "${sourceFile}"`), [args[0], sourceFile]);
});

test('headers use the closest compile command and browse paths are aggregated', () => {
  const root = path.resolve('project');
  const entries = [
    { directory: root, file: path.join(root, 'one', 'a.cpp'), arguments: ['clang++', '-Ione', 'one/a.cpp'] },
    { directory: root, file: path.join(root, 'two', 'b.cpp'), arguments: ['clang++', '-Itwo', 'two/b.cpp'] }
  ];
  assert.equal(selectCompileCommand(entries, path.join(root, 'two', 'b.hpp'))?.file, entries[1].file);
  const browse = createBrowseConfiguration(entries, graph(), root);
  assert.ok(browse.browsePath.includes(path.join(root, 'one')));
  assert.ok(browse.browsePath.includes(path.join(root, 'two')));
});

test('selection keys are deterministic and output paths are bounded', () => {
  const project: ProjectCandidate = { manifest: path.resolve('App.nginproj'), directory: path.resolve('.'), name: 'My App' };
  const output = projectOutputDirectory(path.resolve('.'), project, 'Debug', 'host', 'default');
  assert.ok(isWithin(path.resolve('.'), output));
  assert.equal(safePathComponent('My App/Debug'), 'My_App_Debug');
  const base = {
    workspaceFolder: path.resolve('.'), projectManifest: project.manifest, projectName: project.name,
    configuration: 'Debug', target: 'host', toolchain: 'default', options: { B: '2', A: '1' }, outputDirectory: output
  } satisfies NginContext;
  assert.equal(contextKey(base), contextKey({ ...base, options: { A: '1', B: '2' } }));
  assert.equal(compileCommandsPath(base), path.join(output, 'cmake', 'compile_commands.json'));
  const lock = dependencyLockPath(base);
  assert.equal(lock, path.join(output, 'ngin.lock'));
  assert.deepEqual(lifecycleArguments('lock', base), [
    'package', 'lock', '--project', project.manifest, '--configuration', 'Debug', '--target', 'host',
    '--toolchain', 'default', '--option', 'A=1', '--option', 'B=2', '--output', lock
  ]);
  assert.deepEqual(lifecycleArguments('analyze', base).slice(-4), ['--output', output, '--lock', lock]);
});

test('structured Action diagnostics retain source, rule, and precise range', () => {
  const envelope = parseActionDiagnostics(JSON.stringify({
    kind: 'NGIN.ActionDiagnostics', state: 'complete', diagnostics: [{
      file: 'C:/work/main.cpp', range: { start: { line: 8, column: 16 }, end: { line: 8, column: 17 } },
      severity: 'warning', source: 'NGIN.Tooling.ClangTidy::Analyze', code: 'readability-magic-numbers',
      message: '42 is a magic number', fixes: []
    }]
  }));
  assert.equal(envelope.diagnostics[0].code, 'readability-magic-numbers');
  assert.equal(envelope.diagnostics[0].range.start.column, 16);
  assert.throws(() => parseActionDiagnostics('{"kind":"wrong"}'), /invalid Action diagnostics envelope/);
});

test('source ownership prefers the deepest project and exposes true ambiguities', () => {
  const root = path.resolve('workspace');
  const projects: ProjectCandidate[] = [
    { manifest: path.join(root, 'Root.nginproj'), directory: root, name: 'Root' },
    { manifest: path.join(root, 'nested', 'One.nginproj'), directory: path.join(root, 'nested'), name: 'One' },
    { manifest: path.join(root, 'nested', 'Two.nginproj'), directory: path.join(root, 'nested'), name: 'Two' }
  ];
  const owners = projectsForFile(projects, path.join(root, 'nested', 'src', 'main.cpp'));
  assert.deepEqual(owners.map(project => project.name), ['One', 'Two', 'Root']);
});

test('native debug configuration uses staged graph launch intent', () => {
  const root = path.resolve('workspace');
  const context: NginContext = {
    workspaceFolder: root,
    projectManifest: path.join(root, 'App.nginproj'),
    projectName: 'App', configuration: 'Debug', target: 'host', toolchain: 'default', options: {},
    outputDirectory: path.join(root, 'build', 'App')
  };
  const value = graph();
  value.launches = [{
    identity: 'App:Launch:Default', name: 'Default', default: true, executableKind: 'Product', executable: 'App',
    workingDirectory: 'work', arguments: ['from-graph'], environment: { APP_MODE: 'test', PATH: 'existing' }, secrets: {}
  }];
  const configuration = createNativeDebugConfiguration(value, context, path.join(context.outputDirectory, 'stage', 'bin', 'App.exe'), {
    args: ['from-user'], stopAtEntry: true
  }, 'win32', ';');
  assert.equal(configuration.type, 'cppvsdbg');
  assert.deepEqual(configuration.args, ['from-graph', 'from-user']);
  assert.equal(configuration.cwd, path.resolve(context.outputDirectory, 'stage', 'work'));
  assert.equal(configuration.environment.find(item => item.name === 'APP_MODE')?.value, 'test');
  assert.match(configuration.environment.find(item => item.name === 'PATH')?.value ?? '', /stage[\\/]lib;existing$/);
});

test('project files distinguish graph membership and physical boundaries', async () => {
  const root = await fs.mkdtemp(path.join(tmpdir(), 'ngin-vscode-'));
  try {
    await fs.mkdir(path.join(root, 'src'), { recursive: true });
    await fs.mkdir(path.join(root, 'Nested'), { recursive: true });
    await fs.writeFile(path.join(root, 'App.nginproj'), '<Project Name="App" Type="Application" />');
    await fs.writeFile(path.join(root, 'src', 'main.cpp'), 'int main() {}');
    await fs.writeFile(path.join(root, 'src', 'unused.cpp'), '');
    await fs.writeFile(path.join(root, 'Nested', 'Nested.nginproj'), '<Project Name="Nested" Type="Library" />');
    const value = graph();
    value.buildItems.push(
      { identity: 'Source:src/main.cpp', kind: 'Source', path: 'src/main.cpp' },
      { identity: 'Header:generated.hpp', kind: 'Header', path: 'generated.hpp', generated: true },
      { identity: 'Header:missing.hpp', kind: 'Header', path: 'missing.hpp' }
    );
    const files = await enumerateProjectFiles(root, path.join(root, 'App.nginproj'), value);
    const flatten = (items: typeof files): typeof files => items.flatMap(item => [item, ...flatten(item.children ?? [])]);
    const all = flatten(files);
    assert.equal(all.find(item => item.name === 'App.nginproj')?.state, 'authored');
    assert.equal(all.find(item => item.name === 'main.cpp')?.state, 'selected');
    assert.equal(all.find(item => item.name === 'unused.cpp')?.state, 'unselected');
    assert.equal(all.find(item => item.name === 'generated.hpp')?.state, 'generated');
    assert.equal(all.find(item => item.name === 'missing.hpp')?.state, 'missing');
    assert.equal(all.find(item => item.name === 'Nested')?.state, 'boundary');
  } finally {
    await fs.rm(root, { recursive: true, force: true });
  }
});
