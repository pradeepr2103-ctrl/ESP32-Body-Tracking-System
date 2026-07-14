package dev.slimevr

import com.jme3.system.NanoTimer
import dev.slimevr.autobone.AutoBoneHandler
import dev.slimevr.bridge.Bridge
import dev.slimevr.config.ConfigManager
import dev.slimevr.protocol.ProtocolAPI
import dev.slimevr.protocol.rpc.TransactionInfo
import dev.slimevr.reset.ResetHandler
import dev.slimevr.reset.ResetTimerManager
import dev.slimevr.reset.resetTimer
import dev.slimevr.setup.HandshakeHandler
import dev.slimevr.setup.TapSetupHandler
import dev.slimevr.tracking.processor.HumanPoseManager
import dev.slimevr.tracking.processor.skeleton.HumanSkeleton
import dev.slimevr.tracking.trackers.*
import dev.slimevr.tracking.trackers.udp.TrackersUDPServer
import dev.slimevr.util.ann.VRServerThread
import dev.slimevr.websocketapi.WebSocketVRBridge
import dev.slimevr.websocketapi.WebsocketAPI
import io.eiren.util.Process
import io.eiren.util.ann.ThreadSafe
import io.eiren.util.ann.ThreadSecure
import io.eiren.util.collections.FastList
import io.eiren.util.logging.LogManager
import solarxr_protocol.datatypes.TrackerIdT
import solarxr_protocol.rpc.ResetType
import java.util.*
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.atomic.AtomicInteger
import java.util.function.Consumer
import kotlin.collections.ArrayList
import kotlin.concurrent.schedule

typealias BridgeProvider = (
	server: VRServer,
	computedTrackers: List<Tracker>,
) -> Sequence<Bridge>

const val SLIMEVR_IDENTIFIER = "dev.slimevr.SlimeVR"

