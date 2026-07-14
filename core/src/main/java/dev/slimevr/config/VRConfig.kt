package dev.slimevr.config

import com.fasterxml.jackson.databind.annotation.JsonDeserialize
import com.fasterxml.jackson.databind.annotation.JsonSerialize
import com.fasterxml.jackson.databind.ser.std.StdKeySerializers
import com.github.jonpeterson.jackson.module.versioning.JsonVersionedModel
import dev.slimevr.config.serializers.BridgeConfigMapDeserializer
import dev.slimevr.config.serializers.TrackerConfigMapDeserializer
import dev.slimevr.tracking.trackers.Tracker

@JsonVersionedModel(
	currentVersion = "15",
	defaultDeserializeToVersion = "15",
	toCurrentConverterClass = CurrentVRConfigConverter::class,
)
class VRConfig {
	val server: ServerConfig = ServerConfig()

	val filters: FiltersConfig = FiltersConfig()

	val driftCompensation: DriftCompensationConfig = DriftCompensationConfig()

	val autoBone: AutoBoneConfig = AutoBoneConfig()

	val skeleton: SkeletonConfig = SkeletonConfig()

	val legTweaks: LegTweaksConfig = LegTweaksConfig()

	val tapDetection: TapDetectionConfig = TapDetectionConfig()

	val resetsConfig: ResetsConfig = ResetsConfig()

	val stayAlignedConfig = StayAlignedConfig()

	@JsonDeserialize(using = TrackerConfigMapDeserializer::class)
	@JsonSerialize(keyUsing = StdKeySerializers.StringKeySerializer::class)
	private val trackers: MutableMap<String, TrackerConfig> = HashMap()

	@JsonDeserialize(using = BridgeConfigMapDeserializer::class)
	@JsonSerialize(keyUsing = StdKeySerializers.StringKeySerializer::class)
	private val bridges: MutableMap<String, BridgeConfig> = HashMap()

	val knownDevices: MutableSet<String> = mutableSetOf()

	val velocityConfig: VelocityConfig = VelocityConfig()

	fun getTrackers(): Map<String, TrackerConfig> = trackers

	fun getBridges(): Map<String, BridgeConfig> = bridges

	fun hasTrackerByName(name: String): Boolean = trackers.containsKey(name)

	fun getTracker(tracker: Tracker): TrackerConfig {
		var config = trackers[tracker.name]
		if (config == null) {
			config = TrackerConfig(tracker)
			trackers[tracker.name] = config
		}
		return config
	}

	fun readTrackerConfig(tracker: Tracker) {
		if (tracker.userEditable) {
			val config = getTracker(tracker)
			tracker.readConfig(config)
			if (tracker.isImu()) tracker.resetsHandler.readDriftCompensationConfig(driftCompensation)
			tracker.resetsHandler.readResetConfig(resetsConfig)
			if (tracker.allowReset) {
				tracker.saveMountingResetOrientation(config)
			}
			if (tracker.allowFiltering) {
				tracker
					.filteringHandler
					.readFilteringConfig(filters, tracker.getRotation())
			}
		}
		tracker.allowVelocity = velocityConfig.sendDerivedVelocity
	}

	fun writeTrackerConfig(tracker: Tracker?) {
		if (tracker?.userEditable == true) {
			val tc = getTracker(tracker)
			tracker.writeConfig(tc)
		}
	}

	fun getBridge(bridgeKey: String): BridgeConfig {
		var config = bridges[bridgeKey]
		if (config == null) {
			config = BridgeConfig()
			bridges[bridgeKey] = config
		}
		return config
	}

	fun isKnownDevice(mac: String?): Boolean = knownDevices.contains(mac)

	fun addKnownDevice(mac: String): Boolean = knownDevices.add(mac)

	fun forgetKnownDevice(mac: String): Boolean = knownDevices.remove(mac)
}
