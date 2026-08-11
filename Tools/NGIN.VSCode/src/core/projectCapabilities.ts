import type { CompositionGraph, ProjectCandidate } from '../model';

export function projectCanLaunch(
  project: ProjectCandidate | undefined,
  activeGraph?: CompositionGraph,
  activeManifest?: string
): boolean {
  if (!project) return false;
  return activeGraph && activeManifest === project.manifest
    ? activeGraph.launches.length > 0
    : Boolean(project.hasLaunch);
}
