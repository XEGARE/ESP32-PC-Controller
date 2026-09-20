# ESP32 PC Controller

English | [Русский](README_RU.md)

Remote PC power controller based on ESP32-C3 Super Mini with a web interface, HTTP API, Wi-Fi setup portal, and PC status monitoring.

<img width="763" height="553" alt="1789848547" src="https://github.com/user-attachments/assets/d256c643-2d91-406c-ab39-74c4bb2e8dda" />

## Features

* Turn the PC on remotely
* Request a normal shutdown
* Force the PC to power off
* Check the current PC status
* Configure Wi-Fi through a web interface
* LED connection status indication
* Captive Portal

## Requirements

### Hardware

You will need:

* ESP32-C3 Super Mini
* Female-to-male Dupont wire
* USB Type-C cable

### Arduino IDE libraries

Install the following library:

* **ESPping** by *dvarrel, Daniele Colanardi, Marian Craciunescu*

## Initial Configuration

### 1. Access Point Settings

The initial setup network name and password can be changed in the firmware:

```cpp
// Wi-Fi AP Configuration
#define AP_SSID "ESP32-PC-Controller"
#define AP_PASSWORD "password123"
```

For security reasons, it is recommended to change the default password before flashing the firmware.

### 2. Connect to the Setup Network

After the first boot, the ESP32 will create its own Wi-Fi network:

```text
SSID: ESP32-PC-Controller
Password: password123
```

Connect your phone or computer to this network.

<img width="360" height="402" alt="1784270205" src="https://github.com/user-attachments/assets/a3b72581-a1ec-40fb-9c9b-77ede615738b" />

### 3. Open the Settings Page

If the captive portal does not open automatically, go to [http://192.168.4.1](http://192.168.4.1)

<img width="829" height="636" alt="1789848654" src="https://github.com/user-attachments/assets/e858100c-4405-4462-870f-193292434c9e" />

Enter the following information:

* SSID of your main Wi-Fi network
* Password of your main Wi-Fi network
* Local IP address of the controlled PC

The PC IP address is used by the ESP32 to determine whether the computer is online by sending ICMP ping requests.

It is recommended to assign a static IP address to the PC or configure a DHCP reservation in your router.

## Enable ICMP Ping

The PC must allow incoming ICMPv4 echo requests.

### Windows

Run PowerShell as Administrator:

```powershell
New-NetFirewallRule -Name "Allow ICMPv4" -DisplayName "Allow ICMPv4" -Direction Inbound -Action Allow -Protocol ICMPv4
```

### Linux with iptables

Run:

```bash
sudo iptables -A INPUT -p icmp --icmp-type echo-request -j ACCEPT
```

Depending on your Linux distribution, you may need to save the firewall rules to make them persistent after reboot.

For Debian or Ubuntu with `iptables-persistent`:

```bash
sudo netfilter-persistent save
```

## Hardware Connection

Connect the ESP32 to the motherboard power button header using a female-to-male Dupont wire:

* Connect the male end to the motherboard `POWER+` pin
* Connect the female end to ESP32 `GPIO2`

<img width="3000" height="4000" alt="20260804_203843" src="https://github.com/user-attachments/assets/a91fe683-0197-44bd-a780-f3d48989df3d" />
<img width="4000" height="3000" alt="20260716_192339" src="https://github.com/user-attachments/assets/8d073d5e-0d0f-4c52-a98b-f6dbb2fe3b62" />

Power the ESP32 through its USB Type-C port.

Connecting the board to the PC using USB makes it easier to:

* Update the firmware
* View serial logs
* Debug connection problems

> [!WARNING]
> Check your motherboard documentation before connecting the ESP32. Motherboard front-panel header layouts may vary. Make sure the ESP32 and the motherboard use a compatible electrical connection and share the required ground reference.

## Usage

After the ESP32 connects to your Wi-Fi network, use its local IP address to access the API.

Replace `<ESP32_IP>` with the ESP32 IP address assigned by your router.

### Turn the PC On

```http
POST http://<ESP32_IP>/on
```

Example using `curl`:

```bash
curl -X POST http://<ESP32_IP>/on
```

This endpoint performs a normal power button press.

### Turn the PC Off

```http
POST http://<ESP32_IP>/off
```

Example:

```bash
curl -X POST http://<ESP32_IP>/off
```

This endpoint performs a normal power button press.

The resulting operating system behavior depends on the power button settings configured on the PC.

### Force the PC Off

```http
POST http://<ESP32_IP>/forceoff
```

Example:

```bash
curl -X POST http://<ESP32_IP>/forceoff
```

This endpoint holds the power button long enough to force the PC to shut down.

> [!CAUTION]
> A forced shutdown may cause data loss or filesystem corruption. Use it only when the operating system is not responding.

### Get PC Status

```http
GET http://<ESP32_IP>/status
```

Example:

```bash
curl http://<ESP32_IP>/status
```

Example JSON response:

```json
{
  "state": "on",
  "stateText": "PC is On",
  "value": 1
}
```

The exact values depend on the state definitions used in the firmware.

### Clear Saved Configuration

```http
GET http://<ESP32_IP>/clear_config
```

Example:

```bash
curl http://<ESP32_IP>/clear_config
```

This endpoint clears the saved Wi-Fi and PC configuration.

After the configuration is cleared, the ESP32 will return to setup access point mode.

## API Reference

| Method | Endpoint        | Description                               |
| ------ | --------------- | ----------------------------------------- |
| `POST` | `/on`           | Press the power button to turn on the PC  |
| `POST` | `/off`          | Press the power button to turn off the PC |
| `POST` | `/forceoff`     | Hold the power button to force the PC off |
| `GET`  | `/status`       | Return the current PC state as JSON       |
| `GET`  | `/clear_config` | Clear the saved configuration             |
| `GET`  | `/settings`     | Open the Wi-Fi and PC configuration page  |

## LED Status

The built-in LED connected to `GPIO8` indicates the current network state.

| LED behavior           | Status                                                |
| ---------------------- | ----------------------------------------------------- |
| Continuously on        | Wi-Fi disconnected or setup access point is active    |
| Toggles every second   | Wi-Fi connected and PC responds to ping               |
| Five quick flashes, then 1 second off | Wi-Fi connected, but PC does not respond to ping |
| Blinks quickly         | Attempting to connect to the configured Wi-Fi network |

When the ESP32 cannot connect to the configured Wi-Fi network, it will continue trying for up to three minutes.

After three minutes, it will start the setup access point again:

```text
ESP32-PC-Controller
```

You can then reconnect to the setup network and update the configuration at:

```text
http://192.168.4.1/settings
```

## Security Notes

The API does not provide authentication unless it is implemented separately in the firmware.

Use this device only on a trusted local network. Do not expose the ESP32 directly to the internet without adding authentication, access control, and encryption.

You should also change the default access point password:

```cpp
#define AP_PASSWORD "password123"
```

## Troubleshooting

### The ESP32 cannot detect whether the PC is online

Check that:

* The configured PC IP address is correct
* The PC has a static IP address or DHCP reservation
* ICMPv4 echo requests are allowed by the firewall
* The ESP32 and the PC are connected to the same local network

### The ESP32 does not connect to Wi-Fi

Check the configured SSID and password.

The ESP32 will try to connect for three minutes. If the connection fails, the setup access point will be enabled automatically.

### The power button API does not work

Check:

* The motherboard front-panel header layout
* The connection between `POWER+` and `GPIO2`
* The firmware GPIO configuration
