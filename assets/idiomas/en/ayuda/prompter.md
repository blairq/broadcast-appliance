## How it is built
The text is shown by a web page: `/prompter/` is the speaker's screen (read only) and `/prompter/#control` is the operator's control (password protected). The state —which text, where it is, at what speed— lives in the gateway, not on the tablet: if something restarts, the speech continues where it was.

Speeches are text files in `Discursos/`, INSIDE the open event's folder, with subfolders if needed (`Apertura/intendente.txt`). They are the SAME ones loaded by the rundown's `teleprompter` cue: there is no separate library.

## What they'll call you for
Almost always for the same thing: **"this speech runs nine minutes and I have six"**. The path is `prompter_leer`, count, rewrite and `prompter_escribir`. At 150 words per minute (a normal reading pace in English), six minutes is about 900 words. `prompter_documentos` tells you how long each one runs without opening it.

## How to write a text that will be read aloud
- **A blank line separates paragraphs**, and the control can jump from one to another: it's best for each paragraph to be one idea, so the operator can skip one if it has to be shortened live.
- **No markdown**: no asterisks, no hashes, no bullet dashes. On the tablet it shows exactly as written.
- Short sentences. What reads well on paper doesn't always sound good spoken.
- Round numbers, written the way they're pronounced ("three thousand five hundred", not "3500").

## While someone is reading it
`prompter_control` starts and stops the text, changes the speed, jumps and writes notes to the speaker ("Wrap up"). **On the other side there is a person reading aloud**: starting or stopping the text without being asked is noticed on air instantly. If the speaker is slower than the text, lower the speed 10 wpm at a time instead of stopping it.

`prompter_estado` also says which screens are connected: before someone steps up to the lectern, what matters is that there is at least one `orador` view. If there's none, the tablet is off or nobody opened the link, and that gets reported NOW, not when the speaker is standing there.