const fs = require('node:fs');
const path = require('node:path');
const { randomUUID } = require('node:crypto');

function writeDesktopSettings(destination, value) {
  if (typeof value !== 'string' || Buffer.byteLength(value, 'utf8') > 16384) throw new Error('无效设置请求');
  JSON.parse(value);
  fs.mkdirSync(path.dirname(destination), { recursive: true });
  const temporary = `${destination}.${randomUUID()}.tmp`;
  try {
    const file = fs.openSync(temporary, 'wx');
    try { fs.writeFileSync(file, value, 'utf8'); fs.fsyncSync(file); }
    finally { fs.closeSync(file); }
    fs.renameSync(temporary, destination);
  } finally { try { fs.unlinkSync(temporary); } catch {} }
}
module.exports = { writeDesktopSettings };
