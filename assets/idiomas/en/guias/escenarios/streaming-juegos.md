# Game streaming (streamer)

## What it's for
A streamer plays live: the game capture, their camera in a corner, the audience chat on
screen and alerts. Game audio and microphone balanced.

## Questions of its own
- Which platform (Twitch, YouTube, Facebook)? Is the account connected?
- Which game and how is it captured (window, screen, console through a capture card)?
- Streamer camera? With the background keyed out (chroma)?
- Chat on screen? Highlight messages? Follower alerts?
- Background music (watch out for copyright)?

## What to build
- Scenes: “Game” (capture + small camera + chat), “Just chatting” (big camera), “Break”, “Start”.
- Chat on screen (chat_overlay_add) with the platform's chat (twitch_chat / youtube_chat).
- Audio: game and microphone with their levels; the microphone with a compressor and noise gate.
- Stream (twitch_live_start / youtube_live_create) with title and category.

## Check
Test stream; that the chat shows up; levels (the game doesn't drown out the voice).

## Hand over
The channel link and the chat card for highlighting messages.
