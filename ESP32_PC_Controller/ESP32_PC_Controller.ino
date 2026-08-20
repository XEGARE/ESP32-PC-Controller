#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESPping.h>

// Pin definitions
#define POWER_SWITCH_PIN 2 // GPIO2 - connects to PC power switch (pull LOW to start)
#define STATUS_LED_PIN 8   // GPIO8 - ESP32 onboard LED (active LOW)

// WiFi AP Configuration
#define AP_SSID "ESP32-PC-Controller"
#define AP_PASSWORD "password123"

// Configuration
#define EEPROM_SIZE 512
#define CONFIG_MAGIC 0x12345678
#define DNS_PORT 53
#define WEB_PORT 80
#define POWER_PRESS_TIME 50
#define FORCE_OFF_TIME 5000
#define PING_INTERVAL 5000
#define TRANSITION_TIMEOUT 60000

enum PowerState
{
  PC_OFF = 0,
  PC_STARTING = 1,
  PC_ON = 2,
  PC_SHUTTING_DOWN = 3
};

struct Config
{
  uint32_t magic;
  char wifi_ssid[32];
  char wifi_password[64];
  char pc_ip[16];
  bool configured;
};

WebServer WebServerInstance(WEB_PORT);
DNSServer DnsServerInstance;
Config config;
PowerState CurrentState = PC_OFF;
unsigned long LastPingCheck = 0;
unsigned long LastStartRequest = 0;
unsigned long LastShutdownRequest = 0;
bool LastPingResult = false;
bool ApMode = false;

