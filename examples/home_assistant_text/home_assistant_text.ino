#include <home_assistant_io.h>
#include "config.h"

HomeAssistantState* textState;

// command callback
void setText(const char *text) {
	Serial.println(text);
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
	HomeAssistant.setDeviceId("kitchenScreen");
	HomeAssistantComponent* textComponent = HomeAssistant.addComponent("text");
	HomeAssistantFlatState* state = new HomeAssistantFlatState("homeassistant/kitchenScreen", "text");
	HomeAssistantState* text = textComponent->addState(state);
	text->onCommand(setText);

	// connect: connects wifi, mqtt and registers all of the auto discover topics
	HomeAssistant.connect();
}

void loop() {

}