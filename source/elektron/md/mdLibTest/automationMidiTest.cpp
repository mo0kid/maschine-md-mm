#include "mdLib/mdautomation.h"
#include "mdLib/mdautomationsync.h"
#include "mdLib/mdsysexautomation.h"
#include "synthLib/midiBufferParser.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(_condition)
			return;
		std::cerr << "automationMidiTest: " << _message << '\n';
		std::exit(1);
	}

	void expectMessage(const md::MachineModel _model,
		const md::automation::ParameterChange& _change, const uint8_t _baseChannel,
		const md::automation::ControlChange& _expected)
	{
		const auto encoded = md::automation::encodeParameterChange(_model, _change,
			_baseChannel);
		require(encoded.has_value(), "valid parameter did not encode");
		require(*encoded == _expected, "parameter encoded to the wrong CC");
		const auto decoded = md::automation::decodeParameterChange(_model, *encoded,
			_baseChannel);
		require(decoded.has_value(), "encoded CC did not decode");
		require(*decoded == _change, "CC round trip changed its parameter address");
	}

	void expectRoundTrip(const md::MachineModel _model,
		const md::automation::ParameterChange& _change, const uint8_t _baseChannel)
	{
		const auto encoded = md::automation::encodeParameterChange(_model, _change,
			_baseChannel);
		require(encoded.has_value(), "valid parameter did not encode");
		const auto decoded = md::automation::decodeParameterChange(_model, *encoded,
			_baseChannel);
		require(decoded.has_value(), "encoded CC did not decode");
		require(*decoded == _change, "CC round trip changed its parameter address");
	}

	void testMachinedrum()
	{
		using namespace md::automation;
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 0, 0, 23}, 0, {0xb0, 16, 23});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Effects, 0, 0, 31}, 0, {0xb0, 24, 31});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Routing, 3, 7, 127}, 0, {0xb0, 119, 127});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 4, 0, 64}, 0, {0xb1, 16, 64});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Level, 15, 0, 99}, 0, {0xb3, 11, 99});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Mute, 9, 0, 1}, 4, {0xb6, 13, 1});

		for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
		{
			for(uint8_t page = machinedrum::Synthesis;
				page <= machinedrum::Routing; ++page)
			{
				for(uint8_t index = 0; index < 8; ++index)
					expectRoundTrip(md::MachineModel::Machinedrum,
						{page, track, index, static_cast<uint8_t>(track + index)}, 0);
			}

			for(const auto page : {machinedrum::Level, machinedrum::Mute})
			{
				const ParameterChange change{page, track, 0,
					static_cast<uint8_t>(page == machinedrum::Mute ? 1 : track)};
				expectRoundTrip(md::MachineModel::Machinedrum, change, 0);
			}
		}

		require(!encodeParameterChange(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 16, 0, 0}, 0), "accepted MD track 17");
		require(!encodeParameterChange(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 15, 0, 0}, 13), "accepted overflowing MD channel span");
	}

	void testMonomachine()
	{
		using namespace md::automation;
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Synthesis, 0, 0, 12}, 0, {0xb0, 48, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Amplification, 0, 0, 12}, 0, {0xb0, 56, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Filter, 0, 0, 12}, 0, {0xb0, 72, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Effects, 0, 3, 12}, 0, {0xb0, 83, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Effects, 0, 4, 12}, 0, {0xb0, 84, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Lfo1, 0, 0, 12}, 0, {0xb0, 88, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Lfo2, 0, 0, 12}, 0, {0xb0, 104, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Lfo3, 5, 7, 126}, 2, {0xb7, 119, 126});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Level, 2, 0, 77}, 0, {0xb2, 7, 77});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Mute, 4, 0, 1}, 0, {0xb4, 3, 1});

		for(uint8_t track = 0; track < monomachine::TrackCount; ++track)
		{
			for(uint8_t page = monomachine::Synthesis;
				page <= monomachine::Lfo3; ++page)
			{
				for(uint8_t index = 0; index < 8; ++index)
				{
					const ParameterChange change{page, track, index,
						static_cast<uint8_t>(page * 8 + index)};
					expectRoundTrip(md::MachineModel::Monomachine, change, 0);
				}
			}

			for(const auto page : {monomachine::Level, monomachine::Mute})
			{
				const ParameterChange change{page, track, 0,
					static_cast<uint8_t>(page == monomachine::Mute ? 1 : track)};
				expectRoundTrip(md::MachineModel::Monomachine, change, 0);
			}
		}

		require(!encodeParameterChange(md::MachineModel::Monomachine,
			{monomachine::Synthesis, 6, 0, 0}, 0), "accepted MM track 7");
		require(!encodeParameterChange(md::MachineModel::Monomachine,
			{monomachine::Synthesis, 5, 0, 0}, 11), "accepted overflowing MM channel span");
	}

	void finishDump(md::automation::sysex::Message& _message)
	{
		uint32_t checksum = 0;
		for(size_t i = 9; i < _message.size(); ++i)
			checksum += _message[i];
		checksum &= 0x3fff;
		const auto finalSize = _message.size() + 5;
		const auto length = static_cast<uint16_t>(finalSize - 10);
		_message.push_back(static_cast<uint8_t>(checksum >> 7));
		_message.push_back(static_cast<uint8_t>(checksum & 0x7f));
		_message.push_back(static_cast<uint8_t>(length >> 7));
		_message.push_back(static_cast<uint8_t>(length & 0x7f));
		_message.push_back(0xf7);
	}

	std::vector<uint8_t> rleEncode(const std::vector<uint8_t>& _decoded)
	{
		std::vector<uint8_t> result;
		for(size_t position = 0; position < _decoded.size();)
		{
			const auto value = _decoded[position];
			size_t count = 1;
			while(position + count < _decoded.size()
				&& _decoded[position + count] == value && count < 127)
				++count;
			if(value >= 0x80 || count > 1)
			{
				result.push_back(static_cast<uint8_t>(0x80 | count));
				result.push_back(value);
			}
			else
				result.push_back(value);
			position += count;
		}
		return result;
	}

	std::vector<uint8_t> pack7Bit(const std::vector<uint8_t>& _decoded)
	{
		std::vector<uint8_t> result;
		for(size_t position = 0; position < _decoded.size();)
		{
			const auto headerPosition = result.size();
			result.push_back(0);
			for(uint8_t bit = 0; bit < 7 && position < _decoded.size(); ++bit)
			{
				const auto value = _decoded[position++];
				if(value & 0x80)
					result[headerPosition] |= static_cast<uint8_t>(1u << (6u - bit));
				result.push_back(static_cast<uint8_t>(value & 0x7f));
			}
		}
		return result;
	}

	md::automation::sysex::Message makeMmDump(const uint8_t _command,
		const std::vector<uint8_t>& _decoded, const uint8_t _slot = 0x02)
	{
		md::automation::sysex::Message result{
			0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, _command, 0x01, 0x01, _slot};
		const auto rle = rleEncode(_decoded);
		const auto packed = pack7Bit(rle);
		result.insert(result.end(), packed.begin(), packed.end());
		finishDump(result);
		return result;
	}

	void testSysexRequestsAndStatus()
	{
		using namespace md::automation::sysex;
		require(statusRequest(md::MachineModel::Machinedrum,
			StatusParameter::Global)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x70, 0x01, 0xf7}),
			"wrong MD status request");
		require(globalRequest(md::MachineModel::Monomachine, 7)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x51, 0x07, 0xf7}),
			"wrong MM global request");
		require(kitRequest(md::MachineModel::Machinedrum, 63)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x53, 0x3f, 0xf7}),
			"wrong MD kit request");
		require(kitSave(md::MachineModel::Machinedrum, 63)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x59, 0x3f, 0xf7}),
			"wrong MD kit save");
		require(kitSave(md::MachineModel::Monomachine, 127)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x59, 0x7f, 0xf7}),
			"wrong MM kit save");

		const Message response{0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00,
			0x72, 0x02, 0x37, 0xf7};
		const auto parsed = parseStatusResponse(md::MachineModel::Monomachine,
			response);
		require(parsed && parsed->parameter == StatusParameter::Kit
			&& parsed->value == 0x37, "could not parse status response");
		require(!parseStatusResponse(md::MachineModel::Machinedrum, response),
			"accepted status response for the wrong product");
		const Message setStatus{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			0x71, 0x02, 0x01, 0xf7};
		const Message strictSetStatus{0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00,
			0x71, 0x02, 0x37, 0xf7};
		const auto parsedSetStatus = parseSetStatus(md::MachineModel::Monomachine,
			strictSetStatus);
		require(parsedSetStatus && parsedSetStatus->parameter == StatusParameter::Kit
			&& parsedSetStatus->value == 0x37, "could not parse SET STATUS");
		for(const auto position : {size_t{1}, size_t{2}, size_t{3}, size_t{4},
			size_t{5}, size_t{6}, size_t{9}})
		{
			auto malformed = strictSetStatus;
			malformed[position] ^= 1;
			require(!parseSetStatus(md::MachineModel::Monomachine, malformed),
				"accepted malformed SET STATUS framing");
		}
		auto malformed = strictSetStatus;
		malformed[8] = 0x80;
		require(!parseSetStatus(md::MachineModel::Monomachine, malformed),
			"accepted non-7-bit SET STATUS value");
		malformed = setStatus;
		malformed[7] = 0x7f;
		require(!parseSetStatus(md::MachineModel::Monomachine, malformed),
			"accepted unknown SET STATUS parameter");
		malformed = setStatus;
		malformed[4] = 0x02;
		malformed[8] = 64;
		require(!parseSetStatus(md::MachineModel::Machinedrum, malformed),
			"accepted out-of-range MD Kit slot");

		for(const auto& request : {
			statusRequest(md::MachineModel::Machinedrum, StatusParameter::Global),
			globalRequest(md::MachineModel::Machinedrum, 3),
			kitRequest(md::MachineModel::Machinedrum, 12),
			statusRequest(md::MachineModel::Monomachine, StatusParameter::Pattern),
			globalRequest(md::MachineModel::Monomachine, 7),
			kitRequest(md::MachineModel::Monomachine, 127)})
		{
			const auto model = request[4] == 0x03
				? md::MachineModel::Monomachine
				: md::MachineModel::Machinedrum;
			require(isReadOnlyRequest(model, request),
				"controller query was not classified as read-only");
		}
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, setStatus),
			"state-changing status message was classified as read-only");
		require(!isReadOnlyRequest(md::MachineModel::Monomachine,
			statusRequest(md::MachineModel::Machinedrum, StatusParameter::Kit)),
			"wrong-product query was classified as read-only");
		auto invalidRequest = globalRequest(md::MachineModel::Machinedrum, 0);
		invalidRequest[7] = 8;
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, invalidRequest),
			"out-of-range Global request was classified as read-only");
		invalidRequest = kitRequest(md::MachineModel::Machinedrum, 0);
		invalidRequest[7] = 64;
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, invalidRequest),
			"out-of-range MD Kit request was classified as read-only");
		invalidRequest = statusRequest(md::MachineModel::Machinedrum,
			StatusParameter::Global);
		invalidRequest.push_back(0xf7);
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, invalidRequest),
			"request with trailing data was classified as read-only");
	}

	void testMidiRunningStatus()
	{
		synthLib::MidiBufferParser parser(synthLib::MidiEventSource::Device);
		std::vector<synthLib::SMidiEvent> events;
		parser.write(std::vector<uint8_t>{
			0xb2, 16, 1, 17, 2, 18, 3,
			0xf8,
			19, 4,
			0xf1, 0x05,
			20, 6,
			0x92, 60, 100, 61, 101});
		parser.getEvents(events);
		require(events.size() == 8, "wrong MIDI running-status event count");
		require(events[0].a == 0xb2 && events[0].b == 16 && events[0].c == 1,
			"wrong first running-status CC");
		require(events[1].a == 0xb2 && events[1].b == 17 && events[1].c == 2,
			"running-status CC lost retained status");
		require(events[2].a == 0xb2 && events[2].b == 18 && events[2].c == 3,
			"wrong third running-status CC");
		require(events[3].a == 0xf8,
			"realtime byte was not emitted independently");
		require(events[4].a == 0xb2 && events[4].b == 19 && events[4].c == 4,
			"realtime byte incorrectly cancelled running status");
		require(events[5].a == 0xf1 && events[5].b == 0x05,
			"System Common message was parsed incorrectly");
		require(events[6].a == 0x92 && events[6].b == 60 && events[6].c == 100,
			"orphan data after System Common was not discarded");
		require(events[7].a == 0x92 && events[7].b == 61 && events[7].c == 101,
			"note-on running status was parsed incorrectly");

		parser.write(std::vector<uint8_t>{0xb0, 7});
		parser.discardPartialMessage();
		parser.write(std::vector<uint8_t>{100, 101});
		events.clear();
		parser.getEvents(events);
		require(events.empty(), "discontinuity retained unsafe running status");
	}

	void testMachinedrumDumps()
	{
		using namespace md::automation;
		using namespace md::automation::sysex;
		Message global{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			0x50, 0x06, 0x01, 0x05};
		global.resize(0xb0, 0);
		for(size_t note = 0; note < 128; ++note) {
			const auto group = 0x1a + (note / 7) * 8;
			global[group] = 0x7f;
			global[group + 1 + note % 7] = 0x7f;
		}
		// A remapped note, not the factory drum-note layout.
		global[0x1a + (60 / 7) * 8] &= ~(1u << (6 - 60 % 7));
		global[0x1a + (60 / 7) * 8 + 1 + 60 % 7] = 9;
		global[0xad] = 11;
		finishDump(global);
		const auto parsedGlobal = parseGlobalDump(md::MachineModel::Machinedrum, global);
		require(parsedGlobal && parsedGlobal->slot == 5
			&& parsedGlobal->baseChannel == 11,
			"wrong MD base channel");
		require(parsedGlobal->drumNoteMap[60] == 9 && parsedGlobal->drumNoteMap[61] == 0xff,
			"MD drum key map lost custom assignments or high-bit unmapped entries");
		global[0xad] = 0x7f;
		global.resize(global.size() - 5);
		finishDump(global);
		const auto parsedNone = parseGlobalDump(md::MachineModel::Machinedrum, global);
		require(parsedNone && parsedNone->slot == 5
			&& parsedNone->baseChannel == 0x7f,
			"MD MIDI NONE should be a valid persisted setting");
		global[9] = 8;
		global.resize(global.size() - 5);
		finishDump(global);
		require(!parseGlobalDump(md::MachineModel::Machinedrum, global),
			"accepted out-of-range MD Global slot");

		Message kit{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			0x52, 0x04, 0x01, 0x04};
		kit.resize(1228, 0);
		for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
		{
			for(uint8_t parameter = 0; parameter < 24; ++parameter)
				kit[0x1a + track * 24 + parameter]
					= static_cast<uint8_t>((track * 24 + parameter) & 0x7f);
			kit[0x19a + track] = static_cast<uint8_t>(100 + track);
		}
		finishDump(kit);
		const auto parsedKit = parseKitDump(md::MachineModel::Machinedrum, kit);
		require(parsedKit && parsedKit->slot == 4
			&& parsedKit->parameters.size() == 400, "wrong MD Kit parameter count");
		require(parsedKit->parameters[0]
			== ParameterChange{machinedrum::Synthesis, 0, 0, 0},
			"wrong first MD Kit parameter");
		require(parsedKit->parameters[399]
			== ParameterChange{machinedrum::Level, 15, 0, 115},
			"wrong last MD Kit parameter");
		kit[100] ^= 1;
		require(!parseKitDump(md::MachineModel::Machinedrum, kit),
			"accepted corrupt MD Kit checksum");
	}

	void testMonomachineDumps()
	{
		using namespace md::automation;
		using namespace md::automation::sysex;
		std::vector<uint8_t> globalData(260, 0);
		globalData[0] = 0x91;
		globalData[1] = 5;
		auto global = makeMmDump(0x50, globalData);
		const auto parsedGlobal = parseGlobalDump(md::MachineModel::Monomachine, global);
		require(parsedGlobal && parsedGlobal->slot == 2
			&& parsedGlobal->baseChannel == 5,
			"wrong MM base channel or broken 7-bit/RLE decode");

		// A short track-channel span puts special sequencer channels where naive
		// base+track routing would send ordinary keyboard notes.
		globalData[0] = 8; // Auto Track
		globalData[1] = 0;
		globalData[2] = 2;
		globalData[3] = 2; // Multi Trig / sequencer start
		globalData[4] = 3; // Multi Map / pattern triggers
		const auto shortSpan = parseGlobalDump(md::MachineModel::Monomachine, makeMmDump(0x50, globalData));
		require(shortSpan && shortSpan->noteChannel(0) == 0 && shortSpan->noteChannel(1) == 1
			&& shortSpan->noteChannel(2) == 8 && shortSpan->noteChannel(3) == 8
			&& shortSpan->noteChannel(4) == 8 && shortSpan->noteChannel(5) == 8,
			"keyboard notes were routed to sequencer channels instead of Auto Track");
		globalData[0] = 0x7f;
		const auto disabledAuto = parseGlobalDump(md::MachineModel::Monomachine, makeMmDump(0x50, globalData));
		require(disabledAuto && disabledAuto->noteChannel(4) == 0x7f,
			"disabled Auto Track fell back to a sequencer channel");

		std::vector<uint8_t> kitData(698, 0);
		for(uint8_t track = 0; track < monomachine::TrackCount; ++track)
		{
			kitData[0x0b + track] = static_cast<uint8_t>(90 + track);
			for(uint8_t parameter = 0; parameter < 56; ++parameter)
				kitData[0x11 + track * 72 + parameter]
					= static_cast<uint8_t>((track * 56 + parameter) & 0x7f);
		}
		kitData.back() = 0xe5;
		auto kit = makeMmDump(0x52, kitData);
		const auto parsedKit = parseKitDump(md::MachineModel::Monomachine, kit);
		require(parsedKit && parsedKit->slot == 2
			&& parsedKit->parameters.size() == 342, "wrong MM Kit parameter count");
		require(parsedKit->parameters[0]
			== ParameterChange{monomachine::Synthesis, 0, 0, 0},
			"wrong first MM Kit parameter");
		require(parsedKit->parameters[341]
			== ParameterChange{monomachine::Level, 5, 0, 95},
			"wrong last MM Kit parameter");
		kit[12] ^= 1;
		require(!parseKitDump(md::MachineModel::Monomachine, kit),
			"accepted corrupt MM Kit checksum");
	}

	void testBackupFile(const char* const _path, const md::MachineModel _model)
	{
		std::ifstream file(_path, std::ios::binary);
		require(file.good(), "could not open optional SysEx backup");
		const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
		size_t globalCount = 0;
		size_t kitCount = 0;
		for(size_t begin = 0; begin < bytes.size();)
		{
			begin = static_cast<size_t>(std::find(bytes.begin() + begin, bytes.end(),
				0xf0) - bytes.begin());
			if(begin == bytes.size())
				break;
			const auto endIterator = std::find(bytes.begin() + begin, bytes.end(), 0xf7);
			if(endIterator == bytes.end())
				break;
			const auto end = static_cast<size_t>(endIterator - bytes.begin());
			const md::automation::sysex::Message message(bytes.begin() + begin,
				bytes.begin() + end + 1);
			if(message.size() > 6 && message[6] == 0x50)
			{
				const auto parsed = md::automation::sysex::parseGlobalDump(
					_model, message);
				require(parsed.has_value(), "could not parse real Global dump");
				require(parsed->slot == message[9],
					"real Global dump reported its format version as its slot");
				++globalCount;
			}
			else if(message.size() > 6 && message[6] == 0x52)
			{
				const auto parsed = md::automation::sysex::parseKitDump(_model, message);
				require(parsed.has_value(), "could not parse real Kit dump");
				require(parsed->slot == message[9],
					"real Kit dump reported its format version as its slot");
				++kitCount;
			}
			begin = end + 1;
		}
		require(globalCount > 0, "real backup contained no Global dumps");
		require(kitCount > 0, "real backup contained no Kit dumps");
	}

	void testDumpRequestOrdering()
	{
		using Tracker = md::automation::DumpRequestTracker;
		Tracker kit(false);
		require(kit.statusRequestDue(0, 500),
			"new dump tracker did not request status");
		kit.statusRequestSent(0);
		auto status = kit.observeStatus(7);
		require(status.accepted && status.requestDump && status.selectionChanged,
			"initial status did not select a correlated dump");
		kit.dumpRequestSent(10);
		require(!kit.acceptDump(6) && kit.phase() == Tracker::Phase::AwaitingDump,
			"wrong-slot dump satisfied a correlated request");
		require(!kit.recoverTimedOutDump(2009, 2000),
			"dump timed out before its exact deadline");
		require(kit.recoverTimedOutDump(2010, 2000),
			"dump timeout did not enter status-barrier recovery");
		require(!kit.acceptDump(7),
			"late same-slot dump crossed the retry status barrier");

		kit.statusRequestSent(2010);
		status = kit.observeStatus(7);
		require(status.accepted && status.requestDump && !status.selectionChanged,
			"barrier status did not authorize the replacement dump");
		kit.dumpRequestSent(2020);
		require(kit.acceptDump(7) && kit.ready(),
			"replacement same-slot dump did not complete recovery");
		require(!kit.acceptDump(7),
			"late duplicate dump mutated a completed generation");

		kit.statusRequestSent(3000);
		require(!kit.recoverTimedOutStatus(4999, 2000),
			"periodic status request timed out before its exact deadline");
		require(kit.recoverTimedOutStatus(5000, 2000) && kit.canPollStatus(),
			"lost periodic status response permanently disabled polling");
		kit.statusRequestSent(5000);
		status = kit.observeStatus(7);
		require(status.accepted && !status.requestDump,
			"unchanged Kit poll unnecessarily requested a large dump");
		kit.statusRequestSent(4000);
		status = kit.observeStatus(8);
		require(status.requestDump && status.selectionChanged,
			"changed Kit poll did not invalidate the old selection");

		Tracker global(true);
		global.statusRequestSent(0);
		status = global.observeStatus(1);
		require(status.requestDump, "initial Global status did not request a dump");
		global.dumpRequestSent(1);
		require(global.acceptDump(1), "initial Global dump was rejected");
		global.statusRequestSent(5000);
		status = global.observeStatus(1);
		require(status.requestDump && !status.selectionChanged,
			"same-slot Global poll did not refresh its MIDI channel data");
	}
}

int main(const int _argc, const char* const* _argv)
{
	testMachinedrum();
	testMonomachine();
	testSysexRequestsAndStatus();
	testMidiRunningStatus();
	testMachinedrumDumps();
	testMonomachineDumps();
	testDumpRequestOrdering();
	if(_argc > 1)
		testBackupFile(_argv[1], md::MachineModel::Machinedrum);
	if(_argc > 2)
		testBackupFile(_argv[2], md::MachineModel::Monomachine);
	std::cout << "automationMidiTest: PASS\n";
	return 0;
}
