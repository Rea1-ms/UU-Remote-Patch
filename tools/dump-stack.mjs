import fs from 'node:fs';
import path from 'node:path';
// Offline only: never upload the dump or print its arbitrary strings/memory.
const data = fs.readFileSync(process.argv[2]);
if (data.toString('ascii',0,4)!=='MDMP') throw Error('Not a minidump');
const streams = new Map();
for(let i=0;i<data.readUInt32LE(8);++i){const at=data.readUInt32LE(12)+i*12;streams.set(data.readUInt32LE(at),{size:data.readUInt32LE(at+4),rva:data.readUInt32LE(at+8)});}
const modules=[],moduleStream=streams.get(4)?.rva;
if(moduleStream===undefined)throw Error('Module stream unavailable');
for(let i=0;i<data.readUInt32LE(moduleStream);++i){
    const at=moduleStream+4+i*108,name=data.readUInt32LE(at+20),length=data.readUInt32LE(name);
    modules.push({name:path.win32.basename(data.toString('utf16le',name+4,name+4+length)),base:data.readBigUInt64LE(at),size:BigInt(data.readUInt32LE(at+8))});
}
function locate(address){const m=modules.find(m=>address>=m.base && address<m.base+m.size);return m?`${m.name}+0x${(address-m.base).toString(16)}`:`0x${address.toString(16)}`;}
const exception=streams.get(6)?.rva;
if(exception===undefined)throw Error('Exception stream unavailable');
const id=data.readUInt32LE(exception),context=data.readUInt32LE(exception+164);
const rsp=data.readBigUInt64LE(context+152),rip=data.readBigUInt64LE(context+248);
console.log(JSON.stringify({thread:id,code:'0x'+data.readUInt32LE(exception+8).toString(16),instruction:locate(rip),stack:'0x'+rsp.toString(16),rdx:'0x'+data.readBigUInt64LE(context+136).toString(16),relevantModules:modules.filter(m=>/nemu|renderer|version|GameViewer/i.test(m.name)).map(m=>({name:m.name,base:'0x'+m.base.toString(16)}))}));
const threads=streams.get(3)?.rva;
if(threads===undefined)throw Error('Thread stream unavailable');
for(let i=0;i<data.readUInt32LE(threads);++i){
    const at=threads+4+i*48;if(data.readUInt32LE(at)!==id)continue;
    const base=data.readBigUInt64LE(at+24),size=data.readUInt32LE(at+32),rva=data.readUInt32LE(at+36);
    const rows=[],counts=new Map();
    for(let n=0;n+8<=size;n+=8){const address=data.readBigUInt64LE(rva+n),m=modules.find(m=>address>=m.base && address<m.base+m.size);if(!m)continue;
        const target=locate(address);counts.set(target,(counts.get(target)??0)+1);
        if(rows.length<100)rows.push({offset:'0x'+(base+BigInt(n)-rsp).toString(16),target});
    }
    console.log(JSON.stringify({rawStackCandidates:rows,frequentCandidates:[...counts].sort((a,b)=>b[1]-a[1]).slice(0,12)},null,2));
}
