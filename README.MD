ESP32-C3 ARGB PC Cooling Fan Controller

**Warning:** The code here is AI-generated; it might not adhere to the best standards of coding.

---

**Purpose**
To control two ARGB fans via a web interface using the ESP32-C3.

**Implementation**
The code below allows access to the ESP32 web server (the IP address is printed to the serial port). The interface provides sliders to control fan speeds and RGB color pickers for the fan LEDs. The design is inspired by *The Matrix*.

**Under the Hood**

* Fans are driven by 25 kHz PWM.
* RPM is measured via interrupts.
* LEDs are controlled as addressable LEDs using the Adafruit library.

**Learnings**

* ESP32 pins are highly sensitive to PWM and interrupts; choose them wisely.
* The current pin configuration yields incorrect RPM readings when the ESP32 is connected to a PC over serial.
* Reading the tachometer signal is very tricky—harder than it sounds.
* A robust power supply is critical. One USB port can’t handle all the current. I’m using two USB ports: one for the ESP32 and RGB LEDs, and another (with a DC–DC booster) for the 12 V fans.
* Three power options:

  1. Use a dedicated power supply
  2. Use USB-C with Power Delivery
  3. Use two USB-A ports
* Use 4-pin connectors for the motor and 3-pin connectors for the LEDs to simplify hardware changes.

**Future Work**

* Identify pins that provide reliable RPM readings.
* Integrate LED effects with the XeWe LED OS.
* Add support for a thermal sensor.
