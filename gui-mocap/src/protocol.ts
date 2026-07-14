import * as flatbuffers from 'flatbuffers';
import {
  MessageBundle,
  MessageBundleT,
  StartDataFeedT,
  RpcMessageHeaderT,
  DataFeedMessageHeaderT,
  DataFeedConfigT,
  DataFeedUpdateT,
  BoneT,
  RpcMessage,
  ResetRequestT,
  AutoBoneProcessRequestT,
  AutoBoneApplyRequestT,
  AutoBoneProcessType,
  ResetType,
  TrackerDataMaskT,
  DeviceDataMaskT,
  DataFeedMessage,
} from 'solarxr-protocol';

type BoneCallback = (bones: BoneT[], index: number) => void;

export class ProtocolClient {
  private ws: WebSocket | null = null;
  private url: string;
  private onBones: BoneCallback;
  private onStatus: (connected: boolean, msg: string) => void;
  private onTrackerCount: (count: number) => void;

  constructor(
    url: string,
    onBones: BoneCallback,
    onStatus: (connected: boolean, msg: string) => void,
    onTrackerCount: (count: number) => void,
  ) {
    this.url = url;
    this.onBones = onBones;
    this.onStatus = onStatus;
    this.onTrackerCount = onTrackerCount;
  }

  connect() {
    if (this.ws) this.ws.close();
    this.onStatus(false, 'Connecting...');
    this.ws = new WebSocket(this.url);
    this.ws.binaryType = 'arraybuffer';

    this.ws.onopen = () => {
      this.onStatus(true, 'Connected');
      this.startDataFeed();
    };

    this.ws.onclose = () => {
      this.onStatus(false, 'Disconnected');
      this.ws = null;
    };

    this.ws.onerror = () => {
      this.onStatus(false, 'Connection error');
    };

    this.ws.onmessage = (ev: MessageEvent) => {
      if (ev.data instanceof ArrayBuffer) {
        this.handleMessage(new Uint8Array(ev.data));
      }
    };
  }

  private handleMessage(data: Uint8Array) {
    const buf = new flatbuffers.ByteBuffer(data);
    const bundle = MessageBundle.getRootAsMessageBundle(buf).unpack();
    if (!bundle) return;

    if (bundle.dataFeedMsgs) {
      for (const hdr of bundle.dataFeedMsgs) {
        if (hdr.messageType === DataFeedMessage.DataFeedUpdate) {
          const update = hdr.message as DataFeedUpdateT;
          if (update.bones) {
            this.onBones(update.bones, update.index);
            this.onTrackerCount(update.bones.length);
          }
        }
      }
    }
  }

  private send(msg: MessageBundleT) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
    const builder = new flatbuffers.Builder(1024);
    const offset = msg.pack(builder);
    builder.finish(offset);
    this.ws.send(new Uint8Array(builder.asUint8Array()));
  }

  private startDataFeed() {
    const cfg = new DataFeedConfigT();
    cfg.minimumTimeSinceLast = 25;
    cfg.boneMask = true;
    const trackerMask = new TrackerDataMaskT(true, true, true, true, false, false, false, false, false, false, false, false);
    cfg.dataMask = new DeviceDataMaskT(trackerMask, true);
    cfg.syntheticTrackersMask = trackerMask;

    const startFeed = new StartDataFeedT();
    startFeed.dataFeeds = [cfg];

    const bundle = new MessageBundleT();
    const hdr = new DataFeedMessageHeaderT();
    hdr.messageType = DataFeedMessage.StartDataFeed;
    hdr.message = startFeed;
    bundle.dataFeedMsgs = [hdr];
    this.send(bundle);
  }

  sendResetYaw() {
    const bundle = new MessageBundleT();
    const hdr = new RpcMessageHeaderT();
    hdr.messageType = RpcMessage.ResetRequest;
    hdr.message = new ResetRequestT(ResetType.Yaw, [], null);
    bundle.rpcMsgs = [hdr];
    this.send(bundle);
  }

  sendResetFull() {
    const bundle = new MessageBundleT();
    const hdr = new RpcMessageHeaderT();
    hdr.messageType = RpcMessage.ResetRequest;
    hdr.message = new ResetRequestT(ResetType.Full, [], null);
    bundle.rpcMsgs = [hdr];
    this.send(bundle);
  }

  sendResetMounting() {
    const bundle = new MessageBundleT();
    const hdr = new RpcMessageHeaderT();
    hdr.messageType = RpcMessage.ResetRequest;
    hdr.message = new ResetRequestT(ResetType.Mounting, [], null);
    bundle.rpcMsgs = [hdr];
    this.send(bundle);
  }

  sendAutoBoneRecord() {
    const bundle = new MessageBundleT();
    const hdr = new RpcMessageHeaderT();
    hdr.messageType = RpcMessage.AutoBoneProcessRequest;
    hdr.message = new AutoBoneProcessRequestT(AutoBoneProcessType.RECORD);
    bundle.rpcMsgs = [hdr];
    this.send(bundle);
  }

  sendAutoBoneProcess() {
    const bundle = new MessageBundleT();
    const hdr = new RpcMessageHeaderT();
    hdr.messageType = RpcMessage.AutoBoneProcessRequest;
    hdr.message = new AutoBoneProcessRequestT(AutoBoneProcessType.PROCESS);
    bundle.rpcMsgs = [hdr];
    this.send(bundle);
  }

  sendAutoBoneApply() {
    const bundle = new MessageBundleT();
    const hdr = new RpcMessageHeaderT();
    hdr.messageType = RpcMessage.AutoBoneApplyRequest;
    hdr.message = new AutoBoneApplyRequestT();
    bundle.rpcMsgs = [hdr];
    this.send(bundle);
  }

  disconnect() {
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
  }
}
