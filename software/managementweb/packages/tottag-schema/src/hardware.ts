// What a log file can tell you about the hardware that wrote it.
//
// A .ttg records neither the board revision nor the fitted flash part, which matters because the
// accelerometer scale differs by revision and nothing in the record says so. This module recovers
// what can be recovered, and is explicit about the rest.
//
// The inference rests on one fact from the firmware Makefile: the SAME `REVISION == M` test selects
// both the flash driver and the IMU driver.
//
//     ifeq ($(REVISION), M)      ifeq ($(REVISION), M)
//     SRC += ..._W25N01GWZEIG.c  SRC += imu_bno055.c
//     else                       else
//     SRC += ..._AS5F18G04SND.c  SRC += imu.c
//     endif                      endif
//
// So the flash part implies the IMU, and the flash part shows up in the log as the page payload
// capacity: 2016 bytes on the 2048-byte Winbond part, 4064 on the 4096-byte Alliance part.

import { BNO055_ACCEL_SCALE, BOARD_REVISIONS, PER_CHIP, SCALE_FACTOR, type BoardRevision, type ChipKey } from './constants.ts';
import type { ParseReport } from './log.ts';

export interface HardwareInference {
  /** The fitted part, when the log proves one. */
  readonly chip: ChipKey | null;
  /** Board revisions consistent with that part. */
  readonly revisions: readonly BoardRevision[];
  /** Metres per second squared per accelerometer LSB, when it can be determined. */
  readonly accelScale: number | null;
  /** Largest payload seen, which is the whole basis of the inference. */
  readonly maxPayloadBytes: number;
  /** Plain-English account of how this was decided, for showing next to the number. */
  readonly reason: string;
}

const SMALL = PER_CHIP.W25N01GWZEIG.PAYLOAD_BYTES_PER_PAGE;
const LARGE = PER_CHIP.AS5F18G04SND.PAYLOAD_BYTES_PER_PAGE;

/**
 * Which parts could have produced a page of this payload size.
 *
 * This is the whole inference, stated as what it is rather than as a threshold. A part's payload
 * capacity is `page size - sizeof(nandlog_page_header_t)`, a hard ceiling the firmware cannot
 * exceed: `nandlog_store_record` commits the page when the next record would not fit. So a page
 * larger than a part's capacity did not come from that part — not "probably", but arithmetically.
 *
 * Expressing it this way rather than as `payload > 2016` means the conclusion follows from the
 * extracted geometry instead of from a number chosen to sit between the two. Add a third part and
 * this keeps working; a threshold would silently keep answering the old question.
 */
export function partsConsistentWith(payloadBytes: number): ChipKey[] {
  return (Object.keys(PER_CHIP) as ChipKey[]).filter(
    (chip) => payloadBytes <= PER_CHIP[chip].PAYLOAD_BYTES_PER_PAGE,
  );
}

/**
 * Infer the flash part, and therefore the accelerometer scale, from page payload sizes.
 *
 * The inference is deliberately ONE-WAY. A page larger than the small part's capacity can only have
 * come from the large part, so that direction is proof. The converse is not: a large part whose
 * pages all flushed early on the time-based flush — which is most of an idle tag's log — produces
 * small pages and looks exactly like a small part. Reporting `null` there is the honest answer, and
 * is why this returns a reason rather than just a number.
 */
export function inferHardware(report: ParseReport): HardwareInference {
  const maxPayloadBytes = report.pages.reduce((max, page) => Math.max(max, page.payloadLength), 0);
  const candidates = partsConsistentWith(maxPayloadBytes);

  // Exactly one part left means the others are excluded by arithmetic, not by preference.
  if (candidates.length === 1) {
    const chip = candidates[0]!;
    const excluded = (Object.keys(PER_CHIP) as ChipKey[]).filter((other) => other !== chip);
    const revisions = chip === 'W25N01GWZEIG' ? (['M'] as const) : BOARD_REVISIONS.filter((r) => r !== 'M');
    return {
      chip,
      revisions: [...revisions],
      accelScale: chip === 'W25N01GWZEIG' ? BNO055_ACCEL_SCALE : SCALE_FACTOR.SCALE_Q8,
      maxPayloadBytes,
      reason:
        `A page in this log carries ${maxPayloadBytes} payload bytes. ` +
        excluded
          .map((other) => `The ${other} holds at most ${PER_CHIP[other].PAYLOAD_BYTES_PER_PAGE}, so it cannot have written it.`)
          .join(' ') +
        ` That leaves the ${chip} (capacity ${PER_CHIP[chip].PAYLOAD_BYTES_PER_PAGE}), which the firmware ` +
        `Makefile pairs with ${chip === 'W25N01GWZEIG' ? 'revM and the BNO055 — 1/100 m/s² per LSB' :
          'revisions N, O and P and the BNO08x — Q8, 1/256 m/s² per LSB'}.`,
    };
  }

  return {
    chip: null,
    revisions: BOARD_REVISIONS,
    accelScale: null,
    maxPayloadBytes,
    reason:
      `The largest page holds ${maxPayloadBytes} payload bytes, which ${candidates.length} of the supported ` +
      `parts could have written (${candidates.join(', ')}). Nothing excludes any of them: a ${LARGE}-byte ` +
      'part whose pages all flushed early on the time-based flush produces pages indistinguishable from a ' +
      `${SMALL}-byte part running full. The accelerometer scale is therefore 1/100 m/s² if this is a revM ` +
      'board and 1/256 if it is anything later — a factor of 2.56, so it is left unset rather than guessed.',
  };
}

/** Accelerometer scale for a revision the caller knows from outside the file. */
export function accelScaleForRevision(revision: BoardRevision): number {
  return revision === 'M' ? BNO055_ACCEL_SCALE : SCALE_FACTOR.SCALE_Q8;
}
