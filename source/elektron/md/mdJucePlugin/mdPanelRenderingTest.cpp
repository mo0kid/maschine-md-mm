#include "mdPixelPerfectPanel.h"
#include "mdLcdViewport.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlLookAndFeel.h"
#include "juceRmlUi/rmlDataProvider.h"
#include "juceRmlUi/rmlElemCanvas.h"
#ifdef RMLUI_METAL_RENDERER
#include "juceRmlUi/MetalContext.h"
#endif
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>

namespace juceRmlUi
{
	struct RenderingTestAccess
	{
		static void update(RmlComponent& _component) { _component.update(); }
		static uint32_t pendingUpdates(const RmlComponent& _component)
		{
			return _component.m_pendingUpdates;
		}
		static float targetFPS(const RmlComponent& _component)
		{
			return _component.m_targetFPS;
		}
		static bool hasCustomFPS(const RmlComponent& _component)
		{
			return _component.m_hasCustomFPS;
		}
		static void useDefaultFrameRateFor(RmlComponent& _component,
			const RmlComponent::Renderer _renderer)
		{
			_component.useDefaultFrameRateFor(_renderer);
		}
#ifdef RMLUI_METAL_RENDERER
		static bool hasMetalContext(const RmlComponent& _component)
		{
			return _component.m_metalContext != nullptr;
		}
		static void fallBackFromMetalToSoftware(RmlComponent& _component)
		{
			_component.fallBackFromMetalToSoftware();
		}
#endif
		static void requirePaddedTextures(RmlComponent& _component)
		{
			_component.m_renderProxy->setTextureParameters(4096, false);
		}
	};
}

namespace
{
	void require(bool _condition, const char* _message)
	{
		if (!_condition)
			throw std::runtime_error(_message);
	}

	bool sameColour(const juce::Colour _a, const juce::Colour _b)
	{
		// The software texture compositor can round a premultiplied channel by one.
		return std::abs(int(_a.getRed()) - int(_b.getRed())) <= 1
			&& std::abs(int(_a.getGreen()) - int(_b.getGreen())) <= 1
			&& std::abs(int(_a.getBlue()) - int(_b.getBlue())) <= 1
			&& std::abs(int(_a.getAlpha()) - int(_b.getAlpha())) <= 1;
	}

