# Third-party code vendored into the app

Each library keeps its own licence file in its folder. Only these are vendored. Everything else comes through vcpkg (see `app/vcpkg.json`).

| Library | Version | Licence | Source | Used for |
|---|---|---|---|---|
| Signalsmith Stretch | 1.4.0 (tag commit 2026-09-25) | MIT | https://github.com/Signalsmith-Audio/signalsmith-stretch | Pitch-preserving slow playback for labelling (`audio/stretch_renderer`) |
| Signalsmith Linear | 0.6.4 (tag commit 2026-09-25) | MIT | https://github.com/Signalsmith-Audio/linear | FFT and STFT for Signalsmith Stretch, which pins this version |

Neither library is in the vcpkg registry baseline we use, so both are vendored. Only the headers, licence and readme are kept; tests, demos and tools are left out. From Signalsmith Linear, `platform/linear-xsimd-dispatch.cpp` is also left out, because it is only built when the xsimd back-end is enabled.

Source archives, from GitHub's tag tarballs, SHA-256:
- `signalsmith-stretch-1.4.0.tar.gz`: `077235709ECF2A358545E3CA7BDF32859F70F5C9AFB4493E078BD753C9AFF284`
- `linear-0.6.4.tar.gz`: `2CB10D84B96C626255AB46C4AA4C80F64AE532BE487063CFF6A08903C1445A56`

To update, download a tagged release that is at least two weeks old, check that Stretch's `CMakeLists.txt` pins the matching Linear tag, and replace both folders together.
