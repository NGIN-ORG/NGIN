import * as path from 'node:path';
import * as vscode from 'vscode';
import type { ManifestEditorMetadata } from '../core/manifestMetadata';
import { attributeChoices } from '../core/manifestMetadata';
import { scanXmlTags } from '../core/manifestText';
import type { ActionDiagnostic, ActionDiagnosticFix } from '../model';
import type { SourceAnalysisProvider } from './sourceAnalysis';

function position(point: { line: number; column: number }): vscode.Position {
  return new vscode.Position(Math.max(0, point.line - 1), Math.max(0, point.column - 1));
}

function fixEdit(diagnostic: ActionDiagnostic, fix: ActionDiagnosticFix): vscode.WorkspaceEdit {
  const edit = new vscode.WorkspaceEdit();
  for (const replacement of fix.edits) {
    const file = replacement.file
      ? path.resolve(path.dirname(diagnostic.file), replacement.file)
      : diagnostic.file;
    edit.replace(
      vscode.Uri.file(file),
      new vscode.Range(position(replacement.range.start), position(replacement.range.end)),
      replacement.text
    );
  }
  return edit;
}

export class AnalyzerCodeActionProvider implements vscode.CodeActionProvider {
  static readonly providedCodeActionKinds = [vscode.CodeActionKind.QuickFix, vscode.CodeActionKind.SourceFixAll];

  constructor(private readonly analysis: SourceAnalysisProvider) {}

  provideCodeActions(
    document: vscode.TextDocument,
    _range: vscode.Range | vscode.Selection,
    context: vscode.CodeActionContext
  ): vscode.CodeAction[] {
    const values = this.analysis.actionDiagnostics(document.uri, context.diagnostics);
    if (context.only?.contains(vscode.CodeActionKind.SourceFixAll)) {
      const combined = new vscode.WorkspaceEdit();
      let count = 0;
      for (const diagnostic of values) {
        for (const fix of diagnostic.fixes ?? []) {
          if (fix.safe === false) continue;
          for (const replacement of fix.edits ?? []) {
            const file = replacement.file
              ? path.resolve(path.dirname(diagnostic.file), replacement.file)
              : diagnostic.file;
            combined.replace(vscode.Uri.file(file), new vscode.Range(
              position(replacement.range.start), position(replacement.range.end)), replacement.text);
            count++;
          }
        }
      }
      if (!count) return [];
      const action = new vscode.CodeAction(`Apply ${count} safe NGIN analyzer fix${count === 1 ? '' : 'es'}`, vscode.CodeActionKind.SourceFixAll);
      action.edit = combined;
      return [action];
    }

    return values.flatMap(diagnostic => (diagnostic.fixes ?? []).map(fix => {
      const action = new vscode.CodeAction(fix.title, vscode.CodeActionKind.QuickFix);
      action.edit = fixEdit(diagnostic, fix);
      action.diagnostics = context.diagnostics.filter(candidate =>
        candidate.source === diagnostic.source && candidate.message === diagnostic.message);
      action.isPreferred = fix.safe === true;
      return action;
    }));
  }
}

interface AttributeLocation {
  name: string;
  value: string;
  valueRange: vscode.Range;
  fullRange: vscode.Range;
}

