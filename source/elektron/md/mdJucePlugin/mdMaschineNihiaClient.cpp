#include "mdMaschineNihiaClient.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#endif

namespace mdJucePlugin::maschine
{
	class NihiaClient::Impl
	{
	public:
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
#if defined(__APPLE__)
		enum class NotificationKind
		{
			Device,
			Instance,
		};

		struct CallbackContext
		{
			Impl* owner = nullptr;
			NotificationKind kind = NotificationKind::Device;
		};

		using Bytes = nihia::Bytes;

		static CFDataRef notificationCallback(CFMessagePortRef _local,
			SInt32 _messageId, CFDataRef _data, void* _context);
		void handleNotification(NotificationKind _kind, CFDataRef _data);
		bool createLocalPort(const std::string& _name,
			CallbackContext& _context, CFMessagePortRef& _port);
		static CFMessagePortRef openRemotePort(const std::string& _name);
		static Bytes send(CFMessagePortRef _port, const Bytes& _message,
			bool _requireReply = false, bool* _success = nullptr);
		static bool parsePortNames(const Bytes& _reply,
			std::string& _request, std::string& _notification);
		static void appendU32(Bytes& _data, uint32_t _value);
		static uint32_t readU32(const uint8_t* _data);
		static Bytes words(std::initializer_list<uint32_t> _words);
		static Bytes makeAcknowledge(const std::string& _notificationName);
		static Bytes makeSerialConnect(const Bytes& _serial);
		bool initialiseInstance();
		void setError(const std::string& _error);

		mutable std::mutex m_stateMutex;
		std::mutex m_sendMutex;
		std::condition_variable m_notificationCondition;
		ButtonCallback m_buttonCallback;
		KnobCallback m_knobCallback;
		MainKnobCallback m_mainKnobCallback;
		PadCallback m_padCallback;
		Bytes m_serialBytes;
		std::string m_serial;
		std::string m_lastError;
		bool m_instanceAcknowledged = false;
		bool m_connected = false;
		CallbackContext m_deviceContext{this, NotificationKind::Device};
		CallbackContext m_instanceContext{this, NotificationKind::Instance};
		CFMessagePortRef m_bootstrapPort = nullptr;
		CFMessagePortRef m_deviceRequestPort = nullptr;
		CFMessagePortRef m_deviceNotificationPort = nullptr;
		CFMessagePortRef m_instanceRequestPort = nullptr;
		CFMessagePortRef m_instanceNotificationPort = nullptr;
		dispatch_queue_t m_notificationQueue = nullptr;
#else
		mutable std::mutex m_stateMutex;
		ButtonCallback m_buttonCallback;
		KnobCallback m_knobCallback;
		MainKnobCallback m_mainKnobCallback;
		PadCallback m_padCallback;
		std::string m_lastError{"Maschine NIHIA transport is only available on macOS"};
#endif
	};

#if defined(__APPLE__)
	void NihiaClient::Impl::appendU32(Bytes& _data, const uint32_t _value)
	{
		_data.push_back(static_cast<uint8_t>(_value));
		_data.push_back(static_cast<uint8_t>(_value >> 8u));
		_data.push_back(static_cast<uint8_t>(_value >> 16u));
		_data.push_back(static_cast<uint8_t>(_value >> 24u));
	}

	uint32_t NihiaClient::Impl::readU32(const uint8_t* const _data)
	{
		return static_cast<uint32_t>(_data[0])
			| (static_cast<uint32_t>(_data[1]) << 8u)
			| (static_cast<uint32_t>(_data[2]) << 16u)
			| (static_cast<uint32_t>(_data[3]) << 24u);
	}

	NihiaClient::Impl::Bytes NihiaClient::Impl::words(
		const std::initializer_list<uint32_t> _words)
	{
		Bytes result;
		result.reserve(_words.size() * sizeof(uint32_t));
		for(const auto word : _words)
			appendU32(result, word);
		return result;
	}

	CFMessagePortRef NihiaClient::Impl::openRemotePort(const std::string& _name)
	{
		const auto name = CFStringCreateWithCString(kCFAllocatorDefault,
			_name.c_str(), kCFStringEncodingUTF8);
		if(!name)
			return nullptr;
		const auto port = CFMessagePortCreateRemote(kCFAllocatorDefault, name);
		CFRelease(name);
		return port;
	}

