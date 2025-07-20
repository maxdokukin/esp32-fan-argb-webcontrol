ESP 32 C3 ARGB PC Cooling Fan Controller

Warning: the code here is AI generated, it might not be to the best stanndarts of coding.

Purpose:
to control two argb fans via the web interface using esp 32 c3

implementation:
code below allows to access the esp 32 web server (ip address printed in the serial port)
the interface has sliders to control the fans speeds and rgb color picker s for the fan rgb colors
desiign is inspired by the matrix

under the hood:
fans are controlled by 25khz pwm
rpm is measured through interrupts
led is controlled as addressable led via with adafruit lib

learnings:
esp32 pins are highly sensitive to pwm, and interrupts. pins must be chosen wisely.
the current pins configuration causes incorrect rpm readings when the esp32 board is attached to PC with serial port
reading tachometer is very tricky and harder than it sounds
power supply ius extremely important here. one usb port will not handle all the current. i am using two usb ports, one for esp and rgb, two for fans with dc-dc booster to 12v
three ways to go around it: use power supply only, use usbc with power delivery, use two usb a ports
use cvonnectors to attach fans; 4 pin for the motor and 3 pin for the led. they will make the hardware refurbrishments and transitions way easier

future vectors:
find pins that provide reliable rpm readings
integrate led effects with the XeWe LED OS
add thermo sensor support 
