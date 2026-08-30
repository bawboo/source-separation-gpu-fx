"""Single frozen entry point for both separation back-ends.

HTDemucs and MelBand RoFormer need the same PyTorch build, so freezing them
separately would ship the ~2.5 GB torch tree twice. They are packaged as one
PyInstaller bundle instead and dispatched here:

    htdemucs-worker.exe --session ...        -> HTDemucs IPC worker (unchanged)
    htdemucs-worker.exe roformer --input ... -> MelBand RoFormer worker

The bare form keeps the existing shared-memory IPC contract byte-for-byte, so
the C++ client needs no change for HTDemucs.
"""

from __future__ import annotations

import sys


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "roformer":
        del sys.argv[1]
        import roformer_worker

        return roformer_worker.main()

    import gpu_ipc_worker

    return gpu_ipc_worker.main()


if __name__ == "__main__":
    raise SystemExit(main())
