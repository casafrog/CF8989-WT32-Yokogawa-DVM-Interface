# CF8989-WT32-Yokogawa-DVM-Interface

An ESP32-based Yokogawa DVM client bridge driver to permit MQTT communication for the purposes of remote control and datalogging.

Copyright © 2025 Ted Hyde, CasaFrog Pro Resources LLC https://www.casafrog.com

This application creates a semi-intelligent serial to MQTT bridge to receive GPIB-style commands over an MQTT channel for device control and will return the current status and display response.

Additionally, via a polling time, the status and display updates will happen automatically regardless of a command change.

This was written to accommodate WT32-ETH01 devices such as from wireless-tag.com as an integrated solution and intends ethernet not wifi.
Permits flash storage of MQTT server name (after default connect), unique device id, as well as auto-send time

# MQTT Use Application:
Prior to compilation, a base mqtt server address must be set (look for mqtt-s1.casafrog.com in the code around line 321 and change to your own server name or IP). Alternatively, you can spoof the ip address of your mqtt server with a temporary dns entry in your local dns server to have mqtt-s1.casafrog.com point to your server. Of course, you should not keep it this way, it is a temporary capability to permit updating the server name via mqtt later.

Default ID can also be set statically in code, or can be updated via MQTT afterwards.
(0 for off, 1-50000 for cycles of time) and device id.

Upon first-run, mqtt communication will sed "STATE" topic data, including lab/machines/orphan/STATE/IPAddr which you can use to ensure the device is the one you are looking for. Note that there is no embedded web server on this appliance, so there is no need to know what the ip of the appliance is. This is just a diagnostic item.

Using your own device id, publish a json blob of the following format:
>config={"server":"< your mqtt server >", "ID":"< your machine id >","mqtt_publish_interval": < publish time >}

< your mqtt server > is a DNS name or ip address as a string,
< your machine id > is a unique-to-your-network arbitrary name for your device as a string
< publish time > is a 32 bit unsigned integer. 0 will disable auto-send capability, where 1 or greater will provide a cycle countdown timer for auto update sending. 5000 is about 0.5 sec on most ESP32's. The value represents approximate clock cycles within the main loop section but is not intended to be accurate.

Additional library attribution in **LICENCES** folder.