	NihiaClient::Impl::Bytes NihiaClient::Impl::send(
		CFMessagePortRef const _port, const Bytes& _message,
		const bool _requireReply, bool* const _success)
	{
		if(_success)
			*_success = false;
		if(!_port || !CFMessagePortIsValid(_port) || _message.empty())
			return {};
		const auto message = CFDataCreate(kCFAllocatorDefault, _message.data(),
			static_cast<CFIndex>(_message.size()));
		if(!message)
			return {};
		CFDataRef reply = nullptr;
		const auto status = CFMessagePortSendRequest(_port, 0, message, 2.0,
			5.0, kCFRunLoopDefaultMode, &reply);
		CFRelease(message);
		Bytes result;
		if(status == kCFMessagePortSuccess && reply)
		{
			const auto bytes = CFDataGetBytePtr(reply);
			result.assign(bytes, bytes + CFDataGetLength(reply));
		}
		if(reply)
			CFRelease(reply);
		if(status != kCFMessagePortSuccess || (_requireReply && result.empty()))
			return {};
		if(_success)
			*_success = true;
		return result;
	}

	bool NihiaClient::Impl::parsePortNames(const Bytes& _reply,
		std::string& _request, std::string& _notification)
	{
		constexpr uint32_t trueValue = 0x74727565;
		if(_reply.size() < 12 || readU32(_reply.data()) != trueValue)
			return false;
		const auto requestLength = readU32(_reply.data() + 4);
		if(requestLength == 0 || 8u + requestLength + 4u > _reply.size())
			return false;
		const auto notificationLength = readU32(
			_reply.data() + 8u + requestLength);
		if(notificationLength == 0
			|| 12u + requestLength + notificationLength > _reply.size())
			return false;
		_request.assign(reinterpret_cast<const char*>(_reply.data() + 8),
			requestLength - 1);
		_notification.assign(reinterpret_cast<const char*>(
			_reply.data() + 12u + requestLength), notificationLength - 1);
		return true;
	}

	NihiaClient::Impl::Bytes NihiaClient::Impl::makeAcknowledge(
		const std::string& _notificationName)
	{
		auto result = words({0x03404300, 0x74727565, 0,
			static_cast<uint32_t>(_notificationName.size())});
		result.insert(result.end(), _notificationName.begin(),
			_notificationName.end());
		return result;
	}

	NihiaClient::Impl::Bytes NihiaClient::Impl::makeSerialConnect(
		const Bytes& _serial)
	{
		auto result = words({0x03444900, 0x1600, 0x4e694d32,
			0x70726d79, static_cast<uint32_t>(_serial.size())});
		result.insert(result.end(), _serial.begin(), _serial.end());
		return result;
	}

	bool NihiaClient::Impl::createLocalPort(const std::string& _name,
		CallbackContext& _context, CFMessagePortRef& _port)
	{
		const auto name = CFStringCreateWithCString(kCFAllocatorDefault,
			_name.c_str(), kCFStringEncodingUTF8);
		if(!name)
			return false;
		CFMessagePortContext context{};
		context.info = &_context;
		Boolean shouldFreeInfo = false;
		_port = CFMessagePortCreateLocal(kCFAllocatorDefault, name,
			&notificationCallback, &context, &shouldFreeInfo);
		CFRelease(name);
		if(!_port)
			return false;
		CFMessagePortSetDispatchQueue(_port, m_notificationQueue);
		return true;
	}

	CFDataRef NihiaClient::Impl::notificationCallback(CFMessagePortRef,
		SInt32, CFDataRef const _data, void* const _context)
	{
		const auto context = static_cast<CallbackContext*>(_context);
		if(context && context->owner)
			context->owner->handleNotification(context->kind, _data);
		return nullptr;
	}

