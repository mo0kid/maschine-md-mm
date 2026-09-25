#include "mdsysexautomation.h"

#include <algorithm>
#include <utility>

namespace md::automation::sysex
{
	namespace
	{
		constexpr uint8_t g_globalDump = 0x50;
		constexpr uint8_t g_globalRequest = 0x51;
		constexpr uint8_t g_kitDump = 0x52;
		constexpr uint8_t g_kitRequest = 0x53;
		constexpr uint8_t g_kitSave = 0x59;
		constexpr uint8_t g_statusRequest = 0x70;
		constexpr uint8_t g_setStatus = 0x71;
		constexpr uint8_t g_statusResponse = 0x72;

		uint8_t product(const MachineModel _model)
		{
			return _model == MachineModel::Monomachine ? 0x03 : 0x02;
		}

		Message request(const MachineModel _model, const uint8_t _command,
			const uint8_t _value)
		{
			return {0xf0, 0x00, 0x20, 0x3c, product(_model), 0x00,
				_command, static_cast<uint8_t>(_value & 0x7f), 0xf7};
		}

		bool hasHeader(const MachineModel _model, const MessageView _message,
			const uint8_t _command)
		{
			return _message.size() >= 8 && _message[0] == 0xf0
				&& _message[1] == 0x00 && _message[2] == 0x20
				&& _message[3] == 0x3c && _message[4] == product(_model)
				&& _message[5] == 0x00 && _message[6] == _command
				&& _message.back() == 0xf7;
		}

		bool validDump(const MachineModel _model, const MessageView _message,
			const uint8_t _command)
		{
			if(_message.size() < 13 || !hasHeader(_model, _message, _command))
				return false;
			if(std::any_of(_message.begin() + 1, _message.end() - 1,
				[](const uint8_t _value) { return _value > 0x7f; }))
				return false;

			const auto checksumPosition = _message.size() - 5;
			uint32_t sum = 0;
			for(size_t i = 9; i < checksumPosition; ++i)
				sum += _message[i];
			const auto checksum = static_cast<uint16_t>(
				(_message[checksumPosition] << 7) | _message[checksumPosition + 1]);
			if((sum & 0x3fff) != checksum)
				return false;

			const auto length = static_cast<uint16_t>(
				(_message[checksumPosition + 2] << 7) | _message[checksumPosition + 3]);
			return length == _message.size() - 10;
		}

		bool validStatusValue(const MachineModel _model,
			const StatusParameter _parameter, const uint8_t _value)
		{
			switch(_parameter)
			{
			case StatusParameter::Global:
				return _value < 8;
			case StatusParameter::Kit:
				return _value < (_model == MachineModel::Monomachine ? 128 : 64);
			case StatusParameter::Pattern:
				return _value < 128;
			}
			return false;
		}

		std::optional<StatusResponse> parseStatus(const MachineModel _model,
			const MessageView _message, const uint8_t _command)
		{
			if(_message.size() != 10 || !hasHeader(_model, _message, _command)
				|| std::any_of(_message.begin() + 1, _message.end() - 1,
					[](const uint8_t _value) { return _value > 0x7f; }))
				return std::nullopt;
			const auto parameter = _message[7];
			if(parameter != static_cast<uint8_t>(StatusParameter::Global)
				&& parameter != static_cast<uint8_t>(StatusParameter::Kit)
				&& parameter != static_cast<uint8_t>(StatusParameter::Pattern))
				return std::nullopt;
			const auto typedParameter = static_cast<StatusParameter>(parameter);
			return validStatusValue(_model, typedParameter, _message[8])
				? std::optional<StatusResponse>(StatusResponse{typedParameter, _message[8]})
				: std::nullopt;
		}

		std::optional<std::vector<uint8_t>> decodeMonomachinePayload(
			const MessageView _message)
		{
			const auto end = _message.size() - 5;
			std::vector<uint8_t> rle;
			rle.reserve(end - 10);
			for(size_t position = 10; position < end;)
			{
				const auto highBits = _message[position++];
				for(uint8_t bit = 0; bit < 7 && position < end; ++bit)
				{
					auto value = _message[position++];
					if(highBits & (1u << (6u - bit)))
						value |= 0x80;
					rle.push_back(value);
				}
			}

			std::vector<uint8_t> decoded;
			for(size_t position = 0; position < rle.size(); ++position)
			{
				const auto value = rle[position];
				if((value & 0x80) == 0)
				{
					decoded.push_back(value);
					continue;
				}

				const auto count = static_cast<size_t>(value & 0x7f);
				if(count == 0 || ++position >= rle.size())
					return std::nullopt;
				if(decoded.size() + count > 4096)
					return std::nullopt;
				decoded.insert(decoded.end(), count, rle[position]);
			}
			return decoded;
		}
	}

	Message statusRequest(const MachineModel _model,
		const StatusParameter _parameter)
	{
		return request(_model, g_statusRequest, static_cast<uint8_t>(_parameter));
	}

