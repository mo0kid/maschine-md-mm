#ifdef __APPLE__

#include "MetalContext.h"

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <AppKit/AppKit.h>

#include "juce_gui_basics/juce_gui_basics.h"
#include "juce_gui_extra/juce_gui_extra.h"

// Bridging helpers for void* <-> ObjC id stored in header
#define MTL_DEVICE       ((__bridge id<MTLDevice>)m_device)
#define MTL_CMD_QUEUE    ((__bridge id<MTLCommandQueue>)m_commandQueue)
#define MTL_LAYER        ((__bridge CAMetalLayer*)m_metalLayer)

namespace juceRmlUi
{
	MetalContext::MetalContext()
	{
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		m_device = (void*)[device retain];
		if (m_device)
		{
			id<MTLCommandQueue> queue = [MTL_DEVICE newCommandQueue];
			m_commandQueue = (void*)queue; // newCommandQueue returns +1 retained
		}
	}

	MetalContext::~MetalContext()
	{
		detach();

		if (m_commandQueue) { [(id)m_commandQueue release]; m_commandQueue = nullptr; }
		if (m_device) { [(id)m_device release]; m_device = nullptr; }
	}

	void MetalContext::setListener(Listener* _listener)
	{
		m_listener = _listener;
	}

	bool MetalContext::attachTo(juce::Component& _component)
	{
		if (m_attached)
			return true; // Already attached, don't re-attach

		m_component = &_component;

		if (!m_device)
		{
			NSLog(@"MetalContext::attachTo: no device");
			return false;
		}

		if (!createMetalLayer())
		{
			NSLog(@"MetalContext::attachTo: createMetalLayer failed");
			m_component = nullptr;
			return false;
		}

		NSLog(@"MetalContext::attachTo: success, starting render thread");
		m_shouldExit = false;
		m_renderThread = std::make_unique<std::thread>([this] { renderLoop(); });
		return true;
	}

	void MetalContext::detach()
	{
		if (!m_attached)
			return;

		{
			std::lock_guard lock(m_renderMutex);
			m_shouldExit = true;
		}
		m_renderCV.notify_all();

		if (m_renderThread && m_renderThread->joinable())
			m_renderThread->join();

		m_renderThread.reset();

		destroyMetalLayer();

		m_component = nullptr;
		m_attached = false;
	}

	void MetalContext::triggerRepaint()
	{
		{
			std::lock_guard lock(m_renderMutex);
			m_repaintRequested = true;
		}
		m_renderCV.notify_one();
	}

	void MetalContext::setContinuousRepainting(bool _enabled)
	{
		{
			std::lock_guard lock(m_renderMutex);
			m_continuousRepainting = _enabled;
		}
		// Wake the render thread for both transitions. Enabling should render
		// immediately; disabling should leave continuous mode without waiting for
		// the next frame timeout.
		m_renderCV.notify_one();
	}

	double MetalContext::getRenderingScale() const
	{
		return m_renderingScale;
	}

	int MetalContext::getViewportWidth() const
	{
		return m_viewportWidth;
	}

	int MetalContext::getViewportHeight() const
	{
		return m_viewportHeight;
	}

	void* MetalContext::getDevice() const
	{
		return m_device;
	}

	void* MetalContext::nextDrawable()
	{
		if (!m_metalLayer)
			return nullptr;

		id<CAMetalDrawable> drawable = [MTL_LAYER nextDrawable];
		return (__bridge void*)drawable;
	}

	unsigned long MetalContext::getPixelFormat() const
	{
		if (m_metalLayer)
			return MTL_LAYER.pixelFormat;

		return MTLPixelFormatBGRA8Unorm;
	}

	void MetalContext::renderLoop()
	{
		@autoreleasepool
		{
			if (m_listener)
				m_listener->metalContextCreated(*this);

			m_contextCreated = true;
		}

		bool retryPending = false;
		while (!m_shouldExit)
		{
			bool shouldRender = false;
			{
				std::unique_lock<std::mutex> lock(m_renderMutex);

				if (retryPending)
				{
					// A queued frame could not acquire a drawable. Keep updates gated so
					// stale frames cannot accumulate, but retry at a bounded cadence.
					m_renderCV.wait_for(lock, std::chrono::milliseconds(16), [this]
					{
						return m_shouldExit.load() || m_repaintRequested.load();
					});
				}
				else if (m_continuousRepainting.load())
				{
					// Continuous mode is frame-paced. Do not let the always-true
					// continuous flag turn wait_for() into a busy render loop.
					m_renderCV.wait_for(lock, std::chrono::milliseconds(16), [this]
					{
						return m_shouldExit.load() || m_repaintRequested.load()
							|| !m_continuousRepainting.load();
					});
				}
				else
				{
					// Non-continuous rendering is event driven.
					m_renderCV.wait(lock, [this]
					{
						return m_shouldExit.load() || m_repaintRequested.load()
							|| m_continuousRepainting.load();
					});
				}

				// Consume the request while holding the wait mutex. Clearing it after
				// unlocking can overwrite a concurrent triggerRepaint() and leave the
				// event-driven render thread asleep with a frame still queued.
				const bool repaintRequested = m_repaintRequested.exchange(false);
				shouldRender = !m_shouldExit.load()
					&& (retryPending || repaintRequested || m_continuousRepainting.load());
			}

			if (m_shouldExit)
				break;
			if (!shouldRender)
				continue;

			@autoreleasepool
			{
				updateDrawableSize();

				// The listener reports false only when a queued frame should be
				// retried (for example when CAMetalLayer temporarily has no drawable).
				retryPending = m_listener && !m_listener->renderMetal(*this);
			}
		}

		@autoreleasepool
		{
			if (m_listener && m_contextCreated)
				m_listener->metalContextClosing(*this);

			m_contextCreated = false;
		}
	}

