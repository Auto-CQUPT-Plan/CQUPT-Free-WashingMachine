let timer;
const $ = (id) => document.getElementById(id);

function show(message, isError) {
  const toast = $('toast');
  clearTimeout(timer);
  toast.textContent = message;
  toast.className = 'toast show ' + (isError ? 'err' : 'ok');
  timer = setTimeout(() => {
    toast.className = 'toast';
  }, 2200);
}

function runBar() {
  const activity = $('activity');
  activity.className = 'ticket-rail';
  void activity.offsetWidth;
  activity.className = 'ticket-rail run';
}

function confetti(event) {
  const box = $('bits');
  const x = event ? event.clientX : innerWidth / 2;
  const y = event ? event.clientY : innerHeight * 0.72;
  const colors = ['#ffdbe5', '#ffe8a9', '#94d4c4', '#ffffff'];

  for (let i = 0; i < 12; i++) {
    const piece = document.createElement('i');
    piece.style.left = x + 'px';
    piece.style.top = y + 'px';
    piece.style.background = colors[i % colors.length];
    piece.style.setProperty('--dx', (Math.random() * 140 - 70) + 'px');
    box.appendChild(piece);
    setTimeout(() => piece.remove(), 850);
  }
}

async function send(path, name, button, event) {
  runBar();
  if (button) button.classList.add('busy');
  $('last').textContent = '洗衣券出票中…';

  try {
    const response = await fetch('/' + path, { method: 'POST' });
    if (!response.ok) throw new Error('bad status');

    show('已出票：' + name);
    $('last').textContent = '刚刚：' + name;
    if (button) {
      button.classList.remove('busy');
      button.classList.add('ok');
      setTimeout(() => button.classList.remove('ok'), 700);
    }
    confetti(event);
  } catch (error) {
    if (button) button.classList.remove('busy');
    show('出票失败，检查一下热点连接', true);
    $('last').textContent = '出票失败';
  }
}

function checkHex() {
  const textarea = $('custom');
  const data = textarea.value.trim();
  const ok = /^([0-9a-fA-F]{2}\s*)+$/.test(data);
  textarea.classList.remove('ok', 'bad');

  if (!data) {
    $('bytes').textContent = '0';
    $('valid').textContent = '未输入';
    $('hexState').textContent = 'HEX';
    return false;
  }

  const count = (data.match(/[0-9a-fA-F]{2}/g) || []).length;
  $('bytes').textContent = count;
  $('valid').textContent = ok ? '格式正确' : '格式错误';
  $('hexState').textContent = ok ? '可发送' : '需检查';
  textarea.classList.add(ok ? 'ok' : 'bad');
  return ok;
}

async function sendCustom(event) {
  const data = $('custom').value.trim();
  if (!data) {
    show('先写点 HEX 指令', true);
    return;
  }
  if (!checkHex()) {
    show('格式不对：请输入十六进制字节', true);
    return;
  }

  runBar();
  $('last').textContent = '隐藏抽屉发送中…';

  try {
    const response = await fetch('/custom', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'd=' + encodeURIComponent(data),
    });
    if (!response.ok) throw new Error('bad status');

    show('隐藏抽屉已出票');
    $('last').textContent = '刚刚：隐藏抽屉';
    confetti(event);
  } catch (error) {
    show('发送失败，检查一下热点连接', true);
    $('last').textContent = '发送失败';
  }
}
