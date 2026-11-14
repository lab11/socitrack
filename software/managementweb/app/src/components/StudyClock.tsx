import { formatDateTime } from '@tottag/schema';
import { zoneSentence, type Deployment, type OpenRecord } from '../features/studyClock.ts';

interface Props {
  readonly deployment: Deployment;
  /** Records open on the page that match none of the open logs, so a mismatch is not silent. */
  readonly strays: readonly OpenRecord[];
  readonly busy: boolean;
  readonly onAddRecord: () => void;
}

/**
 * Which clock this deployment's times are on, said every time.
 *
 * Nobody using the tool should have to think about UTC, which means nobody should have to wonder which
 * local time they are looking at either. Without the deployment record the page cannot know the study's
 * zone, so it says what it used instead and how to put that right.
 */
export function StudyClock({ deployment, strays, busy, onAddRecord }: Props) {
  const sentence = zoneSentence(deployment);
  return (
    <div className={`clock clock--${sentence.tone}`}>
      <p className="clock__text">{sentence.text}</p>
      {!deployment.fromRecord && (
        <button type="button" className="button button--quiet button--small" onClick={onAddRecord} disabled={busy}>
          Add the deployment record
        </button>
      )}
      {!deployment.fromRecord && strays.length > 0 && (
        <ul className="clock__strays">
          {strays.map((open) => (
            <li key={open.id}>
              {open.fileName} is open but is for a different deployment, one starting{' '}
              {formatDateTime(open.record.startTime, open.record.timezone)}.
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
