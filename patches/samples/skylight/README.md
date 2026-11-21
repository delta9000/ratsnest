# Skylight voice clips

The WAVs are not in git (`*.wav` is ignored). Regenerate them from the scripts
and voices below. A rerun may not match byte for byte; the sha256s identify the
takes the album was mixed with.

Original scripts written for the *skylight* album (2026-11-20), voiced by the
Kokoro-82M v1.0 text-to-speech model (weights Apache-2.0,
https://huggingface.co/hexgrad/Kokoro-82M; its card lists only permissively
licensed training audio) running on a local inference server. No third-party
recording is in these files. The PA system, the forecast and the interviewees
are fictional; "Skylight Galleria" is an invented mall.

Every clip was transcribed back with a speech-to-text model (parakeet-tdt-0.6b-v2)
and matched its script word for word. Files are the model's output unchanged:
mono 24 kHz 16-bit PCM (`sampler`/`grains` resample on playback). They read
about -21 to -28 dB RMS, peaks -2 to -10 dBFS.

| File | Voice | Speed | Duration | Script |
|---|---|---|---|---|
| `pa-open.wav` | `af_nicole` | 0.95 | 9.3 s | Good morning, and welcome to Skylight Galleria. Our stores are now open. Have a wonderful day. |
| `pa-foodcourt.wav` | `af_nicole` | 0.95 | 8.9 s | Attention shoppers. The food court is now serving lunch, on the lower level, beside the fountain. |
| `pa-closing.wav` | `am_michael` | 0.92 | 8.9 s | Attention shoppers. Skylight Galleria will be closing in fifteen minutes. Please bring your final purchases to the register. |
| `pa-security.wav` | `am_michael` | 1.0 | 5.8 s | Security to the atrium, please. Security to the atrium. There is water on the floor. |
| `pa-closed.wav` | `af_nicole` | 0.9 | 9.1 s | The mall is now closed. Thank you for shopping with us. Please exit through the north entrance. |
| `pa-last.wav` | `af_nicole` | 0.85 | 10.2 s | This is the last announcement. Skylight Galleria is closed. Thank you, for twenty-two years. |
| `wx-local.wav` | `am_eric` | 1.0 | 7.6 s | Your local forecast. Tonight, rain developing after sundown, heavy at times. Lows in the upper fifties. Winds out of the south, at ten to fifteen. |
| `wx-current.wav` | `am_eric` | 1.0 | 5.5 s | Currently, at the airport, it's sixty-one degrees, with light rain. The humidity is ninety-four percent. |
| `wx-flood.wav` | `am_eric` | 0.95 | 3.9 s | A flood watch remains in effect for the metro area, through tomorrow morning. |
| `iv-teen.wav` | `af_sky` | 1.05 | 5.9 s | We come here every Saturday. Like, there's nothing else to do. You just walk around, get a pretzel, see who's here. |
| `iv-mom.wav` | `af_bella` | 0.95 | 6.7 s | When it opened, it was like, the future. The skylight, the fountain. My kids learned to walk in this mall. |
| `iv-guard.wav` | `am_fenrir` | 0.9 | 6.8 s | Nights, it's real quiet. You can hear the fountain from the parking garage. Sometimes the music stays on. |
| `iv-owner.wav` | `am_adam` | 0.95 | 6.0 s | Business was good for a long time. Then everybody started going somewhere else. I don't know where they went. |
| `iv-escalator.wav` | `af_jessica` | 1.05 | 3.0 s | My favorite part is the escalator. It goes up forever. |
| `iv-walker.wav` | `am_echo` | 0.9 | 6.0 s | I walk the mall every morning. Three laps. It's warm, it's dry. It's nice, to be somewhere. |

sha256:

    7ae5b63e448707a3ae8a7ba08deed56a2b3f7f6c782ae7875dc72fa762f79838  pa-open.wav
    5034953b3dd42b08c446e76423421794c751a17095a3433510d418b098fbe3e8  pa-foodcourt.wav
    9cfbcad26a93837704634a999f6292f9e1c277f41d6a4a165839163cea248ed0  pa-closing.wav
    328efca2bcaed0f4bf3b9a37a1512eb60e74cf089734837df2512e1059dee333  pa-security.wav
    b0d931ec1669ebe33642dc40a0335d2d1860a875d9e349d72a823ed2e787f127  pa-closed.wav
    fcfdfaf6d937471050389e822f75aad448c1958e20070a3fd5ed02af25798db4  pa-last.wav
    7ef251a188e9a1e0ba41f69984b2697ab3b6527d70665efa927701385b883162  wx-local.wav
    73f589fa959b23a2f7288a5b226138e4c52e3b45a2a7ec7e05ceeea501fe6f48  wx-current.wav
    506a79120dd328fe1e7476c78ad1041f683dda2dc88d9610b127c13e655d4550  wx-flood.wav
    b2bbd9dc13749a6436711f45d7fbfe3a42966651238c49020c9b35f6a6b1315e  iv-teen.wav
    12c99ecf63b0584838cd2d18369281ade7b1fb2bb9125d5eb334f622843fd18b  iv-mom.wav
    35e6197240e843693fe3195037d1a132ff2435d1c29f2b7f48f50b117875298a  iv-guard.wav
    e761af7755c79e18a12c1223fbffaa5964fd3eb360a1015fbe56b1bf8f2cc6e8  iv-owner.wav
    efd8cc13adcd437209bd7831d0fccbb9dc6c62bdcbc9ae612c919ed496e9b810  iv-escalator.wav
    ebffe6c1054b2fbcb4aa0f9cf48ada69f9036aa909d60dd506e2d990f1790444  iv-walker.wav
