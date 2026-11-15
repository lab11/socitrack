// Small line icons for the navigation, drawn inline so they take the text colour and need no assets.

import type { SVGProps } from 'react';

const base: SVGProps<SVGSVGElement> = {
  width: 18, height: 18, viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor',
  strokeWidth: 1.8, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true,
};

export const CalendarIcon = () => (
  <svg {...base}><rect x="3.5" y="5" width="17" height="15" rx="3" /><path d="M3.5 10h17M8 3v4M16 3v4" /><path d="m9 15 2 2 4-4" /></svg>
);

export const SearchIcon = () => (
  <svg {...base}><circle cx="11" cy="11" r="6.5" /><path d="m20 20-4.2-4.2" /></svg>
);

export const DownloadIcon = () => (
  <svg {...base}><path d="M12 4v11M7.5 10.5 12 15l4.5-4.5" /><path d="M4.5 17.5v1a2 2 0 0 0 2 2h11a2 2 0 0 0 2-2v-1" /></svg>
);

export const SignalIcon = () => (
  <svg {...base}><circle cx="12" cy="13" r="1.6" /><path d="M8.6 9.6a4.8 4.8 0 0 0 0 6.8M15.4 9.6a4.8 4.8 0 0 1 0 6.8M5.8 6.8a8.8 8.8 0 0 0 0 12.4M18.2 6.8a8.8 8.8 0 0 1 0 12.4" /></svg>
);

export const LockIcon = () => (
  <svg {...base} width={14} height={14}><rect x="5" y="10.5" width="14" height="10" rx="2.5" /><path d="M8.5 10.5V8a3.5 3.5 0 0 1 7 0v2.5" /></svg>
);

/** Two people close together: what a TotTag measures. */
export const BrandMark = ({ size = 34 }: { size?: number }) => (
  <svg width={size} height={size} viewBox="0 0 40 40" aria-hidden="true">
    <circle cx="15" cy="20" r="11" fill="var(--brand-a)" />
    <circle cx="25" cy="20" r="11" fill="var(--brand-b)" opacity="0.92" />
    <circle cx="15" cy="17" r="2.2" fill="var(--surface)" />
    <circle cx="25" cy="17" r="2.2" fill="var(--surface)" />
  </svg>
);
