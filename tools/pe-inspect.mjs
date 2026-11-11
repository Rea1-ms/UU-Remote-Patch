import fs from 'node:fs';
import { spawnSync } from 'node:child_process';

const [file, command = 'summary', argument = ''] = process.argv.slice(2);
if (!file) {
  process.stderr.write('Usage: node tools/pe-inspect.mjs FILE summary|exports|refs|disasm [TEXT|RVA]\n');
  process.exit(2);
}
const data = fs.readFileSync(file);
const pe = data.readUInt32LE(0x3c);
if (data.toString('ascii', pe, pe + 4) !== 'PE\0\0') throw new Error('Not a PE image');
const optional = pe + 24;
const is64 = data.readUInt16LE(optional) === 0x20b;
const imageBase = is64 ? data.readBigUInt64LE(optional + 24) : BigInt(data.readUInt32LE(optional + 28));
const directories = optional + (is64 ? 112 : 96);
const sectionTable = optional + data.readUInt16LE(pe + 20);
const sections = Array.from({ length: data.readUInt16LE(pe + 6) }, (_, i) => {
  const offset = sectionTable + i * 40;
  return {
    name: data.toString('ascii', offset, offset + 8).replace(/\0.*/, ''),
    rva: data.readUInt32LE(offset + 12),
    size: data.readUInt32LE(offset + 8),
    raw: data.readUInt32LE(offset + 20),
    rawSize: data.readUInt32LE(offset + 16),
    flags: data.readUInt32LE(offset + 36),
  };
});
const hex = n => '0x' + n.toString(16);
function offset(rva) {
  const section = sections.find(s => rva >= s.rva && rva < s.rva + Math.max(s.size, s.rawSize));
  return section ? section.raw + rva - section.rva : rva;
}
function rvaForOffset(at) {
  const section = sections.find(s => at >= s.raw && at < s.raw + s.rawSize);
  return section ? section.rva + at - section.raw : at;
}
function cstring(rva) {
  const start = offset(rva);
  return data.toString('ascii', start, data.indexOf(0, start));
}
const exports = [];
const exportRva = data.readUInt32LE(directories);
if (exportRva) {
  const table = offset(exportRva);
  const names = offset(data.readUInt32LE(table + 32));
  const ordinals = offset(data.readUInt32LE(table + 36));
  const functions = offset(data.readUInt32LE(table + 28));
  for (let i = 0; i < data.readUInt32LE(table + 24); i++) {
    const ordinal = data.readUInt16LE(ordinals + i * 2);
    exports.push({ name: cstring(data.readUInt32LE(names + i * 4)), rva: data.readUInt32LE(functions + ordinal * 4) });
  }
}
const runtimeFunctions = [];
const exceptionRva = data.readUInt32LE(directories + 24);
const exceptionSize = data.readUInt32LE(directories + 28);
if (is64 && exceptionRva) {
  for (let at = offset(exceptionRva), end = at + exceptionSize; at + 12 <= end; at += 12) {
    runtimeFunctions.push({ start: data.readUInt32LE(at), end: data.readUInt32LE(at + 4) });
  }
}
function enclosing(rva) { return runtimeFunctions.find(f => rva >= f.start && rva < f.end); }
function imports() {
  const result = {};
  const importRva = data.readUInt32LE(directories + 8);
  if (!importRva) return result;
  for (let descriptor = offset(importRva); data.readUInt32LE(descriptor + 12); descriptor += 20) {
    const name = cstring(data.readUInt32LE(descriptor + 12));
    const thunk = offset(data.readUInt32LE(descriptor) || data.readUInt32LE(descriptor + 16));
    const firstThunk = data.readUInt32LE(descriptor + 16);
    const entries = [];
    for (let i = 0; ; i++) {
      const value = is64 ? data.readBigUInt64LE(thunk + i * 8) : BigInt(data.readUInt32LE(thunk + i * 4));
      if (!value) break;
      const ordinalFlag = is64 ? 0x8000000000000000n : 0x80000000n;
      entries.push({ name: value & ordinalFlag ? '#' + (value & 0xffffn) : cstring(Number(value) + 2), iat: hex(firstThunk + i * (is64 ? 8 : 4)) });
    }
    result[name] = entries;
  }
  return result;
}
if (command === 'summary') {
  console.log(JSON.stringify({ file, imageBase: hex(imageBase), sections, imports: imports() }, null, 2));
} else if (command === 'exports') {
  console.log(JSON.stringify(exports.map(e => ({ ...e, rva: hex(e.rva), function: enclosing(e.rva) })), null, 2));
} else if (command === 'refs') {
  const targets = [];
  if (/^0x[0-9a-f]+$/i.test(argument)) targets.push({ rva: Number(argument), encoding: 'address' });
  for (const entries of Object.values(imports())) for (const entry of entries) {
    if (entry.name === argument || (argument.startsWith('?') && entry.name.startsWith(argument))) targets.push({ rva: Number(entry.iat), encoding: 'iat' });
  }
  for (const encoding of ['ascii', 'utf16le']) {
    const needle = Buffer.from(argument, encoding);
    for (let at = data.indexOf(needle); at >= 0; at = data.indexOf(needle, at + 1)) targets.push({ rva: rvaForOffset(at), encoding });
  }
  const references = [];
  for (const section of sections.filter(s => s.flags & 0x20000000)) {
    for (let i = section.raw; i < section.raw + section.rawSize - 7; i++) {
      const rva = section.rva + i - section.raw;
      let destination;
      if (data[i] >= 0x40 && data[i] <= 0x4f && [0x8d, 0x8b].includes(data[i + 1]) && (data[i + 2] & 0xc7) === 5) destination = rva + 7 + data.readInt32LE(i + 3);
      else if (data[i] === 0xff && [0x15, 0x25].includes(data[i + 1])) destination = rva + 6 + data.readInt32LE(i + 2);
      else if ([0xe8, 0xe9].includes(data[i])) destination = rva + 5 + data.readInt32LE(i + 1);
      else continue;
      if (targets.some(t => t.rva === destination)) references.push({ rva: hex(rva), target: hex(destination), function: enclosing(rva) });
    }
  }
  console.log(JSON.stringify({ argument, targets: targets.map(t => ({ ...t, rva: hex(t.rva) })), references }, null, 2));
} else if (command === 'strings') {
  const [start, end] = argument.split(':').map(Number);
  for (let at = offset(start); at < offset(end); ) {
    const next = data.indexOf(0, at);
    const value = data.toString('utf8', at, next);
    if (value.length > 1) console.log(hex(rvaForOffset(at)) + ' ' + value);
    at = next + 1;
  }
} else if (command === 'disasm') {
  const range = argument.split(':').map(Number);
  const entry = exports.find(e => e.name === argument || e.name.startsWith(argument));
  let rva = entry ? entry.rva : range[0];
  if (!Number.isFinite(rva)) throw new Error('Unknown export or RVA');
  for (let i = 0; i < 8 && data[offset(rva)] === 0xe9; i++) rva += 5 + data.readInt32LE(offset(rva) + 1);
  const bounds = range.length === 2 ? { start: rva, end: range[1] } : enclosing(rva) ?? { start: rva, end: rva + 0x80 };
  const result = spawnSync(process.env.OBJDUMP || 'objdump', ['-d', '-M', 'intel', '--start-address=' + hex(imageBase + BigInt(bounds.start)), '--stop-address=' + hex(imageBase + BigInt(Math.min(bounds.end, bounds.start + 0x2400))), file], { encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 });
  if (result.error) throw result.error;
  process.stdout.write(result.stdout);
  process.stderr.write(result.stderr);
  process.exitCode = result.status;
} else throw new Error('Unknown command');
