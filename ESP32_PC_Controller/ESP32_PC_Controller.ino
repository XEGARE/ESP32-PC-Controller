#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESPping.h>
#include <atomic>

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
#define LED_FAST_BLINK_MS 100
#define LED_BLINK_COUNT 5
#define LED_PAUSE_MS 1000

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
std::atomic<bool> LastPingResult{false};
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

        button,
        input {
            font: inherit;
        }

        body {
            min-height: 100vh;
            display: grid;
            place-items: center;
            padding: 24px;
            font-family:
                system-ui,
                -apple-system,
                BlinkMacSystemFont,
                "Segoe UI",
                sans-serif;
            font-size: 15px;
            line-height: 1.5;
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
            flex-shrink: 0;
            display: grid;
            place-items: center;
            width: 48px;
            height: 48px;
            border: 1px solid rgba(0, 212, 255, 0.22);
            border-radius: 14px;
            background: linear-gradient(145deg, #173044, #0c1725);
            color: #dceef7;
            box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.06);
            transition: border-color 0.2s, background 0.2s;
        }

        .logo:hover {
            color: #fff;
            border-color: var(--accent);
            background: #173044;
        }

        .logo:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 4px;
        }

        h1 {
            font-size: clamp(22px, 5vw, 30px);
            font-weight: 600;
            line-height: 1.2;
            letter-spacing: -0.025em;
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
            letter-spacing: 0.08em;
        }

        .status-copy strong {
            display: block;
            margin-top: 5px;
            font-size: 22px;
            font-weight: 600;
            line-height: 1.3;
            letter-spacing: -0.015em;
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
            animation: breathe 2.4s ease-in-out infinite;
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
            font-weight: 600;
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

        .confirmation {
            margin: auto;
            width: min(440px, calc(100% - 32px));
            max-height: calc(100% - 32px);
            overflow: auto;
            padding: 28px;
            border: 1px solid var(--border);
            border-radius: 22px;
            color: var(--text);
            background: linear-gradient(145deg, #182235, #0d131e);
            box-shadow: 0 24px 70px rgba(0, 0, 0, 0.55);
        }

        .confirmation::backdrop {
            background: rgba(3, 7, 13, 0.75);
            backdrop-filter: blur(5px);
        }

        .confirmation h2 {
            font-size: 22px;
            font-weight: 600;
            line-height: 1.3;
            letter-spacing: -0.025em;
        }

        .confirmation p {
            margin: 12px 0 24px;
            color: var(--muted);
        }

        .confirmation-actions {
            display: flex;
            flex-wrap: wrap;
            gap: 12px;
        }

        .confirmation-actions .button {
            flex: 1 1 140px;
        }

        .button:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 4px;
        }

        .language-switch {
            display: inline-flex;
            gap: 2px;
            padding: 3px;
            border: 1px solid var(--border);
            border-radius: 10px;
            flex-shrink: 0;
        }

        .language-switch button {
            border: 0;
            border-radius: 7px;
            padding: 6px 9px;
            background: transparent;
            color: var(--muted);
            font-size: 12px;
            font-weight: 600;
            cursor: pointer;
        }

        .language-switch button[aria-pressed="true"] {
            background: #27344a;
            color: var(--text);
        }

        .language-switch button:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 2px;
        }

        .footer {
            flex-wrap: wrap;
            gap: 12px;
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

        @keyframes breathe {
            0%, 100% {
                transform: scale(0.85);
                opacity: 0.6;
                box-shadow: 0 0 0 4px rgba(38, 217, 128, 0.06);
            }
            50% {
                transform: scale(1.1);
                opacity: 1;
                box-shadow: 0 0 0 9px rgba(38, 217, 128, 0.18);
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

        }
    </style>
</head>

<body>
    <main class="panel">
        <div class="brand">
            <a class="logo"
                href="https://github.com/XEGARE/ESP32-PC-Controller"
                target="_blank" rel="noopener noreferrer"
                data-i18n-aria="github" aria-label="View project on GitHub (opens in a new tab)">
                <svg aria-hidden="true" focusable="false" width="32" height="32"
                    viewBox="0 0 32 32" fill="none" stroke="currentColor"
                    stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                    <rect x="3" y="4" width="26" height="19" rx="3" />
                    <path d="M16 23v5m-6 0h12" />
                    <g stroke="var(--accent)">
                        <path d="M16 8v5" />
                        <path d="M12.5 10.5a5 5 0 1 0 7 0" />
                    </g>
                </svg>
            </a>
            <div>
                <h1>ESP32 PC Controller</h1>
                <div class="subtitle" data-i18n="subtitle">Remote power control</div>
            </div>
        </div>
        <section id="status" class="status off">
            <div class="status-copy">
                <span data-i18n="computerStatus">Computer status</span
                ><strong id="statusText" data-i18n="checking">Checking...</strong>
            </div>
            <div class="indicator"></div>
        </section>
        <div class="controls">
            <button
                class="button primary"
                onclick="SendCommand(this, '/on', 'starting')"
             data-i18n="start">
                Start PC
            </button>
            <button
                class="button"
                onclick="Shutdown(this)"
             data-i18n="shutdown">
                Shut down
            </button>
            <button class="button danger" onclick="ForceOff(this)" data-i18n="forceOff">
                Force power off
            </button>
        </div>
        <div class="footer">
            <div class="language-switch" role="group" aria-label="Language" data-i18n-aria="language">
                <button type="button" data-language="en" lang="en" aria-label="English" aria-pressed="true">EN</button>
                <button type="button" data-language="ru" lang="ru" aria-label="Русский" aria-pressed="false">RU</button>
            </div>
            <a href="/settings" data-i18n="settings">Settings</a
            ><a
                class="author"
                href="https://xegare.com"
                target="_blank"
                rel="noopener"
                 data-i18n="author">by XEGARE</a
            >
        </div>
    </main>
    <dialog id="confirmDialog" class="confirmation"
        aria-labelledby="confirmTitle" aria-describedby="confirmMessage">
        <h2 id="confirmTitle"></h2>
        <p id="confirmMessage"></p>
        <div class="confirmation-actions">
            <button id="cancelAction" class="button" type="button" autofocus data-i18n="cancel">Cancel</button>
            <button id="confirmAction" class="button" type="button"></button>
        </div>
    </dialog>
    <script>
        const translations = {
            "en": {
                "language": "Language",
                "author": "by XEGARE",
                "subtitle": "Remote power control",
                "computerStatus": "Computer status",
                "checking": "Checking...",
                "start": "Start PC",
                "shutdown": "Shut down",
                "forceOff": "Force power off",
                "settings": "Settings",
                "cancel": "Cancel",
                "starting": "Starting...",
                "shutting": "Shutting down...",
                "holding": "Holding power button...",
                "lost": "Connection lost",
                "off": "PC is Off",
                "pcStarting": "PC is Starting",
                "on": "PC is On",
                "pcShutting": "PC is Shutting Down",
                "unknown": "Unknown State",
                "shutdownTitle": "Shut down the PC?",
                "shutdownMessage": "Save your work before shutting down the computer.",
                "forceTitle": "Force power off?",
                "forceMessage": "The computer will be forced to turn off. Unsaved data may be lost.",
                "github": "View project on GitHub (opens in a new tab)"
            },
            "ru": {
                "language": "Язык",
                "author": "от XEGARE",
                "subtitle": "Удалённое управление питанием",
                "computerStatus": "Состояние компьютера",
                "checking": "Проверка...",
                "start": "Включить ПК",
                "shutdown": "Выключить",
                "forceOff": "Выключить принудительно",
                "settings": "Настройки",
                "cancel": "Отмена",
                "starting": "Включение...",
                "shutting": "Выключение...",
                "holding": "Удержание кнопки питания...",
                "lost": "Соединение потеряно",
                "off": "ПК выключен",
                "pcStarting": "ПК включается",
                "on": "ПК включён",
                "pcShutting": "ПК выключается",
                "unknown": "Состояние неизвестно",
                "shutdownTitle": "Выключить компьютер?",
                "shutdownMessage": "Сохраните свою работу перед выключением компьютера.",
                "forceTitle": "Выключить принудительно?",
                "forceMessage": "Питание компьютера будет отключено принудительно. Несохранённые данные могут быть потеряны.",
                "github": "Открыть проект на GitHub (в новой вкладке)"
            }
        }
        let language = "en"
        try {
            if(localStorage.getItem("esp32-pc-language") === "ru") language = "ru"
        } catch (error) {}

        function Translate(key) {
            return translations[language][key] || translations.en[key] || key
        }

        function SetText(element, key) {
            element.dataset.i18n = key
            element.textContent = Translate(key)
        }

        function ApplyLanguage() {
            document.documentElement.lang = language
            document.querySelectorAll("[data-i18n]").forEach(element => {
                element.textContent = Translate(element.dataset.i18n)
            })
            document.querySelectorAll("[data-i18n-aria]").forEach(element => {
                element.setAttribute("aria-label", Translate(element.dataset.i18nAria))
            })
            document.querySelectorAll("[data-language]").forEach(button => {
                button.setAttribute("aria-pressed", String(button.dataset.language === language))
            })
        }

        document.querySelectorAll("[data-language]").forEach(button => {
            button.addEventListener("click", () => {
                language = button.dataset.language
                try { localStorage.setItem("esp32-pc-language", language) } catch (error) {}
                ApplyLanguage()
            })
        })
        ApplyLanguage()
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
                const stateKey = { off: "off", starting: "pcStarting", on: "on", shutting: "pcShutting" }[data.state] || "unknown"
                SetText(document.getElementById("statusText"), stateKey)
            } catch (error) {
                SetText(document.getElementById("statusText"), "lost")
            }
        }
        async function SendCommand(button, url, pendingText) {
            const original = button.dataset.i18n
            button.disabled = true
            SetText(button, pendingText)
            try {
                const requestUrl = new URL(url, window.location.origin);
                await fetch(requestUrl, { method: "POST" })
                setTimeout(UpdateStatus, 500)
            } finally {
                setTimeout(() => {
                    button.disabled = false
                    SetText(button, original)
                }, 1200)
            }
        }
        const confirmDialog = document.getElementById("confirmDialog")
        const confirmAction = document.getElementById("confirmAction")
        let pendingCommand = null

        function OpenConfirmation(button, url, pendingText, title, message, actionText, danger) {
            if(confirmDialog.open || button.disabled) return
            pendingCommand = { button, url, pendingText }
            SetText(document.getElementById("confirmTitle"), title)
            SetText(document.getElementById("confirmMessage"), message)
            SetText(confirmAction, actionText)
            confirmAction.className = danger ? "button danger" : "button primary"
            confirmDialog.showModal()
            document.getElementById("cancelAction").focus()
        }

        function CancelConfirmation() {
            pendingCommand = null
            confirmDialog.close()
        }

        document.getElementById("cancelAction").addEventListener("click", CancelConfirmation)
        confirmDialog.addEventListener("cancel", (event) => {
            event.preventDefault()
            CancelConfirmation()
        })
        confirmDialog.addEventListener("click", (event) => {
            const bounds = confirmDialog.getBoundingClientRect()
            if(event.target === confirmDialog &&
                (event.clientX < bounds.left || event.clientX > bounds.right ||
                 event.clientY < bounds.top || event.clientY > bounds.bottom)) {
                CancelConfirmation()
            }
        })
        confirmAction.addEventListener("click", () => {
            const command = pendingCommand
            pendingCommand = null
            confirmDialog.close()
            if(command) SendCommand(command.button, command.url, command.pendingText)
        })

        function Shutdown(button) {
            OpenConfirmation(button, "/off", "shutting",
                "shutdownTitle", "shutdownMessage", "shutdown", false)
        }
        function ForceOff(button) {
            OpenConfirmation(button, "/forceoff", "holding",
                "forceTitle", "forceMessage", "forceOff", true)
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
    <title data-i18n="pageTitle">Settings | ESP32 PC Controller</title>
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

        button,
        input {
            font: inherit;
        }

        body {
            min-height: 100vh;
            display: grid;
            place-items: center;
            padding: 24px;
            font-family:
                system-ui,
                -apple-system,
                BlinkMacSystemFont,
                "Segoe UI",
                sans-serif;
            font-size: 15px;
            line-height: 1.5;
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
            font-size: clamp(22px, 5vw, 28px);
            font-weight: 600;
            line-height: 1.2;
            letter-spacing: -0.025em;
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
            font-weight: 600;
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
            padding-right: 108px;
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
            font-weight: 600;
            cursor: pointer;
        }

        .save:disabled {
            opacity: 0.6;
            cursor: wait;
        }

        .language-switch {
            display: inline-flex;
            gap: 2px;
            padding: 3px;
            border: 1px solid var(--border);
            border-radius: 10px;
            flex-shrink: 0;
        }

        .language-switch button {
            border: 0;
            border-radius: 7px;
            padding: 6px 9px;
            background: transparent;
            color: var(--muted);
            font-size: 12px;
            font-weight: 600;
            cursor: pointer;
        }

        .language-switch button[aria-pressed="true"] {
            background: #27344a;
            color: var(--text);
        }

        .language-switch button:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 2px;
        }

        .footer {
            flex-wrap: wrap;
            gap: 12px;
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

        }
    </style>
