import { stripVTControlCharacters } from 'node:util';
import * as vscode from 'vscode';
import type { CliResult } from '../model';
import { LifecycleOutputPresenter, type NinjaProgress } from '../core/outputPresentation';

const clearLine = '\r\x1b[2K';
const frames = ['⠋', '⠙', '⠹', '⠸', '⠼', '⠴', '⠦', '⠧', '⠇', '⠏'];

function plainText(value: string): string {
  return stripVTControlCharacters(value).replace(/[\x00-\x08\x0b-\x1f\x7f-\x9f]/gu, '');
}

export class BuildTerminal implements vscode.Pseudoterminal {
  private readonly writes = new vscode.EventEmitter<string>();
  private readonly exits = new vscode.EventEmitter<number>();
  private readonly cancelSource = new vscode.CancellationTokenSource();
  readonly onDidWrite = this.writes.event;
  readonly onDidClose = this.exits.event;
  private timer?: NodeJS.Timeout;
  private columns = 80;
  private started = 0;
  private lastDraw = 0;
  private staticDisplay = false;
  private finished = false;
  private action = 'Preparing build';
  private target: string;
  private progress?: NinjaProgress;

  constructor(private readonly label: string, private readonly args: string[],
    private readonly onCancel: vscode.Event<void>,
    private readonly execute: (token: vscode.CancellationToken, presenter: LifecycleOutputPresenter) => Promise<CliResult | undefined>) {
    this.target = label;
  }

  async open(dimensions: vscode.TerminalDimensions | undefined): Promise<void> {
    this.started = Date.now();
    this.columns = dimensions?.columns ?? this.columns;
    const config = vscode.workspace.getConfiguration();
    this.staticDisplay = config.get('workbench.reduceMotion') === 'on' || config.get('editor.accessibilitySupport') === 'on';
    const cancel = this.onCancel(() => this.interrupt());
    const presenter = new LifecycleOutputPresenter({
      command: 'build', args: this.args, label: this.label,
      append: text => this.writeLine(text),
      activity: this.staticDisplay ? undefined : (target, action, progress) => {
        const targetChanged = this.target !== target;
        this.target = target;
        this.action = action;
        this.progress = progress;
        if (targetChanged || !progress || progress.current === progress.total || Date.now() - this.lastDraw >= 120) this.render();
      }
    });
    this.writeLine('  Ctrl+C to cancel\n\n');
    this.render();
    if (!this.staticDisplay) this.timer = setInterval(() => this.render(), 120);
    let code = 1;
    try {
      const result = await this.execute(this.cancelSource.token, presenter);
      code = this.cancelSource.token.isCancellationRequested ? 130 : result?.exitCode ?? 1;
    } catch (error) {
      presenter.failedToStart(error instanceof Error ? error.message : String(error));
    } finally {
      this.finished = true;
      clearInterval(this.timer);
      if (!this.staticDisplay) this.writes.fire(clearLine);
      cancel.dispose();
      this.cancelSource.dispose();
      this.exits.fire(code);
      this.exits.dispose();
      this.writes.dispose();
    }
  }

  setDimensions(size: vscode.TerminalDimensions): void {
    this.columns = size.columns;
    if (this.started && !this.finished) this.render();
  }

  handleInput(data: string): void {
    if (data.includes('\x03')) this.interrupt();
  }

  close(): void { this.interrupt(); }

  private interrupt(): void {
    if (this.finished || this.cancelSource.token.isCancellationRequested) return;
    this.cancelSource.cancel();
    if (this.staticDisplay) this.writeLine('Canceling…\n');
    else this.render();
  }

  private writeLine(text: string): void {
    const styled = plainText(text)
      .replace(/^(> ngin build .+)$/gm, '\x1b[1m$1\x1b[0m')
      .replace(/^(\s*)([✓✗⚠])/gm, (_, space: string, mark: string) =>
        `${space}\x1b[${mark === '✓' ? 32 : mark === '✗' ? 31 : 33}m${mark}\x1b[0m`);
    this.writes.fire((this.staticDisplay ? '' : clearLine) + styled.replace(/\n/g, '\r\n'));
  }

  private render(): void {
    if (this.staticDisplay) return;
    this.lastDraw = Date.now();
    const elapsed = (this.lastDraw - this.started) / 1000;
    const frame = frames[Math.floor(elapsed / 0.12) % frames.length];
    const p = this.progress;
    const filled = p && p.total > 0 ? Math.min(10, Math.floor(10 * p.current / p.total)) : 0;
    const bar = p && this.columns >= 72 ? `${'━'.repeat(filled)}${'─'.repeat(10 - filled)}  ` : '';
    const count = p ? `[${p.current}/${p.total}] ` : '';
    const activity = this.cancelSource.token.isCancellationRequested ? 'Canceling…' : bar + count + this.action;
    const line = plainText(`${elapsed.toFixed(1)}s  ${activity}  ·  ${this.target}`);
    // Keep the transient row on one physical line, even for wide glyphs or a resized terminal.
    this.writes.fire(`${clearLine}\x1b[?7l\x1b[36m${frame}\x1b[0m  ${line.slice(0, Math.max(0, this.columns - 4))}\x1b[?7h\r`);
  }
}
