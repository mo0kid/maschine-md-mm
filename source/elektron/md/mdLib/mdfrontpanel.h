#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace md
{
	// FrontPanel decodes the Elektron Machinedrum/Monomachine host->panel UART
	// byte stream and reconstructs the two things the panel controller drives:
	//
	//   (a) a 128x64 graphic LCD (Winstar WG12864A, a KS0107/KS0108-compatible
	//       controller pair). The host sends compact "tile" writes: a byte in the
	//       range 0x10..0x1f (bit3 = controller half, bits0-2 = KS0108 page 0-7),
	//       a column base 0x00..0x38 in steps of 8, then 8 payload bytes, one per
	//       LCD column. Each payload byte is 8 vertical pixels, LSB = top.
	//
	//   (b) LED bank command bytes, each sent as [cmd][arg]. All are active-low
	//       (a 0 bit lights the LED). MD defines the first six:
	//         0x20 = step/trig LEDs 1-8       0x21 = step/trig LEDs 9-16
	//         0x22 = status LEDs              0x23 = mode LEDs
	//         0x24 = sound/DRUM LEDs 1-8      0x25 = sound/DRUM LEDs 9-16
	//       MM extends the protocol through 0x2d; raw accessors expose those banks.
	//
	// The stream interleaves both. Decoder behavior follows captured firmware
	// output from the earlier bring-up work and the documented LCD controller
	// protocol. It carries no
	// emulator, MCU/DSP state, or I/O.
	// Feed it host->panel bytes with processByte()/processBytes() and read the
	// reconstructed framebuffer and LED banks back through the accessors.
	class FrontPanel
	{
	public:
		struct LedBankWrite
		{
			uint8_t command = 0;
			uint8_t value = 0xff;
		};

		enum class LedColor : uint8_t
		{
			Off,
			Green,
			Red,
			Yellow,
		};

		static constexpr uint32_t g_lcdWidth  = 128;
		static constexpr uint32_t g_lcdHeight = 64;
		static constexpr uint8_t g_firstLedBank = 0x20;
		static constexpr uint8_t g_lastLedBank = 0x2d;
		static constexpr uint32_t g_ledBankCount =
			g_lastLedBank - g_firstLedBank + 1;

		// LED bank command bytes (host->panel direction). Active-low.
		enum class LedBank : uint8_t
		{
			Step0  = 0x20, // step/trig LEDs 1-8
			Step1  = 0x21, // step/trig LEDs 9-16
			Status = 0x22, // status LEDs (see StatusLed)
			Mode   = 0x23, // mode LEDs (see ModeLed)
			Drum0  = 0x24, // sound-selection / DRUM LEDs, tracks 1-8
			Drum1  = 0x25, // sound-selection / DRUM LEDs, tracks 9-16
		};

		// Bit positions inside the 0x22 status bank (active-low, 0 = lit).
		enum class StatusLed : uint8_t
		{
			Page1     = 0,
			Page2     = 1,
			Page3     = 2,
			Pattern   = 3,
			Song      = 4,
			Routing   = 5,
			Effects   = 6,
			Synthesis = 7,
		};

		// Bit positions inside the 0x23 mode bank (active-low, 0 = lit).
		enum class ModeLed : uint8_t
		{
			Classic     = 0,
			Extended    = 1,
			BankGroupAD = 2,
			BankGroupEH = 3,
			Record      = 4, // grid-edit
			Tempo       = 5,
			Page4       = 6,
			// bit 7 is unused
		};

		FrontPanel();

		// Clear the framebuffer, LED banks and parser back to power-on state.
		void reset();

		// Consume the host->panel byte stream.
		std::optional<LedBankWrite> processByte(uint8_t _byte);
		void processBytes(const uint8_t* _data, size_t _size);
		void processBytes(const std::vector<uint8_t>& _data) { processBytes(_data.data(), _data.size()); }

		// LCD framebuffer, 128x64. _x in [0,127], _y in [0,63]. true = pixel lit.
		bool getLcdPixel(uint32_t _x, uint32_t _y) const;

		// Number of lit LCD pixels (0 until the boot logo/UI has been decoded).
		uint32_t countLitPixels() const;

		// Raw 8-vertical-pixel VRAM byte for a controller half (0/1), page (0-7),
		// column (0-63). LSB = topmost pixel of the page.
		uint8_t getLcdVram(uint32_t _half, uint32_t _page, uint32_t _col) const;

		// Raw active-low bank byte as last written by the stream (0xff at reset).
		uint8_t getLedBankRaw(LedBank _bank) const;
		uint8_t getLedBankRaw(uint8_t _command) const;

		// Did the stream ever issue a command for this bank?
		bool wasLedBankWritten(LedBank _bank) const;
		bool wasLedBankWritten(uint8_t _command) const;

		// Decoded LED state. Boolean accessors return true when the LED is lit.
		bool getStepLed(uint32_t _index) const; // Machinedrum steps 1..16
		LedColor getMonomachineStepLedColor(uint32_t _index) const; // MM steps 1..16
		static LedColor decodeMonomachineStepLedColor(uint8_t _raw,
			uint32_t _indexInBank);
		bool getDrumLed(uint32_t _index) const;  // _index 0..15 -> tracks 1..16
		bool getStatusLed(StatusLed _led) const;
		bool getModeLed(ModeLed _led) const;

		// Optional read-only host annotation. The original one-colour drum
		// lamps merge selection with activity, so they cannot identify both.
		int getSelectedMachinedrumTrack() const { return m_selectedMachinedrumTrack; }
		void setSelectedMachinedrumTrack(int _track)
		{
			m_selectedMachinedrumTrack = _track >= 0 && _track < 16 ? _track : -1;
		}

		int getMachinedrumPlaybackStep() const { return m_machinedrumPlaybackStep; }
		void setMachinedrumPlaybackStep(int step)
		{
			m_machinedrumPlaybackStep = step >= 0 && step < 64 ? step : -1;
		}

		// Stream diagnostics.
		uint32_t getByteCount() const { return m_byteCount; }
		uint32_t getTileWriteCount() const { return m_tileWriteCount; }
		uint32_t getLedCommandCount() const { return m_ledCommandCount; }

	private:
		std::optional<LedBankWrite> decode(uint8_t _byte);
		static uint32_t bankIndex(uint8_t _command) { return _command - g_firstLedBank; }
		static uint32_t bankIndex(LedBank _bank)
		{
			return bankIndex(static_cast<uint8_t>(_bank));
		}

		// VRAM [controller-half 0..1][KS0108 page 0..7][column 0..63].
		std::array<std::array<std::array<uint8_t, 64>, 8>, 2> m_lcdVram{};

		// MD writes 0x20..0x25; MM extends the same active-low protocol through 0x2d.
		std::array<uint8_t, g_ledBankCount> m_ledBank{};
		std::array<bool, g_ledBankCount> m_ledBankWritten{};

		// Streaming parser for the observed panel protocol:
		//   0 idle, 1 tile column-base, 2 tile payload, 3 command argument.
		uint8_t m_parseState = 0;
		uint8_t m_cmd = 0;
		uint8_t m_lcdXaddr = 0;
		uint8_t m_lcdYaddr = 0;
		uint8_t m_lcdCol = 0;
		std::array<uint8_t, 8> m_tilePayload{};

		uint32_t m_byteCount = 0;
		uint32_t m_tileWriteCount = 0;
		uint32_t m_ledCommandCount = 0;
		int m_selectedMachinedrumTrack = -1;
		int m_machinedrumPlaybackStep = -1;
	};

	struct FrontPanelLedTransition
	{
		uint64_t sequence = 0;
		uint64_t emulationCycles = 0;
		uint8_t command = 0;
		uint8_t value = 0xff;
	};

	struct FrontPanelPublishedState
	{
		FrontPanel panel;
		uint64_t ledSequence = 0;
	};

	struct FrontPanelLedTransitionStatus
	{
		uint64_t epoch = 0;
		uint64_t dropped = 0;
		uint64_t producedSequence = 0;
		uint64_t publishedSequence = 0;
	};

	// Cross-thread publication boundary for the reconstructed display. The
	// emulation thread owns and mutates its live FrontPanel, then offers a complete
	// value copy without waiting. Readers may briefly wait while copying the last
	// published value, but can never observe a torn copy. A separate bounded SPSC
	// stream preserves complete LED changes that would otherwise begin and end
	// between whole-panel snapshots.
	class FrontPanelPublisher
	{
	public:
		static constexpr size_t g_ledTransitionCapacity = 2048;
		FrontPanelPublisher();

		bool tryPublish(const FrontPanel& _panel);
		bool tryRead(FrontPanel& _panel) const;
		FrontPanel read() const;
		FrontPanelPublishedState readPublishedState() const;
		bool tryPushLedTransition(uint8_t _command, uint8_t _value,
			uint64_t _emulationCycles);
		// Non-destructive observation for secondary displays/controllers. Returns
		// the transition sequence of the most recent active-low edge for one LED.
		uint64_t getLedActivationSequence(uint8_t _command, uint8_t _bit) const;
		// Non-destructive observation of the active-low LED switching off.
		uint64_t getLedDeactivationSequence(uint8_t _command, uint8_t _bit) const;
		size_t drainLedTransitions(FrontPanelLedTransition* _output, size_t _capacity);
		FrontPanelLedTransitionStatus getLedTransitionStatus() const;
		void reset();

	private:
		mutable std::mutex m_mutex;
		FrontPanel m_snapshot;
		std::array<FrontPanelLedTransition, g_ledTransitionCapacity> m_ledTransitions{};
		std::atomic<size_t> m_ledTransitionWrite{0};
		std::atomic<size_t> m_ledTransitionRead{0};
		std::atomic<uint64_t> m_ledTransitionSequence{0};
		std::atomic<uint64_t> m_ledTransitionDropped{0};
		std::atomic<uint64_t> m_ledTransitionEpoch{0};
		std::atomic<uint64_t> m_publishedLedSequence{0};
		std::array<std::atomic<uint8_t>, FrontPanel::g_ledBankCount>
			m_lastLedValues{};
		std::array<std::atomic<uint64_t>, FrontPanel::g_ledBankCount * 8>
			m_ledActivationSequences{};
		std::array<std::atomic<uint64_t>, FrontPanel::g_ledBankCount * 8>
			m_ledDeactivationSequences{};
	};
}