	bool MetalContext::createMetalLayer()
	{
		if (!m_component || !m_device)
			return false;

		CAMetalLayer* layer = [CAMetalLayer layer];
		if (!layer)
			return false;
		layer.device = MTL_DEVICE;
		layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		layer.framebufferOnly = NO; // We need to read back for screenshots
		layer.opaque = YES;

		// Create an NSView backed by the Metal layer.
		NSView* metalView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
		if (!metalView)
			return false;
		metalView.wantsLayer = YES;
		metalView.layer = layer;

		// Use JUCE's NSViewComponent::attachViewToComponent to embed the view.
		// This handles plugin host view hierarchies correctly (AU, VST3, etc.),
		// including peer changes when the editor is closed and reopened.
		// attachViewToComponent returns a ref-counted object that manages the view lifecycle
		auto* attachment = juce::NSViewComponent::attachViewToComponent(*m_component, metalView);
		if (!attachment)
		{
			[metalView release];
			return false;
		}
		attachment->incReferenceCount();
		m_viewAttachment = attachment;

		// Keep the ownership returned by alloc. NSViewAttachment holds its own
		// reference until destroyMetalLayer() releases the attachment.
		m_metalView = (void*)metalView;
		m_metalLayer = (void*)[layer retain];

		m_attached = true;

		updateViewBounds();
		updateDrawableSize();
		return true;
	}

	void MetalContext::updateViewBounds()
	{
		if (!m_component || !m_metalView)
			return;

		auto* topLevel = m_component->getTopLevelComponent();
		auto* peer = topLevel ? topLevel->getPeer() : nullptr;
		if (!peer)
			return;

		NSView* metalView = (__bridge NSView*)m_metalView;
		const auto area = peer->getAreaCoveredBy(*m_component);
		const auto frame = NSMakeRect(area.getX(), area.getY(), area.getWidth(), area.getHeight());
		if (!NSEqualRects(metalView.frame, frame))
			[metalView setFrame:frame];

	}

	void MetalContext::destroyMetalLayer()
	{
		if (m_viewAttachment)
		{
			auto* attachment = static_cast<juce::ReferenceCountedObject*>(m_viewAttachment);
			attachment->decReferenceCount();
			m_viewAttachment = nullptr;
		}
		if (m_metalView)
		{
			[(id)m_metalView release];
			m_metalView = nullptr;
		}
		if (m_metalLayer)
		{
			[(id)m_metalLayer release];
			m_metalLayer = nullptr;
		}
	}

	void MetalContext::updateDrawableSize()
	{
		if (!m_metalLayer || !m_metalView)
			return;

		NSView* metalView = (__bridge NSView*)m_metalView;
		NSWindow* window = metalView.window;
		if (!window)
			return;

		CAMetalLayer* layer = MTL_LAYER;

		const auto scale = window.backingScaleFactor;
		m_renderingScale = scale;
		layer.contentsScale = scale;

		// The CAMetalLayer is this NSView's backing layer, so AppKit owns its
		// frame in the parent view's coordinate space. Setting the layer frame
		// to the local view bounds would reset its origin to (0, 0), allowing it
		// to cover JUCE siblings such as a standalone window's title bar.
		const CGRect viewBounds = metalView.bounds;

		const auto drawableWidth = static_cast<int>(viewBounds.size.width * scale);
		const auto drawableHeight = static_cast<int>(viewBounds.size.height * scale);

		if (drawableWidth != m_viewportWidth || drawableHeight != m_viewportHeight)
		{
			layer.drawableSize = CGSizeMake(drawableWidth, drawableHeight);
			m_viewportWidth = drawableWidth;
			m_viewportHeight = drawableHeight;
		}
	}
}

#endif // __APPLE__
