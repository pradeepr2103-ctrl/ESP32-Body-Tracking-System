import { ProtocolClient } from './protocol';
import { MocapScene } from './scene';

const connStatus = document.getElementById('conn-status')!;
const trackerCount = document.getElementById('tracker-count')!;
const modelStatus = document.getElementById('model-status')!;
const canvas = document.getElementById('canvas') as HTMLCanvasElement;

const WS_URL = `ws://${location.hostname}:21110`;

const scene = new MocapScene(canvas, (status) => {
  modelStatus.textContent = status;
});
scene.start();

const client = new ProtocolClient(
  WS_URL,
  (bones, _index) => scene.update(bones),
  (connected, msg) => {
    connStatus.textContent = msg;
    connStatus.className = connected ? 'connected' : 'disconnected';
  },
  (count) => {
    trackerCount.textContent = `Trackers: ${count}`;
  },
);

client.connect();

document.getElementById('controls')!.addEventListener('click', (e) => {
  const btn = (e.target as HTMLElement).closest('[data-cmd]') as HTMLElement;
  if (!btn) return;
  switch (btn.dataset.cmd) {
    case 'reset-yaw': client.sendResetYaw(); break;
    case 'reset-full': client.sendResetFull(); break;
    case 'reset-mounting': client.sendResetMounting(); break;
    case 'autobone-record': client.sendAutoBoneRecord(); break;
    case 'autobone-process': client.sendAutoBoneProcess(); break;
    case 'autobone-apply': client.sendAutoBoneApply(); break;
  }
});

window.addEventListener('beforeunload', () => {
  client.disconnect();
  scene.stop();
});
