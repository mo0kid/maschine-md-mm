#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdsim.h"
#include "mdLib/mdtransportpolicy.h"
#include "dsp56kEmu/memory.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <thread>

namespace md
{
	// Exposes only the split reservation/publication used to exercise the producer
	// lifetime race deterministically; production has no scheduling hook.
	struct PanelInputQueueTestAccess
	{
		static std::optional<size_t> claimFifoSlot(PanelInputQueue& _queue,
			const PanelPacket& _packet)
		{
			_queue.m_inFlightProducers.fetch_add(1, std::memory_order_seq_cst);
			if(PanelInputQueue::isRowState(_packet.row))
				_queue.m_recoveryRows[_packet.row - PanelInputQueue::g_firstRow].store(
					_packet.mask, std::memory_order_release);

			auto position = _queue.m_enqueuePosition.load(std::memory_order_relaxed);
			auto& slot = _queue.m_slots[position % _queue.m_slots.size()];
			if(slot.sequence.load(std::memory_order_acquire) != position
				|| !_queue.m_enqueuePosition.compare_exchange_strong(position,
					position + 1, std::memory_order_relaxed))
			{
				_queue.m_inFlightProducers.fetch_sub(1, std::memory_order_seq_cst);
				return std::nullopt;
			}
			return position;
		}

		static void publishFifoSlot(PanelInputQueue& _queue, const size_t _position,
			const PanelPacket& _packet)
		{
			auto& slot = _queue.m_slots[_position % _queue.m_slots.size()];
			_queue.m_pendingPackets.fetch_add(1, std::memory_order_release);
			slot.packet = _packet;
			slot.sequence.store(_position + 1, std::memory_order_release);
			_queue.m_inFlightProducers.fetch_sub(1, std::memory_order_seq_cst);
		}

		static size_t inFlightProducers(const PanelInputQueue& _queue)
		{
			return _queue.m_inFlightProducers.load(std::memory_order_seq_cst);
		}
	};
}

namespace
{
	bool check(const bool _condition, const char* const _message)
	{
		if(_condition)
			return true;
		std::cerr << _message << '\n';
		return false;
	}

	bool testTransportPolicy()
	{
		const auto md = md::transportPolicy(md::MachineModel::Machinedrum);
		const auto mm = md::transportPolicy(md::MachineModel::Monomachine);
		return check(md.backgroundQuantumMicroseconds == 125.0
			&& md.catchUpMaxDspCycles == 100'000
			&& md.hostReceiveIrqMinWords == 3
			&& md.hostReceiveQueueCapacityWords == 16
			&& md.hostTransmitBackpressureThresholdWords == 4
			&& md.hostTransmitBackpressureReleaseUcCycles == 200'000
			&& !md.exactEssiCycleDeadlines,
			"Machinedrum transport policy changed during consolidation")
			&& check(mm.backgroundQuantumMicroseconds == 30.0
				&& mm.catchUpMaxDspCycles == 100'000
				&& mm.hostReceiveIrqMinWords == 1
				&& mm.hostReceiveQueueCapacityWords == 16
				&& mm.hostTransmitBackpressureThresholdWords == 4
				&& mm.hostTransmitBackpressureReleaseUcCycles == 200'000
				&& mm.exactEssiCycleDeadlines,
				"Monomachine transport policy changed during consolidation");
	}

	bool testDspMemoryFallback()
	{
		dsp56k::DefaultMemoryValidator validator;
		dsp56k::Memory memory(validator, 16, 16, 16);
		if(!check(!memory.hasMmuSupport(), "fallback memory unexpectedly uses an MMU mapping"))
			return false;

		memory.set(dsp56k::MemArea_Y, 3, 0x123456);
		return check(memory.get(dsp56k::MemArea_Y, 3) == 0x123456,
			"fallback memory did not preserve the written value")
			&& check(memory.get(dsp56k::MemArea_X, 3) == 0,
				"fallback memory did not preserve separate memory areas");
	}

