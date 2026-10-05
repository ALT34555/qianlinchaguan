document.getElementById('play').onclick = () => window.launcher.start().catch(error => { document.getElementById('status').textContent = String(error); });
document.getElementById('saves').onclick = () => window.launcher.saves().then(error => { document.getElementById('status').textContent = error || '已打开存档目录'; });