class VRServer @JvmOverloads constructor(
	bridgeProvider: BridgeProvider = { _, _ -> sequence {} },
	val processListProvider: () -> Sequence<Process> = { emptySequence() },
	val tryOpenUri: (String) -> Unit = {},
	acquireMulticastLock: () -> Any? = { null },
	@JvmField val configManager: ConfigManager,
) : Thread("VRServer") {

	@JvmField
	val humanPoseManager: HumanPoseManager
	private val trackers: MutableList<Tracker> = FastList()
	val trackersServer: TrackersUDPServer
	private val bridges: MutableList<Bridge> = FastList()
	private val tasks: Queue<Runnable> = LinkedBlockingQueue()
	private val newTrackersConsumers: MutableList<Consumer<Tracker>> = FastList()
	private val trackerStatusListeners: MutableList<TrackerStatusListener> = FastList()
	private val onTick: MutableList<Runnable> = FastList()
	private val lock = acquireMulticastLock()

	@JvmField
	val deviceManager: DeviceManager

	@JvmField
	val autoBoneHandler: AutoBoneHandler

	@JvmField
	val tapSetupHandler: TapSetupHandler

	@JvmField
	val protocolAPI: ProtocolAPI
	private val timer = Timer()
	private val resetTimerManager = ResetTimerManager()
	val fpsTimer = NanoTimer()

	@JvmField
	val resetHandler: ResetHandler

	@JvmField
	val handshakeHandler = HandshakeHandler()

	// WebSocket API server instance (nullable)
	private val websocketAPI: WebsocketAPI?

	init {
		deviceManager = DeviceManager(this)
		resetHandler = ResetHandler()
		tapSetupHandler = TapSetupHandler()
		humanPoseManager = HumanPoseManager(this)
		autoBoneHandler = AutoBoneHandler(this)
		protocolAPI = ProtocolAPI(this)
		val computedTrackers = humanPoseManager.computedTrackers

		// ========== HARDCODE UDP PORT TO 5005 ==========
		val trackerPort = 5005
		LogManager.info("Starting the tracker server on port $trackerPort...")
		trackersServer = TrackersUDPServer(
			trackerPort,
			"Sensors UDP server",
		) { tracker: Tracker -> registerTracker(tracker) }

		for (bridge in bridgeProvider(this, computedTrackers) + sequenceOf(WebSocketVRBridge(computedTrackers, this))) {
			tasks.add(Runnable { bridge.startBridge() })
			bridges.add(bridge)
		}

		// Instantiate the WebSocket API server (don't start it yet)
		websocketAPI = try {
			println("[VRServer] Creating WebsocketAPI instance...")
			WebsocketAPI(this, protocolAPI)
		} catch (e: Exception) {
			println("[VRServer] ❌ Failed to create WebsocketAPI: ${e.message}")
			e.printStackTrace()
			null
		}

		for (tracker in computedTrackers) {
			registerTracker(tracker)
		}

		// ========== FORCE DATA FEED SEND AFTER REGISTRATION ==========
		queueTask {
			try {
				val handler = protocolAPI.dataFeedHandler
				val methods = handler::class.java.methods
				val sendMethod = methods.find {
					it.name.equals("sendDataFeed") ||
					it.name.equals("sendDataFeedToAll") ||
					it.name.equals("broadcastDataFeed") ||
					it.name.equals("onUpdate")
				}
				if (sendMethod != null) {
					sendMethod.invoke(handler)
					LogManager.info("[VRServer] Data feed sent via ${sendMethod.name}.")
				} else {
					LogManager.warning("[VRServer] No suitable data feed method found.")
				}
			} catch (e: Exception) {
				LogManager.warning("[VRServer] Could not send data feed: ${e.message}")
			}
		}

		instance = this
	}

	fun hasBridge(bridgeClass: Class<out Bridge?>): Boolean {
		for (bridge in bridges) {
			if (bridgeClass.isAssignableFrom(bridge.javaClass)) {
				return true
			}
		}
		return false
	}

	@ThreadSafe
	fun getVRBridge(pred: (Bridge) -> Boolean): Bridge? {
		for (bridge in bridges) {
			if (pred(bridge)) return bridge
		}
		return null
	}

	@ThreadSafe
	fun removeVRBridge(bridge: Bridge) {
		bridge.stopBridge()
		bridges.remove(bridge)
	}

	fun addOnTick(runnable: Runnable) {
		onTick.add(runnable)
	}

	@ThreadSafe
	fun addNewTrackerConsumer(consumer: Consumer<Tracker>) {
		queueTask {
			newTrackersConsumers.add(consumer)
			for (tracker in trackers) {
				consumer.accept(tracker)
			}
		}
	}

	@ThreadSafe
	fun trackerUpdated(tracker: Tracker?) {
		queueTask {
			humanPoseManager.trackerUpdated(tracker)
			updateSkeletonModel()
			refreshTrackersDriftCompensationEnabled()
			configManager.vrConfig.writeTrackerConfig(tracker)
			configManager.saveConfig()
		}
	}

	@ThreadSafe
	fun addSkeletonUpdatedCallback(consumer: Consumer<HumanSkeleton>) {
		queueTask { humanPoseManager.addSkeletonUpdatedCallback(consumer) }
	}

	@VRServerThread
	override fun run() {
		println("[VRServer] run() method started")
		trackersServer.start()
		println("[VRServer] UDP server started")

		// Start WebSocket API server
		websocketAPI?.let {
			println("[VRServer] Attempting to start WebSocket API...")
			try {
				it.start()
				println("[VRServer] ✅ WebSocket API started successfully on port ${it.getPort()}")
				LogManager.info("[VRServer] WebSocket API server started on port ${it.getPort()}")
			} catch (e: Exception) {
				println("[VRServer] ❌ Failed to start WebSocket API: ${e.message}")
				e.printStackTrace()
			}
		} ?: println("[VRServer] ❌ websocketAPI is null – cannot start")

		while (true) {
			fpsTimer.update()
			do {
				val task = tasks.poll() ?: break
				task.run()
			} while (true)
			for (task in onTick) {
				task.run()
			}
			for (bridge in bridges) {
				bridge.dataRead()
			}
			for (tracker in trackers) {
				tracker.tick(fpsTimer.timePerFrame)
			}
			humanPoseManager.update()

			// The data feed is automatically sent by the ProtocolAPI when trackers change.
			// The initial feed was sent in init. No need to spam it here.

			for (bridge in bridges) {
				bridge.dataWrite()
			}
			try {
				sleep(1) // 1000Hz
			} catch (error: InterruptedException) {
				LogManager.info("VRServer thread interrupted")
				break
			}
		}
	}

	@ThreadSafe
	fun queueTask(r: Runnable) {
		tasks.add(r)
	}

	@VRServerThread
	private fun trackerAdded(tracker: Tracker) {
		humanPoseManager.trackerAdded(tracker)
		updateSkeletonModel()
		refreshTrackersDriftCompensationEnabled()
	}

	@ThreadSecure
	fun registerTracker(tracker: Tracker) {
		configManager.vrConfig.readTrackerConfig(tracker)
		queueTask {
			trackers.add(tracker)
			trackerAdded(tracker)
			for (tc in newTrackersConsumers) {
				tc.accept(tracker)
			}
		}
	}

	@ThreadSafe
	fun updateSkeletonModel() {
		queueTask {
			humanPoseManager.updateSkeletonModelFromServer()
		}
	}

	fun resetTrackersFull(resetSourceName: String?, bodyParts: List<Int> = ArrayList()) {
		queueTask { humanPoseManager.resetTrackersFull(resetSourceName, bodyParts) }
	}

	fun resetTrackersYaw(resetSourceName: String?, bodyParts: List<Int> = TrackerUtils.allBodyPartsButFingers) {
		queueTask { humanPoseManager.resetTrackersYaw(resetSourceName, bodyParts) }
	}

	fun resetTrackersMounting(resetSourceName: String?, bodyParts: List<Int>? = null) {
		queueTask { humanPoseManager.resetTrackersMounting(resetSourceName, bodyParts) }
	}

	fun clearTrackersMounting(resetSourceName: String?) {
		queueTask { humanPoseManager.clearTrackersMounting(resetSourceName) }
	}

	fun getPauseTracking(): Boolean = humanPoseManager.getPauseTracking()

	fun setPauseTracking(pauseTracking: Boolean, sourceName: String?) {
		queueTask {
			humanPoseManager.setPauseTracking(pauseTracking, sourceName)
		}
	}

	fun togglePauseTracking(sourceName: String?) {
		queueTask {
			humanPoseManager.togglePauseTracking(sourceName)
		}
	}

	fun scheduleResetTrackersFull(resetSourceName: String?, delay: Long, bodyParts: List<Int> = ArrayList(), tx: TransactionInfo? = null) {
		resetTimer(
			resetTimerManager,
			delay,
			onTick = { progress ->
				resetHandler.sendStarted(ResetType.Full, tx, bodyParts, progress, delay.toInt())
			},
			onComplete = {
				queueTask {
					humanPoseManager.resetTrackersFull(resetSourceName, bodyParts)
					resetHandler.sendFinished(ResetType.Full, tx, bodyParts, delay.toInt())
				}
			},
		)
	}

	fun scheduleResetTrackersYaw(resetSourceName: String?, delay: Long, bodyParts: List<Int> = TrackerUtils.allBodyPartsButFingers, tx: TransactionInfo? = null) {
		resetTimer(
			resetTimerManager,
			delay,
			onTick = { progress ->
				resetHandler.sendStarted(ResetType.Yaw, tx, bodyParts, progress, delay.toInt())
			},
			onComplete = {
				queueTask {
					humanPoseManager.resetTrackersYaw(resetSourceName, bodyParts)
					resetHandler.sendFinished(ResetType.Yaw, tx, bodyParts, delay.toInt())
				}
			},
		)
	}

	fun scheduleResetTrackersMounting(resetSourceName: String?, delay: Long, bodyParts: List<Int>? = null, tx: TransactionInfo? = null) {
		resetTimer(
			resetTimerManager,
			delay,
			onTick = { progress ->
				resetHandler.sendStarted(ResetType.Mounting, tx, bodyParts, progress, delay.toInt())
			},
			onComplete = {
				queueTask {
					humanPoseManager.resetTrackersMounting(resetSourceName, bodyParts)
					resetHandler.sendFinished(ResetType.Mounting, tx, bodyParts, delay.toInt())
				}
			},
		)
	}

	fun scheduleSetPauseTracking(pauseTracking: Boolean, sourceName: String?, delay: Long) {
		timer.schedule(delay) {
			queueTask { humanPoseManager.setPauseTracking(pauseTracking, sourceName) }
		}
	}

	fun scheduleTogglePauseTracking(sourceName: String?, delay: Long) {
		timer.schedule(delay) {
			queueTask { humanPoseManager.togglePauseTracking(sourceName) }
		}
	}

	fun setLegTweaksEnabled(value: Boolean) {
		queueTask { humanPoseManager.setLegTweaksEnabled(value) }
	}

	fun setSkatingReductionEnabled(value: Boolean) {
		queueTask { humanPoseManager.setSkatingCorrectionEnabled(value) }
	}

	fun setFloorClipEnabled(value: Boolean) {
		queueTask { humanPoseManager.setFloorClipEnabled(value) }
	}

	val trackersCount: Int
		get() = trackers.size
	val allTrackers: List<Tracker>
		get() = FastList(trackers)

	fun getTrackerById(id: TrackerIdT): Tracker? {
		for (tracker in trackers) {
			if (tracker.trackerNum != id.trackerNum) {
				continue
			}
			if (id.deviceId == null && tracker.device == null) {
				return tracker
			}
			if (tracker.device != null && id.deviceId != null && id.deviceId.id == tracker.device.id) {
				return tracker
			}
		}
		return null
	}

	fun clearTrackersDriftCompensation() {
		for (t in allTrackers) {
			if (t.isImu()) {
				t.resetsHandler.clearDriftCompensation()
			}
		}
	}

	fun refreshTrackersDriftCompensationEnabled() {
		for (t in allTrackers) {
			if (t.isImu()) {
				t.resetsHandler.refreshDriftCompensationEnabled()
			}
		}
	}

	fun trackerStatusChanged(tracker: Tracker, oldStatus: TrackerStatus, newStatus: TrackerStatus) {
		trackerStatusListeners.forEach { it.onTrackerStatusChanged(tracker, oldStatus, newStatus) }
	}

	fun addTrackerStatusListener(listener: TrackerStatusListener) {
		trackerStatusListeners.add(listener)
	}

	fun removeTrackerStatusListener(listener: TrackerStatusListener) {
		trackerStatusListeners.removeIf { listener == it }
	}

	companion object {
		private val nextLocalTrackerId = AtomicInteger()
		lateinit var instance: VRServer
			private set

		val instanceInitialized: Boolean
			get() = ::instance.isInitialized

		@JvmStatic
		fun getNextLocalTrackerId(): Int = nextLocalTrackerId.incrementAndGet()

		@JvmStatic
		val currentLocalTrackerId: Int
			get() = nextLocalTrackerId.get()
	}
}