export function resolveWorkspaceChoice(
  requested: string | undefined,
  choices: readonly string[] | undefined,
  fallback: string | undefined
): string | undefined {
  if (!choices?.length) return requested ?? fallback;
  if (requested && choices.includes(requested)) return requested;
  if (fallback && choices.includes(fallback)) return fallback;
  return choices[0];
}