	bool testMk2PortAInvertedLoopback()
	{
		md::Sim sim;
		sim.write8(md::Sim::g_ppddr, 0x04);
		sim.setMk2PortAInvertedLoopback(true);
		sim.write8(md::Sim::g_ppdat, 0x04);
		if(!check((sim.read8(md::Sim::g_ppdat) & 0x01) == 0,
			"MKII Port A loopback did not invert HIGH"))
			return false;
		sim.write8(md::Sim::g_ppdat, 0x00);
		return check((sim.read8(md::Sim::g_ppdat) & 0x01) != 0,
			"MKII Port A loopback did not invert LOW");
	}

	bool testFrontPanelStepLeds()
	{
		using LedColor = md::FrontPanel::LedColor;
		struct MmStepFixture
		{
			uint8_t bank;
			uint8_t greenMask;
			uint8_t redMask;
		};
		// Fixed protocol fixtures captured from OS 1.32b UART output while toggling
		// TRIG 1..16 in grid-recording mode. Four green/red LED pairs occupy each
		// active-low bank. Keep this table explicit so a production indexing error
		// is not copied into the expected values.
		constexpr std::array<MmStepFixture, 16> mmSteps{{
			{0x20, 0x01, 0x02}, {0x20, 0x04, 0x08},
			{0x20, 0x10, 0x20}, {0x20, 0x40, 0x80},
			{0x21, 0x01, 0x02}, {0x21, 0x04, 0x08},
			{0x21, 0x10, 0x20}, {0x21, 0x40, 0x80},
			{0x22, 0x01, 0x02}, {0x22, 0x04, 0x08},
			{0x22, 0x10, 0x20}, {0x22, 0x40, 0x80},
			{0x23, 0x01, 0x02}, {0x23, 0x04, 0x08},
			{0x23, 0x10, 0x20}, {0x23, 0x40, 0x80},
		}};
		// The firmware produced ff/7f/3f/bf for step 16 when it was respectively
		// off/red/yellow/green, confirming that red is odd, green even, and yellow both.
		struct ColorFixture
		{
			uint8_t activeMask;
			LedColor color;
		};
		for(uint32_t step = 0; step < mmSteps.size(); ++step)
		{
			const auto& fixture = mmSteps[step];
			const std::array<ColorFixture, 4> colors{{
				{0x00, LedColor::Off},
				{fixture.greenMask, LedColor::Green},
				{fixture.redMask, LedColor::Red},
				{static_cast<uint8_t>(fixture.greenMask | fixture.redMask), LedColor::Yellow},
			}};
			for(const auto& color : colors)
			{
				md::FrontPanel panel;
				const uint8_t message[] = {
					fixture.bank, static_cast<uint8_t>(~color.activeMask)};
				panel.processBytes(message, sizeof(message));
				for(uint32_t candidate = 0; candidate < mmSteps.size(); ++candidate)
				{
					const auto expected = candidate == step ? color.color : LedColor::Off;
					if(!check(panel.getMonomachineStepLedColor(candidate) == expected,
						"Monomachine step LED color mapping is wrong"))
						return false;
				}
			}
		}

		md::FrontPanel panel;
		const uint8_t mdMessage[] = {0x20, 0xfe, 0x21, 0x7f};
		panel.processBytes(mdMessage, sizeof(mdMessage));
		return check(panel.getStepLed(0) && panel.getStepLed(15),
			"Machinedrum step LED mapping changed")
			&& check(!panel.getStepLed(1) && !panel.getStepLed(14),
				"Machinedrum unlit step decoding changed")
			&& check(!panel.getStepLed(16)
				&& panel.getMonomachineStepLedColor(16) == LedColor::Off,
				"out-of-range step LED was accepted");
	}

