#include "synthLib/resamplerInOut.h"
#include "synthLib/sampleRateTime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace synthLib;

	void clockConversion()
	{
		for(uint32_t from : {44100u, 48000u, 96000u})
			for(uint32_t to : {44100u, 48000u, 96000u})
				for(uint64_t sample = 0; sample < uint64_t(from) * 86400 * 2; sample += 123457)
					if(rescaleSamplesCeil(sample, float(from), float(to)) != (sample * to + from - 1) / from)
						throw std::runtime_error("48-hour sample conversion rounded incorrectly");
	}

	void verify(const float rate, const bool variable)
	{
		ResamplerInOut resampler(0, 1);
		resampler.setSamplerates(rate, 44100);
		resampler.prepare(512);
		resampler.reserveMidiEventCapacity(1024);
		std::array<float, 512> buffer{};
		TAudioInputs inputs{};
		TAudioOutputs outputs{};
		outputs[0] = buffer.data();
		std::vector<uint64_t> delivered, expected, returned, expectedReturn;
		uint64_t native = 0;
		constexpr std::array<uint32_t, 10> blocks{0, 1, 7, 31, 63, 64, 127, 257, 511, 512};
		uint32_t host = 0, block = 0;
		for(; host < uint32_t(rate) * 2; ++block)
		{
			const auto size = variable ? blocks[block % blocks.size()] : 512u;
			ResamplerInOut::TMidiVec midiIn, midiOut;
			for(uint32_t i = 0; i < size; ++i)
			{
				if((host + i) % 17 == 0 && host + i < uint32_t(rate))
				{
					midiIn.emplace_back(MidiEventSource::Host, 0x90, 60, 100, i);
					expected.push_back((uint64_t(host + i) * 44100 + uint32_t(rate) - 1) / uint32_t(rate));
					expectedReturn.push_back((expected.back() * uint32_t(rate) + 44099) / 44100
						+ resampler.getOutputLatency());
				}
			}
			resampler.process(inputs, outputs, midiIn, midiOut, size,
				[&](const TAudioInputs&, const TAudioOutputs& out, size_t count,
					const ResamplerInOut::TMidiVec& events, ResamplerInOut::TMidiVec& result)
				{
					std::fill_n(out[0], count, 0.0f);
					for(const auto& event : events)
					{
						if(event.offset >= count)
							throw std::runtime_error("event outside native callback");
						delivered.push_back(native + event.offset);
						result.push_back(event);
					}
					native += count;
				});
			for(const auto& event : midiOut)
			{
				if(event.offset >= size)
					throw std::runtime_error("event outside host callback");
				returned.push_back(host + event.offset);
			}
			// Even an event arriving at sample zero of the next host callback
			// must still have its native sample available for rendering.
			if(native > (uint64_t(host + size) * 44100 + uint32_t(rate) - 1) / uint32_t(rate))
				throw std::runtime_error("native rendering ran ahead of available host MIDI");
			host += size;
		}
		std::cout << "rate=" << rate << " variable=" << variable
			<< " sent=" << expected.size() << " received=" << delivered.size();
		if(expected != delivered)
		{
			auto mismatch = std::mismatch(expected.begin(), expected.end(), delivered.begin(), delivered.end());
			std::cout << " first_mismatch=" << std::distance(expected.begin(), mismatch.first);
			if(mismatch.first != expected.end()) std::cout << " expected=" << *mismatch.first;
			if(mismatch.second != delivered.end()) std::cout << " actual=" << *mismatch.second;
			std::cout << '\n';
			throw std::runtime_error("sample-rate conversion changed or lost MIDI deadlines");
		}
		std::cout << " passed\n";
		if(returned != expectedReturn)
			throw std::runtime_error("MIDI output lost its native chunk origin or latency");
	}

	void impulses(float rate, bool variable)
	{
		ResamplerInOut resampler(1, 2);
		resampler.setSamplerates(rate, 44100);
		resampler.prepare(512);
		std::array<float, 512> input{}, note{}, thru{};
		const auto initialPadding = resampler.getInputPaddingSamples();
		TAudioInputs inputs{};
		TAudioOutputs outputs{};
		inputs[0] = input.data();
		outputs[0] = note.data();
		outputs[1] = thru.data();
		std::vector<float> notes, audio;
		constexpr std::array<uint32_t, 4> positions{1003, 8009, 16007, 32003};
		constexpr std::array<uint32_t, 10> blocks{0, 1, 7, 31, 63, 64, 127, 257, 511, 512};
		for(uint32_t host = 0, block = 0; host < 36000; ++block)
		{
			const auto size = variable ? blocks[block % blocks.size()] : 512u;
			std::fill(input.begin(), input.end(), 0.0f);
			ResamplerInOut::TMidiVec midi, result;
			for(auto position : positions)
				if(position >= host && position < host + size)
				{
					input[position - host] = 1;
					midi.emplace_back(MidiEventSource::Host, 0x90, 60, 100, position - host);
				}
			resampler.process(inputs, outputs, midi, result, size,
				[](const TAudioInputs& in, const TAudioOutputs& out, size_t count,
					const ResamplerInOut::TMidiVec& events, ResamplerInOut::TMidiVec&)
				{
					std::fill_n(out[0], count, 0.0f);
					std::copy_n(in[0], count, out[1]);
					for(const auto& event : events)
						out[0][event.offset] = 1;
				});
			if(resampler.getInputPaddingSamples() != initialPadding)
				throw std::runtime_error("variable blocks inserted silence into live input");
			notes.insert(notes.end(), note.begin(), note.begin() + size);
			audio.insert(audio.end(), thru.begin(), thru.begin() + size);
			host += size;
		}
		const auto midiLatency = resampler.getOutputLatency();
		const auto audioLatency = midiLatency + resampler.getInputLatency();
		std::cout << "impulse rate=" << rate << " variable=" << variable
			<< " reported_midi=" << midiLatency << " reported_audio=" << audioLatency;
		bool valid = true;
		for(auto position : positions)
		{
			auto peak = [&](const std::vector<float>& signal)
			{
				const auto found = std::max_element(signal.begin() + position - 1000,
					signal.begin() + position + 2000, [](float a, float b) { return std::abs(a) < std::abs(b); });
				if(std::abs(*found) < 0.05f) throw std::runtime_error("impulse was lost");
				return std::distance(signal.begin(), found) - position;
			};
			const auto noteDelay = peak(notes), audioDelay = peak(audio);
			std::cout << " peak=" << noteDelay << '/' << audioDelay;
			valid = valid && std::abs(double(noteDelay) - midiLatency) <= std::ceil(rate / 44100) + 1
				&& std::abs(double(audioDelay) - audioLatency) <= 2;
		}
		std::cout << '\n';
		if(!valid) throw std::runtime_error("reported delay disagrees with impulse peaks");
	}
}

int main()
{
	bool failed = false;
	try { clockConversion(); }
	catch(const std::exception& error) { std::cerr << error.what() << '\n'; failed = true; }
	for(float rate : {8000.0f, 11025.0f, 16000.0f, 22050.0f, 32000.0f,
		44100.0f, 48000.0f, 88200.0f, 96000.0f, 176400.0f, 192000.0f})
		for(bool variable : {false, true})
			try { verify(rate, variable); impulses(rate, variable); }
			catch(const std::exception& error) { std::cerr << error.what() << '\n'; failed = true; }
	return failed ? 1 : 0;
}
