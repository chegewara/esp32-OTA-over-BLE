# OTA over BLE demo

This is OTA over BLE demo based on Kolban's BLE library. This is 2 part demo:
1. BLE server, it is esp32 app that is connecting to http server and downloading binary, then is serving that binary to BLE client
- in http_client.c we can setup http URL to download binary `#define BINARY_FILE_URL "http://192.168.0.5/hello-world.bin"`
2. BLE client, its app which is receiving binary image from BLE server and performing OTA update
- in this demo we are using indications, but we can also use read from BLE server (this requires slightly changes in both apps)


This example is only proof of concept, prepared and tested with esp-idf v4.x (master) and CMake.