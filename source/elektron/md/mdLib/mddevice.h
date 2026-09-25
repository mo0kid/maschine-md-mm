#pragma once

#include <memory>
#include <string>
#include <utility>

#include "mdhardware.h"
#include "mdsyseximport.h"

#include "synthLib/device.h"

namespace md
{
	struct DevicePreparedStateTestAccess;

	class Device : public synthLib::Device
	{
	public:
		enum class ProjectStateRestoreStatus
		{
			Idle,
			Preparing,
			Initializing,
			Finalizing,
			Failed
		};

		class PreparationContext
		{
		public:
			PreparationContext(const PreparationContext&) = delete;
			PreparationContext& operator=(const PreparationContext&) = delete;
			~PreparationContext() = default;

		private:
			friend class Device;
			friend struct DevicePreparedStateTestAccess;

			PreparationContext(const synthLib::DeviceCreateParams& _params,
				MachineModel _model)
				: m_romData(_params.romData)
				, m_romName(_params.romName)
				, m_homePath(_params.homePath)
				, m_model(_model)
			{
			}

			// Preparation may continue after the originating Device is replaced, so this
			// context must own ROM bytes supplied in DeviceCreateParams. That adds exactly
			// params.romData.size() persistent bytes (8 MiB for an in-memory MD/MM ROM,
			// zero for the normal filesystem-discovery path). Capturing the shared context
			// itself remains O(1); changing Hardware/Rom to shared immutable storage is a
			// separate, wider memory-layout change.
			const std::vector<uint8_t> m_romData;
			const std::string m_romName;
			const std::string m_homePath;
			const MachineModel m_model;
		};

		class PreparedState
		{
		public:
			PreparedState(const PreparedState&) = delete;
			PreparedState& operator=(const PreparedState&) = delete;
			PreparedState(PreparedState&&) noexcept = default;
			PreparedState& operator=(PreparedState&&) noexcept = default;
			~PreparedState() = default;

		private:
			friend class Device;
			friend struct DevicePreparedStateTestAccess;

			PreparedState(std::shared_ptr<const PreparationContext> _context,
				std::unique_ptr<Hardware> _hardware, const bool _containsFlash)
				: m_context(std::move(_context)), m_hardware(std::move(_hardware))
				, m_containsFlash(_containsFlash)
			{
			}

			std::shared_ptr<const PreparationContext> m_context;
			std::unique_ptr<Hardware> m_hardware;
			bool m_containsFlash = false;
			bool m_committed = false;
		};

		Device(const synthLib::DeviceCreateParams& _params,
			const std::vector<uint8_t>& _initialPatchRam = {});

