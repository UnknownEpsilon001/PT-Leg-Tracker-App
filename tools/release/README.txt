Med-Flex Auto Smart release
=====================

What is in this folder

  app\MedFlexAutoSmart.apk         Android app (debug build). Copy to the phone and install it.
  firmware\controller\        Controller ESP32 firmware (CP210x USB chip)
  firmware\display\           CYD display firmware (CH340 USB chip)
  sd-card\                    Copy everything inside to the root of the CYD's microSD card
  server\                     FastAPI server. Run start-server.cmd on the clinic PC.
  upload.cmd                  Flashes the firmware onto whichever boards are plugged in
  load-sounds.cmd             Copies sd-card\sounds onto the CYD's SD card in this PC's reader
  esptool.exe                 Espressif flasher used by upload.cmd (see Licenses below)
  build-info.txt              The commit and date this release was built from

Flashing the boards

  1. Pull the UART jumpers between the controller and the CYD.
  2. Plug in one board or both over USB.
  3. Double-click upload.cmd. It finds each board by its USB chip and asks
     before writing anything.
  4. Put the jumpers back and power both boards.

  Options, from a command prompt in this folder:
    upload.cmd -Board controller
    upload.cmd -ControllerPort COM4 -DisplayPort COM5
    upload.cmd -Erase        wipes the chip first; Wi-Fi settings come back from the SD card

  The upload leaves the settings stored on the boards alone.

SD card

  Put the CYD's microSD card in this PC and double-click load-sounds.cmd. It
  finds the card, copies the four spoken cues into \sounds and checks each copy;
  nothing else on the card is touched. The card must be FAT32.

  sd-card\sounds\ holds the four spoken cues. Device settings live in
  settings.json on the same card; its format is in firmware/README.md in the
  source repository ("Device settings live on the card").

Server

  Needs Python 3. The first run of start-server.cmd creates server\.venv and
  installs the requirements, which needs internet. The server listens on port
  8000 on every network interface, so the phone and the device can reach it at
  http://<this PC's IP>:8000. Data is stored in server\ptlt.sqlite3.

Licenses

  esptool.exe is esptool by Espressif Systems, GPL-2.0 (esptool-LICENSE.txt).
  Source: https://github.com/espressif/esptool