// HTML pages
const char *html_index = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width,initial-scale=1" />
    <title>ESP32 PC Controller</title>
    <style>
        :root {
            --bg: #090d14;
            --panel: #111827;
            --panel2: #182235;
            --text: #f4f7fb;
            --muted: #8b98ac;
            --accent: #00d4ff;
            --accent2: #087ea4;
            --green: #26d980;
            --yellow: #ffb020;
            --red: #ff4d67;
            --border: #263247;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            min-height: 100vh;
            display: grid;
            place-items: center;
            padding: 24px;
            font-family:
                Inter,
                Segoe UI,
                Arial,
                sans-serif;
            color: var(--text);
            background: radial-gradient(
                circle at top,
                #122036 0,
                #090d14 52%
            );
        }

        .panel {
            width: min(100%, 560px);
            padding: 32px;
            border: 1px solid var(--border);
            border-radius: 24px;
            background: linear-gradient(
                145deg,
                rgba(24, 34, 53, 0.96),
                rgba(13, 19, 30, 0.98)
            );
            box-shadow: 0 24px 70px rgba(0, 0, 0, 0.45);
        }

        .brand {
            display: flex;
            align-items: center;
            gap: 14px;
            margin-bottom: 30px;
        }

        .logo {
            display: grid;
            place-items: center;
            width: 48px;
            height: 48px;
            border-radius: 14px;
            background: linear-gradient(
                135deg,
                var(--accent),
                var(--accent2)
            );
            color: #001018;
            font-size: 24px;
            font-weight: 900;
            box-shadow: 0 10px 30px rgba(0, 212, 255, 0.25);
        }

        h1 {
            font-size: clamp(22px, 5vw, 30px);
            letter-spacing: -0.04em;
        }

        .subtitle {
            margin-top: 4px;
            color: var(--muted);
            font-size: 14px;
        }

        .status {
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 16px;
            padding: 22px;
            margin-bottom: 22px;
            border: 1px solid var(--border);
            border-radius: 18px;
            background: rgba(7, 12, 20, 0.55);
        }

        .status-copy span {
            display: block;
            color: var(--muted);
            font-size: 12px;
            text-transform: uppercase;
            letter-spacing: 0.12em;
        }

        .status-copy strong {
            display: block;
            margin-top: 5px;
            font-size: 22px;
        }

        .indicator {
            width: 16px;
            height: 16px;
            border-radius: 50%;
            background: var(--muted);
            box-shadow: 0 0 0 7px rgba(139, 152, 172, 0.1);
        }

        .status.off .indicator {
            background: var(--red);
            box-shadow: 0 0 0 7px rgba(255, 77, 103, 0.12);
        }

        .status.starting .indicator,
        .status.shutting .indicator {
            background: var(--yellow);
            box-shadow: 0 0 0 7px rgba(255, 176, 32, 0.12);
            animation: pulse 1s infinite;
        }

        .status.on .indicator {
            background: var(--green);
            box-shadow: 0 0 0 7px rgba(38, 217, 128, 0.12);
        }

        .controls {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
        }

        .button {
            min-height: 54px;
            border: 0;
            border-radius: 14px;
            padding: 14px 18px;
            color: #fff;
            background: #27344a;
            font-size: 15px;
            font-weight: 700;
            cursor: pointer;
            transition: 0.2s;
        }

        .button:hover {
            transform: translateY(-2px);
            filter: brightness(1.12);
        }

        .button:disabled {
            opacity: 0.55;
            cursor: wait;
            transform: none;
        }

        .button.primary {
            grid-column: 1/-1;
            color: #001018;
            background: linear-gradient(135deg, var(--accent), #4be1ff);
        }

        .button.danger {
            background: #512532;
            color: #ffb8c2;
        }

        .footer {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-top: 24px;
            padding-top: 20px;
            border-top: 1px solid var(--border);
        }

        a {
            color: var(--muted);
            text-decoration: none;
            font-weight: 600;
        }

        .author {
            margin-left: auto;
            font-size: 12px;
            color: #536177;
        }

        a:hover {
            color: var(--accent);
        }

        @keyframes pulse {
            50% {
                transform: scale(0.75);
                opacity: 0.55;
            }
        }

        @media (max-width: 480px) {
            .panel {
                padding: 22px;
            }

            .controls {
                grid-template-columns: 1fr;
            }

            .button.primary {
                grid-column: auto;
            }

            .footer {
                align-items: flex-end;
            }
        }
    </style>
</head>

<body>
    <main class="panel">
        <div class="brand">
            <div class="logo">PC</div>
            <div>
                <h1>ESP32 PC Controller</h1>
                <div class="subtitle">Remote power control</div>
            </div>
        </div>
        <section id="status" class="status off">
            <div class="status-copy">
                <span>Computer status</span
                ><strong id="statusText">Checking...</strong>
            </div>
            <div class="indicator"></div>
        </section>
        <div class="controls">
            <button
                class="button primary"
                onclick="SendCommand(this, '/on', 'Starting...')"
            >
                Start PC
            </button>
            <button
                class="button"
                onclick="SendCommand(this, '/off', 'Shutting down...')"
            >
                Shutdown
            </button>
            <button class="button danger" onclick="ForceOff(this)">
                Force power off
            </button>
        </div>
        <div class="footer">
            <a href="/settings">Settings</a
            ><a
                class="author"
                href="https://xegare.com"
                target="_blank"
                rel="noopener"
                >by XEGARE</a
            >
        </div>
    </main>
    <script>
        async function UpdateStatus() {
            try {
                const statusUrl = new URL("/status", window.location.origin);
                const response = await fetch(statusUrl, { cache: "no-store" })
                if(!response.ok) {
                    throw new Error(`HTTP ${response.status}`);
                }
                const data = await response.json()
                document.getElementById("status").className =
                    "status " + data.state
                document.getElementById("statusText").textContent =
                    data.stateText
            } catch (error) {
                document.getElementById("statusText").textContent =
                    "Connection lost"
            }
        }
        async function SendCommand(button, url, pendingText) {
            const original = button.textContent
            button.disabled = true
            button.textContent = pendingText
            try {
                const requestUrl = new URL(url, window.location.origin);
                await fetch(requestUrl, { method: "POST" })
                setTimeout(UpdateStatus, 500)
            } finally {
                setTimeout(() => {
                    button.disabled = false
                    button.textContent = original
                }, 1200)
            }
        }
        function ForceOff(button) {
            if(
                confirm(
                    "Force power off the computer? Unsaved data may be lost.",
                )
            )
                SendCommand(button, "/forceoff", "Holding power button...")
        }
        setInterval(UpdateStatus, 2000)
        UpdateStatus()
    </script>
</body>
</html>
)rawliteral";

