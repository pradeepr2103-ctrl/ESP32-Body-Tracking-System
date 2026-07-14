import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { BoneT, BodyPart } from 'solarxr-protocol';

const BONE_RADIUS = 0.02;
const JOINT_RADIUS = 0.03;

export const BONE_PARENT: Partial<Record<BodyPart, BodyPart>> = {
  [BodyPart.HEAD]: BodyPart.NECK,
  [BodyPart.NECK]: BodyPart.UPPER_CHEST,
  [BodyPart.UPPER_CHEST]: BodyPart.CHEST,
  [BodyPart.CHEST]: BodyPart.WAIST,
  [BodyPart.WAIST]: BodyPart.HIP,
  [BodyPart.HIP]: BodyPart.NONE,
  [BodyPart.LEFT_UPPER_LEG]: BodyPart.HIP,
  [BodyPart.LEFT_LOWER_LEG]: BodyPart.LEFT_UPPER_LEG,
  [BodyPart.LEFT_FOOT]: BodyPart.LEFT_LOWER_LEG,
  [BodyPart.RIGHT_UPPER_LEG]: BodyPart.HIP,
  [BodyPart.RIGHT_LOWER_LEG]: BodyPart.RIGHT_UPPER_LEG,
  [BodyPart.RIGHT_FOOT]: BodyPart.RIGHT_LOWER_LEG,
  [BodyPart.LEFT_UPPER_ARM]: BodyPart.UPPER_CHEST,
  [BodyPart.LEFT_LOWER_ARM]: BodyPart.LEFT_UPPER_ARM,
  [BodyPart.LEFT_HAND]: BodyPart.LEFT_LOWER_ARM,
  [BodyPart.RIGHT_UPPER_ARM]: BodyPart.UPPER_CHEST,
  [BodyPart.RIGHT_LOWER_ARM]: BodyPart.RIGHT_UPPER_ARM,
  [BodyPart.RIGHT_HAND]: BodyPart.RIGHT_LOWER_ARM,
  [BodyPart.LEFT_SHOULDER]: BodyPart.UPPER_CHEST,
  [BodyPart.RIGHT_SHOULDER]: BodyPart.UPPER_CHEST,
};

// Default resting colour for every stick-figure bone/joint. Individual
// per-bone colours were replaced with a single blue so the whole skeleton
// reads the same way at rest, making the green 90° highlight below stand out
// clearly against it.
const BASE_COLOR = 0x2299ff;
const HIGHLIGHT_COLOR = 0x00ff00;

// ---- 90° highlight configuration -------------------------------------------
// For each limb, the three joints that make up the knee/elbow angle and
// which body parts should turn green when that angle is ~90°.
//   legs: hip -> knee -> ankle
//   arms: chest -> elbow -> wrist   (chest is used as the shoulder reference
//         because a shoulder tracker isn't always present in the data)
// NOTE on joint positions: each BoneT's headPositionG is the PROXIMAL
// (start) point of that bone - the joint between two bones is therefore the
// CHILD bone's headPositionG, not the parent's. E.g. LEFT_LOWER_LEG's
// headPositionG is the knee (end of the upper leg / start of the lower leg);
// LEFT_FOOT's headPositionG is the ankle. Using LEFT_UPPER_LEG's own
// headPositionG as "the knee" (as an earlier version of this file did) is
// wrong - it's the same point as HIP, so the angle would always read as 0.
const ANGLE_HIGHLIGHTS: {
  jointA: BodyPart; // proximal (hip / shoulder)
  jointB: BodyPart; // middle   (knee / elbow) - this bone's headPositionG
  jointC: BodyPart; // distal   (ankle / wrist) - this bone's headPositionG
  highlightParts: BodyPart[];
}[] = [
  {
    jointA: BodyPart.HIP,
    jointB: BodyPart.LEFT_LOWER_LEG,
    jointC: BodyPart.LEFT_FOOT,
    highlightParts: [BodyPart.LEFT_LOWER_LEG, BodyPart.LEFT_FOOT],
  },
  {
    jointA: BodyPart.HIP,
    jointB: BodyPart.RIGHT_LOWER_LEG,
    jointC: BodyPart.RIGHT_FOOT,
    highlightParts: [BodyPart.RIGHT_LOWER_LEG, BodyPart.RIGHT_FOOT],
  },
  {
    jointA: BodyPart.LEFT_UPPER_ARM,
    jointB: BodyPart.LEFT_LOWER_ARM,
    jointC: BodyPart.LEFT_HAND,
    highlightParts: [BodyPart.LEFT_LOWER_ARM, BodyPart.LEFT_HAND],
  },
  {
    jointA: BodyPart.RIGHT_UPPER_ARM,
    jointB: BodyPart.RIGHT_LOWER_ARM,
    jointC: BodyPart.RIGHT_HAND,
    highlightParts: [BodyPart.RIGHT_LOWER_ARM, BodyPart.RIGHT_HAND],
  },
];