	class Resources : public juceRmlUi::DataProvider
	{
		const std::string rml = R"(<rml><head ><style>
			body { width: 512dp; height: 256dp; background-color: #8899aa; }
			div { position: absolute; }
			.panel-led { background-color: #000000; }
			.panel-led.lit { background-color: #00ff00; }
			#lcd { background-color: #225533; border: 3dp #345678; }
			#lcd.elektronCrispLcd { background-color: transparent; border-color: transparent; }
			</style></head><body>
			<div id="lcd" style="left: 30.25dp; top: 40.25dp; width: 220.5dp; height: 116.5dp;"/>
			<div id="rule" class="elektronPixelRule" style="left: 300.25dp; top: 50.25dp; width: 51.25dp; height: 1dp; background-color: #345678;"/>
			<div id="unmarked" style="left: 300.25dp; top: 60.25dp; width: 51.25dp; height: 1dp; background-color: #345678;"/>
			<div id="teardown" class="elektronPixelRule" style="left: 300.25dp; top: 70.25dp; width: 51.25dp; height: 1dp; background-color: #345678;"/>
			<div id="led" class="panel-led" style="left: 400dp; top: 50dp; width: 20dp; height: 20dp;"/>
			<div id="sentinel" style="left: 400dp; top: 200dp; width: 20dp; height: 20dp; background-color: #ff0000;"/>
			</body></rml>)";
	public:
		const char* getResourceByFilename(const std::string& _name, uint32_t& _size) override
		{
			_size = static_cast<uint32_t>(rml.size());
			return _name == "test.rml" ? rml.data() : nullptr;
		}
		std::vector<std::string> getAllFilenames() override { return {}; }
	};

	void samePixels(const juce::Image& _a, const juce::Image& _b, const char* _stage)
	{
		require(_a.getBounds() == _b.getBounds(), "toggle changed output dimensions");
		for (int y = 0; y < _a.getHeight(); ++y)
			for (int x = 0; x < _a.getWidth(); ++x)
				if (_a.getPixelAt(x, y) != _b.getPixelAt(x, y))
					throw std::runtime_error(std::string(_stage) + " differs at " + std::to_string(x) + "," + std::to_string(y)
						+ ": " + _a.getPixelAt(x, y).toString().toStdString() + " vs " + _b.getPixelAt(x, y).toString().toStdString());
	}

	void testRendering(const juce::ImageType& _imageType)
	{
		Resources resources;
		juceRmlUi::RmlInterfaces interfaces(resources);
		juceRmlUi::RmlComponentConfig config;
		config.forceSoftwareRenderer = juceRmlUi::SoftwareRendererMode::ForceOn;
		juceRmlUi::LookAndFeel lookAndFeel;
		juceRmlUi::RmlComponent component(interfaces, resources, "test.rml", 1.f, {}, {}, config);
		component.setLookAndFeel(&lookAndFeel);
		auto* doc = component.getDocument();
		auto* area = doc->GetElementById("lcd");
		auto* canvas = juceRmlUi::ElemCanvas::create(area);
		canvas->setClearEveryFrame(true);
		auto experiment = std::make_unique<mdJucePlugin::PixelPerfectPanel>();
		juce::Image lcd(juce::Image::ARGB, 128, 64, false);
		for (int y = 0; y < 64; ++y)
			for (int x = 0; x < 128; ++x)
				lcd.setPixelAt(x, y, (x + y) % 2 ? juce::Colours::white : juce::Colours::black);
		int canvasPaintCount = 0;
		canvas->setRepaintGraphicsCallback([&](juce::Image& _target, juce::Graphics& _g)
		{
			++canvasPaintCount;
			_g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
			if (!experiment || !experiment->paintLcd(lcd, _g, juce::Colours::green))
			{
				_g.fillAll(juce::Colours::green);
				_g.drawImageWithin(lcd, 0, 0, _target.getWidth(), _target.getHeight(), juce::RectanglePlacement::centred);
			}
		});
		auto paint = [&](float _dpi)
		{
			juce::Image result(juce::Image::ARGB, static_cast<int>(std::ceil(component.getWidth() * _dpi)),
				static_cast<int>(std::ceil(component.getHeight() * _dpi)), true, _imageType);
			// Exercise both the legacy direct image-copy shortcut and its guarded
			// fallback when the backing image has a different pixel size.
			lookAndFeel.getCurrentImage() = result;
			juce::Graphics g(result);
			g.addTransform(juce::AffineTransform::scale(_dpi));
			component.paint(g);
			return result;
		};
		auto settle = [&](float _dpi)
		{
			juce::Image result;
			for (int frame = 0; frame < 6; ++frame)
			{
				juceRmlUi::RenderingTestAccess::update(component);
				result = paint(_dpi);
			}
			return result;
		};
		for (float dpi : {1.f, 1.25f, 2.f})
		{
			std::cout << "Image type " << _imageType.getTypeID() << ", DPI " << dpi << '\n';
			const auto baseline = settle(dpi);
			experiment->apply(component, canvas, true);
			samePixels(baseline, paint(dpi), "pending enable"); // Paint already queued geometry before any layout update.
			const auto crisp = settle(dpi);
			require(doc->GetElementById("unmarked")->GetNumChildren() == 0, "unmarked hairline was altered");
			require(doc->GetElementById("rule")->GetNumChildren() == 1, "explicit rule was not attached exactly once");
			const auto size = canvas->getPaintSize();
			const auto display = canvas->GetBox().GetSize(Rml::BoxArea::Content);
			const auto pos = canvas->GetAbsoluteOffset(Rml::BoxArea::Border).Round();
			constexpr auto inset = mdJucePlugin::lcdInteraction::Viewport::crispInset;
			constexpr auto padding = mdJucePlugin::lcdInteraction::Viewport::crispPadding;
			const int k = std::min((size.x - 2 * inset) / 128, (size.y - 2 * inset) / 64);
			require(k > 0, "test framebuffer should fit");
			const int ox = static_cast<int>(pos.x) + (size.x - 128 * k) / 2;
			const int oy = static_cast<int>(pos.y) + (size.y - 64 * k) / 2;
			const auto viewport = mdJucePlugin::lcdInteraction::Viewport::create(
				display.x, display.y, size.x, size.y, true);
			const auto content = viewport.contentInPaintSpace();
			require(content.x == ox - pos.x && content.y == oy - pos.y
				&& content.width == 128 * k && content.height == 64 * k,
				"painted LCD rectangle disagrees with pointer viewport");
			const auto boundaryDisplayX = (content.x + 68 * k) * display.x / size.x;
			const auto boundaryDisplayY = (content.y + 10 * k) * display.y / size.y;
			const auto boundary = viewport.displayToNative(
				boundaryDisplayX, boundaryDisplayY);
			const auto beforeBoundary = viewport.displayToNative(
				boundaryDisplayX - 0.001, boundaryDisplayY);
			require(boundary && beforeBoundary
				&& static_cast<int>(std::floor(boundary->x)) == 68
				&& static_cast<int>(std::floor(beforeBoundary->x)) == 67,
				"painted LCD cell boundary disagrees with pointer mapping");
			const auto black = crisp.getPixelAt(ox, oy), white = crisp.getPixelAt(ox + k, oy);
			require(black.getBrightness() < .02f && white.getBrightness() > .98f, "LCD palette lost");
			for (int y = 0; y < 64 * k; ++y)
				for (int x = 0; x < 128 * k; ++x)
					require(crisp.getPixelAt(ox + x, oy + y) == ((x / k + y / k) % 2 ? white : black), "unequal or filtered LCD pixels");
			for (int gap = 1; gap <= padding; ++gap)
			{
				require(sameColour(crisp.getPixelAt(ox - gap, oy), juce::Colours::green)
					&& sameColour(crisp.getPixelAt(ox + 128 * k - 1 + gap, oy), juce::Colours::green)
					&& sameColour(crisp.getPixelAt(ox, oy - gap), juce::Colours::green)
					&& sameColour(crisp.getPixelAt(ox, oy + 64 * k - 1 + gap), juce::Colours::green),
					"LCD breathing room is not equal on all sides");
			}
			require(sameColour(crisp.getPixelAt(ox - inset, oy), juce::Colour(0xff345678)),
				"tight bezel lost the skin frame colour");
			require(sameColour(crisp.getPixelAt(ox - inset - 1, oy), juce::Colour(0xff8899aa)),
				"unused LCD surround was not transparent");
			experiment->apply(component, canvas, false);
			samePixels(crisp, paint(dpi), "pending disable");
			samePixels(baseline, settle(dpi), "restored baseline");
		}

		// A canvas texture replacement and the class changes used by the MD/MM
		// LEDs are each fully visible after one Update/Render pass. They must not
		// refill the generic three-frame property-settling allowance. Keep that
		// allowance available to generic callers whose properties may cascade.
		settle(1.f);
		component.enqueueUpdate();
		require(juceRmlUi::RenderingTestAccess::pendingUpdates(component) == 3,
			"generic update lost its property-settling allowance");
		settle(1.f);
		const auto canvasPaintCountBefore = canvasPaintCount;
		canvas->repaint();
		require(juceRmlUi::RenderingTestAccess::pendingUpdates(component) == 0,
			"canvas repaint requested redundant settling frames");
		juceRmlUi::RenderingTestAccess::update(component);
		auto singleFrame = paint(1.f);
		require(canvasPaintCount == canvasPaintCountBefore + 1,
			"one frame did not consume the canvas repaint");

		auto* led = doc->GetElementById("led");
		led->SetClass("lit", true);
		component.enqueueUpdateOnce();
		require(juceRmlUi::RenderingTestAccess::pendingUpdates(component) == 0,
			"LED class update requested redundant settling frames");
		juceRmlUi::RenderingTestAccess::update(component);
		singleFrame = paint(1.f);
		const auto ledPixel = singleFrame.getPixelAt(410, 60);
		require(ledPixel.getGreen() > 250 && ledPixel.getRed() < 5
			&& ledPixel.getBlue() < 5,
			"one frame did not resolve the LED class change");

		// Display changes can produce another paint before RML lays out a new frame.
		experiment->apply(component, canvas, true);
		settle(1.f);
		const auto firstRetinaPaint = paint(2.f);
		require(firstRetinaPaint.getPixelAt(820, 420) == juce::Colours::red, "pending frame moved the panel");
		samePixels(firstRetinaPaint, paint(2.f), "pending retina");
		settle(2.f);
		require(component.toRmlPosition(410, 210) == juce::Point<int>(820, 420), "pointer mapping did not follow backing density");

		// Simulate a renderer requiring power-of-two textures and a sub-native LCD.
		juceRmlUi::RenderingTestAccess::requirePaddedTextures(component);
		area->SetProperty("width", "80.5dp");
		area->SetProperty("height", "40.5dp");
		for (int y = 0; y < 64; ++y)
			for (int x = 0; x < 128; ++x)
				lcd.setPixelAt(x, y, y >= 56 ? juce::Colours::blue : x >= 120 ? juce::Colours::red : juce::Colours::white);
		canvas->repaint();
		const auto small = settle(1.f);
		const auto pos = canvas->GetAbsoluteOffset(Rml::BoxArea::Border).Round();
		const auto smallSize = canvas->getPaintSize();
		const auto smallContent = mdJucePlugin::lcdInteraction::Viewport::create(
			smallSize.x, smallSize.y, smallSize.x, smallSize.y, true).contentInPaintSpace();
		const auto right = small.getPixelAt(static_cast<int>(pos.x + smallContent.x + smallContent.width) - 1,
			static_cast<int>(pos.y + smallContent.y + smallContent.height / 2));
		const auto bottom = small.getPixelAt(static_cast<int>(pos.x + smallContent.x + smallContent.width / 2),
			static_cast<int>(pos.y + smallContent.y + smallContent.height) - 1);
		require(right.getRed() > 250 && right.getGreen() < 5 && right.getBlue() < 5, "padded fallback cropped right edge");
		require(bottom.getBlue() > 250 && bottom.getRed() < 5 && bottom.getGreen() < 5, "padded fallback cropped bottom edge");

		// Removing a marked node must not leave a dangling pointer in the controller.
		auto* rule = doc->GetElementById("rule");
		rule->GetParentNode()->RemoveChild(rule).reset();
		experiment->apply(component, canvas, false);
		experiment->apply(component, canvas, true);
		settle(1.f);
		// A detached element can remain alive in a caller-owned ElementPtr.
		auto* remaining = doc->GetElementById("teardown");
		auto detachedHelper = remaining->RemoveChild(remaining->GetChild(0));
		experiment->apply(component, canvas, false);
		detachedHelper.reset();
		// Re-enable with an intact authored background so teardown also exercises
		// restoring and removing an attached helper.
		remaining->SetProperty("background-color", "#345678");
		experiment->apply(component, canvas, true);
		settle(1.f);
		auto detachedCanvas = area->RemoveChild(canvas);
		experiment.reset();
		settle(1.f);
		Rml::ElementList helpers;
		doc->GetElementsByTagName(helpers, "pixelpanelrule");
		require(helpers.empty(), "removing experiment left helper elements behind");
		require(doc->GetElementById("teardown")->GetProperty("background-color")->Get<Rml::Colourb>(doc->GetCoreInstance())
			== Rml::Colourb(0x34, 0x56, 0x78), "removing experiment did not restore the authored rule");
	}

	void testSoftwareFrameRatePolicy()
	{
	#if JUCE_MAC
		constexpr float defaultAcceleratedFPS = 60.0f;
	#else
		constexpr float defaultAcceleratedFPS = 30.0f;
	#endif
		Resources resources;
		juceRmlUi::RmlInterfaces interfaces(resources);
		for (const auto [configured, expected, custom] : {
			std::tuple{-1, 30.0f, false}, std::tuple{0, 30.0f, false},
			std::tuple{1, 1.0f, true}, std::tuple{47, 47.0f, true},
			std::tuple{300, 300.0f, true}, std::tuple{301, 30.0f, false}})
		{
			juceRmlUi::RmlComponentConfig config;
			config.forceSoftwareRenderer = juceRmlUi::SoftwareRendererMode::ForceOn;
			config.refreshRateLimitHz = configured;
			juceRmlUi::RmlComponent component(interfaces, resources, "test.rml",
				1.f, {}, {}, config);
			require(juceRmlUi::RenderingTestAccess::targetFPS(component) == expected,
				"software frame-rate policy did not preserve the configured/default rate");
			require(juceRmlUi::RenderingTestAccess::hasCustomFPS(component) == custom,
				"software frame-rate policy misclassified the configured rate");
			juceRmlUi::RenderingTestAccess::useDefaultFrameRateFor(component,
				juceRmlUi::RmlComponent::Renderer::Gl3);
			require(juceRmlUi::RenderingTestAccess::targetFPS(component)
				== (custom ? expected : defaultAcceleratedFPS),
				"accelerated renderer did not select/preserve the configured rate");
			juceRmlUi::RenderingTestAccess::useDefaultFrameRateFor(component,
				juceRmlUi::RmlComponent::Renderer::Software);
			require(juceRmlUi::RenderingTestAccess::targetFPS(component) == expected,
				"software fallback did not restore/preserve the configured rate");
		}
	}

#ifdef RMLUI_METAL_RENDERER
	void testPeerlessMetalAttachment()
	{
		juce::Component peerlessComponent;
		juceRmlUi::MetalContext context;
		if (!context.getDevice())
			return;

		require(peerlessComponent.getPeer() == nullptr,
			"Metal attachment test unexpectedly has a peer");
		require(context.attachTo(peerlessComponent),
			"Metal native view could not attach before peer creation");
		context.detach();
	}

	void testMetalFallbackFrameRatePolicy()
	{
		{
			Resources resources;
			juceRmlUi::RmlInterfaces interfaces(resources);
			juceRmlUi::RmlComponentConfig config;
			juceRmlUi::RmlComponent component(interfaces, resources, "test.rml",
				1.f, {}, {}, config);
			if (!juceRmlUi::RenderingTestAccess::hasMetalContext(component))
				return;

			juceRmlUi::RenderingTestAccess::useDefaultFrameRateFor(component,
				juceRmlUi::RmlComponent::Renderer::Metal);
#if JUCE_MAC
			require(juceRmlUi::RenderingTestAccess::targetFPS(component) == 60.0f,
				"Metal did not select the default accelerated frame rate");
#endif
			juceRmlUi::RenderingTestAccess::fallBackFromMetalToSoftware(component);
			require(juceRmlUi::RenderingTestAccess::targetFPS(component) == 30.0f,
				"Metal fallback did not restore the default software frame rate");
		}

		{
			Resources resources;
			juceRmlUi::RmlInterfaces interfaces(resources);
			juceRmlUi::RmlComponentConfig config;
			config.refreshRateLimitHz = 47;
			juceRmlUi::RmlComponent component(interfaces, resources, "test.rml",
				1.f, {}, {}, config);
			if (!juceRmlUi::RenderingTestAccess::hasMetalContext(component))
				return;
			juceRmlUi::RenderingTestAccess::useDefaultFrameRateFor(component,
				juceRmlUi::RmlComponent::Renderer::Metal);
			juceRmlUi::RenderingTestAccess::fallBackFromMetalToSoftware(component);
			require(juceRmlUi::RenderingTestAccess::targetFPS(component) == 47.0f,
				"Metal fallback replaced an explicit frame rate");
		}
	}
#endif
}

int main()
{
	try
	{
		juce::ScopedJuceInitialiser_GUI gui;
		testSoftwareFrameRatePolicy();
		// Cover both native and portable software Graphics destinations.
		testRendering(juce::NativeImageType());
		testRendering(juce::SoftwareImageType());
#ifdef RMLUI_METAL_RENDERER
		testPeerlessMetalAttachment();
		testMetalFallbackFrameRatePolicy();
#endif
		std::cout << "Panel rendering: toggles, density transitions, integer LCD, padded fallback, and removal passed\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
