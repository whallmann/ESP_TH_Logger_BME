# ESP_TH_Logger_BME
 Webserver auf NodeMCU mit Temp/Hum/Pressure BME-280 Sensor

Compiled with:

**ArduinoIDE 1.8.12**

NodeMCU 4 MB microcontroller with pined BME-280 sensor.

* Temperatur Celsius
* Hygro  Percent
* Pressure hPa (hectopascal)

BME-280 Pinout:
- SCL -> Board D1
- SDA -> Board D2
- 3V  -> Board 3V
- GND -> Board G

Webserver implemented:

Shows Gauge instruments produced by google api. See more on

https://developers.google.com/chart

Shows line charts on each source: temp, hum, pressure about the last two days. 
Depends on meassure points availble in memory (aprx. 2.000)


