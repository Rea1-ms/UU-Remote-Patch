import fs from 'node:fs/promises';
const root = 'artifacts/references';
await fs.mkdir(root, {recursive: true});
for (const file of ['app/src/control_msg.c','app/src/control_msg.h','app/src/demuxer.c',
 'server/src/main/java/com/genymobile/scrcpy/Server.java',
 'server/src/main/java/com/genymobile/scrcpy/Options.java',
 'server/src/main/java/com/genymobile/scrcpy/device/DesktopConnection.java',
 'server/src/main/java/com/genymobile/scrcpy/device/Streamer.java']) {
 const res = await fetch(`https://raw.githubusercontent.com/Genymobile/scrcpy/v5.0.1/${file}`);
 if (!res.ok) throw Error(`${res.status} ${file}`);
 await fs.writeFile(`${root}/${file.split('/').at(-1)}`, await res.text());
}
const sha = 'cd4e96eb314630788b7ff2f9820c395788c023e2';
await fs.mkdir('third_party/ffmpeg', {recursive:true});
await fs.writeFile('third_party/ffmpeg/REVISION', sha+'\n');
const queue = ['libavutil/frame.h','libavcodec/packet.h','libavutil/version.h','COPYING.LGPLv2.1'];
const seen = new Set();
while (queue.length) {
 const file = queue.shift();
 if(seen.has(file)) continue;
 seen.add(file);
 const res = await fetch(`https://raw.githubusercontent.com/FFmpeg/FFmpeg/${sha}/${file}`);
 if (!res.ok) throw Error(`${res.status} ${file}`);
 const text = await res.text();
 await fs.mkdir(`third_party/ffmpeg/${file.split('/').slice(0,-1).join('/')}`,{recursive:true});
 await fs.writeFile(`third_party/ffmpeg/${file}`,text);
 for (const match of text.matchAll(/^#\s*include\s+"([^"]+)"/gm)) {
  const include = /^(libavutil|libavcodec)\//.test(match[1]) ? match[1] : file.split('/')[0]+'/'+match[1];
  if (!['libavutil/avconfig.h','libavutil/ffversion.h','libavutil/config.h','libavutil/intmath.h','libavutil/internal.h'].includes(include)) queue.push(include);
 }
}
await fs.writeFile('third_party/ffmpeg/libavutil/avconfig.h',
 '#pragma once\n#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 1\n');
console.log(`Fetched protocol references and ${seen.size} FFmpeg headers at ${sha}`);
