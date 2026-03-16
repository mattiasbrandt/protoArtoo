### ESP32 Communication to RC Servos

ESP32 microcontrollers can control RC servos using PWM signals generated via the LEDC (LED Control) peripheral, which supports up to 16 channels for precise timing. Standard servos like MG996R and MG90S require a 50Hz PWM signal (20ms period) with pulse widths from 1000µs (0°) to 2000µs (180°), though actual range varies by model. Libraries like ESP32Servo simplify integration, mapping angles to pulses. For multiple servos, use separate power supplies to avoid brownouts, as ESP32's 3.3V output can't handle servo current draw. Calibration involves testing min/max pulses for accurate positioning, accounting for mechanical limits.

#### MG996R Specifications
The MG996R is a high-torque metal gear servo suitable for robotics and RC vehicles, offering robust performance but higher power consumption.
- **Weight**: 55g
- **Dimensions**: 40.7 × 19.7 × 42.9 mm
- **Stall Torque**: 9.4 kg/cm (4.8V), 11 kg/cm (6V)
- **Operating Speed**: 0.19 s/60° (4.8V), 0.15 s/60° (6V)
- **Operating Voltage**: 4.8V–6.6V (5V typical)
- **Current Draw**: Idle ~10mA, No-load ~170mA, Stall ~1400mA (6V)
- **Gear Type**: Metal (dual ball bearing)
- **Rotation Range**: Approximately 120° (60° each direction; limited by internal stops)
- **Pulse Width Range**: 1000–2000µs (neutral ~1500µs); some variants extend to 544–2400µs for 0–180°
- **Connector**: JR/Futaba compatible (brown: GND, red: V+, orange: signal)
- **Dead Band Width**: 1µs

#### MG90S Specifications
The MG90S is a compact metal gear micro servo for lighter applications like small drones or animatronics, with lower torque but faster response.
- **Weight**: 13.4g
- **Dimensions**: 22.8 × 12.2 × 28.5 mm
- **Stall Torque**: 1.8 kg/cm (4.8V), 2.2 kg/cm (6V)
- **Operating Speed**: 0.1 s/60° (4.8V), 0.08 s/60° (6V)
- **Operating Voltage**: 4.8V–6V (5V typical)
- **Current Draw**: Idle ~5–6mA, No-load ~90mA, Stall ~750mA (6V)
- **Gear Type**: Metal (double ball bearing)
- **Rotation Range**: 0°–180°
- **Pulse Width Range**: 500–2500µs (neutral 1500µs)
- **Connector**: JR (brown: GND, red: V+, orange: signal)
- **Dead Band Width**: 1µs

#### Generic Servo Details
- **PWM Signal**: Frequency 50Hz (20ms period); pulse widths 1000–2000µs for 0°–180° (neutral 1500µs). Some servos tolerate 30–100Hz, but 50Hz is standard.
- **Voltage Levels**: Signal is 3.3V–5V tolerant; ESP32's 3.3V GPIO works directly for most (e.g., MG996R/MG90S).
- **Power**: 4.8V–6V; current spikes during movement/stall (up to 1–2A for MG996R).
- **Rotation**: Position proportional to pulse width; continuous rotation variants (e.g., MG996R 360°) use pulse for speed/direction.
- **Compatibility**: Works with Arduino/ESP32 via libraries; ESP32 LEDC handles multiple channels without blocking.

#### ESP32 Communication
- **LEDC PWM Setup**: Use up to 16 channels; configure frequency (50Hz), resolution (e.g., 16-bit for fine control), and duty cycle (map angles to pulses).
- **Library Usage**: ESP32Servo.h adapts Arduino Servo library; attach servo to GPIO (e.g., servo.attach(pin, minPulse=1000, maxPulse=2000)).
- **GPIO Selection**: Avoid strapping pins (e.g., GPIO0,2,15 for boot); use any non-conflicting GPIO.
- **Multi-Servo**: Assign different LEDC channels; max 16 simultaneous.

#### Best Practices
- **Power Separation**: Always use external 5V supply for servos (e.g., UBEC or battery); connect ESP32 GND to servo GND for common reference. Avoid powering from ESP32's 3.3V/5V pins—causes resets/brownouts.
- **Wiring**: Short wires to minimize noise; add 100–470µF capacitor across servo power/GND for stability.
- **Calibration**: Map pulses to angles empirically; start with library defaults, adjust min/max via servo.attach(pin, minUs, maxUs). Test in code: sweep 0–180° and note physical limits to avoid straining gears.
- **Jitter Reduction**: Use stable power; disable WiFi during critical ops if interference; set higher resolution (16-bit) for smoother control.
- **Failsafe**: Detach servo (servo.detach()) when idle to save power/prevent humming.
- **Testing**: Use simple sweep code; monitor with oscilloscope/multimeter for pulse accuracy.

