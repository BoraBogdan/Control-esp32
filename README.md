# Code for NodeMCU32-S

The mcu is started in a station & access point mode so we can connect to its network and access the webserver so we can introduce the network credentials.

Files for the webserver, all the html, css and javascript are save in a non-volatile memory SPIFFS, and are located in the folder called _data_

A connection to a network can be done in two ways:

- Entering credentials
- Connecting via WPS (WiFi Protected Setup) without entering credentials

After a connection is established, mcu is going to start sending JSON packets periodically to the [API](https://github.com/BoraBogdan/Monitorizare-date-esp32)

Packets have sensor specific data, and also a microcontroller id field, if you want to add multiple microcontrollers in the architecture

When a connection is made the green led will light up, meaning that the credentials to that network are saved in *preferences* a non-volatile memory

After pressing the button on the mock-up, credentials will be deleted and the red led will be light up.

A demo can be seen on the [API](https://github.com/BoraBogdan/Monitorizare-date-esp32) repo
