package dev.slimevr.protocol.rpc

import com.google.flatbuffers.FlatBufferBuilder
import dev.slimevr.config.MountingMethods
import dev.slimevr.config.config
import dev.slimevr.protocol.GenericConnection
import dev.slimevr.protocol.ProtocolAPI
import dev.slimevr.protocol.ProtocolHandler
import dev.slimevr.protocol.datafeed.createTrackerId
import dev.slimevr.protocol.rpc.autobone.RPCAutoBoneHandler
import dev.slimevr.protocol.rpc.reset.RPCResetHandler
import dev.slimevr.protocol.rpc.settings.RPCSettingsHandler
import dev.slimevr.protocol.rpc.settings.createSettingsResponse
import dev.slimevr.tracking.trackers.TrackerPosition
import dev.slimevr.tracking.trackers.TrackerPosition.Companion.getByBodyPart
import dev.slimevr.tracking.trackers.TrackerStatus
import dev.slimevr.tracking.trackers.TrackerUtils.getTrackerForSkeleton
import dev.slimevr.tracking.trackers.udp.MagnetometerStatus
import io.eiren.util.logging.LogManager
import io.github.axisangles.ktmath.Quaternion
import kotlinx.coroutines.*
import solarxr_protocol.MessageBundle
import solarxr_protocol.datatypes.TransactionId
import solarxr_protocol.rpc.*

class RPCHandler(private val api: ProtocolAPI) : ProtocolHandler<RpcMessageHeader>() {
	private val mainScope = CoroutineScope(SupervisorJob())

	init {
		RPCResetHandler(this, api)
		RPCSettingsHandler(this, api)
		RPCAutoBoneHandler(this, api)

		registerPacketListener(
			RpcMessage.AssignTrackerRequest,
			::onAssignTrackerRequest,
		)
		registerPacketListener(
			RpcMessage.ClearDriftCompensationRequest,
			::onClearDriftCompensationRequest,
		)
		registerPacketListener(
			RpcMessage.HeightRequest,
			::onHeightRequest,
		)
		registerPacketListener(
			RpcMessage.SetPauseTrackingRequest,
			::onSetPauseTrackingRequest,
		)
		registerPacketListener(
			RpcMessage.MagToggleRequest,
			::onMagToggleRequest,
		)
		registerPacketListener(
			RpcMessage.ChangeMagToggleRequest,
			::onChangeMagToggleRequest,
		)
	}

	fun onAssignTrackerRequest(conn: GenericConnection, messageHeader: RpcMessageHeader) {
		val req = messageHeader
			.message(AssignTrackerRequest()) as? AssignTrackerRequest ?: return

		val tracker = api.server.getTrackerById(req.trackerId().unpack()) ?: return

		val pos = getByBodyPart(req.bodyPosition())
		val previousTracker = if (pos != null) {
			getTrackerForSkeleton(api.server.allTrackers, pos)
		} else {
			null
		}
		if (previousTracker != null) {
			previousTracker.trackerPosition = null
			api.server.trackerUpdated(previousTracker)
		}
		tracker.trackerPosition = pos

		if (req.mountingOrientation() != null) {
			if (tracker.allowMounting) {
				tracker
					.resetsHandler
					.mountingOrientation = Quaternion(
					req.mountingOrientation().w(),
					req.mountingOrientation().x(),
					req.mountingOrientation().y(),
					req.mountingOrientation().z(),
				)
				api.server.configManager.vrConfig.resetsConfig.lastMountingMethod =
					MountingMethods.MANUAL
			}
		}

		if (req.displayName() != null) {
			tracker.customName = req.displayName()
		}

		if (tracker.isImu()) {
			tracker.resetsHandler.allowDriftCompensation = req.allowDriftCompensation()
		}

		api.server.trackerUpdated(tracker)
	}

	fun onClearDriftCompensationRequest(
		conn: GenericConnection,
		messageHeader: RpcMessageHeader,
	) {
		if (messageHeader
				.message(ClearDriftCompensationRequest()) !is ClearDriftCompensationRequest
		) {
			return
		}

		api.server.clearTrackersDriftCompensation()
	}

	override fun onMessage(conn: GenericConnection, message: RpcMessageHeader) {
		val consumer = handlers[message.messageType().toInt()]
		if (consumer != null) {
			consumer.accept(conn, message)
		} else {
			LogManager
				.info("[ProtocolAPI] Unhandled RPC packet received id: ${message.messageType()}")
		}
	}

	fun createRPCMessage(fbb: FlatBufferBuilder, messageType: Byte, messageOffset: Int, txId: Long?): Int {
		val data = IntArray(1)

		RpcMessageHeader.startRpcMessageHeader(fbb)
		RpcMessageHeader.addMessage(fbb, messageOffset)
		RpcMessageHeader.addMessageType(fbb, messageType)
		txId?.let { txId ->
			RpcMessageHeader.addTxId(fbb, TransactionId.createTransactionId(fbb, txId))
		}
		data[0] = RpcMessageHeader.endRpcMessageHeader(fbb)

		val messages = MessageBundle.createRpcMsgsVector(fbb, data)

		MessageBundle.startMessageBundle(fbb)
		MessageBundle.addRpcMsgs(fbb, messages)
		return MessageBundle.endMessageBundle(fbb)
	}

	fun createRPCMessage(fbb: FlatBufferBuilder, messageType: Byte, messageOffset: Int): Int = createRPCMessage(fbb, messageType, messageOffset, txId = null)

