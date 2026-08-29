export class OperationCoordinator {
  private readonly reads = new Map<string, Promise<unknown>>();
  private readonly writes = new Map<string, Promise<unknown>>();
  private generation = 0;

  read<T>(key: string, request: () => Promise<T>): Promise<T> {
    const existing = this.reads.get(key) as Promise<T> | undefined;
    if (existing) return existing;
    const operation = request().finally(() => this.reads.delete(key));
    this.reads.set(key, operation);
    return operation;
  }

  write<T>(productId: string, operation: () => Promise<T>): Promise<T> {
    const previous = this.writes.get(productId) ?? Promise.resolve();
    const next = previous.catch(() => undefined).then(operation);
    this.writes.set(productId, next);
    void next.finally(() => {
      if (this.writes.get(productId) === next) this.writes.delete(productId);
    }).catch(() => undefined);
    return next;
  }

  invalidate(): number {
    this.generation++;
    this.reads.clear();
    return this.generation;
  }

  get currentGeneration(): number { return this.generation; }
}