// |dot product| of the two limb direction vectors below this value means the
// angle between them is within ~8.5° of 90° (cos 90° = 0).
const ANGLE_DOT_THRESHOLD = 0.15;

// This mapping is for the Mixamo X Bot
/* const SLIMEVR_TO_MIXAMO: Partial<Record<BodyPart, string>> = {
  [BodyPart.HIP]: 'mixamorigHips',
  [BodyPart.WAIST]: 'mixamorigSpine',
  [BodyPart.CHEST]: 'mixamorigSpine1',
  [BodyPart.UPPER_CHEST]: 'mixamorigSpine2',
  [BodyPart.NECK]: 'mixamorigNeck',
  [BodyPart.HEAD]: 'mixamorigHead',
  [BodyPart.LEFT_UPPER_LEG]: 'mixamorigLeftUpLeg',
  [BodyPart.LEFT_LOWER_LEG]: 'mixamorigLeftLeg',
  [BodyPart.LEFT_FOOT]: 'mixamorigLeftFoot',
  [BodyPart.RIGHT_UPPER_LEG]: 'mixamorigRightUpLeg',
  [BodyPart.RIGHT_LOWER_LEG]: 'mixamorigRightLeg',
  [BodyPart.RIGHT_FOOT]: 'mixamorigRightFoot',
  [BodyPart.LEFT_UPPER_ARM]: 'mixamorigLeftArm',
  [BodyPart.LEFT_LOWER_ARM]: 'mixamorigLeftForeArm',
  [BodyPart.LEFT_HAND]: 'mixamorigLeftHand',
  [BodyPart.RIGHT_UPPER_ARM]: 'mixamorigRightArm',
  [BodyPart.RIGHT_LOWER_ARM]: 'mixamorigRightForeArm',
  [BodyPart.RIGHT_HAND]: 'mixamorigRightHand',
  [BodyPart.LEFT_SHOULDER]: 'mixamorigLeftShoulder',
  [BodyPart.RIGHT_SHOULDER]: 'mixamorigRightShoulder',
}; */

const SLIMEVR_TO_MIXAMO: Partial<Record<BodyPart, string>> = {
  [BodyPart.HIP]: 'mixamorigHips_01',
  [BodyPart.WAIST]: 'mixamorigSpine_02',
  [BodyPart.CHEST]: 'mixamorigSpine1_03',
  [BodyPart.UPPER_CHEST]: 'mixamorigSpine2_04',
  [BodyPart.NECK]: 'mixamorigNeck_05',
  [BodyPart.HEAD]: 'mixamorigHead_06',
  [BodyPart.LEFT_UPPER_LEG]: 'mixamorigLeftUpLeg_056',
  [BodyPart.LEFT_LOWER_LEG]: 'mixamorigLeftLeg_057',
  [BodyPart.LEFT_FOOT]: 'mixamorigLeftFoot_058',
  [BodyPart.RIGHT_UPPER_LEG]: 'mixamorigRightUpLeg_061',
  [BodyPart.RIGHT_LOWER_LEG]: 'mixamorigRightLeg_00',
  [BodyPart.RIGHT_FOOT]: 'mixamorigRightFoot_062',
  [BodyPart.LEFT_UPPER_ARM]: 'mixamorigLeftArm_09',
  [BodyPart.LEFT_LOWER_ARM]: 'mixamorigLeftForeArm_010',
  [BodyPart.LEFT_HAND]: 'mixamorigLeftHand_011',
  [BodyPart.RIGHT_UPPER_ARM]: 'mixamorigRightArm_033',
  [BodyPart.RIGHT_LOWER_ARM]: 'mixamorigRightForeArm_034',
  [BodyPart.RIGHT_HAND]: 'mixamorigRightHand_035',
  [BodyPart.LEFT_SHOULDER]: 'mixamorigLeftShoulder_08',
  [BodyPart.RIGHT_SHOULDER]: 'mixamorigRightShoulder_032',
};