	void NihiaClient::Impl::handleNotification(const NotificationKind _kind,
		CFDataRef const _data)
	{
		if(!_data)
			return;
		const auto bytes = CFDataGetBytePtr(_data);
		const auto size = static_cast<size_t>(CFDataGetLength(_data));
		ButtonCallback callback;
		KnobCallback knobCallback;
		MainKnobCallback mainKnobCallback;
		PadCallback padCallback;
		nihia::ButtonEvent event;
		nihia::KnobEvent knobEvent;
		nihia::MainKnobEvent mainKnobEvent;
		nihia::PadEvent padEvent;
		bool hasButtonEvent = false;
		bool hasKnobEvent = false;
		bool hasMainKnobEvent = false;
		bool hasPadEvent = false;
		{
			std::lock_guard lock(m_stateMutex);
			if(_kind == NotificationKind::Device && size >= 17
				&& readU32(bytes) == 0x03444e2b)
			{
				const auto length = readU32(bytes + 12);
				if(length > 0 && 16u + length <= size)
				{
					m_serialBytes.assign(bytes + 16, bytes + 16 + length);
					m_serial.assign(reinterpret_cast<const char*>(bytes + 16),
						length);
					while(!m_serial.empty() && m_serial.back() == '\0')
						m_serial.pop_back();
					m_notificationCondition.notify_all();
				}
			}
			else if(_kind == NotificationKind::Instance)
			{
				if(size >= 8 && readU32(bytes + 4) == 0x74727565)
				{
					m_instanceAcknowledged = true;
					m_notificationCondition.notify_all();
				}
				hasButtonEvent = nihia::decodeButtonEvent(bytes, size, event);
				hasKnobEvent = nihia::decodeKnobEvent(bytes, size, knobEvent);
				hasMainKnobEvent = nihia::decodeMainKnobEvent(bytes, size,
					mainKnobEvent);
				hasPadEvent = nihia::decodePadEvent(bytes, size, padEvent);
				callback = m_buttonCallback;
				knobCallback = m_knobCallback;
				mainKnobCallback = m_mainKnobCallback;
				padCallback = m_padCallback;
			}
		}
		if(hasButtonEvent && callback)
			callback(event);
		if(hasKnobEvent && knobCallback)
			knobCallback(knobEvent);
		if(hasMainKnobEvent && mainKnobCallback)
			mainKnobCallback(mainKnobEvent);
		if(hasPadEvent && padCallback)
			padCallback(padEvent);
	}

	void NihiaClient::Impl::setError(const std::string& _error)
	{
		std::lock_guard lock(m_stateMutex);
		m_lastError = _error;
	}

	bool NihiaClient::Impl::initialiseInstance()
	{
		const std::vector<Bytes> messages =
		{
			words({0x03566775, 0x4b657973}),
			words({0x03566775, 0x524b6579}),
			words({0x03436753}),
			words({0x0344674d, 0x1600}),
			words({0x03434300, 0x73747274}),
			words({0x03497354, 0}),
			words({0x03436746}),
			words({0x03497374, 0x74727565}),
			words({0x03445200, 2}),
			words({0x03444100, 3, 0, 0x04e297b0}),
			words({0x03647344, 0, 0, 0x01e00110, 0x20,
				0x60000084, 0, 0, 0x1001e001, 0x00ff0001, 0, 3,
				0x00000040}),
			words({0x03647344, 1, 0, 0x01e00110, 0x20,
				0x60010084, 0, 0, 0x1001e001, 0x00ff0001, 0, 3,
				0x00010040}),
		};
		for(const auto& message : messages)
		{
			std::lock_guard sendLock(m_sendMutex);
			send(m_instanceRequestPort, message);
		}
		return true;
	}