	bool testMachinedrumPanelLedBanks()
	{
		// OS 1.63 UART captures assign the three early page indicators to
		// status-bank bits 0..2, then tempo and page 4 to mode-bank bits 5..6.
		// Keep each fixture isolated so a shifted enum cannot accidentally pass.
		struct Fixture
		{
			uint8_t bank;
			uint8_t bit;
			bool isMode;
		};
		constexpr std::array<Fixture, 5> fixtures{{
			{0x22, 0, false}, {0x22, 1, false}, {0x22, 2, false},
			{0x23, 5, true}, {0x23, 6, true},
		}};
		constexpr std::array<md::FrontPanel::StatusLed, 3> pages{{
			md::FrontPanel::StatusLed::Page1,
			md::FrontPanel::StatusLed::Page2,
			md::FrontPanel::StatusLed::Page3,
		}};
		constexpr std::array<md::FrontPanel::ModeLed, 2> modes{{
			md::FrontPanel::ModeLed::Tempo,
			md::FrontPanel::ModeLed::Page4,
		}};

		for(size_t i = 0; i < fixtures.size(); ++i)
		{
			md::FrontPanel panel;
			const auto& fixture = fixtures[i];
			const uint8_t message[] = {fixture.bank,
				static_cast<uint8_t>(~(1u << fixture.bit))};
			panel.processBytes(message, sizeof(message));
			const bool lit = fixture.isMode
				? panel.getModeLed(modes[i - pages.size()])
				: panel.getStatusLed(pages[i]);
			if(!check(lit, "Machinedrum tempo/page LED bank mapping is wrong"))
				return false;
		}

		// OS 1.63 owns the sequencer semantics: raw mode bit 0 accompanies
		// Classic behavior (patterns retain the current kit), while bit 1
		// accompanies Extended behavior (patterns recall their associated kit).
		// Keep the semantic labels tied to those wire bits rather than merely
		// asserting that both UI elements can light.
		md::FrontPanel classic;
		const uint8_t classicMessage[] = {0x23, 0xfe};
		classic.processBytes(classicMessage, sizeof(classicMessage));
		if(!check(classic.getModeLed(md::FrontPanel::ModeLed::Classic)
				&& !classic.getModeLed(md::FrontPanel::ModeLed::Extended),
			"Machinedrum Classic LED was assigned to the Extended wire bit"))
			return false;

		md::FrontPanel extended;
		const uint8_t extendedMessage[] = {0x23, 0xfd};
		extended.processBytes(extendedMessage, sizeof(extendedMessage));
		return check(extended.getModeLed(md::FrontPanel::ModeLed::Extended)
				&& !extended.getModeLed(md::FrontPanel::ModeLed::Classic),
			"Machinedrum Extended LED was assigned to the Classic wire bit");
	}

	bool testFrontPanelTransitionPublication()
	{
		md::FrontPanel panel;
		if(!check(!panel.processByte(0x26),
			"LED command byte was reported as a complete write"))
			return false;
		const auto on = panel.processByte(0x7f);
		if(!check(on && on->command == 0x26 && on->value == 0x7f,
			"completed LED change was not reported"))
			return false;
		panel.processByte(0x26);
		if(!check(!panel.processByte(0x7f),
			"unchanged LED bank produced a transition"))
			return false;
		panel.processByte(0x26);
		const auto off = panel.processByte(0xff);
		if(!check(off && off->command == 0x26 && off->value == 0xff,
			"LED off transition was not reported"))
			return false;

		md::FrontPanelPublisher publisher;
		if(!check(publisher.tryPushLedTransition(0x26, 0x7f, 100),
			"first LED transition was rejected")
			|| !check(publisher.tryPushLedTransition(0x26, 0xff, 120),
				"second LED transition was rejected")
			|| !check(publisher.tryPublish(panel),
				"front-panel snapshot was not published"))
			return false;
		if(!check(publisher.getLedActivationSequence(0x26, 7) == 1,
			"non-destructive LED activation edge was not published")
			|| !check(publisher.getLedDeactivationSequence(0x26, 7) == 2,
				"non-destructive LED deactivation edge was not published"))
			return false;

		const auto published = publisher.readPublishedState();
		std::array<md::FrontPanelLedTransition, 2> transitions;
		const auto count = publisher.drainLedTransitions(
			transitions.data(), transitions.size());
		if(!check(published.ledSequence == 2,
			"snapshot did not carry its LED sequence")
			|| !check(count == 2 && transitions[0].sequence == 1
				&& transitions[0].value == 0x7f
				&& transitions[1].sequence == 2
				&& transitions[1].value == 0xff,
				"LED transitions were not drained losslessly in order"))
			return false;

		for(size_t i = 0; i < md::FrontPanelPublisher::g_ledTransitionCapacity; ++i)
			if(!publisher.tryPushLedTransition(0x20,
				static_cast<uint8_t>(i), i))
				return check(false, "LED transition queue filled too early");
		if(!check(!publisher.tryPushLedTransition(0x20, 0xff, 9999),
			"LED transition overflow was not reported")
			|| !check(publisher.getLedTransitionStatus().dropped == 1,
				"LED transition drop telemetry is wrong"))
			return false;

		const auto epoch = publisher.getLedTransitionStatus().epoch;
		publisher.reset();
		return check(publisher.getLedTransitionStatus().epoch == epoch + 1,
			"LED transition reset did not advance its epoch")
			&& check(publisher.getLedTransitionStatus().dropped == 0,
				"LED transition reset retained drop telemetry")
			&& check(publisher.getLedDeactivationSequence(0x26, 7) == 0,
				"LED deactivation edge survived reset");
	}