function rootAttributes(document: vscode.TextDocument): Map<string, AttributeLocation> {
  const source = document.getText();
  const root = scanXmlTags(source).find(tag => !tag.closing && tag.name === 'Project');
  if (!root) return new Map();
  const text = source.slice(root.start, root.end);
  const result = new Map<string, AttributeLocation>();
  const expression = /\s+([A-Za-z_][\w:.-]*)\s*=\s*(["'])(.*?)\2/g;
  for (let match = expression.exec(text); match; match = expression.exec(text)) {
    const fullStart = root.start + match.index;
    const valueStart = fullStart + match[0].indexOf(match[3]);
    result.set(match[1], {
      name: match[1], value: match[3],
      valueRange: new vscode.Range(document.positionAt(valueStart), document.positionAt(valueStart + match[3].length)),
      fullRange: new vscode.Range(document.positionAt(fullStart), document.positionAt(fullStart + match[0].length))
    });
  }
  return result;
}

function replacementAction(
  document: vscode.TextDocument,
  title: string,
  location: AttributeLocation,
  value: string,
  diagnostics: readonly vscode.Diagnostic[],
  preferred = false
): vscode.CodeAction {
  const action = new vscode.CodeAction(title, vscode.CodeActionKind.QuickFix);
  action.edit = new vscode.WorkspaceEdit();
  action.edit.replace(document.uri, location.valueRange, value);
  action.diagnostics = [...diagnostics];
  action.isPreferred = preferred;
  return action;
}

export class ManifestCodeActionProvider implements vscode.CodeActionProvider {
  static readonly providedCodeActionKinds = [vscode.CodeActionKind.QuickFix, vscode.CodeActionKind.SourceFixAll];

  constructor(private readonly metadata: ManifestEditorMetadata) {}

  provideCodeActions(
    document: vscode.TextDocument,
    _range: vscode.Range | vscode.Selection,
    context: vscode.CodeActionContext
  ): vscode.CodeAction[] {
    const attributes = rootAttributes(document);
    const type = attributes.get('Type');
    const linkage = attributes.get('Linkage');
    const actions: vscode.CodeAction[] = [];
    const related = context.diagnostics.filter(diagnostic => diagnostic.source === 'NGIN');

    if (context.only?.contains(vscode.CodeActionKind.SourceFixAll)) {
      const edit = new vscode.WorkspaceEdit();
      let count = 0;
      if (linkage?.value === 'HeaderOnly') {
        edit.replace(document.uri, linkage.valueRange, 'Interface');
        count++;
      } else if (linkage && type?.value !== 'Library') {
        edit.delete(document.uri, linkage.fullRange);
        count++;
      }
      if (!count) return [];
      const fixAll = new vscode.CodeAction('Fix safe NGIN manifest values', vscode.CodeActionKind.SourceFixAll);
      fixAll.edit = edit;
      return [fixAll];
    }

    if (type?.value === 'Module') {
      actions.push(replacementAction(document, 'Change product type to Library', type, 'Library', related));
      actions.push(replacementAction(document, 'Change product type to Plugin', type, 'Plugin', related));
    } else if (type && !attributeChoices(this.metadata, 'project.root', 'Type').includes(type.value)) {
      for (const value of attributeChoices(this.metadata, 'project.root', 'Type')) {
        actions.push(replacementAction(document, `Change product type to ${value}`, type, value, related));
      }
    }

    if (linkage?.value === 'HeaderOnly') {
      actions.push(replacementAction(document, 'Change library linkage to Interface', linkage, 'Interface', related, true));
    } else if (linkage && type?.value !== 'Library') {
      const remove = new vscode.CodeAction('Remove Linkage from non-library product', vscode.CodeActionKind.QuickFix);
      remove.edit = new vscode.WorkspaceEdit();
      remove.edit.delete(document.uri, linkage.fullRange);
      remove.diagnostics = related;
      remove.isPreferred = true;
      actions.push(remove);
    } else if (linkage && !attributeChoices(this.metadata, 'project.root', 'Linkage').includes(linkage.value)) {
      for (const value of attributeChoices(this.metadata, 'project.root', 'Linkage')) {
        actions.push(replacementAction(document, `Change library linkage to ${value}`, linkage, value, related));
      }
    }
    if (related.some(diagnostic => /package|provider|restore/iu.test(diagnostic.message))) {
      const restore = new vscode.CodeAction('Restore NGIN packages', vscode.CodeActionKind.QuickFix);
      restore.command = { command: 'ngin.restore', title: 'Restore NGIN packages' };
      restore.diagnostics = related;
      actions.push(restore);
    }
    if (related.some(diagnostic => /required|missing/iu.test(diagnostic.message))) {
      const editProduct = new vscode.CodeAction('Edit required project fields…', vscode.CodeActionKind.QuickFix);
      editProduct.command = { command: 'ngin.editProduct', title: 'Edit NGIN product' };
      editProduct.diagnostics = related;
      actions.push(editProduct);
    }
    return actions;
  }
}
