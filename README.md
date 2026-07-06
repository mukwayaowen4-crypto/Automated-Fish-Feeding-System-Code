Project Title: AUTOMATED FISH FEEDING SYSTEM

Problem: 
* High Operational Costs: Fish feed accounts for over 60% of total production expenses. Manual broadcasting relies heavily on human error, leading to substantial financial losses through overfeeding.
* Water Quality Degradation: Overfeeding results in unconsumed feed pellets decomposing at the bottom of ponds, drastically reducing dissolved oxygen levels and increasing toxic ammonia.
* Stunted Growth Cycles: Underfeeding or inconsistent manual feeding intervals disrupt the strict metabolic timelines required for juvenile fish, elongating production cycles and delaying time-to-market.
* Infrastructure Vulnerabilities: Unreliable grid electricity (frequent blackouts) disables standard electrical farm equipment, while a lack of remote monitoring forces farmers to manage ponds completely "blind," risking unnoticed feed depletion.

 The Solution: Automated Fish Feeding System (AFFS)
* Intelligent Life-Stage Scheduling: Utilizing an Arduino Uno paired with a high-precision DS3231 Real-Time Clock (RTC), the system features a manual SPDT growth-stage selector switch. Farmers can instantly toggle between Fingerling Mode (triggering automated dispensing exactly 4 times a day for rapid juvenile growth) and Grower Mode (dropping frequency to 3 times a day for mature fish) without rewriting a single line of code.
* Autonomous Solar Backup Matrix: To survive severe power grid failures, the system features an integrated Solar Photovoltaic (PV) backup array. An automated SPDT relay switching circuit continuously monitors the grid rail; upon power failure, it switches the system load to solar battery backup within 14 milliseconds, maintaining zero-downtime uptime without resetting the microcontroller.
* Sensing & Remote Cellular Telemetry (Inventory Block): An HC-SR04 ultrasonic sensor continuously calculates the remaining feed volume percentage via acoustic time-of-flight mapping. The moment stock falls below a 15% safety threshold, the system bypasses standard routines and commands a SIM800L GSM module to transmit an immediate cellular alert "feed is low, refill" directly to the farmer’s mobile phone, eliminating the need for internet dependency or complex mobile applications.

SETUP INSTRUCTION
 1. Install (Software & Hardware)
* Software: Download the [Arduino IDE](https://www.arduino.cc/en/software) and install the RTClib library via the Library Manager.
* Hardware: Connect the modules to the Arduino Uno pins: Ultrasonic (`D2/D3`), GSM (`D4/D5`), SPDT Stage Switch (`D6/D7`), Relay (`D8`), Motor Driver (`D9`), and RTC (`A4/A5`).
* *Note: Power the GSM module and motor using a separate external or solar power rail, sharing a common ground with the Arduino.*

2. Configure
* Insert an active SIM card into the GSM module with SMS bundle actvited.
* Open the `.ino` firmware file in the Arduino IDE and update your recipient phone number:
const String USER_PHONE_NUMBER = "+256XXXXXXXXX"; // Your phone number.
* (Optional) Adjust the `EMPTY_HOPPER_DISTANCE` constant (default is `12.5` cm) to match your physical hopper depth.

3. Run
* Connect your Arduino Uno to your PC via USB.
* Go to Tools, select Board: Arduino Uno, and choose your active Port.
* Click Upload (`Ctrl + U`).
* Open the Serial Monitor at 9600 Baud to verify system boot diagnostics (`System Ready`). Flip the physical SPDT switch or block the sensor to instantly test the dual scheduling loops and the *"feed is low, refill"* SMS notification.
