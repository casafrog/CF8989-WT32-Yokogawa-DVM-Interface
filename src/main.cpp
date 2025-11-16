// Yokogawa DVM MQTT client bridge driver
// Copyright © 2025 Ted Hyde, CasaFrog Pro Resources LLC  www.casafrog.com
// 12 November 2025 v2.0
//
// This application creates a semi-intelligent serial to MQTT bridge to receive RS232/GPIB-style commands over an MQTT channel
// for device control and will return the current status and display response.
//
// Additionally, via a polling time, the status and display updates will happen automatically regardless of a command change.
//
// This was writtent to accommodate WT32-ETH01 devices such as from wireless-tag.com as an integrated solution and intends ethernet not wifi.
//
// Extended original release to permit flash storage of mqtt server name (after default connect) as well as auto-send time
//  (0 for off, 1-50000 for cycles of time) and device id.
//
//
//
// Buid environment: Microsoft VSCODE 1.106 Copyright (c) 2015 - present Microsoft Corporation
//                   Platform IO Copyright (c) 2014-present PlatformIO <contact@platformio.org>
//
//  Libraries:       Arduino / Espressif-IDF Kernel: SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
//                   WT32_ETH01: Copyright © KH. This library is licensed under the GNU General Public License v3.0.
//                   MQTT PubSubClient: Copyright (c) 2020 Nick O'Leary
//                   ArduinoJSON: Copyright © 2014-2023, Benoit BLANCHON

#define DEBUG_ETHERNET_WEBSERVER_PORT       Serial

// Debug Level from 0 to 4
#define _ETHERNET_WEBSERVER_LOGLEVEL_       3

#include <WebServer_WT32_ETH01.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// System Defaults
String mqttServer = "0.0.0.0";
String ID = "unconfigured";  // Name of our device, must be unique
unsigned long mqtt_publish_interval = 6000;
long updateInterval = 6000; // this is the actual counter and will get updated/overwritten as needed

String cmdTopic     = "lab/machines/orphan/CMD";
String configTopic     = "lab/machines/orphan/CONFIG";
String datarawTopic  = "lab/machines/orphan/DATA/raw";
String datavalueTopic  = "lab/machines/orphan/DATA/value";
String datamodeTopic  = "lab/machines/orphan/DATA/mode";
String stateIDTopic  = "lab/machines/orphan/STATE/id";
String stateIPAddrTopic  = "lab/machines/orphan/STATE/IPAddr";
String statestatusTopic  = "lab/machines/orphan/STATE/status";
String stateconfigTopic  = "lab/machines/orphan/STATE/config";
// above strings will typically get overwritten with stored device id

unsigned long lastMsg = 0;

Preferences preferences;

WebServer server(80);
WiFiClient ethClient;
PubSubClient client(ethClient);

bool cmdReceived = false;
String cmdIncoming = "";

bool configReceived = false;
String configIncoming = "";

JsonDocument configJson;

void callback(char* topic, byte* payload, unsigned int length) 
{
  // Command received
  Serial.print("Message received [");
  Serial.print(topic);
  Serial.println("] ");
  
  String incomingPayload = "";
  for (int i = 0; i < length; i++) 
  {
    incomingPayload += (char)payload[i];
  }

  if (strcmp(topic, cmdTopic.c_str()) == 0)
  {
    Serial.println("COMMAND");
    cmdReceived = true;
    cmdIncoming = incomingPayload;
    Serial.println(cmdIncoming);
  }

  if (strcmp(topic, configTopic.c_str()) == 0)
  {
    Serial.println("CONFIG");
    configReceived = true;
    configIncoming = incomingPayload;
    Serial.println(configIncoming);
  }
  
 
}

void updateStatus()
{
  // Send status request to device. This is unique for Yokogawa units, an "escape-s with newline termination"
  Serial1.write(27); // Send the Escape character
  Serial1.write('S'); // send esc-s to readback current status
  Serial1.println();
  delay(500);
  String statusData = "";
  int statusValue;

  if (Serial1.available()) 
  {
    //Serial.println("receiving Data");
    statusData = Serial1.readStringUntil('\n'); // for a specific device, this might be one character; however this allows us to read a longer string

    // The data conversion gets a bit messy below; the input could be any length string (not a char array)
    // and for a different device might need to remain a string.
    // The MQTT publisher requires a char* array as data input.
    // 
    // For the DVM in this build, the status value is a byte, but we would like to see it in decimal on the MQTT side.
    // The conversion  to int, then to String, then to c_str allows a one-byte value to come in (or a longer string), and a one byte value as decimal to go out, or a
    //  non-standard override, such as "TIMEOUT"

    statusValue = (int)statusData[0]; // special case to convert only the first element as the received status data is a single integer.
    
    client.publish(stateIDTopic.c_str(), ID.c_str());
    client.publish(stateIPAddrTopic.c_str(), ETH.localIP().toString().c_str());
    client.publish(statestatusTopic.c_str(), String(statusValue).c_str()); //Slightly messy workaround to transfer between fixed data types.
  }
  else
  {
    String data = "TIMEOUT";
    client.publish(stateIDTopic.c_str(), ID.c_str());
    client.publish(stateIPAddrTopic.c_str(), ETH.localIP().toString().c_str());
    client.publish(statestatusTopic.c_str(), data.c_str()); 
  }
}