export class MocapScene {
  private scene: THREE.Scene;
  private camera: THREE.PerspectiveCamera;
  private renderer: THREE.WebGLRenderer;
  private animFrameId = 0;

  private nodes = new Map<BodyPart, { mesh: THREE.Mesh; joint: THREE.Mesh }>();
  private boneLines = new Map<string, THREE.Mesh>();

  private mixamoBones = new Map<BodyPart, THREE.Bone>();
  private mixamoScene: THREE.Group | null = null;
  private mixamoLoaded = false;
  private mixamoLoading = false;
  private onModelStatus: ((status: string) => void) | null = null;

  // When true, always render the color-coded stick figure even if a mesh
  // model is loaded. The 90° highlight only exists on the stick figure's
  // per-bone materials - the GLB mesh has no equivalent, so this is the only
  // way to see the highlight while a model is loaded.
  private forceStickFigure = false;

  // World rotation of each bone in the GLB's rest pose. Server rotations are
  // applied as deltas on top of these, so identity server rotations keep the
  // model in its natural default pose.
  private bindPoseWorld = new Map<BodyPart, THREE.Quaternion>();
  private bindPoseLocal = new Map<BodyPart, THREE.Quaternion>();
  private parentWorldInv = new THREE.Quaternion();
  private worldQuat = new THREE.Quaternion();
  private desiredWorld = new THREE.Quaternion();

  // Character root transform - where the whole body stands on the grid and
  // which way it faces. Kept separate from bone-local pose data and applied
  // once per frame, the same split a game engine's character controller makes
  // between "root motion" and "in-place animation clip".
  private rootPosition = new THREE.Vector3(0, 0, 0);
  private rootYawQuat = new THREE.Quaternion();
  private readonly UP_AXIS = new THREE.Vector3(0, 1, 0);

  constructor(canvas: HTMLCanvasElement, onModelStatus?: (status: string) => void) {
    this.onModelStatus = onModelStatus ?? null;

    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setSize(canvas.parentElement!.clientWidth, canvas.parentElement!.clientHeight);
    this.renderer.shadowMap.enabled = true;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.0;

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0xbbbbbb);

    this.camera = new THREE.PerspectiveCamera(50, canvas.width / canvas.height, 0.1, 50);
    this.camera.position.set(2, 0.3, 3);

    this.setupLights();
    this.setupGround();
    this.setupOrbitControls();
    window.addEventListener('resize', () => this.resize());

