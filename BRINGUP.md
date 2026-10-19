# Revision D prototype acceptance

This repository contains hardware, not application firmware. Passing ERC/DRC does
not prove an assembled board works. Validate one assembly before a larger batch.

1. Inspect U4/U5 pin 1, J4 orientation, Q2–Q5, USB-C, LED polarity, wheel seating
   and the filled/capped thermal pads. Compare `fab/assembly-pin1.csv` and drawings.
2. With no power, check for hard shorts on VBUS, VBAT, VSYS and 3V3. J2 pin 1 is
   positive, pin 2 is ground. Check the actual battery lead with a meter before
   plugging it in; JST housings alone do not establish polarity.
3. Start with a current-limited bench source at J2, 3.8 V and 100 mA, USB absent,
   switch OFF. Verify no sustained 3V3 output. Increase the current limit to 1 A
   for boot/load tests, turn ON, and verify regulated 3.3 V before fitting the panel.
4. Sweep the battery source from 4.2 to 3.0 V while applying realistic 3V3 load
   bursts up to 400 mA. Scope the rail for droop/oscillation and check temperatures.
   Verify switch OFF disconnects the rail. Actual low-battery shutdown belongs in
   firmware; a protected battery is still required to prevent deep discharge.
5. Use USB with a protected battery attached. At GPIO6/7 low, input must stay at
   or below the 100 mA mode limit. After valid USB configuration and a 500 mA
   allocation, EN1 high/EN2 low permits USB500. Charge current is about 223 mA
   only when enough input budget remains. Both high select suspend. Never charge
   a battery using a bench supply as a substitute cell that cannot sink current.
6. Test USB flashing in both cable orientations. Hold BOOT, tap RESET, release
   BOOT for ROM recovery. Native USB is GPIO19/20; UART fallback is TP1/TP2 with
   TP3 ground, using 3.3 V logic only.
7. Fit the panel with power OFF and pin 1 aligned per `MECHANICAL.md`. Use a
   GDEY0213B74/SSD1680 driver with the pin map in `DESIGN.md`; perform full black,
   white and checkerboard refreshes. Check BUSY returns and boost voltages match
   the panel specification. Do not probe the boost nodes with the battery loose.
8. Configure button pull-ups. Read GPIO1 after 250 ms, convert its calibrated
   voltage ×2, and compare with a meter. For the wheel, set GPIO5 high, wait 1 ms,
   sample GPIO4 with maximum ADC attenuation, then set GPIO5 low. Calibrate end
   points. Application firmware should publish only after actual wheel movement.
9. Exercise Wi-Fi and screen refresh together near low battery; measure charging
   temperature, sleep/off current and radio performance with the intended case.

Record measured results, instrument conditions and firmware revision. No bench
results have been recorded yet. Cell temperature sensing is absent (fixed TS bias);
charge only within the selected pack's allowed ambient temperature range.
