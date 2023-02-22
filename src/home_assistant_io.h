///
/// @file home_assistant_io.h
///
/// Helper library to make it easy to communicate with Home Assistant via
/// asynchronous MQTT
///

#ifndef HOME_ASSISTANT_IO_H
#define HOME_ASSISTANT_IO_H

#include <vector>
#include <Arduino.h>
#include <AsyncMqttClient.hpp>
#include <ArduinoJson.h>

class HomeAssistantDevice;
class HomeAssistantComponent;
class HomeAssistantState;
class SimpleBuf;

typedef std::function<void(const char *command)> HomeAssistantCommandCb;

///
/// @brief Class for communicating with Home Assistant
///
class HomeAssistantDevice : public AsyncMqttClient
{
	public:
		HomeAssistantDevice();
		~HomeAssistantDevice() {};

		void setDeviceId(const char *device_id);
		void setWiFi(const char *ssid, const char *pass);
		HomeAssistantComponent* addComponent(const char *component);
		void setDiscoveryPrefix(const char *prefix);

		void sendState();
		void connect();

		const char *getPrefix() {return _prefix;}
		const char *getDeviceId() {return _device_id; }

		// these are public, but are intended for internal use
		// FUTURE make these private by allowing our base class to take std::function
		// pointers for callbacks like WiFi.onEvent
		void _connectToWiFi();
		void _connectToMqtt();
		void _reconnectWiFi();
		void _onMqttConnect(bool sessionPresent);
		void _onMqttDisconnect();
		void _onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties,
			size_t len, size_t index, size_t total);

	protected:
		void registerDiscovery();

		const char *_device_id = nullptr;
		const char *_ssid = nullptr;
		const char *_pass = nullptr;
		const char *_prefix = "homeassistant";
		std::vector<HomeAssistantComponent*> _components;

		TimerHandle_t _wifiReconnectTimer;
		TimerHandle_t _mqttReconnectTimer;

	friend HomeAssistantState;
};

extern HomeAssistantDevice HomeAssistant;

///
/// @brief Class managing states belonging to a supported Home Assistant
///        component (e.g. Sensor, Light, etc.)
///
class HomeAssistantComponent
{
	public:
		~HomeAssistantComponent() {};

		HomeAssistantState* addState(const char *state_id);
		HomeAssistantState* addState(HomeAssistantState* state);
		void sendState(void);

	protected:
		HomeAssistantComponent(HomeAssistantDevice *device, const char *component_id);

		HomeAssistantDevice *_device;
		const char *_component_id;
		std::vector<HomeAssistantState*> _states;
		int _uniqifier = 0;

	friend HomeAssistantDevice;
	friend HomeAssistantState;
};

///
/// @brief Class managing a state exposed to home assistant via MQTT
///
class HomeAssistantState
{
	public:
		HomeAssistantState(const char *state_id);
		~HomeAssistantState() {};

		void setClass(const char *device_class);
		void setUnits(const char *);

		void onCommand(HomeAssistantCommandCb commandCb);
		void setValue(const char *value);

	protected:
		HomeAssistantState(HomeAssistantDevice* device, HomeAssistantComponent* component, const char *state_id, int uniqifier);
		void setParents(HomeAssistantDevice* device, HomeAssistantComponent* component, int uniqifier) {_device = device; _component = component; _uniqifier = uniqifier;};
		void sendDiscoveryConfig();

		virtual void sendState(SimpleBuf& buf);
		virtual void emitTopic(SimpleBuf& buf);
		virtual void emitValueTemplate(SimpleBuf& buf);
		virtual bool subscribeToCommand();
		virtual bool bundledCommand() { return true; };
		virtual void onMqttCommand(char *topic, char *payload) {};

		HomeAssistantDevice* _device;
		HomeAssistantComponent* _component;
		const char *_state_id = nullptr;
		int _uniqifier = 0;
		HomeAssistantCommandCb _commandCb;
		char *_value = nullptr;

	friend HomeAssistantComponent;
	friend HomeAssistantDevice;
};

class HomeAssistantFlatState : public HomeAssistantState
{
	public:
		HomeAssistantFlatState(const char *topic, const char *state_id);

	protected:
		virtual void sendState(SimpleBuf& buf);
		virtual void emitTopic(SimpleBuf& buf);
		virtual void emitValueTemplate(SimpleBuf& buf);
		virtual bool subscribeToCommand();
		virtual bool bundledCommand() { return false; };
		virtual void onMqttCommand(char *topic, char *payload);

	private:
		const char *_topic;
};

class SimpleBuf
{
  public:
	SimpleBuf(char *buf, size_t size) : _buf(buf), _size(size) { buf[0] = 0; };

	bool isFull() {
		return _cursor >= _size - 1;
	}

	bool isEmpty() {
		return _cursor == 0;
	}

	int print(const char *s, ...) {
		if (_cursor >= _size - 1)
			return 0;
		va_list args;
		va_start(args, s);
		vsnprintf(_buf + _cursor, _size - _cursor, s, args);
		_cursor += strlen(_buf + _cursor);
		return _size - _cursor - 1;
	};

	char *_buf;
	size_t _cursor = 0;
	size_t _size;
};
#endif // HOME_ASSISTANT_IO_H