#include "mdPixelPerfectPanel.h"

#include "mdLcdViewport.h"

#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Geometry.h"
#include "RmlUi/Core/MeshUtilities.h"
#include "RmlUi/Core/RenderManager.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlElemCanvas.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Factory.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace mdJucePlugin
{
	namespace
	{
		// Attached only to explicitly authored 1dp, flat, non-interactive panel
		// rules. Leave the parent's layout alone, including connections to labels.
		class PixelPanelRule final : public Rml::Element
		{
		public:
			PixelPanelRule(Rml::CoreInstance& _core, const Rml::String& _tag) : Element(_core, _tag) {}

			void init(Rml::Element& _parent)
			{
				m_color = _parent.GetComputedValues().background_color();
				if (const auto* property = _parent.GetLocalProperty(Rml::PropertyId::BackgroundColor))
					m_originalBackground = *property;
				SetProperty("position", "absolute");
				SetProperty("left", "0px");
				SetProperty("top", "0px");
				SetProperty("width", "100%");
				SetProperty("height", "100%");
				SetProperty("pointer-events", "none");
				setEnabled(false);
			}

			void setEnabled(bool _enabled)
			{
				auto* parent = GetParentNode();
				if (!parent)
					return;
				SetProperty("display", _enabled ? "block" : "none");
				if (_enabled)
					parent->SetProperty("background-color", "transparent");
				else if (m_originalBackground)
					parent->SetProperty(Rml::PropertyId::BackgroundColor, *m_originalBackground);
				else
					parent->RemoveProperty(Rml::PropertyId::BackgroundColor);
			}

		private:
			void OnRender() override
			{
				auto* parent = GetParentNode();
				const auto size = parent->GetBox().GetSize(Rml::BoxArea::Content);
				if (size.x <= 0 || size.y <= 0)
					return;
				const Rml::Vector2f snappedSize(std::max(1.f, std::round(size.x)), std::max(1.f, std::round(size.y)));
				const auto color = m_color.ToPremultiplied(parent->GetComputedValues().opacity());
				if (!m_geometry || snappedSize != m_size || color != m_renderColor)
				{
					auto mesh = m_geometry.Release(Rml::Geometry::ReleaseMode::ClearMesh);
					Rml::MeshUtilities::GenerateQuad(mesh, {}, snappedSize, color);
					m_geometry = GetRenderManager()->MakeGeometry(std::move(mesh));
					m_size = snappedSize;
					m_renderColor = color;
				}
				m_geometry.Render(parent->GetAbsoluteOffset(Rml::BoxArea::Content).Round());
			}

			Rml::Colourb m_color;
			Rml::ColourbPremultiplied m_renderColor;
			std::optional<Rml::Property> m_originalBackground;
			Rml::Vector2f m_size;
			Rml::Geometry m_geometry;
		};
	}

	struct PixelPerfectPanel::Impl
	{
		bool enabled = false;
		juce::Component::SafePointer<juceRmlUi::RmlComponent> component;
		Rml::ObserverPtr<Rml::Element> canvas;
		Rml::ObserverPtr<Rml::Element> surround;
		Rml::Colourb frameColour{0x0d, 0x0f, 0x0c};
		std::vector<Rml::ObserverPtr<Rml::Element>> rules;

		~Impl()
		{
			if (surround)
				surround->SetClass("elektronCrispLcd", false);
			// Also support removing the experiment while the document is alive.
			for (auto& observer : rules)
				if (auto* rule = static_cast<PixelPanelRule*>(observer.get()); rule && rule->GetParentNode())
				{
					rule->setEnabled(false);
					rule->GetParentNode()->RemoveChild(rule);
				}
			if (auto* c = static_cast<juceRmlUi::ElemCanvas*>(canvas.get()))
				c->setPixelAligned(false);
			if (component)
				component->setUseNativePixelDensity(false);
		}
	};

	PixelPerfectPanel::PixelPerfectPanel() : m_impl(std::make_unique<Impl>()) {}
	PixelPerfectPanel::~PixelPerfectPanel() = default;

	void PixelPerfectPanel::apply(juceRmlUi::RmlComponent& _component, juceRmlUi::ElemCanvas* _canvas, const bool _enabled)
	{
		auto& state = *m_impl;
		state.enabled = _enabled;
		state.component = &_component;
		state.canvas = _canvas ? _canvas->GetObserverPtr(_canvas->GetCoreInstance()) : nullptr;
		auto* surround = _canvas ? _canvas->GetParentNode() : nullptr;
		if (state.surround.get() != surround)
		{
			if (state.surround)
				state.surround->SetClass("elektronCrispLcd", false);
			state.surround = surround ? surround->GetObserverPtr(surround->GetCoreInstance()) : nullptr;
			if (surround)
				state.frameColour = surround->GetComputedValues().border_top_color();
		}
		if (surround)
			surround->SetClass("elektronCrispLcd", _enabled);
		if (_canvas)
			_canvas->setPixelAligned(_enabled);

		state.rules.erase(std::remove_if(state.rules.begin(), state.rules.end(),
			[](const auto& _rule) { return !_rule; }), state.rules.end());
		if (auto* doc = _component.getDocument(); _enabled && doc)
		{
			static Rml::ElementInstancerGeneric<PixelPanelRule> instancer;
			doc->GetCoreInstance().factory->RegisterElementInstancer("pixelpanelrule", &instancer);
			Rml::ElementList elements;
			doc->GetElementsByClassName(elements, ruleClass);
			for (auto* element : elements)
			{
				// Only explicitly marked, flat, static rules participate. Inserting
				// unrelated 1dp controls into a skin must never opt them in implicitly.
				if (element->GetNumChildren() || element->GetTagName() != "div")
					continue;
				const auto& values = element->GetComputedValues();
				if (values.background_color().alpha == 0)
					continue;
				const auto& box = element->GetBox();
				bool flat = true;
				for (int edge = 0; edge < 4; ++edge)
					flat &= box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge(edge)) == 0 && values.border_radius()[edge] == 0;
				if (!flat)
					continue;
				auto* rule = static_cast<PixelPanelRule*>(element->AppendChild(doc->CreateElement("pixelpanelrule")));
				rule->init(*element);
				state.rules.push_back(rule->GetObserverPtr(rule->GetCoreInstance()));
			}
		}
		for (const auto& observer : state.rules)
			if (auto* rule = static_cast<PixelPanelRule*>(observer.get()))
				rule->setEnabled(_enabled);
		_component.setUseNativePixelDensity(_enabled);
		_component.enqueueUpdate();
	}

	bool PixelPerfectPanel::paintLcd(const juce::Image& _lcd, juce::Graphics& _graphics, const juce::Colour _background) const
	{
		const auto* canvas = static_cast<juceRmlUi::ElemCanvas*>(m_impl->canvas.get());
		if (!m_impl->enabled || !canvas || !_lcd.isValid())
			return false;
		_graphics.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
		const auto size = canvas->getPaintSize();
		const auto viewport = lcdInteraction::Viewport::create(
			size.x, size.y, size.x, size.y, true);
		const auto content = viewport.contentInPaintSpace();
		const juce::Rectangle<float> lcdBounds(
			static_cast<float>(content.x), static_cast<float>(content.y),
			static_cast<float>(content.width), static_cast<float>(content.height));
		const auto colour = m_impl->frameColour;
		_graphics.setColour(juce::Colour(colour.red, colour.green, colour.blue, colour.alpha));
		_graphics.fillRect(lcdBounds.expanded(lcdInteraction::Viewport::crispInset));
		_graphics.setColour(_background);
		_graphics.fillRect(lcdBounds.expanded(lcdInteraction::Viewport::crispPadding));
		// Fit the visible canvas, not its possibly larger power-of-two texture.
		// The hit tester consumes this same rectangle and snapping policy.
		_graphics.drawImage(_lcd, lcdBounds);
		return true;
	}

	bool PixelPerfectPanel::isEnabled() const
	{
		return m_impl->enabled;
	}
}
