import * as path from 'node:path';
import type { ProjectCandidate } from '../model';
import { pathsEqual } from './paths';

export function projectsForFile(projects: readonly ProjectCandidate[], file: string): ProjectCandidate[] {
  const normalized = path.resolve(file);
  return projects.filter(project => {
    if (pathsEqual(project.manifest, normalized)) return true;
    const relative = path.relative(project.directory, normalized);
    return relative !== '' && !relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative);
  }).sort((left, right) => right.directory.length - left.directory.length || left.name.localeCompare(right.name));
}
