#include <home_assistant_io.h>
#include "config.h"

HomeAssistantState* textState = nullptr;

// command callback
void setText(const char *text) {
	Serial.println("Got some text:");
	if (!text) {
		Serial.println("nullptr");
		return;
	}
	Serial.println(text);
	if (textState == nullptr) {
		Serial.println("textState not initialized.  Skipping MQTT command.");
		return;
	}

	textState->setValue(text);
	HomeAssistant.sendState();
}

void setup() {
	Serial.begin(115200);
	Serial.println();

	// set up the configuration
	HomeAssistant.setWiFi(WIFI_SSID, WIFI_PASS);
	HomeAssistant.setServer(MQTT_HOST, MQTT_PORT);
	HomeAssistant.setCredentials(MQTT_USER, MQTT_PASS);
	HomeAssistant.setDeviceId("thingy");

	auto textComponent = HomeAssistant.addComponent("text");
	textState = textComponent->addState(new HomeAssistantFlatState("homeassistant/text/thingytext", "text"));
	textState->onCommand(setText);

	// connect: connects wifi, mqtt and registers all of the auto discover topics
	HomeAssistant.connect();
	Serial.println("setup complete.");
}

void loop() {
}