#### Important Gotchas and Calibration Issues
- **Jitter/Humming**: Caused by WiFi interrupts or unstable power; use dedicated timers/channels or external PWM (e.g., PCA9685). Calibration: Increase deadband if needed.
- **Brownouts/Resets**: High current draw overloads ESP32 regulator; always separate power. Gotcha: ESP32 may reboot on stall.
- **Pulse Mismatch**: Default 500–2500µs may overdrive servos (e.g., MG996R limited to 120°); calibrate to actual range (e.g., 1000–2000µs) to prevent damage/noise.
- **GPIO Conflicts**: Some pins (e.g., 34–39 input-only) can't output PWM; avoid boot pins for servos.
- **Calibration Drift**: Mechanical wear or voltage variations; recalibrate periodically. Gotcha: ESP32's 3.3V signal may not fully drive 5V servos—use level shifter if weak response.
- **Overheating**: Stall current spikes; add timeouts in code to prevent prolonged stalls.
- **Library Issues**: ESP32Servo required (not standard Servo.h); ensure correct min/max in attach() to match servo specs.

### Sources
1. [MG996R High Torque - Metal Gear Dual Ball Bearing Servo - Electronicos Caldas](https://www.electronicoscaldas.com/datasheet/MG996R_Tower-Pro.pdf)
2. [MG996R Metal Gear Servo Motor - Handson Technology](https://www.handsontec.com/dataspecs/motor_fan/MG996R.pdf)
3. [MG996R - Tower Pro](https://towerpro.com.tw/product/mg996r)
4. [SERVO MOTOR MG996R 11KG DATA](http://archive.communica.co.za/Content/Catalog/Documents/D0263792703.pdf)
5. [MG996R Servo Motor Specifications | PDF | Home & Garden](https://www.scribd.com/doc/251702754/MG996R-Tower-Pro)
6. [MG996R Servo, Metal Gear, High Torque - Waveshare](https://www.waveshare.com/mg996r-servo.htm)
7. [TowerPro MG996R Servo Specs and Reviews](https://servodatabase.com/servo/towerpro/mg996r)
8. [MG996R Datasheet (PDF) - ALLDATASHEET.COM](https://www.alldatasheet.com/datasheet-pdf/view/1131873/ETC2/MG996R.html)
9. [High Quality Servo Mg996r Datasheet Manufacturer and Supplier, Factory | Desheng](https://www.dspowerservo.com/servo-mg996r-datasheet)
10. [Servo Motor MG996 360 Degree Continuous Rotation - ProtoSupplies](https://protosupplies.com/product/servo-motor-mg996-360-degree-continuous-rotation)
11. [MG90S Micro Servo](https://makersportal.com/shop/mg90s-micro-servo)
12. [Micro Servo Motor MG90S - Tower Pro](https://www.electronicoscaldas.com/datasheet/MG90S_Tower-Pro.pdf)
13. [Servo Motor Micro MG90S - 360 Degree Continuous Rotation](https://protosupplies.com/product/servo-motor-micro-mg90s-continuous-rotation)
14. [MG90S Servo Motor Datasheet: Specs & Usage](https://studylib.net/doc/25404669/mg90s-datasheet)
15. [MG90S – Metal Gear Micro Servo Motor](https://components101.com/motors/mg90s-metal-gear-servo-motor)
16. [MG90S Datasheet (PDF)](https://www.alldatasheet.com/datasheet-pdf/view/1132104/ETC2/MG90S.html)
17. [MG90S Micro Servo Specifications | PDF](https://www.scribd.com/doc/263826763/MG90S-Tower-Pro)
18. [MG90S micro servo engine compatible with Arduino](https://www.az-delivery.de/en/products/mg90s-micro-servomotor)
19. [SHENZHEN SKY STAR TECHNOLOGY CO., LTD](https://www.tinytronics.nl/product_files/000263_Data%20Sheet%20of%20MG90S%20Analog%20Servo%20Motor.pdf)
20. [Servo Motor MG90S](https://fdm3d.co.za/products/mg90s-servo-motor)
21. [Using Servo Motors with the ESP32](https://dronebotworkshop.com/esp32-servo)
22. [Mastering ESP32 Servo Control: Easy Step-by-Step Guide](https://www.youtube.com/watch?v=GcOlBfT7UkM)
23. [ESP32 Servo Control: Multiple Servos & Smooth PWM Guide](https://zbotic.in/esp32-servo-control-multiple-servos-smooth-pwm-guide)
24. [PWM Servo Control Without Library : r/arduino](https://www.reddit.com/r/arduino/comments/1jewqwx/pwm_servo_control_without_library)
25. [How to Control Servo Motors with an ESP32](https://lastminuteengineers.com/esp32-servo-motor-tutorial)
26. [ESP32 S2 PWM Servo Control - Programming](https://forum.arduino.cc/t/esp32-s2-pwm-servo-control/1018599)
27. [Controlling Servo Motors With ESP32: A Beginner's Guide](https://iotwebplanet.com/controlling-servo-motors-with-esp32-a-beginners-guide)
28. [MCPWM RC Servo Control Example - espressif/esp-idf](https://github.com/espressif/esp-idf/blob/master/examples/peripherals/mcpwm/mcpwm_servo_control/README.md)
29. [ESP32C3 cannot drive a servo and pwm output correctly](https://forum.seeedstudio.com/t/esp32c3-cannot-drive-a-servo-and-pwm-output-correctly/292547)
30. [I got an rc car with a servo, now i try to make a light kit for ...](https://www.facebook.com/groups/esp8266microcontrollers/posts/1923256561457152)
31. [Servo not working with ESP32 - Motors, Mechanics, Power and CNC](https://forum.arduino.cc/t/servo-not-working-with-esp32/1113992)
32. [Servo Problems - ESPHome](https://community.home-assistant.io/t/servo-problems/684150)
33. [ESP32 Troubleshooting Guide](https://randomnerdtutorials.com/esp32-troubleshooting-guide)
34. [HELP! Servo with ESP32 and Arduino IDE not working properly (Jittering)](https://www.reddit.com/r/arduino/comments/12lx3rk/help_servo_with_esp32_and_arduino_ide_not_working)
35. [Servo calibration issues with robotic joints](https://www.facebook.com/groups/1247420012884582/posts/1454111475548767)
36. [Having problems with the libraries in the ESP32 controller](https://forum.arduino.cc/t/having-problems-with-the-libraries-in-the-esp32-controller/1236333)
37. [Can somebody tell me what's the problem? My servo motor don't move after I upload code into my esp32.](https://www.reddit.com/r/esp32/comments/uf7twi/can_somebody_tell_me_whats_the_problem_my_servo)
38. [ESP32Cam and Servo Control](https://esp32.com/viewtopic.php?t=11379)
39. [DC Servo Motor Guide - With ESP32 & Arduino](https://dronebotworkshop.com/servoguide)
40. [ESP32C3 cannot drive a servo and pwm output correctly - XIAO](https://forum.seeedstudio.com/t/esp32c3-cannot-drive-a-servo-and-pwm-output-correctly/292547)


### Servo Calibration in R2D2 Droid Context

In R2-D2 astromech droid replicas, servo calibration is essential for precise control of dome panels, holoprojectors, and body mechanisms, ensuring smooth animations like opening/closing sequences without mechanical stress or buzzing. Systems like MarcDuino (and derivatives such as AstroPixelsPlus or BetterDuino) use PWM pulse widths (typically 800–2200µs) to position servos, with calibration involving setting open/closed positions, reversing directions, and testing for individual variations due to manufacturing tolerances. Calibration often occurs via serial commands or web interfaces, focusing on MG90S or similar servos for panels. Key goals: Align panels flush when closed, achieve full open without overdriving, and incorporate easing for natural motion.

#### Calibration Process
1. **Enter Calibration Mode**: Send serial commands like 'c' (in some systems) or use web tools to access sliders for pulse adjustment. For MarcDuino V3, use #SD/#SR for direction setup first.
2. **Test Positions**: Use :MVxxdddd to move servo xx to pulse dddd (e.g., :MV011500 for neutral). Sweep from 800–2200µs to find physical limits—closed (~900µs), open (~2100µs), neutral (1500µs).
3. **Save Positions**: #SO saves open, #SC saves closed. Test with open/close sequences (e.g., :OPxx, :CLxx) to verify alignment.
4. **Reverse if Needed**: #SD00/01 for all servos (normal/reverse), or #SRxxy for individual (xx=servo number, y=0/1). Use #SWxx to swap open/closed during testing.
5. **Flutter and Easing**: For animations, enable flutter (:SFxx) and set easing (e.g., Ease In/Out Quad) via web interface for smooth transitions.
6. **Verify**: Run sequences like scream (:SE01) or wave (:SE02); adjust if panels misalign or buzz.

#### Specifics for Panels
- **Pie Panels (PP1–PP6)**: Individual open/closed calibration; flutter supported for effects like marching ants.
- **Lower Panels (P1–P10)**: Similar, with aliases for top/bottom groups; calibrate to flush dome alignment.
- **Servo Variations**: MG90S common; offsets stored in EEPROM. Initial offsets often -1/0; adjust per servo for 0–180° mapping.

#### Best Practices
- **Power Stability**: Use external 5V supply; add capacitors to reduce jitter.
- **Incremental Testing**: Start at neutral; increment 100µs steps to avoid damage.
- **Tools**: Use potentiometers or web sliders for fine-tuning; save to non-volatile memory.
- **Batch Calibration**: Calibrate all panels sequentially; group reverses if build symmetry requires.
