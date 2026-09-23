#pragma once

#include "mdMaschineNihiaProtocol.h"

#include <functional>
#include <memory>
#include <string>

namespace mdJucePlugin::maschine
{
	class NihiaClient
	{
	public:
		using ButtonCallback = std::function<void(const nihia::ButtonEvent&)>;
		using KnobCallback = std::function<void(const nihia::KnobEvent&)>;
		using MainKnobCallback = std::function<void(const nihia::MainKnobEvent&)>;
		using PadCallback = std::function<void(const nihia::PadEvent&)>;

		NihiaClient();
		~NihiaClient();

		NihiaClient(const NihiaClient&) = delete;
		NihiaClient& operator=(const NihiaClient&) = delete;

		bool connect();
		void disconnect();
		bool isConnected() const;
		bool sendFrame(uint8_t _display, const ScreenRenderer::Frame& _frame);
		bool sendLeds(const nihia::LedFrame& _frame);
		void setButtonCallback(ButtonCallback _callback);
		void setKnobCallback(KnobCallback _callback);
		void setMainKnobCallback(MainKnobCallback _callback);
		void setPadCallback(PadCallback _callback);
		std::string getSerial() const;
		std::string getLastError() const;

	private:
		class Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
