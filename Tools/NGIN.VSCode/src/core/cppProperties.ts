import * as path from 'node:path';
import { pathExists, readTextFile } from './discovery';

interface CppPropertiesFile {
  configurations?: Array<{
    name?: string;
  }>;
}

export function getCppPropertiesPath(workspaceRoot: string): string {
  return path.join(workspaceRoot, '.vscode', 'c_cpp_properties.json');
}

export async function readCppConfigurationNames(workspaceRoot: string): Promise<string[]> {
  const cppPropertiesPath = getCppPropertiesPath(workspaceRoot);
  if (!(await pathExists(cppPropertiesPath))) {
    return [];
  }

  try {
    const contents = await readTextFile(cppPropertiesPath);
    const parsed = JSON.parse(contents) as CppPropertiesFile;
    return (parsed.configurations ?? [])
      .map((configuration) => configuration.name?.trim())
      .filter((name): name is string => Boolean(name));
  } catch {
    return [];
  }
}
