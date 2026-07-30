import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import type { CompositionGraphPayload, ProjectManifest } from '../../core/types';
import type { NginWorkspaceSnapshot } from '../../state/workspaceState';
import { ProjectMembershipIndex } from '../../ui/membership';
import { buildActiveProjectTreeModel, buildProjectTreeModels } from '../../ui/models';
import { shouldExcludeProjectFile } from '../../ui/projectFiles';

const project: ProjectManifest = {
  path: path.resolve('/repo/App/App.nginproj'),
  directory: path.resolve('/repo/App'),
  name: 'App',
  productKind: 'Application',
  sourceRoots: [],
  configInputs: [],
  buildSources: [],
  profiles: []
};

function graph(): CompositionGraphPayload {
  const manifestPath = project.path;
  return {
    schemaVersion: '4.0',
    kind: 'NGIN.CompositionGraph',
    state: 'resolved',
    identity: { project: 'App', projectPath: project.path, product: 'App', profile: 'Debug' },
    plans: {
      editor: {
        projectRoot: project.directory,
        files: [
          {
            path: 'src/main.cpp',
            absolutePath: path.resolve(project.directory, 'src/main.cpp'),
            kind: 'Source',
            role: 'Source',
            ownerKind: 'project',
            ownerName: 'App',
            generated: false,
            exists: true,
            manifestPath,
            explainIdentity: 'source:src/main.cpp',
            provenance: { sourceKind: 'project', sourceName: 'App', manifestPath, reason: 'selected editor input' }
          },
          {
            path: path.resolve('/repo/shared/common.hpp'),
            absolutePath: path.resolve('/repo/shared/common.hpp'),
            kind: 'Source',
            role: 'Header',
            ownerKind: 'project',
            ownerName: 'App',
            generated: false,
            exists: true,
            manifestPath,
            explainIdentity: 'source:/repo/shared/common.hpp',
            provenance: { sourceKind: 'project', sourceName: 'App', manifestPath, reason: 'selected editor input' }
          },
          {
            path: 'build/generated.cpp',
            absolutePath: path.resolve(project.directory, 'build/generated.cpp'),
            kind: 'Generated',
            role: 'Source',
            ownerKind: 'generator',
            ownerName: 'Codegen',
            generated: true,
            exists: false,
            manifestPath,
            explainIdentity: 'source:build/generated.cpp',
            provenance: { sourceKind: 'project', sourceName: 'App', manifestPath, reason: 'selected generated editor input' }
          }
        ]
      }
    }
  };
}

test('membership index distinguishes selected, unselected, external, generated, and unknown files', () => {
  const index = new ProjectMembershipIndex(graph(), project);
  assert.equal(index.membership(project, path.resolve(project.directory, 'src/main.cpp')).state, 'selected');
  assert.equal(index.membership(project, path.resolve(project.directory, 'src/main.cpp')).profileName, 'Debug');
  assert.equal(index.membership(project, path.resolve(project.directory, 'README.md')).state, 'unselected');
  assert.equal(index.externalFiles(project).length, 1);
  assert.equal(index.generatedFiles()[0]?.exists, false);
  assert.equal(index.containsSelectedDescendant(project, path.resolve(project.directory, 'src')), true);

  const unknown = new ProjectMembershipIndex(undefined, project);
  assert.equal(unknown.membership(project, path.resolve(project.directory, 'README.md')).state, 'unknown');
});

test('solution model keeps external inputs in Solution and generated inputs in Active Project', () => {
  const snapshot: NginWorkspaceSnapshot = {
    launchManifestExists: false,
    stagedCompileCommandsAvailable: false,
    workspace: {
      kind: 'project',
      root: project.directory,
      manifestPath: project.path,
      workspace: { path: project.path, directory: project.directory, name: 'App', projectPaths: [project.path] },
      projects: [project],
      packageCatalog: {}
    },
    context: {
      workspace: undefined as never,
      project,
      profile: { name: 'Debug', configInputs: [] }
    },
    inspectGraph: graph()
  };
  const solution = buildProjectTreeModels(snapshot);
  assert.deepEqual(
    solution.childrenByProject.get(project.path)?.map((child) => child.kind === 'group' ? child.group : child.kind),
    ['manifest', 'files', 'externalInputs']
  );
  assert.deepEqual(
    buildActiveProjectTreeModel(snapshot).groups.map((group) => group.group),
    ['generatedInputs']
  );
});

test('solution explorer configurable exclusions use slash-normalized glob patterns', () => {
  const context = {
    project,
    projects: [project],
    workspaceRoot: path.resolve('/repo'),
    excludePatterns: ['temp/**', '**/*.bak']
  };
  assert.equal(shouldExcludeProjectFile(context, path.resolve(project.directory, 'temp/cache.bin'), 'cache.bin'), true);
  assert.equal(shouldExcludeProjectFile(context, path.resolve(project.directory, 'temp'), 'temp'), true);
  assert.equal(shouldExcludeProjectFile(context, path.resolve(project.directory, 'src/old.bak'), 'old.bak'), true);
  assert.equal(shouldExcludeProjectFile(context, path.resolve(project.directory, 'old.bak'), 'old.bak'), true);
  assert.equal(shouldExcludeProjectFile(context, path.resolve(project.directory, '.clang-format'), '.clang-format'), false);
  assert.equal(shouldExcludeProjectFile(
    { ...context, configuredOutputRoot: path.resolve('/repo') },
    path.resolve(project.directory, 'src'),
    'src'
  ), false);
});
