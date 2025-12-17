<!-- Language Selector -->
<div align="center">
  
[![English](https://img.shields.io/badge/Language-English-blue)](README.md)
[![Русский](https://img.shields.io/badge/Language-Русский-red)](README.ru.md)

</div>

# WakeOnLan Telegram Bot for ESP32 ⚡🤖

[![Platform](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/)
[![Telegram](https://img.shields.io/badge/Telegram-Bot%20API-blue.svg)](https://core.telegram.org/bots)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Wake-on-LAN](https://img.shields.io/badge/Protocol-Wake--on--LAN-orange.svg)](https://en.wikipedia.org/wiki/Wake-on-LAN)

Telegram bot for ESP32 that sends Wake-on-LAN packets to remotely power on servers and computers in your local network. Includes detailed boot monitoring with timing and statistics.

## 🌟 Features

- **🔌 Remote Power On** - Turn on your server from anywhere in the world via Telegram
- **📊 Detailed Monitoring** - Track boot process with progress bar
- **⏱️ Timing Statistics** - Precise measurements from command to full boot
- **🛡️ Security** - User whitelist and command protection
- **🔍 Auto-check** - Automatic server availability verification
- **📱 Notifications** - Real-time interactive status messages

## 📋 Table of Contents

- [Requirements](#-requirements)
- [Installation & Setup](#-installation--setup)
  - [1. Create Telegram Bot](#1-create-telegram-bot)
  - [2. Configure Code](#2-configure-code)
  - [3. Flash ESP32](#3-flash-esp32)
- [🛠️ Server Setup](#-server-setup)
- [📱 Bot Commands](#-bot-commands)
- [📁 Project Structure](#-project-structure)
- [⚙️ Configuration](#-configuration)
- [📊 How It Works](#-how-it-works)
- [🔧 Testing](#-testing)
- [❓ FAQ](#-faq)
- [📄 License](#-license)

## 🛠 Requirements

### Hardware
- ESP32 board (tested on ESP32-C3 Supermini)
- USB cable for programming
- Stable power supply (recommended 5V 1A)

### Software
- [Arduino IDE](https://www.arduino.cc/en/software) or [PlatformIO](https://platformio.org/)
- Libraries:
  - `WiFi` (built-in)
  - `WiFiUdp`
  - `HTTPClient`
  - `ArduinoJson` (version 6.x or higher)

### Network
- 2.4GHz Wi-Fi network
- Server/PC with Wake-on-LAN support in BIOS/UEFI
- Static IP addresses or DHCP reservation

## 🚀 Installation & Setup

### 1. Create Telegram Bot

1. Open Telegram and find `@BotFather`
2. Send command `/newbot`
3. Enter bot name (e.g., `My WoL Bot`)
4. Enter bot username (must end with `bot`, e.g., `my_wol_bot`)
5. Save the received token (looks like `1234567890:ABCdefGHIJKlmnoPQRstuVWXYZ`)

### 2. Get Telegram ID

1. Find `@userinfobot` in Telegram
2. Send command `/start`
3. Copy your `id` (e.g., `123456789`)

### 3. Configure Code

Open `WakeOnLan_Bot.ino` and configure the following parameters:

```cpp
// ========== CONFIGURATION ==========
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const String botToken = "YOUR_BOT_TOKEN";
const String allowedUsers[] = {"YOUR_TELEGRAM_ID", ""}; // Your ID and additional ones

// WoL Settings
const String serverMAC = "AA:BB:CC:DD:EE:FF"; // Server MAC address
const IPAddress serverIP(192, 168, 1, 100);   // Server local IP
const IPAddress broadcastIP(192, 168, 1, 255); // Network broadcast address
