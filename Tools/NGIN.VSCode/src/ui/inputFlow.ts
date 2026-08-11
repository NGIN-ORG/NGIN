import * as vscode from 'vscode';

export const back = Symbol('back');

interface StepOptions {
  title: string;
  step: number;
  totalSteps: number;
  placeholder?: string;
}

export interface PickEntry<T> extends vscode.QuickPickItem {
  value: T;
}

export async function pickStep<T>(options: StepOptions, items: PickEntry<T>[]): Promise<T | typeof back | undefined> {
  const input = vscode.window.createQuickPick<PickEntry<T>>();
  input.title = options.title;
  input.step = options.step;
  input.totalSteps = options.totalSteps;
  input.placeholder = options.placeholder;
  input.items = items;
  input.matchOnDescription = true;
  input.matchOnDetail = true;
  input.buttons = options.step > 1 ? [vscode.QuickInputButtons.Back] : [];
  return new Promise(resolve => {
    let settled = false;
    const finish = (value: T | typeof back | undefined) => {
      if (settled) return;
      settled = true;
      input.hide();
      input.dispose();
      resolve(value);
    };
    input.onDidAccept(() => finish(input.selectedItems[0]?.value));
    input.onDidTriggerButton(button => { if (button === vscode.QuickInputButtons.Back) finish(back); });
    input.onDidHide(() => finish(undefined));
    input.show();
  });
}

export async function inputStep(
  options: StepOptions & { value?: string; prompt?: string; validate?: (value: string) => string | undefined }
): Promise<string | typeof back | undefined> {
  const input = vscode.window.createInputBox();
  input.title = options.title;
  input.step = options.step;
  input.totalSteps = options.totalSteps;
  input.placeholder = options.placeholder;
  input.prompt = options.prompt;
  input.value = options.value ?? '';
  input.buttons = options.step > 1 ? [vscode.QuickInputButtons.Back] : [];
  return new Promise(resolve => {
    let settled = false;
    const finish = (value: string | typeof back | undefined) => {
      if (settled) return;
      settled = true;
      input.hide();
      input.dispose();
      resolve(value);
    };
    input.onDidChangeValue(value => { input.validationMessage = options.validate?.(value); });
    input.onDidAccept(() => {
      const problem = options.validate?.(input.value);
      if (problem) {
        input.validationMessage = problem;
        return;
      }
      finish(input.value);
    });
    input.onDidTriggerButton(button => { if (button === vscode.QuickInputButtons.Back) finish(back); });
    input.onDidHide(() => finish(undefined));
    input.show();
  });
}
