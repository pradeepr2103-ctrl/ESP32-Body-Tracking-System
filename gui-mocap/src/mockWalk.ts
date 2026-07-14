import * as THREE from 'three';
import { BodyPart, BoneT, QuatT, Vec3fT } from 'solarxr-protocol';
import { BONE_PARENT } from './scene';

// SolarXR convention: at identity rotation, a bone's tail points toward -Y
// (from headPositionG). rotationG is a WORLD rotation, so a child bone's
// world rotation is its parent's world rotation composed with a local
// joint-specific delta (this is what makes a knee bend look like a knee
// bend instead of resetting to vertical).

const DEG = Math.PI / 180;

// Approximate adult bone lengths (meters). Only used to walk the FK chain;
// tune to match your rig if you want exact proportions.
const BONE_LENGTH: Partial<Record<BodyPart, number>> = {
  [BodyPart.HIP]: 0.0,
  [BodyPart.WAIST]: 0.12,
  [BodyPart.CHEST]: 0.14,
  [BodyPart.UPPER_CHEST]: 0.12,
  [BodyPart.NECK]: 0.08,
  [BodyPart.HEAD]: 0.22,
  [BodyPart.LEFT_SHOULDER]: 0.16,
  [BodyPart.LEFT_UPPER_ARM]: 0.28,
  [BodyPart.LEFT_LOWER_ARM]: 0.26,
  [BodyPart.LEFT_HAND]: 0.18,
  [BodyPart.RIGHT_SHOULDER]: 0.16,
  [BodyPart.RIGHT_UPPER_ARM]: 0.28,
  [BodyPart.RIGHT_LOWER_ARM]: 0.26,
  [BodyPart.RIGHT_HAND]: 0.18,
  [BodyPart.LEFT_UPPER_LEG]: 0.44,
  [BodyPart.LEFT_LOWER_LEG]: 0.44,
  [BodyPart.LEFT_FOOT]: 0.18,
  [BodyPart.RIGHT_UPPER_LEG]: 0.44,
  [BodyPart.RIGHT_LOWER_LEG]: 0.44,
  [BodyPart.RIGHT_FOOT]: 0.18,
};

// Standing hip height = sum of leg + waist->head chain, roughly.
const HIP_HEIGHT = 0.9;

// FK walk order: parents are always processed before children (mirrors
// BONE_PARENT's topology, rooted at HIP).
const FK_ORDER: BodyPart[] = [
  BodyPart.HIP,
  BodyPart.WAIST,
  BodyPart.CHEST,
  BodyPart.UPPER_CHEST,
  BodyPart.NECK,
  BodyPart.HEAD,
  BodyPart.LEFT_SHOULDER,
  BodyPart.LEFT_UPPER_ARM,
  BodyPart.LEFT_LOWER_ARM,
  BodyPart.LEFT_HAND,
  BodyPart.RIGHT_SHOULDER,
  BodyPart.RIGHT_UPPER_ARM,
  BodyPart.RIGHT_LOWER_ARM,
  BodyPart.RIGHT_HAND,
  BodyPart.LEFT_UPPER_LEG,
  BodyPart.LEFT_LOWER_LEG,
  BodyPart.LEFT_FOOT,
  BodyPart.RIGHT_UPPER_LEG,
  BodyPart.RIGHT_LOWER_LEG,
  BodyPart.RIGHT_FOOT,
];

const X_AXIS = new THREE.Vector3(1, 0, 0);
const Y_AXIS = new THREE.Vector3(0, 1, 0);
const Z_AXIS = new THREE.Vector3(0, 0, 1);
const TAIL_DIR = new THREE.Vector3(0, -1, 0); // identity-rotation tail direction

/** Rectified sine: only bends one way, like a knee. 0 when negative. */
function rect(x: number): number {
  return Math.max(0, Math.sin(x));
}

export type WalkMode = 'in-place' | 'forward' | 'forward-back';

export interface MockWalkOptions {
  /** Steps per minute. 100-115 ~ relaxed walk, 130+ ~ brisk. */
  cadenceSpm?: number;
  /**
   * 'in-place': marches in place, no translation.
   * 'forward': walks continuously forward on +Z.
   * 'forward-back': walks forward, then backward, then forward again -
   *   a smooth pacing cycle. This is the default: natural back-and-forth
   *   human walking instead of an endless one-way drift.
   */
  mode?: WalkMode;
  /** Peak walking speed in m/s (used by 'forward' and 'forward-back'). ~0.9-1.4 is a natural human pace. */
  paceSpeed?: number;
  /** For 'forward-back': how far it walks forward before turning back around, in meters. */
  paceDistance?: number;
}

