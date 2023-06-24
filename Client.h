#include "HTTPClient.h"

#define TIMEOUT_INTERVAL 5000 // ms

WiFiClient client;
HTTPClient http;

void connectWiFi(const char *ssid, const char *password)
{
    Serial.print("Connecting to ");
    Serial.println(ssid);

    // Begin WiFi connection.
    WiFi.begin(ssid, password);

    // We must connect to WiFi, keep waiting.
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    // Connected.
    Serial.println();
    Serial.println("WiFi connected.");
    Serial.print("Board IP address: ");
    Serial.println(WiFi.localIP().toString());

    http.setReuse(true);
}

void sendPutRequest(const char *host, String payload)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi disconnected!");
        return;
    }

    if (!http.begin(host))
    {
        Serial.println("Can not connect to host!");
        return;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Content-Length", String(payload.length()));

    int httpResponseCode = http.PUT(payload);

    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    http.end();
}

const char *sendGetRequest(const char *host)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi disconnected!");
        return "";
    }

    if (!http.begin(host))
    {
        Serial.println("Can not connect to host!");
        return "";
    }

    int httpResponseCode = http.GET();
    String payload = httpResponseCode == 200 ? http.getString() : "";
    http.end();

    return payload.c_str();
}