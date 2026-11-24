import * as vscode from 'vscode';

// Manifest protocol ranges count UTF-8 bytes on disk. VS Code positions count UTF-16
// code units and omit a leading byte-order mark from the open document.
export class ManifestOffsets {
  private readonly preamble: number;
  constructor(readonly document: vscode.TextDocument, readonly bytes: Buffer) {
    this.preamble = bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf ? 3 : 0;
  }

  static async read(document: vscode.TextDocument): Promise<ManifestOffsets> {
    return new ManifestOffsets(document, Buffer.from(await vscode.workspace.fs.readFile(document.uri)));
  }

  offset(position: vscode.Position): number {
    const before = this.document.getText(new vscode.Range(new vscode.Position(0, 0), position));
    return this.preamble + Buffer.byteLength(before);
  }

  range(start: number, end: number): vscode.Range {
    const position = (byte: number) => this.document.positionAt(this.bytes.toString('utf8', this.preamble, byte).length);
    return new vscode.Range(position(start), position(end));
  }
}
