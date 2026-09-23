#pragma once

#include "jucePluginLib/controller.h"
#include "mdLib/mdautomation.h"
#include "mdLib/mdautomationsync.h"
#include "mdLib/mdtypes.h"
#include "mdRealtimeQueue.h"

#include <atomic>
#include <deque>
#include <map>
#include <mutex>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	struct ControllerAutomationTestAccess;

	class Controller : public pluginLib::Controller
	{
	public:
		explicit Controller(AudioPluginAudioProcessor& _p);
		~Controller() override;

		void onStateLoaded() override;

		uint8_t getPartCount() const override;

		bool parseSysexMessage(const pluginLib::SysEx&,
			synthLib::MidiEventSource) override;
		bool parseControllerMessage(const synthLib::SMidiEvent& _event) override;
		bool parseMidiMessage(const synthLib::SMidiEvent& _event) override;
		void processRealtimeParameterChanges(size_t _maximumChanges) override;
		void processOfflineControllerWork() override
		{
			processPendingMidiMessages();
		}

		void sendParameterChange(const pluginLib::Parameter& _parameter,
			pluginLib::ParamValue _value, pluginLib::Parameter::Origin _origin) override;

		bool isAutomationSynchronized() const
		{
			return m_automationReady.load(std::memory_order_acquire);
		}
		uint8_t getAutomationBaseChannel() const
		{
			return m_baseChannel.load(std::memory_order_acquire);
		}
		uint8_t getCurrentPattern() const
		{
			return m_currentPattern.load(std::memory_order_acquire);
		}
		uint16_t getDrumHitMask() const;
		bool hasAutomationGlobalSnapshot() const
		{
			return m_haveGlobal.load(std::memory_order_acquire);
		}
		bool hasAutomationKitSnapshot() const
		{
			return m_haveKit.load(std::memory_order_acquire);
		}
		uint64_t getTransmittedAutomationChangeCount() const
		{
			return m_transmittedAutomationChanges.load(std::memory_order_acquire);
		}
		uint64_t getTransmittedAutomationDigest() const
		{
			return m_transmittedAutomationDigest.load(std::memory_order_acquire);
		}
		uint64_t getRealtimeAutomationOverflowCount() const
		{
			return m_realtimeAutomationOverflows.load(std::memory_order_acquire);
		}
		uint64_t getSynchronizationRequestCount() const
		{
			return m_synchronizationRequests.load(std::memory_order_acquire);
		}
		int getLastFirmwareKitValue(const pluginLib::Parameter& _parameter) const;
		void requestAutomationState();
		std::vector<uint8_t> createAutomationSnapshot() const;
		bool restoreAutomationSnapshot(const std::vector<uint8_t>& _snapshot);

	private:
		friend struct ControllerAutomationTestAccess;
		struct Address
		{
			uint8_t page = 0;
			uint8_t track = 0;
			uint8_t index = 0;

			bool operator<(const Address& _other) const
			{
				if(page != _other.page) return page < _other.page;
				if(track != _other.track) return track < _other.track;
				return index < _other.index;
			}
		};

		struct AutomationSlot
		{
			Address address;
			// One atomic publication is the source of truth for this address. The low
			// byte is the value, the middle bits identify the publication, and the top
			// bit means that the value still needs to reach the firmware. Queue entries
			// are only delivery hints and may safely be stale or absent.
			std::atomic<uint64_t> publication{0};
			// Publications older than this revision are intentionally superseded. UI
			// edits and overflow advance the floor; ordinary DAW writes do not, so
			// every queued host value retains its FIFO delivery semantics.
			std::atomic<uint64_t> deliveryFloorRevision{0};
			// Exact publication whose queue hint was dropped. A versioned marker avoids
			// clearing a newer producer's recovery obligation after a concurrent scan.
			std::atomic<uint64_t> scanPublication{0};
			// Raw value from the most recently accepted stored-Kit dump. This is
			// diagnostic truth, distinct from the live/session publication above.
			std::atomic<uint16_t> lastFirmwareKitValue{0x100};
		};

		struct QueuedAutomationChange
		{
			md::automation::ParameterChange change;
			size_t slotIndex = 0;
			uint64_t publication = 0;
		};

		static constexpr size_t RealtimeAutomationCapacity = 4096;
		static constexpr uint64_t PublicationDirty = uint64_t{1} << 63;
		static constexpr uint64_t PublicationValueMask = 0x7f;
		static constexpr uint64_t PublicationRevisionMask =
			~(PublicationDirty | uint64_t{0xff});
		static_assert(std::atomic<uint64_t>::is_always_lock_free,
			"realtime automation requires lock-free 64-bit atomics");

		pluginLib::Parameter* createParameter(pluginLib::Controller& _controller,
			const pluginLib::Description& _description, uint8_t _part, int _uid,
			const pluginLib::Parameter::PartFormatter& _formatter) override;
		void requestKitState();
		void requestAutomationState(bool _forceApplyKitDump);
		void transmitParameterChange(const md::automation::ParameterChange& _change);
		bool transmitRealtimeParameterChange(
			const md::automation::ParameterChange& _change);
		void drainRealtimeParameterChanges(size_t _maximumChanges, bool _realtime);
		bool deliverAutomationPublication(const QueuedAutomationChange& _queued,
			bool _realtime);
		uint64_t createPublication(uint8_t _value, bool _dirty);
		void publishAutomationIntent(const md::automation::ParameterChange& _change,
			bool _supersedeEarlier = false);
		uint8_t publishFirmwareValue(const Address& _address, uint8_t _value,
			uint64_t _kitRequestRevision = 0);
		static uint8_t publicationValue(uint64_t _publication);
		static uint64_t publicationRevision(uint64_t _publication);
		static bool publicationIsDirty(uint64_t _publication);
		AutomationSlot* findAutomationSlot(const Address& _address);
		const AutomationSlot* findAutomationSlot(const Address& _address) const;
		void completeSynchronizationIfReady();
		bool firmwareReadyForAutomation() const;
		void applyKitParameters(const std::vector<md::automation::ParameterChange>& _changes);
		void onControllerTimer() override;
		void sendMissingSynchronizationRequests();
		void sendSynchronizationRequest(const pluginLib::SysEx& _message) const;
		static uint64_t milliseconds();

		const md::MachineModel m_model;
		std::atomic<uint8_t> m_baseChannel{0x7f};
		std::atomic<bool> m_haveGlobal{false};
		std::atomic<bool> m_haveKit{false};
		std::atomic<bool> m_automationReady{false};
		std::atomic<uint64_t> m_lastStatePollMs{0};
		std::atomic<uint64_t> m_lastPatternStatusRequestMs{0};
		std::atomic<uint64_t> m_kitDumpRequestRevision{0};
		std::atomic<bool> m_forceApplyRequestedKitDump{false};
		std::atomic<bool> m_applyRequestedKitDump{true};
		// The timer/offline consumer is serialized by pluginLib::Controller, but
		// explicit state loads and program changes may request a resync from another
		// non-realtime thread. Keep the protocol trackers and their coupled request
		// flags single-owner across both entry paths. Recursive locking is deliberate:
		// parsing SET STATUS delegates to requestKitState(), and request helpers delegate
		// to sendMissingSynchronizationRequests(). This mutex is never taken by the
		// realtime parameter-publication path.
		std::recursive_mutex m_synchronizationLock;
		md::automation::DumpRequestTracker m_globalSynchronization{true};
		md::automation::DumpRequestTracker m_kitSynchronization{false};
		std::atomic<uint64_t> m_synchronizationEpoch{0};
		std::atomic<uint64_t> m_nextAutomationRevision{1};
		std::atomic<uint64_t> m_transmittedAutomationChanges{0};
		std::atomic<uint64_t> m_transmittedAutomationDigest{14695981039346656037ull};
		std::atomic<uint8_t> m_currentGlobal{0xff};
		std::atomic<uint8_t> m_currentKit{0xff};
		std::atomic<uint8_t> m_currentPattern{0xff};
		std::array<uint8_t, 128> m_drumNoteMap{}; // Guarded by m_synchronizationLock.
		std::array<std::atomic<uint64_t>, 16> m_drumHitUntil{};
		std::deque<AutomationSlot> m_automationSlots;
		std::map<Address, size_t> m_automationSlotIndices;
		RealtimeQueue<QueuedAutomationChange,
			RealtimeAutomationCapacity> m_realtimeAutomationChanges;
		size_t m_dirtyScanPosition = 0;
		bool m_minimumBudgetRecoveryTurn = true;
		std::atomic_flag m_realtimeAutomationDrain = ATOMIC_FLAG_INIT;
		std::atomic<uint64_t> m_realtimeAutomationOverflows{0};
		mutable std::atomic<uint64_t> m_synchronizationRequests{0};
		bool m_syntheticFirmwareReadyForTests = false;
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Controller)
	};
}
