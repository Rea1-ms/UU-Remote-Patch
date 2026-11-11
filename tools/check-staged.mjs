import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
const git = args => execFileSync('git', ['-c','safe.directory='+process.cwd().replaceAll('\\','/'),...args]);
const files = git(['diff','--cached','--name-only','--diff-filter=ACMR','-z']).toString('utf8').split('\0').filter(Boolean);
let identifiers = [];
try { identifiers = JSON.parse(fs.readFileSync('private/devices.json','utf8')).map(d=>d.serial).filter(s=>typeof s==='string' && s.length>3); }
catch (error) { if(error.code!=='ENOENT')throw error; }
const problems=[];
for (const file of files) {
    if (/(^|\/)(\.env(?:\..*)?|config\.local\.(?:ini|json)|patch-root\.txt)$|^(?:private|artifacts|build|dist)\/|\.(?:apk|dmp|i64)$/i.test(file)) {
        problems.push(`${file}: private/local artifact staged`);continue;
    }
    const content=git(['show',':'+file]).toString('utf8');
    if(identifiers.some(serial=>content.includes(serial)))problems.push(`${file}: local device identifier found`);
    if(/(?:ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|AKIA[0-9A-Z]{16}|sk-(?:proj-)?[A-Za-z0-9_-]{32,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)/.test(content))
        problems.push(`${file}: credential-shaped content found`);
}
if(problems.length)throw Error(problems.join('\n'));
console.log(`Checked ${files.length} staged files; no local identifiers or known credential patterns found. This is a targeted check, not a guarantee.`);