		float getSamplerate() const override;
		bool isValid() const override;
		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool supportsStateTransactions() const override { return true; }
		std::unique_ptr<synthLib::Device::StateTransaction> beginStateTransaction(
			std::shared_ptr<const std::vector<uint8_t>> _state,
			synthLib::StateType _type) override;
		bool finishStateTransaction(synthLib::Device::StateTransaction& _transaction) override;
		std::shared_ptr<const PreparationContext> getPreparationContext() const
		{
			return m_preparationContext;
		}
		static std::unique_ptr<PreparedState> prepareFactoryReset(
			std::shared_ptr<const PreparationContext> _context);
		static std::unique_ptr<PreparedState> prepareState(
			std::shared_ptr<const PreparationContext> _context,
			const std::vector<uint8_t>& _state, synthLib::StateType _type,
			const FactoryFlashSnapshot& _factoryFlash = {},
			std::string* _error = nullptr);
		// A sparse UW state cannot be validated without its matching factory
		// baseline. Keep the live Hardware authoritative while an isolated candidate
		// performs first-run initialization, then cold-boot the validated images.
		bool hasDeferredStateRestore() const
		{
			return m_restoreStatus == ProjectStateRestoreStatus::Initializing
				|| m_restoreStatus == ProjectStateRestoreStatus::Finalizing;
		}
		uint64_t deferredStateGeneration() const { return m_deferredStateGeneration; }
		std::unique_ptr<PreparedState> takeFinishedDeferredState(uint64_t& _generation);
		static std::unique_ptr<PreparedState> makeDeferredStateReboot(
			const PreparedState& _validated);
		// Requires exclusive access to this Device. A successful exchange leaves the
		// retired Hardware in _prepared so its destruction can happen after the
		// caller releases any process/control lock.
		bool commitPreparedState(PreparedState& _prepared);
		bool commitDeferredStateRestore(PreparedState& _prepared, uint64_t _generation);
		bool rejectDeferredStateRestore(uint64_t _generation, std::string _error);
		ProjectStateRestoreStatus projectStateRestoreStatus() const { return m_restoreStatus; }
		const std::string& projectStateRestoreError() const { return m_restoreError; }
		bool captureFactoryFlashCachePersistence(std::string& _filename,
			FactoryFlashSnapshot& _snapshot, std::string& _error);
		static bool materializeFactoryFlashCache(FactoryFlashSnapshot& _snapshot,
			const std::shared_ptr<const PreparationContext>& _context,
			std::string& _error);
		static bool writeFactoryFlashCachePersistence(const std::string& _filename,
			const std::vector<uint8_t>& _cache, std::string& _error);
		uint32_t getChannelCountIn() override;
		uint32_t getChannelCountOut() override;
		// The synchronous scheduler needs no extra host-block MIDI reserve.
		uint32_t getDefaultLatencyBlocks() const override { return 0; }
		uint32_t getInternalLatencyInputToOutput() const override
		{
			return g_hostAudioInputSafetyFrames;
		}
		bool setDspClockPercent(uint32_t _percent) override;
		uint32_t getDspClockPercent() const override;
		uint64_t getDspClockHz() const override;
		MachineModel getModel() const { return m_model; }
		uint64_t hardwareEpoch() const { return m_hardwareEpoch; }
		// All import lifecycle calls require the owning Plugin/processor lock.
		// Payload ownership remains in TurboMidiTransfer; dialogs carry only tickets.
		std::optional<SysexImportTicket> beginUserSysexImport();
		SysexImportStartResult startUserSysexImport(const SysexImportTicket& ticket,
			PreparedMidiSysexTransfer& transfer, bool receiveModeConfirmed);
		bool cancelUserSysexImport(const SysexImportTicket& ticket, std::vector<uint8_t>& retired);
		bool resumeUserSysexImport(const SysexImportTicket& ticket, uint32_t transferId,
			size_t receiveStep, bool receiveModeConfirmed);
		bool retireUserSysexImport(const SysexImportTicket& ticket, std::vector<uint8_t>& retired);
		SysexImportProgress userSysexImportProgress() const;
		void setNativeProgramChangesEnabled(bool _enabled) { m_nativeProgramChangesEnabled = _enabled; }
		bool nativeProgramChangesEnabled() const { return m_nativeProgramChangesEnabled; }
		bool isProjectStateRestorePending() const
		{
			return m_restoreStatus == ProjectStateRestoreStatus::Preparing
				|| m_restoreStatus == ProjectStateRestoreStatus::Initializing
				|| m_restoreStatus == ProjectStateRestoreStatus::Finalizing
				|| m_hardware->isProjectStateRestorePending();
		}
		// Panel interaction remains directed at the audible live machine while a
		// replacement is prepared. It is intentionally ephemeral across the reboot.
		bool sendPanelEvent(uint8_t _command, uint8_t _argument)
		{
			return m_hardware->trySendPanelEvent(_command, _argument);
		}
		PanelInputQueueStatus getPanelInputStatus() const
		{
			return m_hardware->getPanelInputStatus();
		}
		FrontPanel getFrontPanelSnapshot() const { return m_frontPanelPublisher->read(); }
		std::shared_ptr<FrontPanelPublisher> getFrontPanelPublisher() const
		{
			return m_frontPanelPublisher;
		}
		FrontPanelPublishedState getFrontPanelPublishedState() const
		{
			return m_frontPanelPublisher->readPublishedState();
		}
		size_t drainFrontPanelLedTransitions(FrontPanelLedTransition* _output,
			size_t _capacity)
		{
			return m_frontPanelPublisher->drainLedTransitions(_output, _capacity);
		}
		FrontPanelLedTransitionStatus getFrontPanelLedTransitionStatus() const
		{
			return m_frontPanelPublisher->getLedTransitionStatus();
		}
		// Direct access for the in-process editor (option B): the front-panel LCD/LED state
		// and panel-event injection. Only valid for a local (non-bridged) device instance.
		Hardware& getHardware() { return *m_hardware; }
		const Hardware& getHardware() const { return *m_hardware; }

	protected:
		void extraLatencyChanged() override;
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

	private:
		friend struct DevicePreparedStateTestAccess;
		bool matchesUserSysexImport(const SysexImportTicket& ticket) const;

		class StateTransactionImpl final : public synthLib::Device::StateTransaction
		{
		public:
			bool prepare() override;

		private:
			friend class Device;
			StateTransactionImpl(std::shared_ptr<const PreparationContext> _context,
				std::shared_ptr<const std::vector<uint8_t>> _state,
				synthLib::StateType _type,
				FactoryFlashSnapshot _factoryFlash, uint64_t _generation,
				std::unique_ptr<PreparedState> _displaced)
				: m_context(std::move(_context)), m_state(std::move(_state)), m_type(_type)
				, m_factoryFlash(std::move(_factoryFlash))
				, m_generation(_generation), m_displaced(std::move(_displaced))
			{
			}

			std::shared_ptr<const PreparationContext> m_context;
			std::shared_ptr<const std::vector<uint8_t>> m_state;
			synthLib::StateType m_type;
			FactoryFlashSnapshot m_factoryFlash;
			uint64_t m_generation;
			std::unique_ptr<PreparedState> m_displaced;
			std::unique_ptr<PreparedState> m_prepared;
			std::string m_error;
			// Disposed with the transaction outside the realtime device lock.
			std::vector<uint8_t> m_retiredSysex;
		};

		void clearProjectStateRestore();
		void failProjectStateRestore(std::string _error);

		const MachineModel m_model;
		std::shared_ptr<FrontPanelPublisher> m_frontPanelPublisher;
		std::shared_ptr<const PreparationContext> m_preparationContext;
		std::unique_ptr<Hardware> m_hardware;
		std::unique_ptr<PreparedState> m_deferredPreparedState;
		std::shared_ptr<const std::vector<uint8_t>> m_requestedState;
		synthLib::StateType m_requestedStateType = synthLib::StateTypeGlobal;
		ProjectStateRestoreStatus m_restoreStatus = ProjectStateRestoreStatus::Idle;
		std::string m_restoreError;
		bool m_nativeProgramChangesEnabled = true;
		std::string m_mdFlashCacheFilename;
		uint64_t m_hardwareEpoch = 0;
		const uint64_t m_sysexDeviceId;
		SysexImportTicket m_sysexTicket;
		bool m_sysexStarted = false;
		bool m_sysexPendingCancelled = false;
		uint64_t m_deferredStateGeneration = 0;
	};
}
