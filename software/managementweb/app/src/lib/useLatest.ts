// An asynchronous value that follows its inputs, keeping only the answer to the latest question.

import { useEffect, useState } from 'react';

export interface Latest<T> {
  readonly value: T | undefined;
  readonly loading: boolean;
  readonly error: string | null;
}

/**
 * Run `load` whenever `deps` change and keep its result. An answer that arrives after the inputs have
 * moved on is dropped, so a slider dragged quickly never shows a window it has already left.
 */
export function useLatest<T>(load: () => Promise<T> | null, deps: readonly unknown[]): Latest<T> {
  const [state, setState] = useState<Latest<T>>({ value: undefined, loading: false, error: null });
  useEffect(() => {
    const pending = load();
    if (!pending) {
      setState({ value: undefined, loading: false, error: null });
      return undefined;
    }
    let current = true;
    setState((previous) => ({ ...previous, loading: true, error: null }));
    pending.then(
      (value) => { if (current) setState({ value, loading: false, error: null }); },
      (error: unknown) => { if (current) setState({ value: undefined, loading: false, error: error instanceof Error ? error.message : String(error) }); },
    );
    return () => { current = false; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);
  return state;
}
