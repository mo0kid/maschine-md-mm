#pragma once

#include "mdLib/mdtypes.h"

#include <array>
#include <cstdint>

namespace mdJucePlugin
{
	// Route incoming host MIDI without interrupting notes held across a focus
	// switch. The two output bits are MD=1 and MM=2.
	class CombinedMidiRouter
	{
	public:
		static constexpr uint8_t machinedrum = 1;
		static constexpr uint8_t monomachine = 2;
		static constexpr uint8_t both = machinedrum | monomachine;

		void reset()
		{
			for(auto& channel : m_noteOwners)
				channel.fill(0);
			m_sustainOwners.fill(0);
			for(auto& channel : m_mmKeyboardNoteChannels) channel.fill(0);
			m_mmKeyboardSustainChannels.fill(0);
		}

		// All live MIDI input follows the selected MM track. Retain ownership per
		// source channel so releasing one keyboard cannot release another's notes.
		uint16_t monomachineChannels(const uint8_t status, const uint8_t data1,
			const uint8_t data2, const uint8_t trackChannel)
		{
			if(status < 0x80 || status >= 0xf0)
				return 0;
			const auto sourceChannel = static_cast<uint8_t>(status & 0x0f);
			auto& notes = m_mmKeyboardNoteChannels[sourceChannel];
			auto& sustain = m_mmKeyboardSustainChannels[sourceChannel];
			// Unknown/disabled routes never fall back to an arbitrary input channel:
			// that channel may be configured to start the pattern sequencer.
			const auto current = static_cast<uint16_t>(trackChannel < 16 ? 1u << trackChannel : 0);
			const auto kind = status & 0xf0;
			const auto note = data1 & 0x7f;
			if(kind == 0x90 && data2 != 0)
			{
				notes[note] |= current;
				return current;
			}
			if(kind == 0x80 || kind == 0x90)
			{
				const auto previous = notes[note];
				notes[note] = 0;
				return previous ? previous : current;
			}
			if(kind == 0xa0)
				return notes[note]
					? notes[note] : current;
			if(kind == 0xb0 && (data1 == 120 || data1 == 123))
			{
				auto channels = sustain;
				for(auto& held : notes)
				{
					channels |= held;
					held = 0;
				}
				sustain = 0;
				return channels ? channels : current;
			}
			if(kind == 0xb0 && data1 == 64)
			{
				if(data2 >= 64)
				{
					sustain |= current;
					return current;
				}
				const auto channels = sustain;
				sustain = 0;
				return channels ? channels : current;
			}
			return current;
		}

		uint8_t route(const uint8_t status, const uint8_t data1,
			const uint8_t data2, const md::MachineModel focused)
		{
			const auto destination = focused == md::MachineModel::Monomachine
				? monomachine : machinedrum;
			// MIDI timing and transport remain shared so both sequencers follow
			// the host even when one is not being played from the keyboard.
			if(status >= 0xf8 || (status >= 0xf1 && status <= 0xf6))
				return both;
			if(status >= 0xf0)
				return destination;
			const auto channel = status & 0x0f;
			const auto kind = status & 0xf0;
			if(kind == 0x90 && data2 != 0)
			{
				m_noteOwners[channel][data1 & 0x7f] |= destination;
				return destination;
			}
			if(kind == 0x80 || kind == 0x90)
			{
				auto& owners = m_noteOwners[channel][data1 & 0x7f];
				const auto result = owners ? owners : destination;
				owners = 0;
				return result;
			}
			if(kind == 0xa0)
			{
				const auto owners = m_noteOwners[channel][data1 & 0x7f];
				return owners ? owners : destination;
			}
			if(kind == 0xb0)
			{
				if(data1 == 120 || data1 == 123)
				{
					m_noteOwners[channel].fill(0);
					m_sustainOwners[channel] = 0;
					return both;
				}
				if(data1 == 64)
				{
					if(data2 >= 64)
					{
						m_sustainOwners[channel] |= destination;
						return destination;
					}
					const auto owners = m_sustainOwners[channel];
					m_sustainOwners[channel] = 0;
					return owners ? owners : destination;
				}
			}
			return destination;
		}

	private:
		std::array<std::array<uint8_t, 128>, 16> m_noteOwners{};
		std::array<uint8_t, 16> m_sustainOwners{};
		std::array<std::array<uint16_t, 128>, 16> m_mmKeyboardNoteChannels{};
		std::array<uint16_t, 16> m_mmKeyboardSustainChannels{};
	};
}
