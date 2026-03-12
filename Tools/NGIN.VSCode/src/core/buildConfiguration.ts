export interface NginBuildConfigurationOption {
  name: string;
  description: string;
}

export const DEFAULT_BUILD_CONFIGURATION = 'Debug';

export const NGIN_BUILD_CONFIGURATIONS: readonly NginBuildConfigurationOption[] = [
  { name: 'Debug', description: 'No optimization, full debug information.' },
  { name: 'Release', description: 'Optimized build for runtime performance.' },
  { name: 'RelWithDebInfo', description: 'Optimized build with debug information.' },
  { name: 'MinSizeRel', description: 'Optimized build for smaller binaries.' }
];

export function normalizeBuildConfiguration(value?: string): string {
  const trimmed = value?.trim();
  return trimmed ? trimmed : DEFAULT_BUILD_CONFIGURATION;
}
