import fs from 'node:fs/promises';
import path from 'node:path';
const root = path.resolve('third_party');
const sources = [
  ...['include/MinHook.h', 'src/buffer.c', 'src/buffer.h', 'src/hook.c', 'src/trampoline.c', 'src/trampoline.h',
    'src/hde/hde64.c', 'src/hde/hde64.h', 'src/hde/pstdint.h', 'src/hde/table64.h', 'LICENSE.txt']
    .map(file => [`minhook/${file}`, `https://raw.githubusercontent.com/TsudaKageyu/minhook/v1.3.4/${file}`]),
  ['json/json.hpp', 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp'],
  ['json/LICENSE.MIT', 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT'],
];
for (const [file, url] of sources) {
  const destination = path.join(root, file);
  try { await fs.access(destination); continue; } catch {}
  const response = await fetch(url);
  if (!response.ok) throw new Error(`${response.status}: ${url}`);
  await fs.mkdir(path.dirname(destination), { recursive: true });
  await fs.writeFile(destination, Buffer.from(await response.arrayBuffer()));
  console.log(file);
}
