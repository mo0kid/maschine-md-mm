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
			m_mmKeyboardNoteChannels.fill(0);
			m_mmKeyboardSustainChannels = 0;
		}

		// Channel 1 is the live keyboard input for MM. Translate it to the
		// focused track's channel; other source channels retain their explicit
		// track assignment. Return a mask because one pitch may be held on more
		// than one track when the performer switches tracks mid-phrase.
		uint16_t monomachineChannels(const uint8_t status, const uint8_t data1,
			const uint8_t data2, const uint8_t selectedTrack,
			const uint8_t baseChannel)
		{
			if(status < 0x80 || status >= 0xf0)
				return 0;
			const auto sourceChannel = static_cast<uint8_t>(status & 0x0f);
			if(sourceChannel != 0)
				return static_cast<uint16_t>(1u << sourceChannel);
			// The global dump may not have arrived at startup. MM's factory
			// base channel is 1 (zero-based 0), so use it until known.
			const auto base = baseChannel < 16 ? baseChannel : 0;
			const auto trackChannel = static_cast<uint8_t>(
				base + selectedTrack < 16 ? base + selectedTrack : sourceChannel);
			const auto current = static_cast<uint16_t>(1u << trackChannel);
			const auto kind = status & 0xf0;
			const auto note = data1 & 0x7f;
			if(kind == 0x90 && data2 != 0)
			{
				m_mmKeyboardNoteChannels[note] |= current;
				return current;
			}
			if(kind == 0x80 || kind == 0x90)
			{
				const auto previous = m_mmKeyboardNoteChannels[note];
				m_mmKeyboardNoteChannels[note] = 0;
				return previous ? previous : current;
			}
			if(kind == 0xa0)
				return m_mmKeyboardNoteChannels[note]
					? m_mmKeyboardNoteChannels[note] : current;
			if(kind == 0xb0 && (data1 == 120 || data1 == 123))
			{
				auto channels = m_mmKeyboardSustainChannels;
				for(auto& held : m_mmKeyboardNoteChannels)
				{
					channels |= held;
					held = 0;
				}
				m_mmKeyboardSustainChannels = 0;
				return channels ? channels : current;
			}
			if(kind == 0xb0 && data1 == 64)
			{
				if(data2 >= 64)
				{
					m_mmKeyboardSustainChannels |= current;
					return current;
				}
				const auto channels = m_mmKeyboardSustainChannels;
				m_mmKeyboardSustainChannels = 0;
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
		std::array<uint16_t, 128> m_mmKeyboardNoteChannels{};
		uint16_t m_mmKeyboardSustainChannels = 0;
	};
}
