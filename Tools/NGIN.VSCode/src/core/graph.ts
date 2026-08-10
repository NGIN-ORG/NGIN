import type { CompositionGraph } from '../model';

const collections = [
  'options', 'packages', 'exports', 'capabilityBindings', 'actions', 'plugins',
  'contributions', 'buildItems', 'launches', 'publishes', 'edges'
] as const;

export function parseCompositionGraph(value: string): CompositionGraph {
  let parsed: unknown;
  try {
    parsed = JSON.parse(value);
  } catch (error) {
    throw new Error(`NGIN returned invalid graph JSON: ${error instanceof Error ? error.message : String(error)}`);
  }

  if (!parsed || typeof parsed !== 'object') throw new Error('NGIN returned an empty graph payload.');
  const graph = parsed as Record<string, unknown>;
  if (graph.kind !== 'NGIN.CompositionGraph' || graph.state !== 'resolved') {
    throw new Error('NGIN returned an unsupported or unresolved Composition Graph.');
  }
  if (!graph.product || typeof graph.product !== 'object' || !graph.selection || typeof graph.selection !== 'object') {
    throw new Error('NGIN returned a Composition Graph without product or selection data.');
  }
  for (const property of collections) {
    if (!Array.isArray(graph[property])) throw new Error(`NGIN Composition Graph is missing the ${property} collection.`);
  }
  if (!('testing' in graph)) throw new Error('NGIN Composition Graph is missing testing data.');
  return graph as unknown as CompositionGraph;
}

export function displayOptionValue(value: unknown): string {
  if (typeof value !== 'string') return value == null ? '' : String(value);
  try {
    const parsed = JSON.parse(value) as { value?: unknown };
    if (parsed && typeof parsed === 'object' && 'value' in parsed) return String(parsed.value ?? '');
  } catch {
    // Plain String and Path option values are valid display values.
  }
  return value;
}
