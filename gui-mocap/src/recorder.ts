import { BodyPart, BoneT, QuatT, Vec3fT } from 'solarxr-protocol';

// Plain-JSON-serializable shape of a single recorded frame. BoneT itself
// isn't JSON-safe (it's a flatbuffers class instance), so we flatten it.
export interface RecordedBone {
  bodyPart: BodyPart;
  rotationG: [number, number, number, number] | null;
  boneLength: number;
  headPositionG: [number, number, number] | null;
}

export interface RecordedRoot {
  x: number;
  z: number;
  yawRad: number;
}

export interface RecordedFrame {
  /** Milliseconds since the start of the recording. */
  t: number;
  bones: RecordedBone[];
  /** Character world position/facing at this frame. Optional for back-compat
   * with recordings made before root motion existed, and for hand-authored
   * files that only care about bone rotations. */
  root?: RecordedRoot;
}

export interface Recording {
  version: 1;
  /** Informational only - actual playback uses each frame's own timestamp. */
  fps: number;
  frames: RecordedFrame[];
}

function toRecordedBone(b: BoneT): RecordedBone {
  return {
    bodyPart: b.bodyPart,
    rotationG: b.rotationG ? [b.rotationG.x, b.rotationG.y, b.rotationG.z, b.rotationG.w] : null,
    boneLength: b.boneLength,
    headPositionG: b.headPositionG ? [b.headPositionG.x, b.headPositionG.y, b.headPositionG.z] : null,
  };
}

function toBoneT(b: RecordedBone): BoneT {
  return new BoneT(
    b.bodyPart,
    b.rotationG ? new QuatT(b.rotationG[0], b.rotationG[1], b.rotationG[2], b.rotationG[3]) : null,
    b.boneLength,
    b.headPositionG ? new Vec3fT(b.headPositionG[0], b.headPositionG[1], b.headPositionG[2]) : null,
  );
}

/** Captures whatever BoneT[] frames you feed it (live sensors or mock data) into a replayable Recording. */
export class QuaternionRecorder {
  private frames: RecordedFrame[] = [];
  private recording = false;
  private startTime = 0;

  get isRecording() {
    return this.recording;
  }

  get frameCount() {
    return this.frames.length;
  }

  start() {
    this.frames = [];
    this.recording = true;
    this.startTime = performance.now();
  }

  /** Stops and returns the finished recording. */
  stop(): Recording {
    this.recording = false;
    return { version: 1, fps: this.estimateFps(), frames: this.frames };
  }

  capture(bones: BoneT[], root?: RecordedRoot) {
    if (!this.recording) return;
    this.frames.push({
      t: performance.now() - this.startTime,
      bones: bones.map(toRecordedBone),
      root,
    });
  }

  private estimateFps(): number {
    if (this.frames.length < 2) return 0;
    const durationSec = (this.frames[this.frames.length - 1].t - this.frames[0].t) / 1000;
    return durationSec > 0 ? (this.frames.length - 1) / durationSec : 0;
  }
}

/** Plays back a Recording (or externally-authored quaternion JSON in the same shape) frame-by-frame at real timing. */
export class RecordingPlayer {
  private frames: RecordedFrame[] = [];
  private playing = false;
  private playStartMs = 0;
  private idx = 0;
  private rafId = 0;
  private loop = true;
  private onFrame: (bones: BoneT[], root?: RecordedRoot) => void;
  private onFinish: (() => void) | null = null;

  constructor(onFrame: (bones: BoneT[], root?: RecordedRoot) => void) {
    this.onFrame = onFrame;
  }

  load(rec: Recording) {
    this.stop();
    this.frames = rec.frames;
    this.idx = 0;
  }

  get frameCount() {
    return this.frames.length;
  }

  get isPlaying() {
    return this.playing;
  }

  play(loop = true, onFinish?: () => void) {
    if (this.frames.length === 0) return;
    this.loop = loop;
    this.onFinish = onFinish ?? null;
    this.playing = true;
    this.idx = 0;
    this.playStartMs = performance.now() - this.frames[0].t;

    const tick = () => {
      if (!this.playing) return;
      const elapsed = performance.now() - this.playStartMs;

      while (this.idx < this.frames.length - 1 && this.frames[this.idx + 1].t <= elapsed) {
        this.idx++;
      }
      const frame = this.frames[this.idx];
      this.onFrame(frame.bones.map(toBoneT), frame.root);

      if (this.idx >= this.frames.length - 1) {
        if (this.loop) {
          this.playStartMs = performance.now();
          this.idx = 0;
        } else {
          this.playing = false;
          this.onFinish?.();
          return;
        }
      }
      this.rafId = requestAnimationFrame(tick);
    };
    this.rafId = requestAnimationFrame(tick);
  }

  stop() {
    this.playing = false;
    cancelAnimationFrame(this.rafId);
  }
}

export function downloadRecording(rec: Recording, filename = `bodysync-recording-${Date.now()}.json`) {
  const blob = new Blob([JSON.stringify(rec)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

export async function loadRecordingFromFile(file: File): Promise<Recording> {
  const text = await file.text();
  const parsed = JSON.parse(text);
  if (!parsed || !Array.isArray(parsed.frames)) {
    throw new Error('Not a valid recording: missing "frames" array');
  }
  return parsed as Recording;
}