/** World-space root transform for the character: where it stands on the grid and which way it faces. */
export interface RootMotion {
  x: number;
  z: number;
  yawRad: number;
}

export class MockWalkQuaternionSource {
  private t = 0;
  private strideDuration: number;
  private mode: WalkMode;
  private paceSpeed: number;
  private paceDistance: number;
  private paceAngularFreq: number;
  private pacePeriod: number;
  private lastRoot: RootMotion = { x: 0, z: 0, yawRad: 0 };

  constructor(opts: MockWalkOptions = {}) {
    this.strideDuration = 120 / (opts.cadenceSpm ?? 112);
    this.mode = opts.mode ?? 'forward-back';
    this.paceSpeed = opts.paceSpeed ?? 1.0;
    this.paceDistance = opts.paceDistance ?? 1.8;
    // forwardZ(t) = paceDistance * sin(angularFreq * t); peak speed = paceDistance * angularFreq.
    this.paceAngularFreq = this.paceSpeed / this.paceDistance;
    // Full forward+back cycle period. We wrap elapsed time into this period before
    // ever calling Math.sin() on it, so the sine argument never grows unbounded -
    // this is what keeps root motion exact even after hours of runtime (unwrapped
    // "sin(huge number)" is the classic source of slow numerical drift).
    this.pacePeriod = (2 * Math.PI) / this.paceAngularFreq;
  }

  reset() {
    this.t = 0;
  }

  update(dtSeconds: number) {
    this.t += dtSeconds;
  }

  /**
   * Root motion: the character's world position + facing, as a game engine's
   * character controller would expose it. Computed from wrapped time only
   * (never accumulated/integrated), so it is exactly periodic and cannot drift
   * regardless of session length. Call after getFrame() for the current frame's value.
   */
  getRootMotion(): RootMotion {
    return this.lastRoot;
  }

  /** Local (joint-space) delta rotation on top of the parent's world rotation. */
  private localDelta(bp: BodyPart, phase: number): THREE.Quaternion {
    const q = new THREE.Quaternion();

    switch (bp) {
      case BodyPart.HIP: {
        // Root: subtle pelvis rotation (yaw sway) + weight-shift tilt.
        const yaw = 3 * DEG * Math.sin(phase);
        const tilt = 4 * DEG * Math.sin(phase * 2);
        q.setFromAxisAngle(Y_AXIS, yaw).multiply(
          new THREE.Quaternion().setFromAxisAngle(Z_AXIS, tilt),
        );
        return q;
      }
      case BodyPart.WAIST:
        return q.setFromAxisAngle(Y_AXIS, -2 * DEG * Math.sin(phase));
      case BodyPart.CHEST:
        return q.setFromAxisAngle(Y_AXIS, -3 * DEG * Math.sin(phase));
      case BodyPart.UPPER_CHEST:
        return q.setFromAxisAngle(Y_AXIS, -2 * DEG * Math.sin(phase));
      case BodyPart.NECK:
        return q.setFromAxisAngle(Y_AXIS, 1.5 * DEG * Math.sin(phase));
      case BodyPart.HEAD:
        return q; // identity: head stabilizes relative to neck

      case BodyPart.LEFT_SHOULDER:
      case BodyPart.RIGHT_SHOULDER:
        return q; // fixed, arm swing happens at the upper arm

      case BodyPart.LEFT_UPPER_ARM: {
        // Arms swing opposite to the same-side leg.
        const swing = 22 * DEG * Math.sin(phase + Math.PI);
        return q.setFromAxisAngle(X_AXIS, swing);
      }
      case BodyPart.RIGHT_UPPER_ARM: {
        const swing = 22 * DEG * Math.sin(phase);
        return q.setFromAxisAngle(X_AXIS, swing);
      }
      case BodyPart.LEFT_LOWER_ARM: {
        const bend = 12 * DEG + 8 * DEG * rect(phase + Math.PI + 0.4);
        return q.setFromAxisAngle(X_AXIS, bend);
      }
      case BodyPart.RIGHT_LOWER_ARM: {
        const bend = 12 * DEG + 8 * DEG * rect(phase + 0.4);
        return q.setFromAxisAngle(X_AXIS, bend);
      }
      case BodyPart.LEFT_HAND:
      case BodyPart.RIGHT_HAND:
        return q;

      case BodyPart.LEFT_UPPER_LEG: {
        const swing = 28 * DEG * Math.sin(phase);
        return q.setFromAxisAngle(X_AXIS, swing);
      }
      case BodyPart.RIGHT_UPPER_LEG: {
        const swing = 28 * DEG * Math.sin(phase + Math.PI);
        return q.setFromAxisAngle(X_AXIS, swing);
      }
      case BodyPart.LEFT_LOWER_LEG: {
        // Knee only flexes forward, peaks during swing (leg lifted off ground).
        const flex = 55 * DEG * rect(phase + Math.PI * 0.15);
        return q.setFromAxisAngle(X_AXIS, -flex);
      }
      case BodyPart.RIGHT_LOWER_LEG: {
        const flex = 55 * DEG * rect(phase + Math.PI + Math.PI * 0.15);
        return q.setFromAxisAngle(X_AXIS, -flex);
      }
      case BodyPart.LEFT_FOOT: {
        const ankle = 12 * DEG * Math.sin(phase + 0.6);
        return q.setFromAxisAngle(X_AXIS, ankle);
      }
      case BodyPart.RIGHT_FOOT: {
        const ankle = 12 * DEG * Math.sin(phase + Math.PI + 0.6);
        return q.setFromAxisAngle(X_AXIS, ankle);
      }
      default:
        return q;
    }
  }

