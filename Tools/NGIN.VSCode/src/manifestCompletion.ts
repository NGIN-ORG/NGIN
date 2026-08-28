import * as vscode from 'vscode';
import { documentRoots, loadManifestMetadata, type MetadataElement } from './core/manifestMetadata';

function activeElementId(document: vscode.TextDocument, position: vscode.Position,
                         roots: Map<string, string>, elements: Map<string, MetadataElement>): string | undefined {
  const source = document.getText(new vscode.Range(new vscode.Position(0, 0), position));
  const stack: string[] = [];
  const tags = /<\/?([A-Za-z_:][\w:.-]*)(?:\s[^<>]*?)?\/?>/g;
  for (const match of source.matchAll(tags)) {
    const token = match[0];
    if (token.startsWith('</')) {
      stack.pop();
      continue;
    }
    if (token.endsWith('/>') || token.startsWith('<?') || token.startsWith('<!')) continue;
    const authoredName = match[1];
    if (stack.length === 0) {
      const root = roots.get(authoredName);
      if (root) stack.push(root);
      continue;
    }
    const parent = elements.get(stack[stack.length - 1]);
    const localName = authoredName.includes(':') ? authoredName.slice(authoredName.indexOf(':') + 1) : authoredName;
    const child = parent?.children
      .map((candidate) => elements.get(candidate.id))
      .find((candidate) => candidate?.name === localName);
    if (child) stack.push(child.id);
  }
  return stack.at(-1);
}

