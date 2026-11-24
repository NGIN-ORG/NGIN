import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginCli } from './core/cli';
import type { NginController } from './core/controller';
import { ManifestOffsets } from './core/manifestOffsets';
import { pathsEqual } from './core/paths';

interface Span { path: string; start: number; end: number }
interface Link { source: Span; definition: Span }

export function registerManifestNavigation(cli: NginCli, controller: NginController): vscode.Disposable {
  const selector = { scheme: 'file', language: 'ngin' };
  const cache = new Map<string, Promise<Link[]>>();
  const watcher = vscode.workspace.createFileSystemWatcher('**/*.{ngin,nginproj,nginpkg}');
  const clear = () => cache.clear();

  async function index(doc: vscode.TextDocument): Promise<Link[]> {
    if (doc.isDirty) return [];
    const workspace = controller.discoveries.find(d =>
      pathsEqual(d.workspaceManifest, doc.uri.fsPath) || d.projects.some(p => pathsEqual(p.manifest, doc.uri.fsPath))
      || d.packages?.some(p => pathsEqual(p.manifest, doc.uri.fsPath)))?.workspaceManifest;
    if (!workspace) return [];
    let pending = cache.get(workspace);
    if (!pending) {
      pending = cli.run(['editor', 'symbols', '--workspace', workspace], path.dirname(workspace))
        .then(result => {
          const data = JSON.parse(result.stdout);
          if (data.kind !== 'NGIN.EditorSymbols' || data.version !== 1 || !Array.isArray(data.links))
            throw new Error('Unsupported NGIN manifest symbol index.');
          return data.links as Link[];
        });
      cache.set(workspace, pending);
    }
    try { return await pending; }
    catch (error) { if (cache.get(workspace) === pending) cache.delete(workspace); throw error; }
  }

  const equal = (a: Span, b: Span) => pathsEqual(a.path, b.path) && a.start === b.start && a.end === b.end;
  const at = (span: Span, doc: vscode.TextDocument, byte: number) =>
    pathsEqual(span.path, doc.uri.fsPath) && span.start <= byte && byte < span.end;
  async function location(span: Span): Promise<vscode.Location | undefined> {
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(span.path));
    if (document.isDirty) return undefined;
    const offsets = await ManifestOffsets.read(document);
    return new vscode.Location(document.uri, offsets.range(span.start, span.end));
  }
  return vscode.Disposable.from(watcher, watcher.onDidChange(clear), watcher.onDidCreate(clear), watcher.onDidDelete(clear),
    vscode.workspace.onDidSaveTextDocument(doc => { if (doc.languageId === 'ngin') clear(); }),
    vscode.languages.registerDefinitionProvider(selector, {
      async provideDefinition(doc, pos) {
        const byte = (await ManifestOffsets.read(doc)).offset(pos);
        const link = (await index(doc)).find(l => at(l.source, doc, byte));
        return link ? location(link.definition) : undefined;
      }
    }),
    vscode.languages.registerReferenceProvider(selector, {
      async provideReferences(doc, pos, options) {
        const byte = (await ManifestOffsets.read(doc)).offset(pos);
        const links = await index(doc);
        const symbol = links.find(l => at(l.definition, doc, byte) || at(l.source, doc, byte))?.definition;
        if (!symbol) return [];
        const uses = links.filter(l => equal(symbol, l.definition)).map(l => l.source);
        if (options.includeDeclaration) uses.push(symbol);
        return (await Promise.all(uses.map(location))).filter((l): l is vscode.Location => !!l);
      }
    }));
}
