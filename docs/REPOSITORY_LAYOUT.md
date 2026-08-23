# Public repository layout

## Commit to Git

- `assets/branding/`: application artwork.
- `assets/models/`: YAML and `model-manifest.json` only.
- `cpp/`, `plugin/`, `src/`, `worker/`: application source.
- `tests/`: source-level and smoke tests.
- `tools/`: current build, package and verification scripts.
- `packaging/windows/`: Inno Setup source.
- `third_party/JUCE` and `third_party/demucs`: Git submodule links only.
- `patches/`: reproducible project-specific changes applied to dependencies.
- Root documentation, project licence and third-party notices.

## Never commit

- Model weights: `.th`, `.nm`, `.pt`, `.pth`, `.ckpt`, `.safetensors`.
- `build/`, `dist/`, `Release/`, `results/` or Python caches.
- Runtime ZIPs, Setup EXEs, VST3 binaries and portable packages.
- Local Python/FFmpeg path files, test media or machine-generated logs.
- API tokens, code-signing certificates, private keys or GitHub credentials.

Release binaries belong to GitHub Releases, not the repository's source tree.
The `.gitignore` enforces these boundaries and `tools/check_public_repo.ps1`
performs an additional pre-push audit.
