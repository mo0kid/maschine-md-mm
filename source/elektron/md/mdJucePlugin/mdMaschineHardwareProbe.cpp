#include "mdMaschineNihiaClient.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace
{
	md::FrontPanel makeDiagnosticPanel()
	{
		md::FrontPanel panel;
		for(uint8_t half = 0; half < 2; ++half)
			for(uint8_t page = 0; page < 8; ++page)
				for(uint8_t column = 0; column < 64; column += 8)
				{
					panel.processByte(static_cast<uint8_t>(0x10 | (half << 3) | page));
					panel.processByte(column);
					for(uint8_t i = 0; i < 8; ++i)
						panel.processByte(static_cast<uint8_t>(
							((column + i + page * 3 + half * 7) % 17) < 4
								? 0x7e : 0x00));
				}
		return panel;
	}
}

int main()
{
	using namespace mdJucePlugin::maschine;
	NihiaClient client;
	client.setButtonCallback([](const nihia::ButtonEvent& _event)
	{
		std::cout << "button " << _event.id << ' '
			<< (_event.pressed ? "pressed" : "released") << '\n';
	});
	if(!client.connect())
	{
		std::cerr << "Maschine connection failed: " << client.getLastError()
			<< '\n';
		return 1;
	}
	std::cout << "Connected to Maschine MK3 " << client.getSerial() << '\n';
	const auto panel = makeDiagnosticPanel();
	const auto left = ScreenRenderer::render(panel,
		md::MachineModel::Machinedrum, md::MachineModel::Machinedrum, true);
	const auto right = ScreenRenderer::render(panel,
		md::MachineModel::Monomachine, md::MachineModel::Machinedrum, true);
	if(!client.sendFrame(0, left) || !client.sendFrame(1, right))
	{
		std::cerr << "Maschine display write failed\n";
		return 1;
	}
	std::cout << "Gearmulator frames sent; listening for buttons for 10 seconds\n";
	std::this_thread::sleep_for(std::chrono::seconds(10));
	return 0;
}
