import type { CompositionGraph, ProjectCandidate } from '../model';

export function projectCanRun(
  project: ProjectCandidate | undefined,
  activeGraph?: CompositionGraph,
  activeManifest?: string
): boolean {
  if (!project) return false;
  return activeGraph && activeManifest === project.manifest
    ? activeGraph.runs.length > 0
    : Boolean(project.hasRun);
}