void updateDisplay()
{
  //Receive data from device. This is unique for Yokogawa units, an "escape-d with newline termination"
  Serial1.write(27); // Send the Escape character
  Serial1.write('D'); // send esc-d to readback current display
  Serial1.println();

  delay(500);
  String receivedData = "";
  if (Serial1.available()) 
  {
    Serial.println("receiving Data");

    receivedData = Serial1.readStringUntil('\n');
    //Serial.print("Received: ");
    //Serial.println(receivedData);

    //Send response to MQTT broker -- response from device based upon command 
    
    if (!client.publish(datarawTopic.c_str(), receivedData.c_str()))
    {
      Serial.println("Response failed to send.");
    }
    else
    {
      if (receivedData.length() > 4) 
      {
        String receivedMode = receivedData.substring(0,4); // keeps 4 leading mode characters
        client.publish(datamodeTopic.c_str(), receivedMode.c_str());

        String receivedValue = receivedData.substring(4); // removes 4 leading mode characters
        double numericValue = atof(receivedValue.c_str());
        char valueBuffer[32];
        sprintf(valueBuffer, "%.8f", numericValue); // and converts back to a raw floating pount value
        client.publish(datavalueTopic.c_str(), valueBuffer);
      }
      
      Serial.print("Resposne Sent: " + String(datarawTopic) + " => ");
      Serial.println(receivedData);

    }
      
  }
  else
  {
    Serial.print("Device Timeout.");
    //Send timeout status to MQTT broker
    String data = "TIMEOUT";
    client.publish(stateIDTopic.c_str(), ID.c_str());
    client.publish(stateIPAddrTopic.c_str(), ETH.localIP().toString().c_str());
    client.publish(statestatusTopic.c_str(), data.c_str()); 
  }
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection to ");
    Serial.print(mqttServer);
    client.setServer(mqttServer.c_str(), 1883);

    // Attempt to connect
    if (client.connect(ID.c_str(), "", ""))
    {
      Serial.println("...connected");

      updateStatus();
            
      // Subscribe to the command topic
      client.subscribe(cmdTopic.c_str());

      // Subscribe to the command topic
      client.subscribe(configTopic.c_str());

    }
    else
    {
      Serial.print("...failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

  // Prepare the Json string for config topic
  configJson["server"] = mqttServer;
  configJson["ID"] = ID;
  configJson["mqtt_publish_interval"] = mqtt_publish_interval;
  String configJsonString;
  serializeJson(configJson, configJsonString);
  Serial.println(configJsonString);
  client.publish(stateconfigTopic.c_str(), configJsonString.c_str()); 

}

void processCommand()
{
  // An inbound MQTT command (CMD) has been received, and needs to be processed.

  cmdReceived = false;  // set the loop-polling (status) to false
  //Send command to device
  Serial1.println(cmdIncoming);

  // wait for a response
  delay(1000);

  updateDisplay(); 

  cmdIncoming = "";
}

void processConfig()
{
  // An inbound MQTT config change (CONFIG) has been received, and needs to be processed.

  Serial.println("Processing New Config");
  configReceived = false; // set the loop-polling (status) to false

  Serial.println(configIncoming);
  DeserializationError error = deserializeJson(configJson, configIncoming);
 
  String temp_mqttServer = configJson["server"];
  String temp_ID = configJson["ID"];
  unsigned long temp_mqtt_publish_interval = configJson["mqtt_publish_interval"];

  // Store the updated config parameters to nvram using Preferences functions.
  preferences.begin("mqtt-app", false);
  preferences.clear();
  preferences.putString("default", "no");
  preferences.putString("server", temp_mqttServer);
  preferences.putString("id", temp_ID);
  preferences.putULong("autosend", temp_mqtt_publish_interval);
       
  mqttServer = preferences.getString("server", "null");  
  ID = preferences.getString("id", "null");  
  mqtt_publish_interval = preferences.getULong("autosend", 0);
  updateInterval = mqtt_publish_interval;

  Serial.print("Prefs: Server:");
  Serial.println(mqttServer);
  Serial.print("Prefs: ID:");
  Serial.println(ID);
  Serial.print("Prefs: MQTT Publish Interval:");
  Serial.println(mqtt_publish_interval);

  preferences.end();

  // Rebuild topic strings with the (updated) device ID
  cmdTopic     = "lab/machines/"+ID+"/CMD";
  configTopic     = "lab/machines/"+ID+"/CONFIG";
  datarawTopic  = "lab/machines/"+ID+"/DATA/raw";
  datavalueTopic  = "lab/machines/"+ID+"/DATA/value";
  datamodeTopic  = "lab/machines/"+ID+"/DATA/mode";
  stateIDTopic  = "lab/machines/"+ID+"/STATE/id";
  stateIPAddrTopic  = "lab/machines/"+ID+"/STATE/IPAddr";
  statestatusTopic  = "lab/machines/"+ID+"/STATE/status";
  stateconfigTopic  = "lab/machines/"+ID+"/STATE/config";

  configIncoming = "";
  // Force a disconnect and reconnect using (updated) config info
  client.disconnect();
  reconnect();

}

void setup()
{
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\nStartup...");

  preferences.begin("mqtt-app", false);
  String prefDefault = preferences.getString("default", "default");  
  if(prefDefault == "default")
  {
    // there are no preferences set yet as either the nvram is corrupted or this is the first run, so let's set them and recall back
    preferences.clear();
    preferences.putString("default", "no");
    preferences.putString("server", "mqtt-s1.casafrog.com");
    preferences.putString("id", "unconfigured");
    preferences.putULong("autosend", 5000);
    

  } 
    
  // Load (or reload, if first run) the basic preferences
  prefDefault = preferences.getString("default", "null");  
  mqttServer = preferences.getString("server", "null");  
  ID = preferences.getString("id", "null");  
  mqtt_publish_interval = preferences.getULong("autosend", 0);
  updateInterval = mqtt_publish_interval;

  Serial.print("Prefs: Server:");
  Serial.println(mqttServer);
  Serial.print("Prefs: ID:");
  Serial.println(ID);
  Serial.print("Prefs: MQTT Publish Interval:");
  Serial.println(mqtt_publish_interval);
  Serial.print("Prefs: Default Config Status:");
  Serial.println(prefDefault);

  preferences.end();

  // Rebuild topic strings with the (recently loaded) device ID
  cmdTopic     = "lab/machines/"+ID+"/CMD";
  configTopic     = "lab/machines/"+ID+"/CONFIG";
  datarawTopic  = "lab/machines/"+ID+"/DATA/raw";
  datavalueTopic  = "lab/machines/"+ID+"/DATA/value";
  datamodeTopic  = "lab/machines/"+ID+"/DATA/mode";
  stateIDTopic  = "lab/machines/"+ID+"/STATE/id";
  stateIPAddrTopic  = "lab/machines/"+ID+"/STATE/IPAddr";
  statestatusTopic  = "lab/machines/"+ID+"/STATE/status";
  stateconfigTopic  = "lab/machines/"+ID+"/STATE/config";

  Serial1.begin(9600, SERIAL_8N1, RXD2, TXD2); // secondary serial port for device, RXD2=5, TXD2=17
  while (!Serial1);
  Serial.print("\nStarted Comms for Device...");

  WT32_ETH01_onEvent();

  //bool begin(uint8_t phy_addr=ETH_PHY_ADDR, int power=ETH_PHY_POWER, int mdc=ETH_PHY_MDC, int mdio=ETH_PHY_MDIO, 
  //           eth_phy_type_t type=ETH_PHY_TYPE, eth_clock_mode_t clk_mode=ETH_CLK_MODE);
  //ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE);
  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER);

  WT32_ETH01_waitForConnect(); //we expect a DHCP server attached here providing either a dynamic IP or a reservation.
  Serial.print("IP Address: ");
  Serial.println(ETH.localIP());

  client.setServer(mqttServer.c_str(), 1883); // expect a lot of c_str now; the default variable storage is String, this library wants char*...
  client.setCallback(callback);

  // Publish a minimal config/status update
  client.publish(stateIDTopic.c_str(), ID.c_str());
  client.publish(stateIPAddrTopic.c_str(), ETH.localIP().toString().c_str());

  // Allow the hardware to sort itself out
  delay(3500);

  // Prepare the Json string for config topic
  configJson["server"] = mqttServer;
  configJson["ID"] = ID;
  configJson["mqtt_publish_interval"] = mqtt_publish_interval;
  String configJsonString;
  serializeJson(configJson, configJsonString);
  Serial.println(configJsonString);
  client.publish(stateconfigTopic.c_str(), configJsonString.c_str()); 
  // Return the current config back to MQTT as a json blob as STATE/CONFIG
}



void loop() 
{
  // One big loop. To rule.....
  if (!client.connected()) 
  {
    reconnect();
  }

  if(cmdReceived)
  {
    processCommand();
  }

  if(configReceived)
  {
    processConfig();
  }

  if(mqtt_publish_interval > 0)
  {
     if(updateInterval <=0)
    {
      updateDisplay();
      delay(100);
      updateStatus();
      updateInterval = mqtt_publish_interval;
      Serial.println("*");
    }
    else
    {
      updateInterval --;
    }
  }
 

  client.loop();

}