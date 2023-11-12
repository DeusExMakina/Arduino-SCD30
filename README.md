# Arduino-SCD30
Implementation made for Arduino REV4 Wifi + SCD30 + LCD + 4 touch buttons
![](https://github.com/DeusExMakina/Arduino-SCD30/blob/main/Arduino%20screenshot.png)

# Hardware
## Mandatory
- Arduino REV4 Wifi
- Grove - CO2 & Temperature & Humidity Sensor (SCD30)
## Optional
- Grove - 16x2 LCD
- Grove - Touch Sensor
Remove or change code to adapt your configuration.
## Comments
Wifi may be replaced by Ethernet.

# Software
## Front-End
The code use [Google Charts](https://developers.google.com/chart/interactive/docs/gallery/linechart) to render charts.
## Storage
Data are stored on SRAM.
Plug a SD Card may be a good option.
EEPROM is not viable.
## About the code
I used to write C 20 years ago.
I am not satisfied from read_time() procedure. Time is displayed with only one digit (Eg. 1:8:4 instead of 01:08:04) and string management seems a bit crappy.
