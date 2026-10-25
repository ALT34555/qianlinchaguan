const play = document.getElementById('play');
play.onclick = async () => {
  play.disabled = true;
  document.getElementById('status').textContent = '正在启动游戏…';
  try { await window.launcher.start(); document.getElementById('status').textContent = ''; }
  catch (error) { document.getElementById('status').textContent = String(error); }
  finally { play.disabled = false; }
};
window.launcher.onError(message => { document.getElementById('status').textContent = message; });
document.getElementById('saves').onclick = () => window.launcher.saves().then(error => { document.getElementById('status').textContent = error || '已打开存档目录'; });
