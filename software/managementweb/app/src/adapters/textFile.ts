// Choosing one small text file, such as a list of noted events. One of the adapters allowed to touch a
// browser API.

/** The chosen file's name and text, or null when nothing was chosen. */
export function pickTextFile(accept = '.txt,.csv,.log,text/plain'): Promise<{ name: string; text: string } | null> {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = accept;
  return new Promise((resolve) => {
    let settled = false;
    const finish = (file: File | null) => {
      if (settled) return;
      settled = true;
      if (!file) resolve(null);
      else file.text().then((text) => resolve({ name: file.name, text }), () => resolve(null));
    };
    input.addEventListener('change', () => finish(input.files?.[0] ?? null));
    input.addEventListener('cancel', () => finish(null));
    window.addEventListener('focus', () => setTimeout(() => { if (!input.files?.length) finish(null); }, 800), { once: true });
    input.click();
  });
}
