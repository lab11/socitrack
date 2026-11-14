// Distances as people in a study talk about them. Pure.
//
// Tags measure in millimeters. Child-development researchers in the United States mostly think in feet,
// so feet are offered first, with meters a click away.

export type DistanceUnit = 'ft' | 'm';

export const MM_PER: Readonly<Record<DistanceUnit, number>> = { ft: 304.8, m: 1000 };
export const UNIT_WORDS: Readonly<Record<DistanceUnit, { short: string; long: string }>> = {
  ft: { short: 'ft', long: 'feet' },
  m: { short: 'm', long: 'meters' },
};

/** The desktop tool's default cutoff: distances past 30 feet are left out of a chart. */
export const DEFAULT_CUTOFF_FT = 30;
/** A starting point for "touching distance": about an arm's length. */
export const DEFAULT_TOUCHING_FT = 3;

export const toUnit = (mm: number, unit: DistanceUnit): number => mm / MM_PER[unit];
export const toMm = (value: number, unit: DistanceUnit): number => value * MM_PER[unit];
