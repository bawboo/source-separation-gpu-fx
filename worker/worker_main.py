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

import os
import sys

# Both back-ends need this, and it only works before torch is imported -- so it
# belongs at the single entry point they share. It used to live in
# gpu_ipc_worker, which the roformer branch never imports, leaving the RoFormer
# path with no fallback at all. That went unnoticed because the arm64 runtime's
# torch implements the operations natively; the Intel one, pinned to 2.2.2,
# does not. The gap was luck, not design.
if sys.platform == "darwin":
    os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")


def _run_as_multiprocessing_helper() -> int | None:
    """Host multiprocessing's helper process when it re-runs this binary.

    On POSIX, multiprocessing starts its resource tracker by launching
    sys.executable with [-B, -S, -I, -c, "from multiprocessing.resource_tracker
    import main;main(<fd>)"]. Frozen, sys.executable is this worker, so those
    arguments reach our own parser, it exits with a usage error, and every
    semaphore the tracker was supposed to clean up leaks instead. The RoFormer
    back-end reaches this through librosa and joblib.

    multiprocessing.freeze_support() does not cover it -- that function returns
    immediately anywhere but Windows -- and Windows has no resource tracker, so
    this never showed up there.

    Returns None when this is an ordinary invocation.
    """

    if "-c" not in sys.argv:
        return None
    index = sys.argv.index("-c")
    if index + 1 >= len(sys.argv):
        return None
    code = sys.argv[index + 1]
    if "multiprocessing" not in code:
        return None
    # Leave argv looking the way "python -c ..." leaves it.
    sys.argv = sys.argv[index + 1:]
    exec(compile(code, "<multiprocessing>", "exec"), {"__name__": "__main__"})
    return 0


def main() -> int:
    helper_status = _run_as_multiprocessing_helper()
    if helper_status is not None:
        return helper_status

    if len(sys.argv) > 1 and sys.argv[1] == "--import-check":
        # Prove a frozen bundle can reach what inference needs. --help only
        # parses arguments, so it passes even when a module the RoFormer path
        # imports was excluded from the bundle -- which is invisible until a
        # separation is attempted, on a user's machine, after a download.
        import gpu_ipc_worker  # noqa: F401
        import roformer_worker  # noqa: F401
        from mel_band_roformer.clean_api import (  # noqa: F401
            MelBandRoformerSession,
        )

        print("import-check ok")
        return 0

    if len(sys.argv) > 1 and sys.argv[1] == "roformer":
        del sys.argv[1]
        import roformer_worker

        return roformer_worker.main()

    import gpu_ipc_worker

    return gpu_ipc_worker.main()


if __name__ == "__main__":
    raise SystemExit(main())