	bool NihiaClient::Impl::connect()
	{
		disconnect();
		m_notificationQueue = dispatch_queue_create(
			"com.gearmulator.maschine-notifications", DISPATCH_QUEUE_SERIAL);
		m_bootstrapPort = openRemotePort(
			"com.native-instruments.NIHostIntegrationAgent");
		if(!m_bootstrapPort)
		{
			setError("Native Instruments Host Integration Agent is unavailable");
			return false;
		}

		const auto pidConnect = words({0x03447500, 0x1600, 0x4e694d32,
			0x70726d79, 0});
		std::string deviceRequestName;
		std::string deviceNotificationName;
		if(!parsePortNames(send(m_bootstrapPort, pidConnect, true),
			deviceRequestName, deviceNotificationName))
		{
			setError("Maschine MK3 session could not be created");
			disconnect();
			return false;
		}
		if(!createLocalPort(deviceNotificationName, m_deviceContext,
			m_deviceNotificationPort)
			|| !(m_deviceRequestPort = openRemotePort(deviceRequestName)))
		{
			setError("Maschine MK3 device ports could not be opened");
			disconnect();
			return false;
		}
		{
			std::lock_guard sendLock(m_sendMutex);
			send(m_deviceRequestPort, makeAcknowledge(deviceNotificationName));
			send(m_deviceRequestPort, words({0x03447143}));
		}
		{
			std::unique_lock stateLock(m_stateMutex);
			if(!m_notificationCondition.wait_for(stateLock,
				std::chrono::seconds(3), [this]{ return !m_serialBytes.empty(); }))
			{
				m_lastError = "Maschine MK3 did not report an active hardware instance";
				stateLock.unlock();
				disconnect();
				return false;
			}
		}

		std::string instanceRequestName;
		std::string instanceNotificationName;
		if(!parsePortNames(send(m_bootstrapPort,
			makeSerialConnect(m_serialBytes), true), instanceRequestName,
			instanceNotificationName))
		{
			setError("Maschine MK3 instance session could not be created");
			disconnect();
			return false;
		}
		if(!createLocalPort(instanceNotificationName, m_instanceContext,
			m_instanceNotificationPort)
			|| !(m_instanceRequestPort = openRemotePort(instanceRequestName)))
		{
			setError("Maschine MK3 instance ports could not be opened");
			disconnect();
			return false;
		}
		{
			std::lock_guard sendLock(m_sendMutex);
			send(m_instanceRequestPort,
				makeAcknowledge(instanceNotificationName));
		}
		{
			std::unique_lock stateLock(m_stateMutex);
			if(!m_notificationCondition.wait_for(stateLock,
				std::chrono::seconds(3),
				[this]{ return m_instanceAcknowledged; }))
			{
				m_lastError = "Maschine MK3 instance did not acknowledge the session";
				stateLock.unlock();
				disconnect();
				return false;
			}
		}
		initialiseInstance();
		{
			std::lock_guard stateLock(m_stateMutex);
			m_connected = true;
			m_lastError.clear();
		}
		return true;
	}

	void NihiaClient::Impl::disconnect()
	{
		{
			std::lock_guard stateLock(m_stateMutex);
			m_connected = false;
			m_instanceAcknowledged = false;
			m_serialBytes.clear();
			m_serial.clear();
		}
		const auto releasePort = [](CFMessagePortRef& _port)
		{
			if(!_port)
				return;
			CFMessagePortInvalidate(_port);
			CFRelease(_port);
			_port = nullptr;
		};
		releasePort(m_instanceNotificationPort);
		releasePort(m_instanceRequestPort);
		releasePort(m_deviceNotificationPort);
		releasePort(m_deviceRequestPort);
		releasePort(m_bootstrapPort);
		if(m_notificationQueue)
		{
#if !OS_OBJECT_USE_OBJC
			dispatch_release(m_notificationQueue);
#endif
			m_notificationQueue = nullptr;
		}
	}

	bool NihiaClient::Impl::isConnected() const
	{
		std::lock_guard lock(m_stateMutex);
		return m_connected;
	}

	bool NihiaClient::Impl::sendFrame(const uint8_t _display,
		const ScreenRenderer::Frame& _frame)
	{
		if(_display > 1 || !isConnected())
			return false;
		const auto message = nihia::encodeDisplayFrame(_display, _frame);
		std::lock_guard sendLock(m_sendMutex);
		if(!m_instanceRequestPort || !CFMessagePortIsValid(m_instanceRequestPort))
			return false;
		bool delivered = false;
		send(m_instanceRequestPort, message, false, &delivered);
		if(!delivered)
		{
			std::lock_guard stateLock(m_stateMutex);
			m_connected = false;
		}
		return delivered;
	}

	bool NihiaClient::Impl::sendLeds(const nihia::LedFrame& _frame)
	{
		if(!isConnected())
			return false;
		const auto message = nihia::encodeLedFrame(_frame);
		std::lock_guard sendLock(m_sendMutex);
		if(!m_instanceRequestPort || !CFMessagePortIsValid(m_instanceRequestPort))
			return false;
		bool delivered = false;
		send(m_instanceRequestPort, message, false, &delivered);
		if(!delivered)
		{
			std::lock_guard stateLock(m_stateMutex);
			m_connected = false;
		}
		return delivered;
	}