	bool testPanelInputReleaseRecovery()
	{
		md::PanelInputQueue queue;
		const auto held = md::PanelPacket{0x26, 0x80};
		if(!check(queue.tryPush(held.row, held.mask),
			"panel queue rejected the initial held state"))
			return false;

		// Model restore-paused draining: the accepted press and coalescible encoder
		// pulses fill the finite queue while the emulation consumer is stopped.
		for(size_t i = 1; i < md::PanelInputQueue::g_capacityPackets; ++i)
			if(!check(queue.tryPush(0x30, 0x01),
				"panel queue filled before its documented capacity"))
				return false;

		// The release cannot enter the FIFO, but it must survive as authoritative
		// row state even if the producer (the Editor during destruction) goes away.
		if(!check(!queue.tryPush(held.row, 0),
			"saturated panel queue reported accepting the release")
			|| !check(!queue.tryPush(0x23, 0),
				"row state bypassed pending row recovery")
			|| !check(!queue.tryPush(0x31, 0xff),
				"encoder pulse bypassed pending row recovery"))
			return false;

		auto status = queue.status();
		if(!check(status.rowRecoveryPending,
			"rejected release did not arm row recovery")
			|| !check(status.overflowCount == 1 && status.rejectedPackets == 3,
				"panel rejection telemetry is wrong")
			|| !check(status.droppedPulsePackets == 1,
				"coalescible encoder drop telemetry is wrong")
			|| !check(status.coalescedRowPackets == 1,
				"authoritative row coalescing telemetry is wrong")
			|| !check(queue.size()
				== (md::PanelInputQueue::g_capacityPackets + 7) * 2,
				"retained row recovery was absent from pending byte telemetry"))
			return false;

		md::PanelInputQueue::DrainBuffer batch;
		bool sawPress = false;
		bool sawReleaseAfterPress = false;
		for(size_t pass = 0; pass < 4; ++pass)
		{
			const auto count = queue.drain(batch);
			for(size_t i = 0; i < count; ++i)
			{
				if(batch[i] == held)
					sawPress = true;
				if(sawPress && batch[i].row == held.row && batch[i].mask == 0)
					sawReleaseAfterPress = true;
			}
		}
		if(!check(queue.pendingPackets() == 0 && queue.hasPending(),
			"row recovery lost its emulation-thread wake after FIFO drain"))
			return false;

		// At this point the simulated Editor/producer is gone. Recovery lives in the
		// queue, so the next post-restore consumer pass still emits every row.
		const auto recoveryCount = queue.drain(batch);
		if(!check(recoveryCount == 7,
			"panel recovery did not publish one complete row snapshot"))
			return false;
		for(size_t i = 0; i < recoveryCount; ++i)
			if(sawPress && batch[i].row == held.row && batch[i].mask == 0)
				sawReleaseAfterPress = true;

		status = queue.status();
		return check(sawPress && sawReleaseAfterPress,
			"authoritative release did not follow the accepted press")
			&& check(!status.rowRecoveryPending && queue.size() == 0,
				"panel release recovery did not quiesce")
			&& check(status.recoveredRowPackets == 7,
				"panel recovery telemetry did not report the row snapshot");
	}