</head>
<body>
    <main class="panel">
        <h1 data-i18n="heading">Controller settings</h1>
        <div class="subtitle" data-i18n="subtitle">Network and target computer</div>
        <form id="configForm">
            <div class="field">
                <label for="ssid" data-i18n="ssid">Wi-Fi network</label
                ><input id="ssid" name="ssid" maxlength="31" required />
            </div>
            <div class="field">
                <label for="password" data-i18n="password">Wi-Fi password</label>
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
                     data-i18n="show">
                        Show
                    </button>
                </div>
            </div>
            <div class="field">
                <label for="pc_ip" data-i18n="ip">Computer IP address</label
                ><input
                    id="pc_ip"
                    name="pc_ip"
                    maxlength="15"
                    inputmode="decimal"
                    placeholder="192.168.1.100"
                    required
                />
            </div>
            <button id="saveButton" class="save" type="submit" data-i18n="save">
                Save and restart
            </button>
        </form>
        <div class="footer">
            <div class="language-switch" role="group" aria-label="Language" data-i18n-aria="language">
                <button type="button" data-language="en" lang="en" aria-label="English" aria-pressed="true">EN</button>
                <button type="button" data-language="ru" lang="ru" aria-label="Русский" aria-pressed="false">RU</button>
            </div>
            <a id="backToControl" href="/" data-i18n="back">Back to control</a
            ><a
                class="author"
                href="https://xegare.com"
                target="_blank"
                rel="noopener"
                 data-i18n="author">by XEGARE</a
            >
        </div>
    </main>
    <script>
        const translations = {
            "en": {
                "language": "Language",
                "author": "by XEGARE",
                "pageTitle": "Settings | ESP32 PC Controller",
                "heading": "Controller settings",
                "subtitle": "Network and target computer",
                "ssid": "Wi-Fi network",
                "password": "Wi-Fi password",
                "ip": "Computer IP address",
                "show": "Show",
                "hide": "Hide",
                "save": "Save and restart",
                "back": "Back to control",
                "saving": "Saving...",
                "saved": "Saved. Restarting...",
                "saveError": "Failed to save settings"
            },
            "ru": {
                "language": "Язык",
                "author": "от XEGARE",
                "pageTitle": "Настройки | ESP32 PC Controller",
                "heading": "Настройки контроллера",
                "subtitle": "Сеть и управляемый компьютер",
                "ssid": "Сеть Wi-Fi",
                "password": "Пароль Wi-Fi",
                "ip": "IP-адрес компьютера",
                "show": "Показать",
                "hide": "Скрыть",
                "save": "Сохранить и перезапустить",
                "back": "К управлению",
                "saving": "Сохранение...",
                "saved": "Сохранено. Перезапуск...",
                "saveError": "Не удалось сохранить настройки"
            }
        }
        let language = "en"
        try {
            if(localStorage.getItem("esp32-pc-language") === "ru") language = "ru"
        } catch (error) {}

        function Translate(key) {
            return translations[language][key] || translations.en[key] || key
        }

        function SetText(element, key) {
            element.dataset.i18n = key
            element.textContent = Translate(key)
        }

        function ApplyLanguage() {
            document.documentElement.lang = language
            document.querySelectorAll("[data-i18n]").forEach(element => {
                element.textContent = Translate(element.dataset.i18n)
            })
            document.querySelectorAll("[data-i18n-aria]").forEach(element => {
                element.setAttribute("aria-label", Translate(element.dataset.i18nAria))
            })
            document.querySelectorAll("[data-language]").forEach(button => {
                button.setAttribute("aria-pressed", String(button.dataset.language === language))
            })
        }

        document.querySelectorAll("[data-language]").forEach(button => {
            button.addEventListener("click", () => {
                language = button.dataset.language
                try { localStorage.setItem("esp32-pc-language", language) } catch (error) {}
                ApplyLanguage()
            })
        })
        ApplyLanguage()
        function TogglePassword() {
            const input = document.getElementById("password")
            const button = document.querySelector(".toggle")
            const visible = input.type === "text"
            input.type = visible ? "password" : "text"
            SetText(button, visible ? "show" : "hide")
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
                SetText(button, "saving")
                const saveConfigUrl = new URL("/save_config", window.location.origin);
                try {
                    const response = await fetch(saveConfigUrl, {
                        method: "POST",
                        body: new FormData(event.target)
                    })
                    if(!response.ok) throw new Error(`HTTP ${response.status}`)
                    SetText(button, "saved")

                    const mainPageUrl = new URL("/", window.location.origin);
                    setTimeout(async () => {
                      while (true) {
                        try {
                          const response = await fetch(mainPageUrl, {
                            cache: "no-store"
                          })

                          if (response.ok) {
                            window.location.href = mainPageUrl
                            return
                          }
                        } catch (e) {
                        }

                        await new Promise(resolve => setTimeout(resolve, 1000))
                      }
                    }, 2000)
                } catch (error) {
                    button.disabled = false
                    SetText(button, "save")
                    alert(Translate("saveError"))
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

void HandleStatusLED()
{
  static unsigned long cycleStarted = 0;
  static int previousMode = -1;
  const unsigned long now = millis();

  const int mode = WiFi.status() != WL_CONNECTED ? 0 : (LastPingResult ? 1 : 2);

  if(mode != previousMode)
  {
    cycleStarted = now;
    previousMode = mode;
  }

  if(mode == 0)
  {
    digitalWrite(STATUS_LED_PIN, LOW);
    return;
  }

  const unsigned long elapsed = now - cycleStarted;
  bool ledOn;
  if(mode == 1)
  {
    ledOn = (elapsed % 2000) < 1000;
  }
  else
  {
    const unsigned long burstDuration = (LED_BLINK_COUNT * 2 - 1) * LED_FAST_BLINK_MS;
    const unsigned long phase = elapsed % (burstDuration + LED_PAUSE_MS);
    ledOn = phase < burstDuration && (phase / LED_FAST_BLINK_MS) % 2 == 0;
  }
  digitalWrite(STATUS_LED_PIN, ledOn ? LOW : HIGH);
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

  HandleStatusLED();

  delay(10);
}