const char *html_config = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width,initial-scale=1" />
    <title>Settings | ESP32 PC Controller</title>
    <style>
        :root {
            --bg: #090d14;
            --panel: #111827;
            --text: #f4f7fb;
            --muted: #8b98ac;
            --accent: #00d4ff;
            --border: #263247;
            --input: #0b111c;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            min-height: 100vh;
            display: grid;
            place-items: center;
            padding: 24px;
            font-family:
                Inter,
                Segoe UI,
                Arial,
                sans-serif;
            color: var(--text);
            background: radial-gradient(
                circle at top,
                #122036 0,
                #090d14 52%
            );
        }

        .panel {
            width: min(100%, 560px);
            padding: 32px;
            border: 1px solid var(--border);
            border-radius: 24px;
            background: linear-gradient(
                145deg,
                rgba(24, 34, 53, 0.96),
                rgba(13, 19, 30, 0.98)
            );
            box-shadow: 0 24px 70px rgba(0, 0, 0, 0.45);
        }

        h1 {
            font-size: 28px;
        }

        .subtitle {
            margin: 7px 0 28px;
            color: var(--muted);
        }

        .field {
            margin-bottom: 18px;
        }

        label {
            display: block;
            margin-bottom: 8px;
            color: #c7d0df;
            font-size: 13px;
            font-weight: 700;
        }

        input {
            width: 100%;
            height: 50px;
            padding: 0 15px;
            border: 1px solid var(--border);
            border-radius: 12px;
            outline: none;
            background: var(--input);
            color: var(--text);
            font-size: 15px;
        }

        input:focus {
            border-color: var(--accent);
            box-shadow: 0 0 0 3px rgba(0, 212, 255, 0.12);
        }

        .password {
            position: relative;
        }

        .password input {
            padding-right: 72px;
        }

        .toggle {
            position: absolute;
            right: 8px;
            top: 7px;
            height: 36px;
            padding: 0 10px;
            border: 0;
            border-radius: 9px;
            background: #1b293d;
            color: var(--muted);
            cursor: pointer;
        }

        .save {
            width: 100%;
            height: 52px;
            margin-top: 8px;
            border: 0;
            border-radius: 13px;
            background: linear-gradient(135deg, var(--accent), #4be1ff);
            color: #001018;
            font-size: 15px;
            font-weight: 800;
            cursor: pointer;
        }

        .save:disabled {
            opacity: 0.6;
            cursor: wait;
        }

        .footer {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-top: 24px;
            padding-top: 20px;
            border-top: 1px solid var(--border);
        }

        a {
            color: var(--muted);
            text-decoration: none;
            font-weight: 600;
        }

        .author {
            margin-left: auto;
            font-size: 12px;
            color: #536177;
        }

        a:hover {
            color: var(--accent);
        }

        @media (max-width: 480px) {
            .panel {
                padding: 22px;
            }

            .footer {
                align-items: flex-end;
            }
        }
    </style>
</head>
<body>
    <main class="panel">
        <h1>Controller settings</h1>
        <div class="subtitle">Network and target computer</div>
        <form id="configForm">
            <div class="field">
                <label for="ssid">Wi-Fi network</label
                ><input id="ssid" name="ssid" maxlength="31" required />
            </div>
            <div class="field">
                <label for="password">Wi-Fi password</label>
                <div class="password">
                    <input
                        type="password"
                        id="password"
                        name="password"
                        maxlength="63"
                    /><button
                        class="toggle"
                        type="button"
                        onclick="TogglePassword()"
                    >
                        Show
                    </button>
                </div>
            </div>
            <div class="field">
                <label for="pc_ip">Computer IP address</label
                ><input
                    id="pc_ip"
                    name="pc_ip"
                    maxlength="15"
                    inputmode="decimal"
                    placeholder="192.168.1.100"
                    required
                />
            </div>
            <button id="saveButton" class="save" type="submit">
                Save and restart
            </button>
        </form>
        <div class="footer">
            <a id="backToControl" href="/">Back to control</a
            ><a
                class="author"
                href="https://xegare.com"
                target="_blank"
                rel="noopener"
                >by XEGARE</a
            >
        </div>
    </main>
    <script>
        function TogglePassword() {
            const input = document.getElementById("password")
            const button = document.querySelector(".toggle")
            const visible = input.type === "text"
            input.type = visible ? "password" : "text"
            button.textContent = visible ? "Show" : "Hide"
        }
        document
            .getElementById("pc_ip")
            .addEventListener("input", (event) => {
                event.target.value = event.target.value
                    .replace(/[^0-9.]/g, "")
                    .split(".")
                    .slice(0, 4)
                    .join(".")
            })
        const getConfigUrl = new URL("/get_config", window.location.origin);
        fetch(getConfigUrl, { cache: "no-store" })
            .then((response) => response.json())
            .then((data) => {
                document.getElementById("ssid").value = data.ssid || ""
                document.getElementById("password").value =
                    data.password || ""
                document.getElementById("pc_ip").value = data.pc_ip || ""

                const apMode = data.ApMode === true || data.ApMode === 1
                document.getElementById("backToControl").hidden = apMode
            })
        document
            .getElementById("configForm")
            .addEventListener("submit", async (event) => {
                event.preventDefault()
                const button = document.getElementById("saveButton")
                button.disabled = true
                button.textContent = "Saving..."
                const saveConfigUrl = new URL("/save_config", window.location.origin);
                try {
                    await fetch(saveConfigUrl, {
                        method: "POST",
                        body: new FormData(event.target)
                    })
                    button.textContent = "Saved. Restarting..."
                } catch (error) {
                    button.disabled = false
                    button.textContent = "Save and restart"
                    alert("Failed to save settings")
                }
            })
    </script>
</body>
</html>
)rawliteral";

