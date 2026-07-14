import { ProtocolClient } from './protocol';
import { MocapScene } from './scene';
import { MockWalkQuaternionSource } from './mockWalk';
import {
  QuaternionRecorder,
  RecordingPlayer,
  downloadRecording,
  loadRecordingFromFile,
  type Recording,
} from './recorder';
import type { BoneT } from 'solarxr-protocol';

const connStatus = document.getElementById('conn-status')!;
const trackerCount = document.getElementById('tracker-count')!;
const modelStatus = document.getElementById('model-status')!;
const canvas = document.getElementById('canvas') as HTMLCanvasElement;
const mockToggle = document.getElementById('toggle-mock') as HTMLButtonElement;
const viewToggle = document.getElementById('toggle-view') as HTMLButtonElement;
const recToggle = document.getElementById('rec-toggle') as HTMLButtonElement;
const playToggle = document.getElementById('play-toggle') as HTMLButtonElement;
const recExport = document.getElementById('rec-export') as HTMLButtonElement;
const recImport = document.getElementById('rec-import') as HTMLButtonElement;
const recFileInput = document.getElementById('rec-file-input') as HTMLInputElement;
const recStatus = document.getElementById('rec-status')!;

const WS_URL = `ws://${location.hostname}:21110`;

const scene = new MocapScene(canvas, (status) => {
  modelStatus.textContent = status;
});
scene.start();

// --- Source state -----------------------------------------------------
// Exactly one of these drives the skeleton at a time: live sensors, the
// mock walk cycle, or a loaded/recorded playback.
type Source = 'live' | 'mock' | 'playback';
let activeSource: Source = 'live';
let lastRecording: Recording | null = null;

const recorder = new QuaternionRecorder();
const player = new RecordingPlayer((bones, root) => {
  scene.update(bones);
  // Recordings made before root motion existed (or hand-authored files) may
  // have no root field - hold position rather than snapping to origin.
  if (root) scene.setRootMotion(root.x, root.z, root.yawRad);
});

/** Single entry point for any BoneT[] frame, regardless of where it came from. */
function handleFrame(bones: BoneT[], source: Source, root?: { x: number; z: number; yawRad: number }) {
  if (activeSource !== source) return;
  if (root) scene.setRootMotion(root.x, root.z, root.yawRad);
  scene.update(bones);
  if (recorder.isRecording && source !== 'playback') {
    recorder.capture(bones, root);
    recStatus.textContent = `Recording... ${recorder.frameCount} frames`;
  }
}

// --- Live sensor feed ---------------------------------------------------
const client = new ProtocolClient(
  WS_URL,
  (bones, _index) => {
    handleFrame(bones, 'live');
    if (activeSource === 'live') trackerCount.textContent = `Trackers: ${bones.length}`;
  },
  (connected, msg) => {
    if (activeSource === 'live') {
      connStatus.textContent = msg;
      connStatus.className = connected ? 'connected' : 'disconnected';
    }
  },
  () => {},
);

client.connect();

// --- Mock walk-cycle test mode: drives the skeleton without any sensors -
let mockRafId = 0;
const mockSource = new MockWalkQuaternionSource({ cadenceSpm: 112, mode: 'forward-back', paceDistance: 1.8, paceSpeed: 1.0 });
let lastMockTime = 0;

function mockLoop(now: number) {
  const dt = lastMockTime ? (now - lastMockTime) / 1000 : 0;
  lastMockTime = now;
  mockSource.update(dt);
  const bones = mockSource.getFrame();
  const root = mockSource.getRootMotion();
  handleFrame(bones, 'mock', root);
  if (activeSource === 'mock') trackerCount.textContent = `Trackers: ${bones.length} (mock)`;
  mockRafId = requestAnimationFrame(mockLoop);
}

function stopMock() {
  cancelAnimationFrame(mockRafId);
  mockToggle.classList.remove('btn-rec');
  mockToggle.textContent = 'Test: Walk Cycle';
  scene.setRootMotion(0, 0, 0); // return the character to the center of the grid
}

function stopPlayback() {
  player.stop();
  playToggle.classList.remove('recording');
  playToggle.textContent = 'Play';
  scene.setRootMotion(0, 0, 0);
}

function setSource(next: Source) {
  if (activeSource === 'mock' && next !== 'mock') stopMock();
  if (activeSource === 'playback' && next !== 'playback') stopPlayback();
  activeSource = next;
}

viewToggle.addEventListener('click', () => {
  const stickFigureOn = scene.toggleStickFigureView();
  viewToggle.textContent = stickFigureOn ? 'View: Stick Figure' : 'View: Model';
  viewToggle.classList.toggle('btn-apply', stickFigureOn);
});

mockToggle.addEventListener('click', () => {
  if (activeSource === 'mock') {
    setSource('live');
    connStatus.textContent = 'Disconnected';
    connStatus.className = 'disconnected';
    return;
  }
  setSource('mock');
  mockToggle.classList.add('btn-rec');
  mockToggle.textContent = 'Test: Stop Walk Cycle';
  connStatus.textContent = 'Test mode (walk cycle)';
  connStatus.className = 'connected';
  mockSource.reset();
  lastMockTime = 0;
  mockRafId = requestAnimationFrame(mockLoop);
});

// --- Record / Play / Export / Import ------------------------------------
recToggle.addEventListener('click', () => {
  if (recorder.isRecording) {
    lastRecording = recorder.stop();
    recToggle.classList.remove('recording');
    recToggle.textContent = 'Record';
    recStatus.textContent = `Captured ${lastRecording.frames.length} frames (~${lastRecording.fps.toFixed(1)} fps)`;
    playToggle.disabled = lastRecording.frames.length === 0;
    recExport.disabled = lastRecording.frames.length === 0;
  } else {
    recorder.start();
    recToggle.classList.add('recording');
    recToggle.textContent = 'Stop Recording';
    recStatus.textContent = 'Recording... 0 frames';
  }
});

playToggle.addEventListener('click', () => {
  if (activeSource === 'playback') {
    setSource('live');
    recStatus.textContent = lastRecording ? `Captured ${lastRecording.frames.length} frames` : '';
    return;
  }
  if (!lastRecording || lastRecording.frames.length === 0) return;

  setSource('playback');
  player.load(lastRecording);
  playToggle.classList.add('recording');
  playToggle.textContent = 'Stop';
  recStatus.textContent = `Playing back ${lastRecording.frames.length} frames...`;
  player.play(true);
});

recExport.addEventListener('click', () => {
  if (!lastRecording) return;
  downloadRecording(lastRecording);
});

recImport.addEventListener('click', () => recFileInput.click());

recFileInput.addEventListener('change', async () => {
  const file = recFileInput.files?.[0];
  recFileInput.value = '';
  if (!file) return;
  try {
    lastRecording = await loadRecordingFromFile(file);
    recStatus.textContent = `Loaded ${lastRecording.frames.length} frames from ${file.name}`;
    playToggle.disabled = lastRecording.frames.length === 0;
    recExport.disabled = lastRecording.frames.length === 0;
  } catch (err) {
    recStatus.textContent = `Import failed: ${(err as Error).message}`;
  }
});

// --- Reset / AutoBone controls -------------------------------------------
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
  cancelAnimationFrame(mockRafId);
  player.stop();
});