	Message globalRequest(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_globalRequest, _slot);
	}

	Message kitRequest(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_kitRequest, _slot);
	}

	bool isReadOnlyRequest(const MachineModel _model, const MessageView _message)
	{
		if(_message.size() != 9
			|| std::any_of(_message.begin() + 1, _message.end() - 1,
				[](const uint8_t _value) { return _value > 0x7f; })
			|| !hasHeader(_model, _message, _message[6]))
			return false;
		switch(_message[6])
		{
		case g_globalRequest:
			return _message[7] < 8;
		case g_kitRequest:
			return _message[7]
				< (_model == MachineModel::Monomachine ? 128 : 64);
		case g_statusRequest:
			return _message[7] == static_cast<uint8_t>(StatusParameter::Global)
					|| _message[7] == static_cast<uint8_t>(StatusParameter::Kit)
					|| _message[7] == static_cast<uint8_t>(StatusParameter::Pattern);
		default:
			return false;
		}
	}

	Message kitSave(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_kitSave, _slot);
	}

	std::optional<StatusResponse> parseStatusResponse(const MachineModel _model,
		const MessageView _message)
	{
		return parseStatus(_model, _message, g_statusResponse);
	}

	std::optional<StatusResponse> parseSetStatus(const MachineModel _model,
		const MessageView _message)
	{
		return parseStatus(_model, _message, g_setStatus);
	}

	uint8_t GlobalDump::noteChannel(const uint8_t _track) const
	{
		const auto safe = [&](unsigned channel) {
			return channel < 16 && channel != multiTrigChannel && channel != multiMapChannel;
		};
		const unsigned direct = unsigned(baseChannel) + _track;
		if(_track < 6 && _track < channelSpan && safe(direct) && direct != autoTrackChannel)
			return static_cast<uint8_t>(direct);
		return _track < 6 && safe(autoTrackChannel) ? autoTrackChannel : 0x7f;
	}

	std::optional<GlobalDump> parseGlobalDump(const MachineModel _model,
		const MessageView _message)
	{
		if(!validDump(_model, _message, g_globalDump))
			return std::nullopt;
		// Dump headers are command, format version, revision, original position.
		// The request correlation identity is the original position, not the format
		// version (which happens to be a small in-range value on both machines).
		const auto slot = _message[9];
		if(slot >= 8)
			return std::nullopt;

		uint8_t channel = 0;
		GlobalDump result{};
		if(_model == MachineModel::Machinedrum)
		{
			constexpr size_t baseChannelPosition = 0xad;
			if(_message.size() <= baseChannelPosition)
				return std::nullopt;
			channel = _message[baseChannelPosition];
		}
		else
		{
			const auto decoded = decodeMonomachinePayload(_message);
			if(!decoded || decoded->size() < 5)
				return std::nullopt;
			channel = (*decoded)[1];
			result.autoTrackChannel = (*decoded)[0];
			result.channelSpan = (*decoded)[2];
			result.multiTrigChannel = (*decoded)[3];
			result.multiMapChannel = (*decoded)[4];
		}

		if(channel >= 16 && channel != 0x7f) return std::nullopt;
		result.slot = slot;
		result.baseChannel = channel;
		result.drumNoteMap.fill(0xff);
		if(_model == MachineModel::Machinedrum)
		{
			// MD Global: 16 output routings followed by the 128-entry key map,
			// packed as one MSB byte and seven low-byte payloads per group.
			for(size_t note = 0; note < result.drumNoteMap.size(); ++note)
			{
				const auto group = 0x1a + (note / 7) * 8;
				const auto bit = note % 7;
				result.drumNoteMap[note] = _message[group + 1 + bit]
					| ((_message[group] & (1u << (6 - bit))) ? 0x80 : 0);
			}
		}
		return result;
	}

	std::optional<KitDump> parseKitDump(
		const MachineModel _model, const MessageView _message)
	{
		if(!validDump(_model, _message, g_kitDump))
			return std::nullopt;
		const auto slot = _message[9];
		if(slot >= (_model == MachineModel::Monomachine ? 128 : 64))
			return std::nullopt;

		std::vector<ParameterChange> result;
		if(_model == MachineModel::Machinedrum)
		{
			constexpr size_t parameterPosition = 0x1a;
			constexpr size_t levelPosition = 0x19a;
			if(_message.size() <= levelPosition + machinedrum::TrackCount)
				return std::nullopt;
			result.reserve(machinedrum::TrackCount * 25);
			for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
			{
				for(uint8_t page = machinedrum::Synthesis;
					page <= machinedrum::Routing; ++page)
				{
					for(uint8_t index = 0; index < 8; ++index)
					{
						const auto offset = parameterPosition + track * 24 + page * 8 + index;
						result.push_back({page, track, index, _message[offset]});
					}
				}
				result.push_back({machinedrum::Level, track, 0,
					_message[levelPosition + track]});
			}
			return KitDump{slot, std::move(result)};
		}

		const auto decoded = decodeMonomachinePayload(_message);
		constexpr size_t levelPosition = 0x0b;
		constexpr size_t parameterPosition = 0x11;
		constexpr size_t parameterStride = 72;
		if(!decoded || decoded->size() < parameterPosition
			+ monomachine::TrackCount * parameterStride)
			return std::nullopt;
		result.reserve(monomachine::TrackCount * 57);
		for(uint8_t track = 0; track < monomachine::TrackCount; ++track)
		{
			for(uint8_t page = monomachine::Synthesis;
				page <= monomachine::Lfo3; ++page)
			{
				for(uint8_t index = 0; index < 8; ++index)
				{
					const auto offset = parameterPosition + track * parameterStride
						+ page * 8 + index;
					result.push_back({page, track, index, (*decoded)[offset]});
				}
			}
			result.push_back({monomachine::Level, track, 0,
				(*decoded)[levelPosition + track]});
		}
		return KitDump{slot, std::move(result)};
	}
}