    this.loadCharacter();
  }

  private setupLights() {
    const ambient = new THREE.AmbientLight(0x404060, 0.6);
    this.scene.add(ambient);

    const dir = new THREE.DirectionalLight(0xffffff, 1.2);
    dir.position.set(5, 10, 7);
    dir.castShadow = true;
    dir.shadow.mapSize.width = 1024;
    dir.shadow.mapSize.height = 1024;
    this.scene.add(dir);

    const fill = new THREE.DirectionalLight(0x4488ff, 0.3);
    fill.position.set(-3, 1, -4);
    this.scene.add(fill);
  }

  private setupGround() {
      const grid = new THREE.GridHelper(8, 16, 0x000000, 0x888888);
      this.scene.add(grid);
  }

  private setupOrbitControls() {
    let isDragging = false;
    let prevX = 0;
    let prevY = 0;
    let theta = 0;
    let phi = 1.2;
    let radius = 3.5;

    const updateCam = () => {
      this.camera.position.x = radius * Math.sin(phi) * Math.sin(theta);
      this.camera.position.y = radius * Math.cos(phi) - 0.3;
      this.camera.position.z = radius * Math.sin(phi) * Math.cos(theta);
      this.camera.lookAt(0, -0.3, 0);
    };

    this.renderer.domElement.addEventListener('pointerdown', (e) => {
      isDragging = true;
      prevX = e.clientX;
      prevY = e.clientY;
    });
    window.addEventListener('pointermove', (e) => {
      if (!isDragging) return;
      theta -= (e.clientX - prevX) * 0.01;
      phi = Math.max(0.05, Math.min(Math.PI - 0.05, phi + (e.clientY - prevY) * 0.01));
      prevX = e.clientX;
      prevY = e.clientY;
      updateCam();
    });
    window.addEventListener('pointerup', () => {
      isDragging = false;
    });
    this.renderer.domElement.addEventListener(
      'wheel',
      (e) => {
        radius = Math.max(1.2, Math.min(8, radius + e.deltaY * 0.005));
        updateCam();
      },
      { passive: true },
    );
    updateCam();
  }

  private resize() {
    const parent = this.renderer.domElement.parentElement!;
    const w = parent.clientWidth;
    const h = parent.clientHeight;
    this.renderer.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  private async loadCharacter() {
    if (this.mixamoLoading) return;
    this.mixamoLoading = true;
    this.onModelStatus?.('Loading model...');

    try {
      const loader = new GLTFLoader();
      const gltf = await loader.loadAsync('/human.glb');

      const model = gltf.scene;
      model.scale.set(1, 1, 1);
      model.traverse((child) => {
        if (child instanceof THREE.Mesh) {
          child.castShadow = true;
          child.receiveShadow = true;
        }
      });

      const skinnedMesh = model.getObjectByProperty('type', 'SkinnedMesh') as THREE.SkinnedMesh | null;
      if (!skinnedMesh || !skinnedMesh.skeleton) {
        this.onModelStatus?.('Model has no skeleton');
        return;
      }

      const bones = skinnedMesh.skeleton.bones;
      console.log('Model bones:', bones.map(b => b.name));
      for (const [bodyPart, boneName] of Object.entries(SLIMEVR_TO_MIXAMO)) {
        const bone = bones.find((b) => b.name === boneName);
        if (bone) {
          this.mixamoBones.set(Number(bodyPart) as BodyPart, bone);
          this.bindPoseLocal.set(Number(bodyPart) as BodyPart, bone.quaternion.clone()); // NEW
        }
      }

      this.scene.add(model);
      this.mixamoScene = model;
      model.visible = !this.forceStickFigure;

      if (this.mixamoBones.size === 0) {
        this.onModelStatus?.('Model loaded, but 0/20 bones matched — check bone names in console');
        console.warn(
          'No SLIMEVR_TO_MIXAMO names matched this model\'s skeleton. ' +
          'Falling back to stick figure. Available bone names logged above.',
        );
        return; // mixamoLoaded stays false -> updateStickFigure() keeps rendering
      }

      // Compute bind-pose world rotations
      this.mixamoScene.updateMatrixWorld(true);
      for (const [bp, bone] of this.mixamoBones) {
        this.bindPoseWorld.set(bp, bone.getWorldQuaternion(new THREE.Quaternion()));
      }

      this.mixamoLoaded = true;
      this.onModelStatus?.(`Model loaded (${this.mixamoBones.size}/20 bones matched)`);
    } catch (err) {
      this.onModelStatus?.(`Model error: ${(err as Error).message}`);
      this.mixamoLoading = false;
    }
  }

  /**
   * Places the whole character on the grid: world position (x, z) and facing
   * (yaw, radians). Ground height (y) is intentionally not touched here - the
   * small vertical bob is part of the in-place animation, not the root
   * transform, so it stays consistent regardless of where on the grid the
   * character is standing. Call this once per frame before update(bones).
   */
  setRootMotion(x: number, z: number, yawRad: number) {
    this.rootPosition.set(x, 0, z);
    this.rootYawQuat.setFromAxisAngle(this.UP_AXIS, yawRad);
    if (this.mixamoScene) {
      this.mixamoScene.position.copy(this.rootPosition);
      this.mixamoScene.quaternion.copy(this.rootYawQuat);
    }
  }

  /** Flips between the mesh model and the color-coded stick figure. Returns the new state (true = stick figure forced on). No-op visually if no model is loaded, since the stick figure is already what's shown. */
  toggleStickFigureView(): boolean {
    this.forceStickFigure = !this.forceStickFigure;
    if (this.mixamoScene) this.mixamoScene.visible = !this.forceStickFigure;
    return this.forceStickFigure;
  }

  update(bones: BoneT[]) {
    if (this.mixamoLoaded && !this.forceStickFigure) {
      this.applyMixamoPose(bones);
      this.hideStickFigure();
    } else {
      this.updateStickFigure(bones);
    }
  }

  private applyMixamoPose(bones: BoneT[]) {
      const rootBone = this.mixamoBones.get(BodyPart.HIP);
      if (!rootBone) return;

      const dataByPart = new Map<BodyPart, BoneT>();
      for (const b of bones) dataByPart.set(b.bodyPart, b);

      const rootData = dataByPart.get(BodyPart.HIP);
      if (rootData && rootData.headPositionG) {
        rootBone.position.set(rootData.headPositionG.x, rootData.headPositionG.y, rootData.headPositionG.z);
      }

      const order: BodyPart[] = [
        BodyPart.HIP,
        BodyPart.WAIST,
        BodyPart.CHEST,
        BodyPart.UPPER_CHEST,
        BodyPart.NECK,
        BodyPart.HEAD,
        BodyPart.LEFT_UPPER_ARM,
        BodyPart.LEFT_LOWER_ARM,
        BodyPart.LEFT_HAND,
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

      const usesBindOffset = new Set<BodyPart>([
        BodyPart.LEFT_UPPER_LEG, BodyPart.LEFT_LOWER_LEG, BodyPart.LEFT_FOOT,
        BodyPart.RIGHT_UPPER_LEG, BodyPart.RIGHT_LOWER_LEG, BodyPart.RIGHT_FOOT,
      ]);

      for (const bp of order) {
        const mixamoBone = this.mixamoBones.get(bp);
        if (!mixamoBone) continue;

        const data = dataByPart.get(bp);
        if (!data || !data.rotationG) {
          const restLocal = this.bindPoseLocal.get(bp);
          if (restLocal) mixamoBone.quaternion.copy(restLocal);
          else mixamoBone.quaternion.identity();
          mixamoBone.updateMatrixWorld(true);
          continue;
        }

        this.worldQuat.set(data.rotationG.x, data.rotationG.y, data.rotationG.z, data.rotationG.w);
        const bindWorld = this.bindPoseWorld.get(bp);

        if (usesBindOffset.has(bp) && bindWorld) {
          this.desiredWorld.copy(bindWorld).multiply(this.worldQuat);
        } else {
          this.desiredWorld.copy(this.worldQuat);
        }

        if (!mixamoBone.parent || !(mixamoBone.parent instanceof THREE.Bone)) {
          mixamoBone.quaternion.copy(this.desiredWorld);
        } else {
          mixamoBone.parent.updateMatrixWorld(true);
          mixamoBone.parent.getWorldQuaternion(this.parentWorldInv);
          this.parentWorldInv.invert();
          this.parentWorldInv.multiply(this.desiredWorld);
          mixamoBone.quaternion.copy(this.parentWorldInv);
        }

        mixamoBone.updateMatrixWorld(true);
      }

      if (this.mixamoScene) {
        this.mixamoScene.updateMatrixWorld(true);
      }
  }

  private hideStickFigure() {
    for (const [, node] of this.nodes) {
      node.joint.visible = false;
      node.mesh.visible = false;
    }
    for (const [, line] of this.boneLines) {
      line.visible = false;
    }
  }

  private updateStickFigure(bones: BoneT[]) {
    const seen = new Set<BodyPart>();

    for (const bone of bones) {
      const bp = bone.bodyPart;
      seen.add(bp);
      let node = this.nodes.get(bp);

      if (!node) {
        const color = BASE_COLOR;
        const jointGeo = new THREE.SphereGeometry(JOINT_RADIUS, 12, 12);
        const jointMat = new THREE.MeshStandardMaterial({
          color,
          emissive: color,
          emissiveIntensity: 0.3,
        });
        const joint = new THREE.Mesh(jointGeo, jointMat);
        joint.castShadow = true;
        this.scene.add(joint);

        const boneGeo = new THREE.CylinderGeometry(BONE_RADIUS, BONE_RADIUS, 1, 6);
        const boneMat = new THREE.MeshStandardMaterial({ color, transparent: true, opacity: 0.8 });
        const mesh = new THREE.Mesh(boneGeo, boneMat);
        mesh.castShadow = true;
        this.scene.add(mesh);

        node = { mesh, joint };
        this.nodes.set(bp, node);
      }

      const p = bone.headPositionG;
      const q = bone.rotationG;
      if (!p || !q) {
        node.mesh.visible = false;
        node.joint.visible = false;
        continue;
      }

      // Local (in-place) position/rotation transformed into world space by
      // the character root - same root-motion split as the mixamo model.
      const worldP = new THREE.Vector3(p.x, p.y, p.z)
        .applyQuaternion(this.rootYawQuat)
        .add(this.rootPosition);
      node.joint.position.copy(worldP);

      const len = bone.boneLength;
      if (len > 0.001) {
        node.mesh.visible = true;
        const localMid = new THREE.Vector3(p.x, p.y - len / 2, p.z)
          .applyQuaternion(this.rootYawQuat)
          .add(this.rootPosition);
        node.mesh.position.copy(localMid);
        node.mesh.quaternion.copy(this.rootYawQuat).multiply(
          new THREE.Quaternion(q.x, q.y, q.z, q.w),
        );
        node.mesh.scale.y = len;
      } else {
        node.mesh.visible = false;
      }
    }

    // Which limbs are currently bent to ~90° (knee or elbow)?
    const highlightSet = new Set<BodyPart>();
    const dirA = new THREE.Vector3();
    const dirB = new THREE.Vector3();

    for (const cfg of ANGLE_HIGHLIGHTS) {
      const a = this.nodes.get(cfg.jointA)?.joint.position;
      const b = this.nodes.get(cfg.jointB)?.joint.position;
      const c = this.nodes.get(cfg.jointC)?.joint.position;
      if (!a || !b || !c) continue;
      if (!seen.has(cfg.jointA) || !seen.has(cfg.jointB) || !seen.has(cfg.jointC)) continue;

      dirA.subVectors(b, a).normalize();
      dirB.subVectors(c, b).normalize();
      const dot = Math.abs(dirA.dot(dirB));

      if (dot < ANGLE_DOT_THRESHOLD) {
        for (const part of cfg.highlightParts) highlightSet.add(part);
      }
    }

    for (const [bp, node] of this.nodes) {
      node.joint.visible = seen.has(bp);
      if (!seen.has(bp)) {
        node.mesh.visible = false;
        continue;
      }

      const color = highlightSet.has(bp) ? HIGHLIGHT_COLOR : BASE_COLOR;

      const jointMat = node.joint.material as THREE.MeshStandardMaterial;
      jointMat.color.set(color);
      jointMat.emissive.set(color);
      jointMat.emissiveIntensity = highlightSet.has(bp) ? 0.7 : 0.3;

      const meshMat = node.mesh.material as THREE.MeshStandardMaterial;
      meshMat.color.set(color);
    }

    this.updateBoneLines(seen);
  }

  private updateBoneLines(activeParts: Set<BodyPart>) {
    const connKey = (a: BodyPart, b: BodyPart) => `${Math.min(a, b)}-${Math.max(a, b)}`;
    const needed = new Set<string>();

    for (const [child, parent] of Object.entries(BONE_PARENT)) {
      const c = Number(child) as BodyPart;
      const p = parent as BodyPart;
      if (activeParts.has(c) && activeParts.has(p)) {
        const key = connKey(c, p);
        needed.add(key);
        if (!this.boneLines.has(key)) {
          const geo = new THREE.CylinderGeometry(0.008, 0.008, 1, 4);
          const mat = new THREE.MeshBasicMaterial({ color: 0x666688, transparent: true, opacity: 0.4 });
          const mesh = new THREE.Mesh(geo, mat);
          this.scene.add(mesh);
          this.boneLines.set(key, mesh);
        }
      }
    }

    for (const [key, mesh] of this.boneLines) {
      if (needed.has(key)) {
        const [a, b] = key.split('-').map(Number) as [BodyPart, BodyPart];
        const posA = this.nodes.get(a)?.joint.position;
        const posB = this.nodes.get(b)?.joint.position;
        if (posA && posB) {
          const mid = new THREE.Vector3().addVectors(posA, posB).multiplyScalar(0.5);
          const dir = new THREE.Vector3().copy(posB).sub(posA);
          const len = dir.length();
          mesh.position.copy(mid);
          mesh.scale.y = len;
          if (len > 0.001) {
            mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.clone().normalize());
          }
          mesh.visible = true;
        }
      } else {
        mesh.visible = false;
      }
    }
  }

  start() {
    const animate = () => {
      this.animFrameId = requestAnimationFrame(animate);
      this.renderer.render(this.scene, this.camera);
    };
    animate();
  }

  stop() {
    cancelAnimationFrame(this.animFrameId);
  }
}
