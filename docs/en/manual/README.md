# Operator manual

For people who **use** Broadcast Appliance (not for people who build it). If you
just downloaded it, start with `README.txt`, which comes inside the program
folder.

- [Guest guide](guest-guide.md) — a one-pager to hand to whoever is joining.
- *En español:* [Manual del operador](../../es/manual/README.md).

## 1. What it is and what you need

A portable live broadcast studio: scenes with cameras and graphics, processed
audio, remote guests who join with a link, recording, and output to YouTube,
Twitch, Facebook or any RTMP server. It runs from a folder (a disk or a USB
stick) without installing anything.

- **Windows** 10 (1809 or later) or 11, 64-bit. A recent 64-bit **Linux** (the
  published build is compiled on Ubuntu 24.04) with a desktop and OpenGL.
- A graphics card with an up-to-date driver (it encodes in hardware; if it
  can't, it falls back to x264 on the CPU).
- An internet upload speed of at least **twice** the bitrate you plan to stream.
- Headphones for monitoring.

## 2. Your first live show in 10 minutes

The bar on the left holds the pages: ⌂ Dashboard · ▦ Scenes · ◉ Inputs ·
🎚 Audio · ☎ Room · ▶ YouTube · ◆ Twitch · f Facebook · ⚙ Settings ·
✦ Assistant.

1. **Inputs (◉):** add the camera (USB, capture card, IP camera or NDI) and the
   microphone. Every input shows a live thumbnail.
2. **Scenes (▦):** add the camera and a title to the "Main" scene. Drag to move
   things and pull a corner to resize them.
3. **Destination:** on YouTube, Twitch or Facebook, follow the guide on that tab
   to connect your account (about 5 minutes, only once). For any other service,
   "Go live" asks for an RTMP server and a stream key; the key is stored
   encrypted, so next time you can leave it blank.
4. **Do a test recording** of one minute and watch it in `Recordings/` before
   your first live show.
5. **Go live.** At the top of every page you'll see "ON AIR" with the elapsed
   time, plus "REC" if you're recording.

## 3. Safeguards while you're on air

- Closing the window, updating, deleting a scene or an input, stopping the
  stream or stopping the recording all **ask for confirmation**. If the scene
  you're about to delete is on air, the warning says so.
- If the stream or the recording **stops on its own** (internet, server, full
  disk), a red warning stays in the bottom bar until you dismiss it.
- If the program crashes, it restarts by itself and **resumes** the recording
  and the stream that were running.
- If it starts in **safe mode** (after several crashes in a row), your scenes
  aren't loaded, but they aren't lost either: close it and open it again. A copy
  is kept in `Config/app.json.antes-del-modo-seguro`.

## 4. Audio

The mixer on the Dashboard has the volume and mute for each source. The Audio
page (🎚) builds a chain of modules for each source; the usual one is **gate**
(removes noise when nobody is talking) → **compressor** (evens out the level) →
**limiter** (stops peaks). The analyzer measures loudness in **LUFS**: for live
speech aim for −16 to −14. Red on the meters means it's clipping: turn it down.

## 5. Guests (Room ☎)

1. In the **Access** panel, copy the link (the room is already in it) or show
   the QR code, and give the guest the **PIN** through some other channel. There
   is a button to generate a new PIN (the old one stops working).
2. The guest lands in the **waiting room**: you can see them, talk to them
   privately, and **admit** or reject them.
3. Once they're in, their "Guest N" sources can be added to a scene like any
   other source. Guests can share their screen, send files, chat and raise
   their hand.
4. **Speakers mode:** a queue of turns with a time limit per person and a clock
   everyone can see.
5. **Access from outside your network:** through a Cloudflare tunnel or through
   your own server with WireGuard (there's a setup assistant on the same page).
   Inside the local network the link uses an IP address, and the guest's
   browser warns "Your connection is not private": that's expected, and the
   [guest guide](guest-guide.md) explains how to get past it.

The switches (waiting room, room closed, speakers mode) always show the
**actual** state: if someone changes it from the web console or the assistant,
they update on their own.

## 6. Streaming and recording

| Quality | Suggested video bitrate | Minimum upload |
|---|---|---|
| 720p30 | 2500–4000 kbps | 8 Mbps |
| 1080p30 | 4500–6000 kbps | 12 Mbps |
| 1080p60 | 6000–9000 kbps | 18 Mbps |

With the encoder on "auto" it picks whatever hardware is available and falls
back to x264 if that fails. Recordings are MKV on purpose: if the power goes
out, the file is still usable. Facebook only allows streaming to Pages, not to
personal profiles.

## 7. Updates, backups and moving

- **Updating:** unpack the new version into a different folder and pick it in
  Settings → System and data. Before switching, `Config/` and `Credenciales/`
  (your credentials) are backed up into `Respaldos/` (the last 5 are kept). You
  can go back to the previous version. The installed version is shown in that
  same section.
- **Moving to another disk:** copy the whole folder. `Credenciales/` is
  encrypted, but the key travels in the same folder: whoever has the disk has
  access.

## 8. AI assistant (✦)

A chat that can see and drive the app (Claude Code, Gemini CLI or opencode,
with your own account). Whatever guests type is never run on its own: if the
assistant has read third-party content, every action asks for your approval
with a click.

## 9. Troubleshooting

| Problem | What to do |
|---|---|
| It doesn't open, or the window is black | You need a desktop with graphics acceleration. Update the video driver. Check `Logs/app.log`. |
| The camera doesn't show up | Close other programs that may be using it; add it again in Inputs. |
| Echo | The guest needs to wear headphones. |
| The guest can't get in | Right PIN, room open, and they accept the browser warning (see the guest guide). |
| The picture gets blocky or drops | Lower the bitrate; use a cable instead of Wi-Fi. |
| "Recovery" window (Windows) | It only shows up after several crashes in a row: click "Retry". If it keeps happening, send `Logs/`. |

To ask for help, send the `Logs/` folder (`app.log`, `launcher.log`, `ba.log`).
**Never** send `Credenciales/`.

## 10. Glossary

- **Bitrate:** how much data is sent per second; more means a better picture and more upload needed.
- **RTMP:** the standard way to send live video to a platform (server + key).
- **Stream key:** the password for your live stream; whoever has it can broadcast on your channel.
- **Waiting room (lobby):** where the guest waits until you admit them.
- **Gate / compressor / limiter / de-esser:** audio modules: they mute the noise / even out the level / stop peaks / soften the "s" sounds.
- **LUFS:** a measure of perceived loudness.
- **NDI / SRT / RTSP:** ways of bringing video in over the network.
- **Tunnel (Cloudflare) / WireGuard:** ways for a guest to join from outside your network.
- **Safe mode:** a minimal startup after several crashes, without loading the scenes.
