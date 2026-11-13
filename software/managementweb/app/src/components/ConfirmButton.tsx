import { useEffect, useRef, useState, type ReactNode } from 'react';

/**
 * A button that asks before doing something that cannot be undone.
 *
 * Inline rather than a browser dialog: the question sits where the person was already looking, says
 * exactly what will be lost, and a stray click on the first button costs nothing.
 */
export function ConfirmButton({ label, question, confirmLabel, onConfirm, disabled, className = 'button button--quiet', danger = false }: {
  label: ReactNode;
  question: ReactNode;
  confirmLabel: string;
  onConfirm: () => void;
  disabled?: boolean;
  className?: string;
  danger?: boolean;
}) {
  const [asking, setAsking] = useState(false);
  const confirmRef = useRef<HTMLButtonElement>(null);
  useEffect(() => { if (asking) confirmRef.current?.focus(); }, [asking]);

  if (!asking) {
    return <button type="button" className={className} disabled={disabled} onClick={() => setAsking(true)}>{label}</button>;
  }
  return (
    <div className="confirm" role="alertdialog" aria-live="polite">
      <p className="confirm__question">{question}</p>
      <div className="confirm__actions">
        <button ref={confirmRef} type="button" className={`button${danger ? ' button--danger' : ''}`}
          onClick={() => { setAsking(false); onConfirm(); }}>{confirmLabel}</button>
        <button type="button" className="button button--quiet" onClick={() => setAsking(false)}>Cancel</button>
      </div>
    </div>
  );
}
