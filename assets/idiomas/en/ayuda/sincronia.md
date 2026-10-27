## The idea
**Synchronized** inputs (class `sala`: the venue's cameras, capture cards and microphones) come out aligned with each other and with the audio: the studio waits for the slowest one. That's why the studio latency is set by ONE input (the slowest of the synchronized ones): to lower it you have to improve that one. Inputs of class `propia` (WebRTC guests, remote session, windows, shared screens) don't drag the others along: if they arrive late their timing is shifted, audio and video together.

The latency **doesn't move with every scene cut**: changing it live freezes or skips the outputs. Live it only goes up (if requested steadily for 2 s, once, with a notice); it comes down on its own with nothing on air, or on request.

## How to work
1. `estudio_latencia`: the set latency, `la_marca` and each input with its class, what it asks for and how long it takes.
2. If it's set by something that doesn't need to be aligned with the venue (a guest, a window, a camera shooting something else): `sincronia_clase {"entrada", "clase": "propia"}` (off air: the input is rebuilt).
3. If it's set by a venue camera: `tiempos_pipeline {"entrada"}` shows whether it's the capture (try another mode: `video_devices {"device"}`) or a card.
4. With the new setup running and nothing on air, it comes down on its own; if not, `estudio_latencia {"bajar": true}` (with something on air there's a jump: tell the operator).
5. Check `cuadros_tarde_60s` in estudio_latencia: it has to be 0. Each input asks for its p90 age + its jitter + 5 ms, and the video compositor adds only 5 ms (`video.margenComposicionMs`); if late frames show up, that input jitters more than it asked for.
6. To check that two cameras ended up aligned: a clock (a phone's, with milliseconds) in front of both and `video_sincronia {"a", "b"}`: the offset has to be less than one frame.

## Tell the operator
With numbers: “the studio latency is 95 ms and it's set by Camera 2 (capture 75 ms); the guest runs with their own delay, 40 ms more”.