	bool testPanelInputRecoveryWaitsForClaimedProducer()
	{
		md::PanelInputQueue queue;
		for(size_t i = 0; i + 1 < md::PanelInputQueue::g_capacityPackets; ++i)
			if(!check(queue.tryPush(0x30, 0x01),
				"panel queue filled before the final FIFO slot"))
				return false;

		const md::PanelPacket held{0x24, 0x02};
		const auto claimed = md::PanelInputQueueTestAccess::claimFifoSlot(queue,
			held);
		if(!check(claimed.has_value(), "failed to reserve the final FIFO slot")
			|| !check(md::PanelInputQueueTestAccess::inFlightProducers(queue) == 1,
				"stalled panel producer was not tracked in flight"))
			return false;

		bool releaseAccepted = true;
		std::thread releaseProducer([&queue, &held, &releaseAccepted]
		{
			releaseAccepted = queue.tryPush(held.row, 0);
		});
		releaseProducer.join();
		if(!check(!releaseAccepted,
			"release unexpectedly entered a FIFO with its final slot reserved")
			|| !check(queue.status().rowRecoveryPending,
				"rejected release did not arm recovery behind the reservation"))
			return false;

		md::PanelInputQueue::DrainBuffer batch;
		size_t drained = 0;
		for(size_t pass = 0; pass < 4; ++pass)
		{
			const auto count = queue.drain(batch);
			drained += count;
			for(size_t i = 0; i < count; ++i)
				if(!check(batch[i] == md::PanelPacket{0x30, 0x01},
					"recovery overtook the stalled FIFO reservation"))
					return false;
		}
		if(!check(drained == md::PanelInputQueue::g_capacityPackets - 1,
			"did not drain every published packet ahead of the reservation")
			|| !check(queue.pendingPackets() == 0 && queue.hasPending(),
				"stalled reservation lost its retained recovery wake"))
			return false;

		// With FIFO accounting alone this empty-looking pass would emit the release
		// snapshot before the already-claimed press can publish.
		if(!check(queue.drain(batch) == 0,
			"recovery overtook an in-flight producer's claimed FIFO slot")
			|| !check(queue.status().rowRecoveryPending,
				"blocked recovery was cleared while a producer remained in flight"))
			return false;
		const md::PanelPacket gatedRow{0x25, 0x08};
		if(!check(!queue.tryPush(gatedRow.row, gatedRow.mask),
			"closed recovery gate admitted a newer FIFO packet"))
			return false;

		md::PanelInputQueueTestAccess::publishFifoSlot(queue, *claimed, held);
		const auto count = queue.drain(batch);
		if(!check(count == 8 && batch[0] == held,
			"claimed press was not delivered before its recovery snapshot"))
			return false;
		bool sawRelease = false;
		bool sawGatedRow = false;
		for(size_t i = 1; i < count; ++i)
		{
			if(batch[i].row == held.row && batch[i].mask == 0)
				sawRelease = true;
			if(batch[i] == gatedRow)
				sawGatedRow = true;
		}

		return check(sawRelease,
			"authoritative release did not follow the claimed press")
			&& check(sawGatedRow,
				"row state rejected by the closed gate was not recovered")
			&& check(md::PanelInputQueueTestAccess::inFlightProducers(queue) == 0,
				"published producer remained marked in flight")
			&& check(!queue.hasPending() && queue.size() == 0,
				"claimed-producer recovery did not quiesce")
			&& check(queue.tryPush(0x30, 0x01),
				"FIFO admission did not reopen after recovery cleared");
	}
}

int main()
{
	if(!testTransportPolicy() || !testDspMemoryFallback() || !testMk2PortAInvertedLoopback()
		|| !testFrontPanelStepLeds() || !testMachinedrumPanelLedBanks()
		|| !testFrontPanelTransitionPublication()
		|| !testPanelInputReleaseRecovery()
		|| !testPanelInputRecoveryWaitsForClaimedProducer())
		return 1;
	std::cout << "mdLib tests passed\n";
	return 0;
}
