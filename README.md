# Arduino-ESP32-InflusDB-Weather
Estacion de medicion de temperatura y humedad

* Sensor de humedad: DHT-11
* Board: ESP-32
* Monitoreo: Wifi

## Implementacion
 - Hosting de InfluxDB en Docker swarm local (Portainer.io, Traefix, InfluxDB)
 - Reporte de metricas en InfluxDB mediante api rest 
 - Api rest en ESP-32 para reporte via web
 - Multitarea en ESP-32
