import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  // The schema package ships TypeScript sources with no build step, which is deliberate: it keeps
  // the package dependency-free and lets `node --experimental-strip-types` run its tests directly.
  // Excluding it from dependency pre-bundling makes Vite treat it as source, so a change there is
  // picked up without a rebuild and type errors surface in the app's own typecheck.
  optimizeDeps: { exclude: ['@tottag/schema'] },
  build: { target: 'es2022', sourcemap: true },
  server: { port: 5173 },
});
