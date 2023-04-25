#include "HTTPClient.h"

WiFiClient client;
HTTPClient http;

#define TIMEOUT_INTERVAL 5000

void connectWiFi(char *ssid, char *password)
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
    Serial.println(WiFi.localIP());
    
    http.setReuse(true);
}

void connectServer(char *host, int port)
{
    Serial.print("Connecting to ");
    Serial.print(host);
    Serial.print(":");
    Serial.println(port);

    // We must connect to server, keep trying.
    while (!client.connect(host, port, 5000))
    {
        delay(500);
        Serial.print(".");
    }

    // Connected.
    Serial.println();
    Serial.println("Server connected.");
    Serial.println("Server IP address: ");
    Serial.println(client.remoteIP());
}

void sendPutRequest(String host, String payload)
{
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

void sendCommand(char *cmd)
{
    client.print(cmd);
}

void receiveResponse()
{
    // Save the time we started waiting.
    unsigned long timeout = millis();

    // Wait for the response from server.
    while (client.available() == 0)
    {
        if (millis() - timeout > TIMEOUT_INTERVAL)
        {
            Serial.println(">>> Client Timeout!");
            return;
        }
    }

    // Read all the lines of the reply from server and print them to Serial.
    while (client.available())
    {
        String line = client.readStringUntil('\n');
        Serial.println(line);
    }
}