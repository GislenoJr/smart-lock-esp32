# Smart Lock ESP32

Embedded smart lock system based on ESP32, featuring RFID authentication, MQTT logging, and modular firmware architecture using ESP-IDF and FreeRTOS.

---

## Overview

This project implements an electronic access control system using an ESP32 microcontroller. The system reads RFID tags, validates access permissions, actuates a lock mechanism, and logs events through MQTT for monitoring.

The firmware is structured using a modular component-based architecture, following ESP-IDF best practices.

---

## Features

- RFID-based authentication (RC522)
- Access control with authorized UID validation
- MQTT communication for real-time event logging
- Offline log buffering and later synchronization
- Wi-Fi connection management with reconnection handling
- Embedded web interface for Wi-Fi configuration (captive portal)
- LED status indication (connection, access status, system states)
- Modular firmware architecture using ESP-IDF components
- FreeRTOS-based task management

---

## System Architecture

The firmware is organized into components to ensure scalability and maintainability:

- `rfid_reader`: Handles RFID tag reading and UID extraction
- `mqtt_manager`: Manages MQTT connection and message publishing
- `access_log`: Handles logging and offline queueing
- `wifi_ap`: Manages Wi-Fi configuration (AP and Station modes)
- `web_server`: Captive portal for network configuration
- `dns_server`: DNS redirection for captive portal
- `led_manager`: System state indication via LEDs
- `reset_manager`: Handles system reset and configuration reset
- `nvs_manager`: Persistent storage (credentials, settings)

---

## Hardware

- ESP32
- RFID Module RC522
- Relay module (lock actuation)
- Power supply circuitry
- Supporting components (resistors, connectors, etc.)

---

## Firmware Stack

- ESP-IDF
- FreeRTOS
- MQTT (TLS / TCP)
- Wi-Fi (Station + Access Point modes)
- NVS (Non-Volatile Storage)

---

## How It Works

1. The system initializes Wi-Fi and attempts to connect to a configured network  
2. If not configured, it starts in Access Point mode with a captive portal  
3. The user configures Wi-Fi credentials through the web interface  
4. Once connected:
   - MQTT client is initialized  
   - System begins monitoring RFID inputs  
5. When a tag is read:
   - UID is validated  
   - Access is granted or denied  
   - Event is logged via MQTT  
6. If offline:
   - Events are stored locally and sent when connection is restored  

---

## Author

Gisleno Rodrigues and Josue Sucupira
Computer Engineering Students – UFC (Quixadá)

## Project Structure

```bash
smart-lock/
├── components/
├── main/
├── CMakeLists.txt
