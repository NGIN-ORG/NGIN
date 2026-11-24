import { createHash } from 'node:crypto';
import * as path from 'node:path';
import * as vscode from 'vscode';
import { ManifestOffsets } from '../core/manifestOffsets';
import { selectionArguments } from '../core/commandArguments';
import {
  encodeEditorItem,
  parseEditorAuthoringPlan,
  parseEditorProductSnapshot,
  type EditorAuthoringPlan,
  type EditorIntent,
  type EditorItemRequest,
  type EditorProductSnapshot
} from '../core/editorProtocol';
import type { NginCli } from '../core/cli';
import type { NginContext } from '../model';
import { OperationCoordinator } from '../core/operationCoordinator';

export interface PlanRequest {
  intent: EditorIntent;
  items?: EditorItemRequest[];
  from?: string;
  to?: string;
  package?: string;
  version?: string;
  exact?: boolean;
  selectedExport?: string;
}

export class AuthoringService {
  constructor(private readonly cli: NginCli, private readonly operations: OperationCoordinator) {}

  snapshot(context: NginContext): Promise<EditorProductSnapshot> {
    const key = ['editor-snapshot', context.projectManifest, context.configuration, context.target,
      context.toolchain, JSON.stringify(context.options)].join('|');
    return this.operations.read(key, async () => {
      const result = await this.cli.run(
        ['editor', 'snapshot', ...selectionArguments(context)],
        context.workspaceFolder,
        { cwd: path.dirname(context.projectManifest) }
      );
      return parseEditorProductSnapshot(result.stdout);
    });
  }

  async plan(context: NginContext, request: PlanRequest): Promise<EditorAuthoringPlan> {
    const snapshot = await this.snapshot(context);
    const args = ['editor', 'plan', '--intent', request.intent, '--precondition', snapshot.manifestHash];
    for (const item of request.items ?? []) args.push('--item', encodeEditorItem(item));
    if (request.from) args.push('--from', request.from);
    if (request.to) args.push('--to', request.to);
    if (request.package) args.push('--package', request.package);
    if (request.selectedExport) args.push('--export', request.selectedExport);
    if (request.version) args.push(request.exact ? '--exact' : '--version', request.version);
    args.push(...selectionArguments(context));
    const result = await this.cli.run(args, context.workspaceFolder, { cwd: path.dirname(context.projectManifest) });
    return parseEditorAuthoringPlan(result.stdout);
  }

  async apply(
    context: NginContext,
    plan: EditorAuthoringPlan,
    contents: ReadonlyMap<string, Uint8Array> = new Map()
  ): Promise<void> {
    if (plan.state !== 'ready') {
      throw new Error(plan.diagnostics.map(value => value.message).join('\n') || 'The NGIN authoring plan was rejected.');
    }
    await this.operations.write(context.projectManifest, async () => {
      const documents = new Map<string, ManifestOffsets>();
      for (const precondition of plan.preconditions) {
        const document = await vscode.workspace.openTextDocument(vscode.Uri.file(precondition.path));
        if (document.isDirty) throw new Error(`Save or revert ${path.basename(precondition.path)} before applying this authoring plan.`);
        const offsets = await ManifestOffsets.read(document);
        const actual = `sha256:${createHash('sha256').update(offsets.bytes).digest('hex')}`;
        if (actual !== precondition.sha256) {
          throw new Error(`${path.basename(precondition.path)} changed after the authoring plan was produced. Review the operation again.`);
        }
        documents.set(precondition.path, offsets);
      }
      const edit = new vscode.WorkspaceEdit();
      for (const operation of plan.filesystem) {
        const uri = vscode.Uri.file(path.resolve(path.dirname(context.projectManifest), operation.path));
        if (operation.operation === 'create') {
          edit.createFile(uri, { ignoreIfExists: false, contents: contents.get(operation.path) });
        } else if ((operation.operation === 'rename' || operation.operation === 'move') && operation.to) {
          edit.renameFile(uri, vscode.Uri.file(path.resolve(path.dirname(context.projectManifest), operation.to)),
            { overwrite: false });
        } else if (operation.operation === 'delete') {
          edit.deleteFile(uri, { ignoreIfNotExists: false, recursive: false });
        }
      }
      for (const change of plan.textEdits) {
        const offsets = documents.get(change.path);
        if (!offsets || offsets.document.isDirty) throw new Error(`Review the plan for ${change.path} again.`);
        edit.replace(offsets.document.uri, offsets.range(change.start, change.end), change.text);
      }
      if (!await vscode.workspace.applyEdit(edit)) throw new Error('VS Code could not apply the NGIN authoring plan.');
      this.operations.invalidate();
    });
  }
}