export function registerManifestCompletion(context: vscode.ExtensionContext): vscode.Disposable {
  const metadataPath = vscode.Uri.joinPath(context.extensionUri, 'schemas', 'manifest-editor-metadata.json').fsPath;
  const metadata = loadManifestMetadata(metadataPath);
  const elements = new Map(metadata.elements.map((element) => [element.id, element]));
  const rootNames = new Set(metadata.documents.flatMap(document => documentRoots(metadata, document.kind)));
  const roots = new Map(
    metadata.elements
      .filter((element) => element.id.endsWith('root') && rootNames.has(element.name))
      .map((element) => [element.name, element.id])
  );
  const prefixes = new Map(metadata.namespaces.map((entry) => [entry.uri, entry.prefix]));
  const manifestDocuments: vscode.DocumentSelector = [
    { language: 'ngin', scheme: 'file' },
    { language: 'ngin', scheme: 'untitled' }
  ];

  const completion = vscode.languages.registerCompletionItemProvider(
    manifestDocuments,
    {
      provideCompletionItems(document, position) {
        const line = document.lineAt(position.line).text.slice(0, position.character);
        const currentId = activeElementId(document, position, roots, elements);
        const current = currentId ? elements.get(currentId) : undefined;
        const openTagStart = line.lastIndexOf('<');
        const openTagEnd = line.lastIndexOf('>');
        const incompleteName = openTagStart > openTagEnd
          ? /^<([A-Za-z_:][\w:.-]*)/u.exec(line.slice(openTagStart))?.[1] : undefined;
        const incompleteLocalName = incompleteName?.includes(':')
          ? incompleteName.slice(incompleteName.indexOf(':') + 1) : incompleteName;
        const attributeElement = incompleteName
          ? elements.get(roots.get(incompleteName) ?? '')
            ?? current?.children.map(child => elements.get(child.id)).find(child => child?.name === incompleteLocalName)
          : current;
        if (!current && !attributeElement) {
          if (!line.endsWith('<')) return [];
          return [...roots.entries()].map(([name]) => {
            const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Class);
            item.insertText = new vscode.SnippetString(`${name} Name="\${1:Name}">\n  \${0}\n</${name}>`);
            return item;
          });
        }

        if (openTagStart > openTagEnd && !line.slice(openTagStart).startsWith('</')) {
          const valueMatch = /([A-Za-z_][\w:.-]*)\s*=\s*["'][^"']*$/u.exec(line.slice(openTagStart));
          if (valueMatch) {
            const attribute = attributeElement?.attributes.find(candidate => candidate.name === valueMatch[1]);
            return (attribute?.values ?? []).map(value => {
              const item = new vscode.CompletionItem(value, vscode.CompletionItemKind.EnumMember);
              item.detail = `${attribute?.name} value from the NGIN manifest schema`;
              item.insertText = value;
              return item;
            });
          }
          const existing = new Set([...line.slice(openTagStart).matchAll(/([A-Za-z_][\w:.-]*)\s*=/g)].map(match => match[1]));
          return (attributeElement?.attributes ?? []).filter(attribute => !existing.has(attribute.name)).map((attribute) => {
            const item = new vscode.CompletionItem(attribute.name, vscode.CompletionItemKind.Property);
            item.detail = `${attribute.required ? 'required' : 'optional'} ${attribute.type} attribute`;
            item.insertText = new vscode.SnippetString(`${attribute.name}="\${1}"`);
            return item;
          });
        }
        if (!line.endsWith('<')) return [];
        if (!current) return [];
        return current.children.flatMap((childReference) => {
          const child = elements.get(childReference.id);
          if (!child) return [];
          const prefix = child.namespace ? prefixes.get(child.namespace) : undefined;
          const name = prefix ? `${prefix}:${child.name}` : child.name;
          const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Field);
          item.documentation = new vscode.MarkdownString(child.documentation || `NGIN ${child.name} element`);
          const requiredAttributes = child.attributes.filter((attribute) => attribute.required);
          const attributes = requiredAttributes.map((attribute, index) =>
            ` ${attribute.name}="\${${index + 1}:${attribute.name}}"`).join('');
          item.insertText = child.children.length === 0
            ? new vscode.SnippetString(`${name}${attributes} />`)
            : new vscode.SnippetString(`${name}${attributes}>\n  \${0}\n</${name}>`);
          return [item];
        });
      }
    },
    '<', ' ', '"', "'"
  );

  const hover = vscode.languages.registerHoverProvider(manifestDocuments, {
    provideHover(document, position) {
      const range = document.getWordRangeAtPosition(position, /[A-Za-z_][\w:.-]*/);
      if (!range) return undefined;
      const word = document.getText(range);
      const offset = document.offsetAt(position);
      const source = document.getText();
      const tagStart = source.lastIndexOf('<', offset);
      const tagEnd = source.indexOf('>', offset);
      if (tagStart < 0 || tagEnd < offset) return undefined;
      const tagName = /^<\/?([A-Za-z_][\w:.-]*)/.exec(source.slice(tagStart, tagEnd + 1))?.[1];
      const localName = tagName?.includes(':') ? tagName.slice(tagName.indexOf(':') + 1) : tagName;
      const candidates = metadata.elements.filter(element => element.name === localName);
      if (!candidates.length) return undefined;
      const attribute = candidates.flatMap(element => element.attributes).find(item => item.name === word);
      const markdown = new vscode.MarkdownString();
      if (attribute) {
        markdown.appendMarkdown(`**${attribute.name}** · ${attribute.type}${attribute.required ? ' · required' : ''}`);
        if (attribute.values?.length) markdown.appendMarkdown(`\n\nAllowed values: ${attribute.values.map(value => `\`${value}\``).join(', ')}`);
        if (attribute.documentation) markdown.appendMarkdown(`\n\n${attribute.documentation}`);
      } else if (word === localName || word === tagName) {
        const documented = candidates.find(element => element.documentation)?.documentation;
        markdown.appendMarkdown(`**${tagName}**`);
        if (documented) markdown.appendMarkdown(`\n\n${documented}`);
        const attributes = candidates[0].attributes;
        if (attributes.length) {
          markdown.appendMarkdown(`\n\nAttributes: ${attributes.map(item => `\`${item.name}\``).join(', ')}`);
        }
      } else {
        return undefined;
      }
      return new vscode.Hover(markdown, range);
    }
  });

  return vscode.Disposable.from(completion, hover);
}
