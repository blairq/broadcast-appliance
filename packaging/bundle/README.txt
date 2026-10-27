BROADCAST APPLIANCE — README
============================

Stream and record live, with remote guests who join with a link. Everything
runs from this folder (it works from a disk or a USB stick): nothing gets
installed.

HOW TO OPEN IT
--------------
Windows 10 (1809 or later) / 11, 64-bit
  1. Unpack the whole .zip into a folder (don't open it from inside the zip).
  2. Double-click BroadcastAppliance.exe.
  3. If Windows shows "Windows protected your PC": More info -> Run anyway.
     If the firewall asks, allow access on private networks (guests need it
     to get in).

64-bit Linux (desktop with OpenGL)
  1. Unpack the .tar.gz:  tar xzf BroadcastAppliance-*-linux-x64.tar.gz
  2. Open a terminal in the folder and run:  ./BroadcastAppliance
     (if it says "permission denied":  chmod +x BroadcastAppliance bin/64bit/*)
  Linux won't run programs from a FAT/exFAT USB stick: copy the folder to an
  ext4/NTFS disk, or mount the stick with execute permission.

FIRST STEPS (10 minutes)
------------------------
  1. Inputs (◉): add your camera and your microphone.
  2. Scenes (▦): build a scene with the camera and a title.
  3. YouTube / Twitch / Facebook: each tab explains step by step how to connect
     your account. For any other destination, "Go live" asks for an RTMP server
     and a stream key (the key is stored encrypted).
  4. Do a test RECORDING of one minute before your first live show: it ends up
     in Recordings/.
  5. Room (☎): copy the link or show the QR code, give your guest the PIN and
     admit them from the waiting room.

WHERE EVERYTHING GOES (all inside this folder)
----------------------------------------------
  Recordings/     recordings
  Config/         your settings and scenes
  Credenciales/   accounts and keys, encrypted. Whoever has the whole folder
                  has access: take care of the disk.
  Respaldos/      backups of Config/ and Credenciales/ before each update
  Logs/           logs for support (app.log, launcher.log, ba.log)
  Shares/, Media/ files shared by guests, and your media

UPDATING
--------
Unpack the new version into a DIFFERENT folder and pick it in Settings ->
System and data -> Update from a folder. Your settings, credentials and
recordings are left untouched, and you can go back to the previous version.

IF SOMETHING GOES WRONG
-----------------------
  - It doesn't open or stays black: you need a desktop with graphics
    acceleration (an up-to-date video driver). Check Logs/app.log.
  - The guest sees "Your connection is not private": that's normal with a link
    by IP address; they should click Advanced -> Proceed. There's a guide to
    hand them in Web/guest/ayuda.html
  - To ask for help, send the Logs/ folder (don't send Credenciales/).

Full manual: docs/en/manual/ at https://github.com/blairq/broadcast-appliance
(in Spanish: LEEME.txt and docs/es/manual/)
License: GPL-3.0 (see LICENSE and data/licencias/).
