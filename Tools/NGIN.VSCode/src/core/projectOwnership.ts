import * as path from 'node:path';
import type { CompositionGraph, NginContext, ProjectCandidate } from '../model';
import { pathsEqual } from './paths';

export function projectsForFile(projects: readonly ProjectCandidate[], file: string): ProjectCandidate[] {
  const normalized = path.resolve(file);
  return projects.filter(project => {
    if (pathsEqual(project.manifest, normalized)) return true;
    const relative = path.relative(project.directory, normalized);
    return relative !== '' && !relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative);
  }).sort((left, right) => right.directory.length - left.directory.length || left.name.localeCompare(right.name));
}

const compiledFileKinds = new Set(['Source', 'Header', 'CxxModule', 'Resource']);

export function graphOwnsFile(graph: CompositionGraph, context: NginContext, file: string): boolean {
  const normalized = path.resolve(file);
  const projectDirectory = path.dirname(context.projectManifest);
  return graph.buildItems.some(item => {
    if (!compiledFileKinds.has(item.kind)) return false;
    const base = item.generated ? path.join(context.outputDirectory, 'actions') : projectDirectory;
    const candidate = path.isAbsolute(item.path) ? item.path : path.resolve(base, item.path);
    return pathsEqual(candidate, normalized);
  });
}
