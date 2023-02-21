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

	private:
		void registerDiscovery();

		const char *_device_id = nullptr;
		const char *_ssid = nullptr;
		const char *_pass = nullptr;
		const char *_prefix = "homeassistant";
		std::vector<HomeAssistantComponent*> _components;

		TimerHandle_t _wifiReconnectTimer;
		TimerHandle_t _mqttReconnectTimer;
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
		void sendState(void);

	protected:
		HomeAssistantComponent(HomeAssistantDevice *device, const char *component_id);

	private:
		HomeAssistantDevice *_device;
		const char *_component_id;
		std::vector<HomeAssistantState*> _states;
		int _uniqifier = 0;

	friend HomeAssistantDevice;
};

///
/// @brief Class managing a state exposed to home assistant via MQTT
///
class HomeAssistantState
{
	public:
		~HomeAssistantState() {};

		void setClass(const char *device_class);
		void setUnits(const char *);

		void onCommand(HomeAssistantCommandCb commandCb);
		void setValue(const char *value);
		void setValue(int value);
		void setValue(float value);
		void setValue(double value);

	protected:
		HomeAssistantState(HomeAssistantDevice *device, HomeAssistantComponent *component, const char *state_id, int uniqifier);
		void getJsonField(char *output, size_t size);
		void sendDiscoveryConfig();

		friend HomeAssistantComponent;
		friend HomeAssistantDevice;

	private:
		HomeAssistantDevice* _device;
		HomeAssistantComponent* _component;
		const char *_state_id;
		int _uniqifier = 0;
		HomeAssistantCommandCb _commandCb;
};

#endif // HOME_ASSISTANT_IO_H