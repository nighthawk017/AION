# AION
Arduino day counter using an ESP NodeMCU and a LED matrix. AION shows the number of days left until a target date that you set from your phone or computer.

## Prerequisites
* NodeMCU V3 development board (ESP8266)
* MAX7219 dot LED matrix 8x32 (4 chained FC16 modules)
* Arduino IDE
* A 2.4 GHz WiFi network with internet access (AION gets the date from the internet)

## Wiring

Connect the LED matrix to the NodeMCU board:

| LED matrix | NodeMCU |
|------------|---------|
| VCC        | 3V      |
| GND        | G       |
| DIN        | D7      |
| CS         | D8      |
| CLK        | D5      |

![Diagram showing how to connect the LED matrix to the NodeMCU development board](resources/diagram.png)

## Arduino IDE setup

1. In **File > Preferences > Additional boards manager URLs**, add
   `https://arduino.esp8266.com/stable/package_esp8266com_index.json`.
2. In **Tools > Board > Boards Manager**, install **esp8266**.
3. Select the board **Tools > Board > ESP8266 Boards > NodeMCU 1.0 (ESP-12E Module)**.
4. In **Tools > Manage Libraries**, install **MD_Parola** and **MD_MAX72XX**. WiFi, the web server, EEPROM and time come with the board package.
5. The Arduino IDE only opens a sketch whose folder has the same name as the sketch file. The sketch is `aion.ino`, so clone the repository into a folder named `aion`:
   ```
   git clone https://github.com/nighthawk017/AION.git aion
   ```
   or rename the `AION` folder to `aion`.

Before uploading, you may want to change these settings at the top of `aion.ino`:
* `AP_PASSWORD`: the password of the `DATE_TIMER` network (`password` by default). Anyone who knows it can change the board's settings. It must be 8 to 63 characters long.
* `TIMEZONE`: the timezone used to decide when a new day starts (Central European Time by default). The comment next to it has examples.

## How to use AION

1. Upload the code to the NodeMCU development board via the Arduino IDE.
2. Once the code is uploaded, or after the board is reset, connect to the board's `DATE_TIMER` WiFi network (password `password`, unless you changed `AP_PASSWORD`) and open http://192.168.4.4 in your browser. The instructions are also displayed on the LED matrix.

   You have 3 minutes after each reset to do this. After that, if settings were saved before, AION connects to the internet with them on its own. If nothing was saved yet, it keeps waiting. To change the settings later, press reset and connect again within the 3 minutes.
3. Fill in the name and password of the WiFi network used to connect to the internet (at most 32 and 63 characters), and the date AION should count down to, then click Save. A field left empty keeps its saved value.
4. AION closes the `DATE_TIMER` network, connects to your WiFi and starts counting down the days until the target date. The settings are saved, so AION reconnects by itself after a power cut or reset.

## What the display shows

| Display | Meaning |
|---------|---------|
| "Connect to DATE TIMER WiFi access point, then access 192.168.4.4 in your browser." | Waiting for settings (see step 2). |
| "Incorrect wifi credentials. Connect to DATE TIMER WiFi access point, then access 192.168.4.4 to resubmit." | AION could not connect to your WiFi (it tries for about 100 seconds). The `DATE_TIMER` network is back; submit the form again. |
| "Syncing time..." | Connected, waiting for the date from the internet. |
| A number, e.g. `42` | Days left until the target date: `1` the day before, `0` on the day. After the target date the number goes negative (`-1`, `-2`, ...). |

If the WiFi connection drops while counting down, AION keeps counting and reconnects by itself.