	void NihiaClient::Impl::setButtonCallback(ButtonCallback _callback)
	{
		std::lock_guard lock(m_stateMutex);
		m_buttonCallback = std::move(_callback);
	}

	void NihiaClient::Impl::setKnobCallback(KnobCallback _callback)
	{
		std::lock_guard lock(m_stateMutex);
		m_knobCallback = std::move(_callback);
	}

	void NihiaClient::Impl::setMainKnobCallback(MainKnobCallback _callback)
	{
		std::lock_guard lock(m_stateMutex);
		m_mainKnobCallback = std::move(_callback);
	}

	void NihiaClient::Impl::setPadCallback(PadCallback _callback)
	{
		std::lock_guard lock(m_stateMutex);
		m_padCallback = std::move(_callback);
	}

	std::string NihiaClient::Impl::getSerial() const
	{
		std::lock_guard lock(m_stateMutex);
		return m_serial;
	}

	std::string NihiaClient::Impl::getLastError() const
	{
		std::lock_guard lock(m_stateMutex);
		return m_lastError;
	}
#else
	bool NihiaClient::Impl::connect() { return false; }
	void NihiaClient::Impl::disconnect() {}
	bool NihiaClient::Impl::isConnected() const { return false; }
	bool NihiaClient::Impl::sendFrame(uint8_t, const ScreenRenderer::Frame&)
	{
		return false;
	}
	bool NihiaClient::Impl::sendLeds(const nihia::LedFrame&) { return false; }
	void NihiaClient::Impl::setButtonCallback(ButtonCallback _callback)
	{
		std::lock_guard lock(m_stateMutex);
		m_buttonCallback = std::move(_callback);
	}
	void NihiaClient::Impl::setKnobCallback(KnobCallback _callback)
	{
		std::lock_guard lock(m_stateMutex);
		m_knobCallback = std::move(_callback);
	}
	void NihiaClient::Impl::setMainKnobCallback(MainKnobCallback _callback)
	{
		std::lock_guard lock(m_stateMutex);
		m_mainKnobCallback = std::move(_callback);
	}
	void NihiaClient::Impl::setPadCallback(PadCallback _callback)
	{
		std::lock_guard lock(m_stateMutex);
		m_padCallback = std::move(_callback);
	}
	std::string NihiaClient::Impl::getSerial() const { return {}; }
	std::string NihiaClient::Impl::getLastError() const
	{
		std::lock_guard lock(m_stateMutex);
		return m_lastError;
	}
#endif

	NihiaClient::NihiaClient() : m_impl(std::make_unique<Impl>()) {}
	NihiaClient::~NihiaClient() { m_impl->disconnect(); }
	bool NihiaClient::connect() { return m_impl->connect(); }
	void NihiaClient::disconnect() { m_impl->disconnect(); }
	bool NihiaClient::isConnected() const { return m_impl->isConnected(); }
	bool NihiaClient::sendFrame(const uint8_t _display,
		const ScreenRenderer::Frame& _frame)
	{
		return m_impl->sendFrame(_display, _frame);
	}
	bool NihiaClient::sendLeds(const nihia::LedFrame& _frame)
	{
		return m_impl->sendLeds(_frame);
	}
	void NihiaClient::setButtonCallback(ButtonCallback _callback)
	{
		m_impl->setButtonCallback(std::move(_callback));
	}
	void NihiaClient::setKnobCallback(KnobCallback _callback)
	{
		m_impl->setKnobCallback(std::move(_callback));
	}
	void NihiaClient::setMainKnobCallback(MainKnobCallback _callback)
	{
		m_impl->setMainKnobCallback(std::move(_callback));
	}
	void NihiaClient::setPadCallback(PadCallback _callback)
	{
		m_impl->setPadCallback(std::move(_callback));
	}
	std::string NihiaClient::getSerial() const { return m_impl->getSerial(); }
	std::string NihiaClient::getLastError() const
	{
		return m_impl->getLastError();
	}
}