  /** Builds a full BoneT[] frame via forward kinematics down the SlimeVR hierarchy. */
  getFrame(): BoneT[] {
    const cyclePos = (this.t % this.strideDuration) / this.strideDuration;
    const phase = cyclePos * Math.PI * 2;

    const worldRot = new Map<BodyPart, THREE.Quaternion>();
    const headPos = new Map<BodyPart, THREE.Vector3>();

    // Vertical bob: two bounces per stride (one per footfall).
    const bob = 0.02 * Math.abs(Math.sin(phase));
    const lateral = 0.015 * Math.sin(phase); // weight-shift sway - stays local to the character

    // --- Root motion: world position + facing, kept fully separate from the
    // local joint animation below (same split a game engine's animation
    // controller makes between "clip-local" motion and "root motion"). It's a
    // pure function of wrapped time, so it never accumulates error and never
    // has to be "corrected" - there's nothing to drift.
    let rootZ = 0;
    let rootYaw = 0;

    if (this.mode === 'forward') {
      // Linear in t - float64 has ~15-16 significant digits, so this alone
      // never loses meaningful precision within any realistic session length.
      rootZ = this.t * this.paceSpeed;
    } else if (this.mode === 'forward-back') {
      // Smooth forward <-> backward pacing: position follows a sine wave over
      // one wrapped period, and the whole body turns 180 degrees at each
      // turnaround point (where velocity crosses zero) so it always walks
      // facing its direction of travel, like a person pacing back and forth.
      const w = this.paceAngularFreq;
      const wrappedT = this.t % this.pacePeriod;
      const wt = w * wrappedT; // bounded to [0, 2*PI)
      rootZ = this.paceDistance * Math.sin(wt);
      const velocitySign = Math.cos(wt); // >0 walking +Z, <0 walking -Z
      const turnSharpness = 3.5;
      const facingBack = 1 - Math.tanh(turnSharpness * velocitySign); // 0 (+Z) .. 2 (-Z)
      rootYaw = (Math.PI / 2) * facingBack;
    }

    this.lastRoot = { x: 0, z: rootZ, yawRad: rootYaw };

    for (const bp of FK_ORDER) {
      const parent = BONE_PARENT[bp];
      const delta = this.localDelta(bp, phase);

      let world: THREE.Quaternion;
      if (parent === undefined || parent === BodyPart.NONE || !worldRot.has(parent)) {
        // Root (HIP): purely local sway. World-space facing/translation is
        // handled by root motion above, applied once to the character's root
        // transform instead of being baked into every bone's world rotation.
        world = delta.clone();
      } else {
        world = worldRot.get(parent)!.clone().multiply(delta);
      }
      worldRot.set(bp, world);

      let head: THREE.Vector3;
      if (parent === undefined || parent === BodyPart.NONE || !headPos.has(parent)) {
        // In-place local position only - no forwardZ/rootYaw here, so the
        // authored gait always looks the same regardless of where root
        // motion has carried the character on the grid.
        head = new THREE.Vector3(lateral, HIP_HEIGHT + bob, 0);
      } else {
        const parentHead = headPos.get(parent)!;
        const parentWorld = worldRot.get(parent)!;
        const parentLen = BONE_LENGTH[parent] ?? 0;
        const tail = TAIL_DIR.clone().applyQuaternion(parentWorld).multiplyScalar(parentLen);
        head = parentHead.clone().add(tail);
      }
      headPos.set(bp, head);
    }

    const bones: BoneT[] = [];
    for (const bp of FK_ORDER) {
      const q = worldRot.get(bp)!;
      const p = headPos.get(bp)!;
      bones.push(
        new BoneT(
          bp,
          new QuatT(q.x, q.y, q.z, q.w),
          BONE_LENGTH[bp] ?? 0,
          new Vec3fT(p.x, p.y, p.z),
        ),
      );
    }
    return bones;
  }
}