	fun createRPCMessage(fbb: FlatBufferBuilder, messageType: Byte, messageOffset: Int, respondTo: RpcMessageHeader?): Int = createRPCMessage(fbb, messageType, messageOffset, respondTo?.txId()?.id())

	override fun messagesCount(): Int = RpcMessage.names.size

	fun onSetPauseTrackingRequest(conn: GenericConnection, messageHeader: RpcMessageHeader) {
		val req = messageHeader
			.message(SetPauseTrackingRequest()) as? SetPauseTrackingRequest ?: return

		api.server.setPauseTracking(req.pauseTracking(), RPCResetHandler.RESET_SOURCE_NAME)
	}

	fun onHeightRequest(conn: GenericConnection, messageHeader: RpcMessageHeader?) {
		val fbb = FlatBufferBuilder(32)

		val posTrackers = api.server.allTrackers.filter { !it.isInternal && it.status == TrackerStatus.OK && it.hasPosition && it.trackerPosition != null }
		val response = if (posTrackers.isNotEmpty()) {
			HeightResponse
				.createHeightResponse(
					fbb,
					posTrackers.minOf { it.position.y },
					posTrackers.find { it.trackerPosition == TrackerPosition.HEAD }?.position?.y
						?: posTrackers.maxOf { it.position.y },
				)
		} else {
			HeightResponse
				.createHeightResponse(
					fbb,
					0f,
					0f,
				)
		}
		fbb.finish(createRPCMessage(fbb, RpcMessage.HeightResponse, response, messageHeader))
		conn.send(fbb.dataBuffer())
	}

	fun onMagToggleRequest(conn: GenericConnection, messageHeader: RpcMessageHeader) {
		val req = messageHeader
			.message(MagToggleRequest()) as? MagToggleRequest ?: return
		val fbb = FlatBufferBuilder(32)

		if (req.trackerId() == null) {
			val response = MagToggleResponse.createMagToggleResponse(
				fbb,
				0,
				api.server.configManager.vrConfig.server.useMagnetometerOnAllTrackers,
			)
			fbb.finish(createRPCMessage(fbb, RpcMessage.MagToggleResponse, response, messageHeader))
			conn.send(fbb.dataBuffer())
			return
		}

		val tracker = api.server.getTrackerById(req.trackerId().unpack()) ?: return
		val trackerId = createTrackerId(fbb, tracker)
		val response = MagToggleResponse.createMagToggleResponse(
			fbb,
			trackerId,
			tracker.config.shouldHaveMagEnabled == true,
		)
		fbb.finish(createRPCMessage(fbb, RpcMessage.MagToggleResponse, response, messageHeader))
		conn.send(fbb.dataBuffer())
	}

	fun onChangeMagToggleRequest(conn: GenericConnection, messageHeader: RpcMessageHeader) {
		val req = messageHeader
			.message(ChangeMagToggleRequest()) as? ChangeMagToggleRequest ?: return

		if (req.trackerId() == null) {
			mainScope.launch {
				withTimeoutOrNull(MAG_TIMEOUT) {
					api.server.configManager.vrConfig.server.defineMagOnAllTrackers(req.enable())
				}

				val fbb = FlatBufferBuilder(32)
				val response = MagToggleResponse.createMagToggleResponse(
					fbb,
					0,
					api.server.configManager.vrConfig.server.useMagnetometerOnAllTrackers,
				)
				fbb.finish(createRPCMessage(fbb, RpcMessage.MagToggleResponse, response, messageHeader))
				conn.send(fbb.dataBuffer())
			}
			return
		}

		val tracker = api.server.getTrackerById(req.trackerId().unpack()) ?: return
		if (tracker.device == null || tracker.config.shouldHaveMagEnabled == req.enable()) return
		val state = req.enable()
		tracker.config.shouldHaveMagEnabled = state
		// Don't apply magnetometer setting if use magnetometer global setting is not enabled
		if (!api.server.configManager.vrConfig.server.useMagnetometerOnAllTrackers) {
			val fbb = FlatBufferBuilder(32)
			val trackerId = createTrackerId(fbb, tracker)
			val response = MagToggleResponse.createMagToggleResponse(
				fbb,
				trackerId,
				state,
			)
			fbb.finish(createRPCMessage(fbb, RpcMessage.MagToggleResponse, response, messageHeader))
			conn.send(fbb.dataBuffer())
			return
		}

		mainScope.launch {
			withTimeoutOrNull(MAG_TIMEOUT) {
				tracker.device.setMag(if (state) MagnetometerStatus.ENABLED else MagnetometerStatus.DISABLED, tracker.trackerNum)
			}

			val fbb = FlatBufferBuilder(32)
			val trackerId = createTrackerId(fbb, tracker)
			val response = MagToggleResponse.createMagToggleResponse(
				fbb,
				trackerId,
				state,
			)
			fbb.finish(createRPCMessage(fbb, RpcMessage.MagToggleResponse, response, messageHeader))
			conn.send(fbb.dataBuffer())
		}
	}

	fun sendSettingsChangedResponse(conn: GenericConnection, messageHeader: RpcMessageHeader?) {
		val fbb = FlatBufferBuilder(32)
		val settings = createSettingsResponse(fbb, api.server)
		val outbound = createRPCMessage(fbb, RpcMessage.SettingsResponse, settings, messageHeader)
		fbb.finish(outbound)
		conn.send(fbb.dataBuffer())
	}
}
const val MAG_TIMEOUT: Long = 10_000L
