#include <WiFi.h>
#include "home_assistant_io.h"

#pragma region Helper functions

/// @brief WiFi library event callback
void WiFiEvent(WiFiEvent_t event) {
    Serial.printf("[WiFi-event] event: %d\n", event);
    switch(event) {
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
        HomeAssistant._connectToMqtt();
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("WiFi lost connection");
		HomeAssistant._reconnectWiFi();
        break;
    }
}

void onMqttConnectEvent(bool sessionPresent) {
	HomeAssistant._onMqttConnect(sessionPresent);
}

void onMqttDisconnectEvent(AsyncMqttClientDisconnectReason reason) {
	HomeAssistant._onMqttDisconnect();
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
	Serial.println("Subscribe acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
	Serial.print("  qos: ");
	Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
	Serial.println("Unsubscribe acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties,
		size_t len, size_t index, size_t total) {
	Serial.println("Publish received.");
	Serial.print("  topic: ");
	Serial.println(topic);
	Serial.print("  qos: ");
	Serial.println(properties.qos);
	Serial.print("  dup: ");
	Serial.println(properties.dup);
	Serial.print("  retain: ");
	Serial.println(properties.retain);
	Serial.print("  len: ");
	Serial.println(len);
	Serial.print("  index: ");
	Serial.println(index);
	Serial.print("  total: ");
	Serial.println(total);
	HomeAssistant._onMqttMessage(topic, payload, properties, len, index, total);
}

void onMqttPublish(uint16_t packetId) {
	Serial.println("Publish acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
}

#pragma endregion

void HomeAssistantDevice::connect()
{
	_connectToWiFi();
}

void HomeAssistantDevice::_reconnectWiFi() {
	xTimerStop(_mqttReconnectTimer, 0);
	xTimerStart(_wifiReconnectTimer, 0);
}

void HomeAssistantDevice::_connectToWiFi() {
	xTimerStop(_mqttReconnectTimer, 0);
	Serial.println("Connecting to Wi-Fi...");
	WiFi.begin(_ssid, _pass);
}

void HomeAssistantDevice::_connectToMqtt() {
	Serial.println("Connecting to MQTT...");
	AsyncMqttClient::connect();
}

void HomeAssistantDevice::_onMqttConnect(bool sessionPresent) {
	Serial.println("Connected to MQTT.");
	Serial.print("Session present: ");
	Serial.println(sessionPresent);

	registerDiscovery();
}

void HomeAssistantDevice::_onMqttDisconnect() {
	Serial.println("Disconnected from MQTT.");

	if (WiFi.isConnected())
	{
		xTimerStart(_mqttReconnectTimer, 0);
	}
}

void HomeAssistantDevice::_onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties,
		size_t len, size_t index, size_t total)
{
	int statesCount = 0;
	for (auto &component: _components) {
		for (auto &state: component->_states) {
			statesCount++;
		}
	}
	DynamicJsonDocument doc(JSON_OBJECT_SIZE(statesCount));
	DeserializationError err = deserializeJson(doc, payload);
	doc.shrinkToFit();
	if (err) {
		Serial.print("onMqttMessage: Failed to parse json: ");
		Serial.println(payload);
		Serial.println(err.f_str());
	}

	for (auto &component: _components) {
		for (auto &state: component->_states) {
			if (state->_commandCb) {
				state->_commandCb(doc[state->_state_id].as<const char *>());
			}
		}
	}
}

class SimpleBuf
{
  public:
	SimpleBuf(char *buf, size_t size) : _buf(buf), _size(size) { buf[0] = 0; };

	bool isFull() {
		return _cursor >= _size - 1;
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

void HomeAssistantDevice::registerDiscovery()
{
	char configTopic[80];
	char payload[256];
	for (auto &component: _components) {
		for (auto &state: component->_states) {
			SimpleBuf buf(configTopic, sizeof(configTopic));
			buf.print("%s/%s/%s", _prefix, component->_component_id, _device_id);
			if (state->_uniqifier) {
				buf.print("%d", state->_uniqifier);
			}
			buf.print("/config");
			if (buf.isFull()) {
				Serial.print("Discovery topic too long. Skipping: ");
				Serial.println(configTopic);
				continue;
			}

			SimpleBuf buf(payload, sizeof(payload));
			buf.print("{\"~\": \"%s/%s\", \"stat_t\": \"~/state\"", _prefix, _device_id);
			if (state->_commandCb) {
				buf.print(", \"cmd_t\": \"~/set\"");
			}
			buf.print(", \"value_template\": \"{{ value_json.%s}}\"}", state->_state_id);
			if (buf.isFull()) {
				Serial.print("Discovery payload too long.  Skipped: ");
				Serial.println(payload);
				continue;
			}

			if (0 == publish(configTopic, 0, true, payload)) {
				Serial.print("Failed to publish discovery topic: ");
				Serial.println(configTopic);
				Serial.println(payload);
			}
		}
	}
}

HomeAssistantDevice::HomeAssistantDevice()
{
	auto reconnectMqtt = [](TimerHandle_t timer) {
		HomeAssistantDevice* had = static_cast<HomeAssistantDevice*>(pvTimerGetTimerID(timer));
		had->_connectToMqtt();
	};
	auto reconnectWiFi = [](TimerHandle_t timer) {
		HomeAssistantDevice* had = static_cast<HomeAssistantDevice*>(pvTimerGetTimerID(timer));
		had->_connectToWiFi();
	};
	_mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, static_cast<void*>(this), reconnectMqtt);
	_wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, static_cast<void*>(this), reconnectWiFi);

	WiFi.onEvent(WiFiEvent);  // REVIEW use std::bind?

	// FUTURE Update the AsyncMqttClient library to take a callable object as
	// a callback parameter.  These callbacks all reference the single global
	// instance.
	onConnect(onMqttConnectEvent);
	onDisconnect(onMqttDisconnectEvent);
	onSubscribe(onMqttSubscribe);
	onUnsubscribe(onMqttUnsubscribe);
	onMessage(onMqttMessage);
	onPublish(onMqttPublish);
}

void HomeAssistantDevice::setDeviceId(const char *device_id)
{
	_device_id = device_id;
}

void HomeAssistantDevice::setWiFi(const char *ssid, const char *pass)
{
	_ssid = ssid;
	_pass = pass;
}

HomeAssistantComponent *HomeAssistantDevice::addComponent(const char *component_id)
{
	HomeAssistantComponent* component = new HomeAssistantComponent(this, component_id);

	_components.push_back(component);
	return component;
}

void HomeAssistantDevice::setDiscoveryPrefix(const char *prefix)
{
	_prefix = prefix;
}

HomeAssistantDevice HomeAssistant;

HomeAssistantState *HomeAssistantComponent::addState(const char *state_id)
{
	HomeAssistantState* state = new HomeAssistantState(_device, this, state_id, _uniqifier++);
	_states.push_back(state);
	return state;
}

HomeAssistantComponent::HomeAssistantComponent(HomeAssistantDevice *device, const char *component_id)
	: _device(device), _component_id(component_id)
{
}

void HomeAssistantState::onCommand(HomeAssistantCommandCb commandCb)
{
	_commandCb = commandCb;
}

HomeAssistantState::HomeAssistantState(HomeAssistantDevice *device,
		HomeAssistantComponent *component, const char *state_id, int uniqifier)
	: _device(device), _component(component), _state_id(state_id), _uniqifier(uniqifier)
{
}