void BlinkStatusLED()
{
  static bool ledState = false;
  ledState = !ledState;
  digitalWrite(STATUS_LED_PIN, ledState);
}

void saveConfig()
{
  EEPROM.put(0, config);
  EEPROM.commit();
}

void loadConfig()
{
  EEPROM.get(0, config);
  if(config.magic != CONFIG_MAGIC)
  {
    memset(&config, 0, sizeof(config));
    config.magic = CONFIG_MAGIC;
    config.configured = false;
    saveConfig();
  }
}

void clearConfig()
{
  memset(&config, 0, sizeof(config));
  saveConfig();
}

void startAPMode()
{
  if(ApMode) return;

  Serial.println("Starting AP mode...");

  WiFi.mode(WIFI_AP_STA);

  if(WiFi.softAP(AP_SSID, AP_PASSWORD))
  {
    Serial.printf("AP SSID: %s\n", AP_SSID);
    Serial.printf("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("Captive portal should now be accessible");

    DnsServerInstance.start(DNS_PORT, "*", WiFi.softAPIP());

    ApMode = true;
  }
  else
  {
    Serial.println("Failed to start AP!");
  }
}

void connectToWiFi()
{
  Serial.printf("Connecting to WiFi: %s\n", config.wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(config.wifi_ssid, config.wifi_password);

  int attempts = 0;
  while(WiFi.status() != WL_CONNECTED && attempts < 720) // 720 attempts * 250ms = 180 seconds (3 minutes)
  {
    delay(250);
    attempts++;
    BlinkStatusLED();
  }

  if(WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi connected!");
    Serial.printf("IP address: %s | http://%s:%d\n", WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str(), WEB_PORT);
    ApMode = false;
  }
  else
  {
    Serial.println("\nWiFi connection failed!");
    digitalWrite(STATUS_LED_PIN, LOW);
  }
}

String GetStateString(PowerState state)
{
  switch(state)
  {
  case PC_OFF:
    return "off";
  case PC_STARTING:
    return "starting";
  case PC_ON:
    return "on";
  case PC_SHUTTING_DOWN:
    return "shutting";
  default:
    return "unknown";
  }
}

String GetStateText(PowerState state)
{
  switch(state)
  {
  case PC_OFF:
    return "PC is Off";
  case PC_STARTING:
    return "PC is Starting";
  case PC_ON:
    return "PC is On";
  case PC_SHUTTING_DOWN:
    return "PC is Shutting Down";
  default:
    return "Unknown State";
  }
}

bool GetStateBoolean(PowerState state)
{
  switch(state)
  {
  case PC_OFF:
    return false;
  case PC_STARTING:
    return true;
  case PC_ON:
    return true;
  case PC_SHUTTING_DOWN:
    return false;
  default:
    return false;
  }
}

String GetWiFiStateString()
{
  if(WiFi.status() == WL_CONNECTED) return "Connected";
  if(ApMode) return "AP Mode";
  return "unknown";
}

void PressPowerButton()
{
  Serial.println("Power button pressed");

  pinMode(POWER_SWITCH_PIN, OUTPUT);
  digitalWrite(POWER_SWITCH_PIN, LOW);
  delay(POWER_PRESS_TIME);
  pinMode(POWER_SWITCH_PIN, INPUT);

  if(CurrentState == PC_OFF || CurrentState == PC_SHUTTING_DOWN) CurrentState = PC_STARTING;
  else if(CurrentState == PC_ON || CurrentState == PC_STARTING) CurrentState = PC_SHUTTING_DOWN;

  Serial.printf("State set to: %s\n", GetStateString(CurrentState).c_str());
}

void ForcePowerOff()
{
  Serial.println("Force power off executed");

  pinMode(POWER_SWITCH_PIN, OUTPUT);
  digitalWrite(POWER_SWITCH_PIN, LOW);
  delay(FORCE_OFF_TIME);
  pinMode(POWER_SWITCH_PIN, INPUT);

  CurrentState = PC_OFF;
}

void sendStartPage()
{
  WebServerInstance.send(200, "text/html", ApMode ? html_config : html_index);
}

void setupWebServer()
{
  WebServerInstance.on("/", HTTP_GET, sendStartPage);

  WebServerInstance.on("/settings", HTTP_GET, []()
  {
    WebServerInstance.send(200, "text/html", html_config);
  });

  WebServerInstance.on("/on", HTTP_POST, []()
  {
    if(ApMode) return sendStartPage();

    PressPowerButton();
    WebServerInstance.send(200, "text/plain", "Power button pressed");
  });

  WebServerInstance.on("/off", HTTP_POST, []()
  {
    if(ApMode) return sendStartPage();

    PressPowerButton();
    WebServerInstance.send(200, "text/plain", "Shutdown requested");
  });

  WebServerInstance.on("/forceoff", HTTP_POST, []()
  {
    if(ApMode) return sendStartPage();

    ForcePowerOff();
    WebServerInstance.send(200, "text/plain", "Force shutdown executed");
  });

  WebServerInstance.on("/status", HTTP_GET, []()
  {
    if(ApMode) return sendStartPage();

    String json = "{\"state\":\"" + GetStateString(CurrentState) + "\",";
    json += "\"stateText\":\"" + GetStateText(CurrentState) + "\",";
    json += "\"value\":" + String(GetStateBoolean(CurrentState)) + "}";

    WebServerInstance.send(200, "application/json", json);
  });

  WebServerInstance.on("/save_config", HTTP_POST, []()
  {
    if(WebServerInstance.hasArg("ssid"))
    {
      strcpy(config.wifi_ssid, WebServerInstance.arg("ssid").c_str());
    }
    if(WebServerInstance.hasArg("password"))
    {
      strcpy(config.wifi_password, WebServerInstance.arg("password").c_str());
    }
    if(WebServerInstance.hasArg("pc_ip"))
    {
      strcpy(config.pc_ip, WebServerInstance.arg("pc_ip").c_str());
    }

    config.configured = true;
    saveConfig();

    WebServerInstance.send(200, "text/plain", "Configuration saved");

    delay(1000);
    ESP.restart();
  });

  WebServerInstance.on("/clear_config", HTTP_GET, []()
  {
    clearConfig();

    WebServerInstance.send(200, "text/plain", "Configuration cleared");

    delay(1000);
    ESP.restart();
  });

  WebServerInstance.on("/get_config", HTTP_GET, []()
  {
    String json = "{\"ssid\":\"" + String(config.wifi_ssid) + "\",";
    json += "\"password\":\"" + String(config.wifi_password) + "\",";
    json += "\"pc_ip\":\"" + String(config.pc_ip) + "\",";
    json += "\"ApMode\":" + String(ApMode) + "}";

    WebServerInstance.send(200, "application/json", json);
  });

  // Android
  WebServerInstance.on("/generate_204", HTTP_ANY, sendStartPage);
  WebServerInstance.on("/gen_204", HTTP_ANY, sendStartPage);

  // Apple
  WebServerInstance.on("/hotspot-detect.html", HTTP_ANY, sendStartPage);
  WebServerInstance.on("/library/test/success.html", HTTP_ANY, sendStartPage);
  WebServerInstance.on("/success.html", HTTP_ANY, sendStartPage);

  // Windows
  WebServerInstance.on("/connecttest.txt", HTTP_ANY, sendStartPage);
  WebServerInstance.on("/ncsi.txt", HTTP_ANY, sendStartPage);
  WebServerInstance.on("/fwlink", HTTP_ANY, sendStartPage);

  WebServerInstance.onNotFound(sendStartPage);

  WebServerInstance.begin();
  Serial.println("Web server started");
}

bool PingPC()
{
  if(strlen(config.pc_ip) == 0 || WiFi.status() != WL_CONNECTED) return false;

  IPAddress targetIP;
  if(!targetIP.fromString(config.pc_ip)) return false;

  return Ping.ping(targetIP);
}

void HandleWiFi()
{
  static unsigned long disconnectedSince = 0;
  static unsigned long lastReconnectAttempt = 0;

  if(WiFi.status() == WL_CONNECTED)
  {
    disconnectedSince = 0;

    if(ApMode && config.configured)
    {
      Serial.println("Main WiFi restored!");

      DnsServerInstance.stop();

      WiFi.softAPdisconnect(false);
      WiFi.mode(WIFI_STA);

      ApMode = false;
      
      Serial.printf("IP address: %s | http://%s:%d\n", WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str(), WEB_PORT);
    }
    return;
  }

  if(disconnectedSince == 0)
  {
    disconnectedSince = millis();
    Serial.println("WiFi connection lost!");
  }

  if(config.configured && millis() - lastReconnectAttempt >= 5000)
  {
    Serial.println("Trying WiFi reconnect...");

    WiFi.reconnect();

    lastReconnectAttempt = millis();
  }

  if(!ApMode && millis() - disconnectedSince >= 30000)
  {
    Serial.println("WiFi unavailable for 30 sec -> starting AP");
    startAPMode();
  }
}

void PowerStateHandler(void *pvParameters)
{
  while(true)
  {
    if(!ApMode)
    {
      if(millis() - LastPingCheck > PING_INTERVAL)
      {
        LastPingResult = PingPC();
        LastPingCheck = millis();

        Serial.printf("Ping result: %s, Current State: %s, WiFi: %s\n", LastPingResult ? "SUCCESS" : "FAILED", GetStateString(CurrentState).c_str(), GetWiFiStateString());
      }

      PowerState newState = CurrentState;

      if(LastPingResult)
      {
        if(CurrentState != PC_ON)
        {
          newState = PC_ON;
          Serial.printf("State change: %s -> ON (ping success)\n", GetStateString(CurrentState).c_str());
        }
      }
      else
      {
        unsigned long currentTime = millis();
        bool hasRecentStartEvent = false;

        if(LastStartRequest > 0 && (currentTime - LastStartRequest) < TRANSITION_TIMEOUT) hasRecentStartEvent = true;

        bool hasRecentShutdownEvent = (LastShutdownRequest > 0 && (currentTime - LastShutdownRequest) < TRANSITION_TIMEOUT);

        if(hasRecentStartEvent && !LastPingResult)
        {
          if(CurrentState != PC_STARTING)
          {
            newState = PC_STARTING;
            Serial.printf("State change: %s -> STARTING (recent start event + ping failed)\n", GetStateString(CurrentState).c_str());
          }
        }
        else if(hasRecentShutdownEvent && !LastPingResult)
        {
          if(CurrentState != PC_SHUTTING_DOWN)
          {
            newState = PC_SHUTTING_DOWN;
            Serial.printf("State change: %s -> SHUTTING_DOWN (recent shutdown event + ping failed)\n", GetStateString(CurrentState).c_str());
          }
        }
        else if(!LastPingResult)
        {
          if(CurrentState != PC_OFF)
          {
            newState = PC_OFF;
            Serial.printf("State change: %s -> OFF (no recent events + ping failed)\n", GetStateString(CurrentState).c_str());
          }
        }
      }

      if(newState != CurrentState)
      {
        PowerState oldState = CurrentState;
        CurrentState = newState;

        if(CurrentState == PC_STARTING) LastStartRequest = millis();
        else if(CurrentState == PC_SHUTTING_DOWN) LastShutdownRequest = millis();

        Serial.printf("Power state changed: %s -> %s\n", GetStateString(oldState).c_str(), GetStateString(CurrentState).c_str());
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void WebServerHandler(void *pvParameters)
{
  while(true)
  {
    if(ApMode) DnsServerInstance.processNextRequest();
    WebServerInstance.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("ESP32 PC Controller Starting...");

  pinMode(POWER_SWITCH_PIN, INPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);

  EEPROM.begin(EEPROM_SIZE);
  loadConfig();

  if(config.configured && strlen(config.wifi_ssid) > 0)
  {
    delay(1000);
    connectToWiFi();
  }

  if(WiFi.status() != WL_CONNECTED) startAPMode();

  xTaskCreate(PowerStateHandler, "PowerStateHandler", 4096, nullptr, 1, nullptr);

  setupWebServer();
  xTaskCreate(WebServerHandler, "WebServerHandler", 4096, nullptr, 1, nullptr);

  Serial.println("Setup complete!");
}

void loop()
{
  HandleWiFi();

  static unsigned long lastBlink = 0;
  if(millis() - lastBlink > 1000)
  {
    if(WiFi.status() == WL_CONNECTED) BlinkStatusLED();
    lastBlink = millis();
  }

  delay(10);
}