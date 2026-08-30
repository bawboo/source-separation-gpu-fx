# Music SSP FX - third-party notices and release gates

Audit date: 2026-07-21

This document records the dependencies used by the current local engineering
build. It is an engineering compliance inventory, not legal advice.

## Public-release status and remaining gates

1. **JUCE route selected: AGPL.** This repository declares the project source
   under AGPL-3.0-or-later and includes the complete licence text. A publisher
   using a JUCE commercial licence may choose a different route only if they
   have the rights to relicense all project contributions.
2. **Model weights are never redistributed - settled by design.** The Demucs
   source repository is MIT-licensed, but it does not expressly state that the
   pretrained `.th` weight files carry the same MIT terms, and the 99 MelBand
   RoFormer checkpoints come from many separate authors with their own terms.
   The project therefore *never* ships a weight file: every checkpoint is
   fetched at first use from its own pinned upstream URL and verified against a
   pinned SHA-256, into a per-user cache. Git history, the source archive, the
   runtime ZIPs, and the Release assets contain no weights, so no
   redistribution licence is required for any of them. This gate is closed for
   as long as that rule holds; adding any weight to a distributed artefact
   would reopen it and needs separate clearance per checkpoint. See
   [upstream issue #327](https://github.com/facebookresearch/demucs/issues/327)
   and the [MUSDB18 dataset terms](https://sigsep.github.io/datasets/musdb.html)
   for the Demucs side.
3. **FFmpeg corresponding source/build information remains a binary-release
   gate.** The selected Windows FFmpeg build reports GPLv3-enabled options. Before
   publishing runtime ZIPs, identify its exact upstream source/version and satisfy
   the corresponding-source and notice requirements, or remove FFmpeg from the
   distributed runtime and require a separately installed copy.
4. **Publisher metadata is still generic.** The product name is now
   `Music SSP FX`; replace the placeholder support contact before a polished
   public binary release. Code signing is strongly recommended for Windows but
   is not required for a source-only repo, and is deliberately deferred for the
   current releases - unsigned builds show a SmartScreen warning on first run.

## Open source is not automatically research use

"Open source" describes how software source code is licensed and distributed;
"research use" describes the permitted purpose of using a separately licensed
asset. A project can be both, either, or neither. If a model grant permits only
research use, publishing an unrestricted end-user application on GitHub does not
by itself satisfy that purpose restriction. Permission to use a model also does
not necessarily include permission to copy or redistribute its weight file.

For a public repository, the conservative release layout is therefore:

1. publish the application source and its complete build instructions under a
   JUCE-compatible licence;
2. exclude `.th` and `.nm` model files from Git history and release assets;
3. require the user to obtain a model from its official source, after seeing and
   accepting whatever separate terms apply; and
4. do not publish a binary bundle containing weights until the weight copyright
   holder provides an explicit redistribution licence or written permission.

This layout reduces redistribution risk but does not cure a model's use-purpose
restriction. If the intended public application permits ordinary music
production rather than a defined scientific or educational study, treat it as a
general-purpose application, not as research, unless the rights holder confirms
otherwise.

## Local `ht_demucs_4stems.nm` audit

The locally installed Neutone Gen file was inspected on 2026-07-19 without
modifying it. It is a TorchScript ZIP archive. Its
`model/extra/metadata.json` identifies the model as `htdemucs`, credits Tai
Nakamura, links to the Demucs paper/code, and describes training on MUSDB plus
800 songs. It contains no `license`, `research-use`, `commercial-use`,
`copyright`, or distribution-terms field, and the archive contains no separate
licence/readme file. Neutone making the `.nm` available for loading in its own
plugin is not, by itself, evidence that third parties may repackage, convert, or
redistribute that file.

MUSDB18's official dataset page says the tracks can only be used for academic
purposes and notes that individual tracks come from multiple sources with
different licences. That does not conclusively determine the copyright status
of trained weights, but it is an additional reason not to infer broad
redistribution or commercial rights from the Demucs source-code licence.

## Dependency inventory

| Component | How it is used/shipped | Licence/compliance action |
|---|---|---|
| JUCE 8 | Statically compiled into the Standalone and VST3 binaries | AGPLv3 or JUCE commercial licence; preserve the JUCE notice. See [JUCE licence](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md). |
| Steinberg VST3 SDK | VST3 interfaces compiled through JUCE | Current VST3 SDK is MIT-licensed. Preserve `VST3-SDK-LICENSE.txt`. Do not use Steinberg/VST logos or trademarks in a way that implies endorsement. See [VST3 SDK](https://github.com/steinbergmedia/vst3sdk). |
| Demucs source | Python source copied into the sidecar | MIT. Preserve `DEMUCS-LICENSE.txt`. See [Demucs repository](https://github.com/facebookresearch/demucs). |
| MelBand RoFormer inference package (`mel-band-roformer-infer`) | Python package embedded in the shared frozen worker | MIT. See [openmirlab/melband-roformer-infer](https://github.com/openmirlab/melband-roformer-infer). Preserve the exact distribution licence file. |
| MelBand RoFormer checkpoints (99 models) | Never shipped. Downloaded on first use from each model's own pinned URL and verified against a pinned SHA-256; rolling per-user cache | Terms vary per checkpoint author. The project distributes none of them, so no redistribution licence is exercised. Do not add any checkpoint to a Release asset. |
| librosa / soundfile / ml_collections / beartype / rotary-embedding-torch | Embedded worker dependencies pulled in by the RoFormer back-end | ISC (librosa), BSD-3-Clause (soundfile, plus libsndfile's LGPL notice), Apache-2.0 (ml_collections), MIT (beartype, rotary-embedding-torch). Preserve each exact distribution licence file. |
| Demucs pretrained weights | Not included in Git, Setup, or runtime ZIPs; downloaded by the installed app/installer from the pinned official URL | **Resolved by never redistributing (see gate 2).** Do not add the weights to repository or Release assets without separate clearance. See [upstream issue #327](https://github.com/facebookresearch/demucs/issues/327), the [training/data notes](https://github.com/facebookresearch/demucs/blob/main/docs/training.md), and the [MUSDB18 dataset terms](https://sigsep.github.io/datasets/musdb.html). |
| CPython 3.10/3.11 | Embedded in the CPU/CUDA PyInstaller workers | PSF licence. Preserve the Python licence from the exact interpreter used to freeze each worker. |
| PyInstaller 6.16.0 | Freezes the worker and Python runtime into a portable onedir application | GPLv2-or-later with the upstream exception for distributing bundled applications. Preserve the exact PyInstaller licensing terms; the exception does not change licences of bundled dependencies. |
| PyTorch 2.13.0+cpu / 2.1.2+cu121 | Embedded in the Windows CPU/CUDA workers | BSD-style licence plus bundled third-party notices. Preserve the full licence/notice file from each exact wheel. |
| NumPy 1.26.4 | Embedded worker dependency | BSD-3-Clause plus its bundled third-party notices. Preserve the exact distribution licence file. |
| PyYAML 6.0.1 | Embedded worker dependency | MIT. Preserve the exact distribution licence file. |
| tqdm 4.65.0 | Embedded worker dependency | MIT/MPL dual-licensing terms. Preserve the exact distribution licence file. |
| einops 0.7.0 (macOS) / 0.8.2 (current Windows build) | Python source copied into the sidecar and/or embedded worker | MIT. Preserve the exact installed distribution licence. |
| julius 0.2.8 | Python source copied into the sidecar | MIT. Preserve `JULIUS-LICENSE.txt`. |
| imageio-ffmpeg 0.6.0 | macOS build-time provider for the bundled FFmpeg executable | BSD-2-Clause for the Python project; the included FFmpeg binary retains FFmpeg's separate terms. Preserve both notices. |
| NVIDIA CUDA runtime/driver | Windows PyTorch CUDA libraries are embedded; the NVIDIA display driver is supplied by the user's system | Audit the exact PyTorch wheel's bundled NVIDIA components and notices before publication. The portable package does not install a driver or a separate CUDA toolkit. |
| Apple Metal / MPS | Uses macOS system frameworks; no Apple framework is copied | Available only when the host OS and Apple Silicon hardware support it. |
| FFmpeg | A separate executable is bundled for media decode and MP4 muxing | Preserve `ffmpeg -L` output and audit the exact binary's build configuration. The current Windows test binary reports `--enable-gpl --enable-version3`; do not describe it as LGPL-only. The macOS builder captures the shipped imageio-ffmpeg binary's own `-L` output. Follow the corresponding-source and notice obligations of each exact binary and review codec-patent requirements. See [FFmpeg legal/compliance page](https://ffmpeg.org/legal.html). |

## FFmpeg boundary in the portable builds

The application searches for FFmpeg in this order:

1. `HTFX_FFMPEG` environment variable;
2. `htfx-ffmpeg.txt` next to the Standalone executable;
3. `Resources/sidecar/ffmpeg-path.txt` in a packaged Standalone/VST3;
4. the local build-time fallback; then `ffmpeg`/`ffmpeg.exe` on `PATH`.

The self-contained Windows package copies the selected `ffmpeg.exe`,
`ffprobe.exe`, and adjacent codec DLLs. The macOS package copies the native
FFmpeg executable supplied by the pinned imageio-ffmpeg wheel. WAV/AIFF/FLAC
and other formats supported natively by JUCE can still use the fallback reader
when FFmpeg is unavailable; video import/export requires the bundled FFmpeg.

Video export stream-copies the original video and encodes only the replacement
audio as AAC in an MP4 container. If the original video codec cannot be copied
into MP4, the operation reports an error instead of silently invoking a GPL
H.264 encoder.

## Notices included in local packages

The `Licenses` folder created by the packaging scripts contains:

- this notice (`THIRD_PARTY_NOTICES.md`);
- `JUCE-LICENSE.md`;
- `DEMUCS-LICENSE.txt`;
- licences/notices for the exact bundled Python, PyTorch, NumPy, PyYAML, tqdm,
  einops, julius, and PyInstaller distributions; and
- the exact bundled FFmpeg binary's licence output (plus imageio-ffmpeg's
  licence in the macOS package).

The Windows and macOS filenames differ slightly because the Windows packager
uses fixed uppercase names while the macOS builder derives names from Python
distribution metadata. Treat the package's `Licenses` directory and this
inventory as a unit. Regenerate and re-audit them whenever a runtime wheel,
Python distribution, FFmpeg binary, model, or build architecture changes.
