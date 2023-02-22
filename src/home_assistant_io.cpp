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

void HomeAssistantDevice::sendState()
{
	char payload[256];
	SimpleBuf buf(payload, sizeof(payload));

	for (auto &component: _components) {
		for (auto &state: component->_states) {
			state->sendState(buf);
		}
	}

	if (buf.isEmpty()) {
		Serial.println("MQTT state is empty, not sending.");
		return;
	}

	buf.print("}");

	if (buf.isFull()) {
		Serial.println("MQTT state payload too big, not sent due to truncation:");
		Serial.println(payload);
		return;
	}

	char topic[80];
	snprintf(topic, sizeof(topic), "%s/%s/state", _prefix, _device_id);
	Serial.print("MQTT publish to topic: ");
	Serial.println(topic);
	Serial.println(payload);

	if (0 == publish(topic, 0, true, payload)) {
		Serial.println("MQTT publish failed.");
	}
}

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
	if (!_ssid || !_pass) {
		Serial.println("WiFi ssid and/or password are empty.  Call HomeAssistent.setWiFi() before connect().");
	}
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
	// null terminate the message, put it into a buffer that we can let ArduinoJson
	// scribble on.
	char message[256];
	size_t n = min(sizeof(message) - 1, len);
	strncpy(message, payload, n);
	message[n] = 0;

	char bundleCommandTopic[80];
	SimpleBuf buf(bundleCommandTopic, sizeof(bundleCommandTopic));
	buf.print("%s/%s/set", _prefix, _device_id);
	if (strcmp(topic, bundleCommandTopic)) {
		for (auto &component: _components) {
			for (auto &state: component->_states) {
				if (state->_commandCb && !state->bundledCommand()) {
					state->onMqttCommand(topic, payload);
				}
			}
		}
		return;
	}

	int statesCount = 0;
	for (auto &component: _components) {
		for (auto &state: component->_states) {
			if (state->_commandCb && state->bundledCommand())
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
		return;
	}

	for (auto &component: _components) {
		for (auto &state: component->_states) {
			if (state->_commandCb) {
				const char *value = doc[state->_state_id].as<const char *>();
				if (value) {
					state->_commandCb(value);
				}
			}
		}
	}
}

void HomeAssistantDevice::registerDiscovery()
{
	bool subscribeToBundleCommand = false;

	for (auto &component: _components) {
		for (auto &state: component->_states) {
			if (state->_commandCb) {
				if (state->subscribeToCommand()) {
					subscribeToBundleCommand = true;
				}
			}
			state->sendDiscoveryConfig();
		}
	}

	if (subscribeToBundleCommand) {
		char topic[80];
		SimpleBuf buf(topic, sizeof(topic));
		buf.print("%s/%s/set", _prefix, _device_id);
		if (buf.isFull()) {
			Serial.print("Command topic too long. Skipping: ");
			Serial.println(topic);
			return;
		}
		Serial.print("Subscribe to topic: ");
		Serial.println(topic);
		subscribe(topic, 0);
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

HomeAssistantState *HomeAssistantComponent::addState(HomeAssistantState *state)
{
	state->setParents(_device, this, _uniqifier++);
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

void HomeAssistantState::setValue(const char *value)
{
	if (_value) {
		free(_value);
		_value = nullptr;
	}
	_value = strdup(value);
}

HomeAssistantState::HomeAssistantState(HomeAssistantDevice *device,
		HomeAssistantComponent *component, const char *state_id, int uniqifier)
	: _device(device), _component(component), _state_id(state_id), _uniqifier(uniqifier)
{
}

HomeAssistantState::HomeAssistantState(const char *state_id) : _state_id(state_id)
{
}

void HomeAssistantState::sendDiscoveryConfig()
{
	char configTopic[80];
	{
		SimpleBuf buf(configTopic, sizeof(configTopic));
		buf.print("%s/%s/%s", _device->_prefix, _component->_component_id, _device->_device_id);
		if (_uniqifier) {
			buf.print("%d", _uniqifier);
		}
		buf.print("/config");
		if (buf.isFull()) {
			Serial.print("Discovery topic too long. Skipping: ");
			Serial.println(configTopic);
			return;
		}
	}

	char payload[256];
	SimpleBuf buf(payload, sizeof(payload));
	buf.print("{\"~\": \"");
	this->emitTopic(buf);
	buf.print("\", \"stat_t\": \"~/state\"");
	if (_commandCb) {
		buf.print(", \"cmd_t\": \"~/set\"");
	}
	this->emitValueTemplate(buf);
	buf.print("}");

	if (buf.isFull()) {
		Serial.print("Discovery payload too long.  Skipped: ");
		Serial.println(payload);
		return;
	}

	Serial.print("MQTT publish config topic: ");
	Serial.println(configTopic);
	Serial.println(payload);

	if (0 == _device->publish(configTopic, 0, true, payload)) {
		Serial.print("Failed to publish discovery topic: ");
		Serial.println(configTopic);
		Serial.println(payload);
	}
}

void HomeAssistantState::sendState(SimpleBuf &buf)
{
	if (!_value) {
		return;
	}

	buf.print(buf.isEmpty() ? "{" : ",");
	buf.print("\"%s\": \"%s\"", _state_id, _value);
}

void HomeAssistantState::emitTopic(SimpleBuf &buf)
{
	buf.print("%s/%s", _device->_prefix, _device->_device_id);
}

void HomeAssistantState::emitValueTemplate(SimpleBuf &buf)
{
	buf.print(", \"value_template\": \"{{ value_json.%s}}\"", _state_id);
}

bool HomeAssistantState::subscribeToCommand()
{
	// true indicates that we want our set payload to be shared with other states
	// using the device's set topic
	return true;
}

HomeAssistantFlatState::HomeAssistantFlatState(const char *topic, const char *state_id) : HomeAssistantState(state_id)
{
	_topic = topic;
}

void HomeAssistantFlatState::sendState(SimpleBuf &buf)
{
	if (!_value) {
		return;
	}

	Serial.print("MQTT publish flat to topic: ");
	Serial.println(_topic);
	Serial.println(_value);

	if (0 == _device->publish(_topic, 0, true, _value)) {
		Serial.println("MQTT publish failed.");
	}
}

void HomeAssistantFlatState::emitTopic(SimpleBuf &buf)
{
	buf.print("%s", _topic);
}

void HomeAssistantFlatState::emitValueTemplate(SimpleBuf &buf)
{
}

bool HomeAssistantFlatState::subscribeToCommand()
{
	char topic[80];
	SimpleBuf buf(topic, sizeof(topic));
	buf.print("%s/set", _topic);
	if (buf.isFull()) {
		Serial.print("Command topic too long. Skipping: ");
		Serial.println(topic);
		return false;
	}
	Serial.print("Subscribe to topic: ");
	Serial.println(topic);
	_device->subscribe(topic, 0);
	return false;
}

void HomeAssistantFlatState::onMqttCommand(char *topic, char *payload)
{
	char commandTopic[80];
	SimpleBuf buf(commandTopic, sizeof(commandTopic));
	buf.print("%s/set", _topic);
	if (!strcmp(commandTopic, topic) && _commandCb) {
		_commandCb(payload);
	}
}
