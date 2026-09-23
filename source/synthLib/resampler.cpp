#include "resampler.h"
#include "sampleRateTime.h"

#include <cassert>
#include <cmath>

#include "libresample/include/libresample.h"

#include "dsp56kBase/fastmath.h"

synthLib::Resampler::Resampler(const float _samplerateIn, const float _samplerateOut)
	: m_samplerateIn(_samplerateIn)
	, m_samplerateOut(_samplerateOut)
	, m_factorInToOut(static_cast<double>(_samplerateIn) / _samplerateOut)
	, m_factorOutToIn(static_cast<double>(_samplerateOut) / _samplerateIn)
	, m_outputPtrs({})
{
}

synthLib::Resampler::~Resampler()
{
	destroyResamplers();
}

double synthLib::Resampler::getGroupDelay() const
{
	return 0.0;
}

void synthLib::Resampler::prepare(const uint32_t _numChannels,
	const uint32_t _maxOutputSamples)
{
	setChannelCount(_numChannels);
	const auto maxInputSamples = static_cast<size_t>(std::ceil(
		static_cast<double>(_maxOutputSamples) * m_factorInToOut)) + 1024;
	for(auto& buffer : m_tempOutput)
		buffer.reserve(maxInputSamples);
}

uint32_t synthLib::Resampler::process(TAudioOutputs& _output, const uint32_t _numChannels, const uint32_t _numSamples, bool _allowLessOutput, const TProcessFunc& _processFunc)
{
	assert(_numChannels <= m_outputPtrs.size());

	setChannelCount(_numChannels);

	if (getSamplerateIn() == getSamplerateOut())
	{
		_processFunc(_output, _numSamples);
		return _numSamples;
	}

	uint32_t index = 0;
	uint32_t remaining = _numSamples;

	while (remaining > 0)
	{
		for (uint32_t i = 0; i < _numChannels; ++i)
			m_outputPtrs[i] = &_output[i][index];

		const uint32_t outBufferUsed = processResample(
			m_outputPtrs, _numChannels, remaining, _processFunc);

		index += outBufferUsed;
		remaining -= outBufferUsed;

		if(_allowLessOutput)
			break;
//		if (remaining > 0)
//			LOG("outBufferUsed " << outBufferUsed << " outLen " << _numSamples);
	}

	return index;
}

uint32_t synthLib::Resampler::processResample(const TAudioOutputs& _output, const uint32_t _numChannels, const uint32_t _numSamples, const TProcessFunc& _processFunc)
{
	const auto availableInputLen = static_cast<uint32_t>(m_tempOutput[0].size());
	// Ask for the source range needed by this absolute output boundary. Adding
	// the requested length on every retry counted buffered output twice and
	// made native rendering run ahead when the host varied its block size.
	const auto target = rescaleSamplesCeil(m_legacyOutputSamples + _numSamples, m_samplerateOut, m_samplerateIn)
		+ static_cast<uint64_t>(resample_get_filter_width(m_resamplerOut[0]));
	const auto requested = target > m_legacyInputSamples
		? static_cast<uint32_t>(target - m_legacyInputSamples) : 0u;
	const auto inputLen = availableInputLen + requested;

	if (requested)
	{
		TAudioOutputs tempBuffers;
		tempBuffers.fill(nullptr);

		for (uint32_t i = 0; i < _numChannels; ++i)
		{
			m_tempOutput[i].resize(inputLen, 0.0f);
			tempBuffers[i] = &m_tempOutput[i][availableInputLen];
		}

		_processFunc(tempBuffers, requested);
		m_legacyInputSamples += requested;
	}

	uint32_t outBufferUsed = 0;
	int inBufferUsed = 0;

	for (uint32_t i = 0; i < _numChannels; ++i)
	{
		float* output = _output[i];

		outBufferUsed = resample_process(m_resamplerOut[i], m_factorOutToIn, m_tempOutput[i].data(), static_cast<int>(inputLen), 0, &inBufferUsed, output, static_cast<int>(_numSamples));

		if (static_cast<uint32_t>(inBufferUsed) < inputLen)
		{
//			LOG("inBufferUsed " << inBufferUsed << " inputLen " << inputLen);
			// libresample consumes a prefix. Preserve the unconsumed tail for the
			// next callback; retaining the prefix repeats stale samples at every
			// partial-consumption boundary.
			m_tempOutput[i].erase(m_tempOutput[i].begin(),
				m_tempOutput[i].begin() + inBufferUsed);
		}
		else
		{
			m_tempOutput[i].clear();
		}
	}

	m_legacyOutputSamples += outBufferUsed;
	return outBufferUsed;
}

void synthLib::Resampler::destroyResamplers()
{
	for (const auto& resampler : m_resamplerOut)
	{
		if (resampler)
			resample_close(resampler);
	}
	m_resamplerOut.clear();
	m_legacyInputSamples = 0;
	m_legacyOutputSamples = 0;
}

void synthLib::Resampler::setChannelCount(uint32_t _numChannels)
{
	if (m_tempOutput.size() == _numChannels)
		return;

	destroyResamplers();

	m_resamplerOut.resize(_numChannels);
	m_tempOutput.resize(_numChannels);

	for (auto& buf : m_tempOutput)
		buf.clear();

	const auto factor = static_cast<double>(m_factorOutToIn);
	for (auto& resampler : m_resamplerOut)
		resampler = resample_open(1, factor, factor);
}
