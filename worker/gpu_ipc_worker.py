from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import ssl
import sys
import threading
import time
from pathlib import Path
from urllib.parse import urlparse
from urllib.request import Request, urlopen

import numpy as np

# Demucs already routes unsupported complex-number operations through CPU.
# Allow PyTorch to do the same for any remaining MPS kernels before torch is
# imported, which is required for this environment variable to take effect.
if sys.platform == "darwin":
    os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")

import torch

if getattr(sys, "frozen", False):
    # Portable layout:
    # Resources/sidecar/Runtime/htdemucs-worker/htdemucs-worker[.exe]
    # The existing source/model sidecar remains the single data root.
    RUNTIME_DIR = Path(sys.executable).resolve().parent
    PROJECT_ROOT = RUNTIME_DIR.parents[1]
    WORKER_DIR = PROJECT_ROOT / "worker"
    WORKSPACE_ROOT = PROJECT_ROOT
else:
    WORKER_DIR = Path(__file__).resolve().parent
    PROJECT_ROOT = WORKER_DIR.parent
    WORKSPACE_ROOT = PROJECT_ROOT.parents[1]
PACKAGED_DEMUCS_REPO = PROJECT_ROOT / "demucs_repo"
PACKAGED_DEPENDENCIES = PROJECT_ROOT / "deps"
DEMUCS_REPO = (
    PACKAGED_DEMUCS_REPO
    if (PACKAGED_DEMUCS_REPO / "demucs" / "states.py").is_file()
    else WORKSPACE_ROOT / "work" / "demucs"
)
DEPENDENCY_DIR = (
    PACKAGED_DEPENDENCIES
    if (PACKAGED_DEPENDENCIES / "einops" / "__init__.py").is_file()
    else WORKSPACE_ROOT / "work" / "bench_deps"
)
sys.path.insert(0, str(WORKER_DIR))
sys.path.insert(0, str(PROJECT_ROOT / "src"))

import ipc_protocol as p  # noqa: E402

if os.name == "nt":
    from ipc_worker import (  # noqa: E402
        EVENT_MODIFY_STATE,
        FILE_MAP_ALL_ACCESS,
        SYNCHRONIZE,
        WAIT_OBJECT_0,
        WAIT_TIMEOUT,
        get_handle_count,
        heartbeat,
        kernel32,
        open_handle,
        read_i32,
        read_u32,
        read_u64,
        set_last_error,
        win_error,
        write_i32,
        write_u32,
        write_u64,
    )
else:
    from ipc_worker_posix import (  # noqa: E402
    EVENT_MODIFY_STATE,
    FILE_MAP_ALL_ACCESS,
    SYNCHRONIZE,
    WAIT_OBJECT_0,
    WAIT_TIMEOUT,
    get_handle_count,
    heartbeat,
    kernel32,
    open_handle,
    read_i32,
    read_u32,
    read_u64,
    set_last_error,
    win_error,
    write_i32,
    write_u32,
    write_u64,
    )
from htdemucs_gpu_fx import (  # noqa: E402
    HTDEMUCS_SPEC,
    StreamSpec,
    StreamingOLAEngine,
    load_demucs_checkpoint,
    load_demucs_registry_model,
    validate_eager_model,
)


def open_mapping_and_events(session: str):
    mapping = kernel32.OpenFileMappingW(FILE_MAP_ALL_ACCESS, False, p.mapping_name(session))
    if not mapping:
        raise win_error("OpenFileMappingW")
    view = kernel32.MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0)
    if not view:
        kernel32.CloseHandle(mapping)
        raise win_error("MapViewOfFile")
    try:
        request = open_handle(kernel32.OpenEventW, p.event_name(session, "request"))
        response = open_handle(kernel32.OpenEventW, p.event_name(session, "response"))
        ready = open_handle(kernel32.OpenEventW, p.event_name(session, "ready"))
        shutdown = open_handle(kernel32.OpenEventW, p.event_name(session, "shutdown"))
    except Exception:
        kernel32.UnmapViewOfFile(view)
        kernel32.CloseHandle(mapping)
        raise
    return mapping, view, (request, response, ready, shutdown)


def validate_header(base: int) -> None:
    expected = {
        p.OFF_MAGIC: p.MAGIC,
        p.OFF_ABI_VERSION: p.ABI_VERSION,
        p.OFF_HEADER_BYTES: p.HEADER_BYTES,
        p.OFF_TOTAL_BYTES: p.TOTAL_BYTES,
        p.OFF_SAMPLE_RATE: p.SAMPLE_RATE,
        p.OFF_CHANNELS: p.CHANNELS,
        p.OFF_MAX_FRAMES: p.MAX_FRAMES,
        p.OFF_INPUT_SLOT_STRIDE: p.INPUT_SLOT_STRIDE,
        p.OFF_OUTPUT_SLOT_STRIDE: p.OUTPUT_SLOT_STRIDE,
        p.OFF_INPUT_BASE_OFFSET: p.INPUT_BASE_OFFSET,
        p.OFF_OUTPUT_BASE_OFFSET: p.OUTPUT_BASE_OFFSET,
    }
    for offset, value in expected.items():
        actual = read_u32(base, offset)
        if actual != value:
            raise ValueError(f"ABI field at {offset} is {actual}, expected {value}")

    sources = read_u32(base, p.OFF_SOURCES)
    segment_frames = read_u32(base, p.OFF_SEGMENT_FRAMES)
    hop_frames = read_u32(base, p.OFF_HOP_FRAMES)
    requested_backend = read_u32(base, p.OFF_REQUESTED_BACKEND)
    if not 1 <= sources <= p.MAX_SOURCES:
        raise ValueError(f"invalid active source count {sources}")
    if not 0 < hop_frames <= p.MAX_FRAMES:
        raise ValueError(f"invalid hop frames {hop_frames}")
    if segment_frames <= hop_frames:
        raise ValueError(
            f"segment frames {segment_frames} must exceed hop frames {hop_frames}"
        )
    if requested_backend not in (
        p.BACKEND_AUTO,
        p.BACKEND_CUDA,
        p.BACKEND_CPU,
        p.BACKEND_MPS,
    ):
        raise ValueError(f"invalid requested backend {requested_backend}")


def float_view(address: int, shape: tuple[int, ...]) -> np.ndarray:
    count = int(np.prod(shape))
    array_type = ctypes.c_float * count
    return np.ctypeslib.as_array(array_type.from_address(address)).reshape(shape)


def set_fixed_text(base: int, offset: int, size: int, message: str) -> None:
    encoded = message.encode("utf-8", errors="replace")[: size - 1]
    ctypes.memset(base + offset, 0, size)
    if encoded:
        ctypes.memmove(base + offset, encoded, len(encoded))


def resolve_device(device_name: str) -> torch.device:
    if device_name == "auto":
        if torch.cuda.is_available():
            return torch.device("cuda:0")
        if torch.backends.mps.is_available():
            return torch.device("mps")
        return torch.device("cpu")
    device = torch.device(device_name)
    if device.type == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but torch.cuda.is_available() is false")
    if device.type == "mps" and not torch.backends.mps.is_available():
        reason = (
            "this PyTorch build has no MPS support"
            if not torch.backends.mps.is_built()
            else "MPS is unavailable on this Mac"
        )
        raise RuntimeError(f"MPS was requested but {reason}")
    if device.type not in ("cuda", "mps", "cpu"):
        raise ValueError(f"unsupported compute device {device}")
    return device


def synchronize_device(device: torch.device) -> None:
    if device.type == "cuda":
        torch.cuda.synchronize(device)
    elif device.type == "mps":
        torch.mps.synchronize()


def accelerator_name(device: torch.device) -> str:
    if device.type == "cuda":
        return torch.cuda.get_device_name(device)
    if device.type == "mps":
        get_name = getattr(torch.backends.mps, "get_name", None)
        return str(get_name()) if callable(get_name) else "Apple Metal (MPS)"
    return "CPU"


def runtime_self_test(
    output_path: Path,
    models_directory: Path,
    model_name: str,
    device_name: str,
) -> int:
    """Validate the selected runtime, model registry and accelerator without IPC."""

    device = resolve_device(device_name)
    probe = torch.arange(64, dtype=torch.float32, device=device).reshape(8, 8)
    probe_result = probe @ probe.transpose(0, 1)
    synchronize_device(device)
    if not bool(torch.isfinite(probe_result).all().item()):
        raise RuntimeError("the runtime tensor self-test produced non-finite values")

    model = load_demucs_registry_model(
        model_name,
        models_directory,
        DEMUCS_REPO,
        DEPENDENCY_DIR,
    )
    source_names = tuple(str(source) for source in model.sources)
    if int(model.samplerate) != p.SAMPLE_RATE:
        raise RuntimeError(
            f"model {model_name} sample rate {model.samplerate} != {p.SAMPLE_RATE}"
        )
    if int(model.audio_channels) != p.CHANNELS:
        raise RuntimeError(
            f"model {model_name} channels {model.audio_channels} != {p.CHANNELS}"
        )
    model.to(device)
    synchronize_device(device)

    report = {
        "schema_version": 1,
        "status": "pass",
        "runtime": "cuda" if device.type == "cuda" else "cpu",
        "device": str(device),
        "device_name": accelerator_name(device),
        "torch_version": torch.__version__,
        "torch_cuda_version": torch.version.cuda,
        "model": model_name,
        "sources": source_names,
        "sample_rate": int(model.samplerate),
        "audio_channels": int(model.audio_channels),
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = output_path.with_suffix(output_path.suffix + ".partial")
    temporary.write_text(json.dumps(report, indent=2), encoding="utf-8")
    temporary.replace(output_path)
    return 0


def _write_download_status(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".partial")
    temporary.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    temporary.replace(path)


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def install_registry_model(
    models_directory: Path,
    model_name: str,
    status_path: Path,
) -> int:
    """Download one allowlisted model transaction into the per-user registry."""

    models_directory = models_directory.resolve()
    manifest_path = models_directory / "model-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    model = manifest.get("models", {}).get(model_name)
    if not isinstance(model, dict):
        raise ValueError(f"unknown model {model_name}")
    file_names = model.get("files")
    artifacts = manifest.get("artifacts")
    allowlist = set(manifest.get("download_host_allowlist", []))
    if not isinstance(file_names, list) or not isinstance(artifacts, dict):
        raise ValueError("model manifest has no artifact registry")

    selected: list[tuple[str, dict[str, object]]] = []
    total_bytes = 0
    for file_name in file_names:
        if not isinstance(file_name, str) or Path(file_name).name != file_name:
            raise ValueError(f"unsafe model artifact name {file_name!r}")
        artifact = artifacts.get(file_name)
        if not isinstance(artifact, dict):
            raise ValueError(f"missing artifact metadata for {file_name}")
        url = str(artifact.get("url", ""))
        parsed = urlparse(url)
        if parsed.scheme != "https" or parsed.hostname not in allowlist:
            raise ValueError(f"model URL is not allowlisted: {url}")
        expected_bytes = int(artifact.get("bytes", 0))
        expected_hash = str(artifact.get("sha256", "")).lower()
        if expected_bytes <= 0 or len(expected_hash) != 64:
            raise ValueError(f"invalid artifact contract for {file_name}")
        selected.append((file_name, artifact))
        total_bytes += expected_bytes

    models_directory.mkdir(parents=True, exist_ok=True)
    completed_before_file = 0
    ssl_context = ssl.create_default_context()
    for file_index, (file_name, artifact) in enumerate(selected):
        target = models_directory / file_name
        expected_bytes = int(artifact["bytes"])
        expected_hash = str(artifact["sha256"]).lower()
        if (
            target.is_file()
            and target.stat().st_size == expected_bytes
            and _file_sha256(target) == expected_hash
        ):
            completed_before_file += expected_bytes
            continue

        partial = models_directory / f"{file_name}.partial"
        request = Request(
            str(artifact["url"]),
            headers={"User-Agent": "HTDemucs-GPU-FX/0.1 model-manager"},
        )
        downloaded = 0
        _write_download_status(
            status_path,
            {
                "schema_version": 1,
                "state": "downloading",
                "model": model_name,
                "file": file_name,
                "file_index": file_index,
                "file_count": len(selected),
                "completed_bytes": completed_before_file,
                "total_bytes": total_bytes,
            },
        )
        try:
            with urlopen(request, timeout=60, context=ssl_context) as response, partial.open(
                "wb"
            ) as output:
                if getattr(response, "status", 200) != 200:
                    raise RuntimeError(f"HTTP status {response.status} for {file_name}")
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    output.write(chunk)
                    downloaded += len(chunk)
                    _write_download_status(
                        status_path,
                        {
                            "schema_version": 1,
                            "state": "downloading",
                            "model": model_name,
                            "file": file_name,
                            "file_index": file_index,
                            "file_count": len(selected),
                            "completed_bytes": completed_before_file + downloaded,
                            "total_bytes": total_bytes,
                        },
                    )
            if downloaded != expected_bytes:
                raise RuntimeError(
                    f"downloaded size {downloaded} != {expected_bytes} for {file_name}"
                )
            actual_hash = _file_sha256(partial)
            if actual_hash != expected_hash:
                raise RuntimeError(
                    f"SHA-256 {actual_hash} != {expected_hash} for {file_name}"
                )
            partial.replace(target)
        except Exception:
            partial.unlink(missing_ok=True)
            raise
        completed_before_file += expected_bytes

    _write_download_status(
        status_path,
        {
            "schema_version": 1,
            "state": "installed",
            "model": model_name,
            "completed_bytes": total_bytes,
            "total_bytes": total_bytes,
        },
    )
    return 0


def run(
    session: str,
    checkpoint: Path | None,
    models_directory: Path | None,
    model_name: str,
    device_name: str,
) -> int:
    device = resolve_device(device_name)

    mapping, view, handles = open_mapping_and_events(session)
    request, response, ready, shutdown = handles
    base = int(view)
    heartbeat_stop = threading.Event()

    def heartbeat_loop() -> None:
        while not heartbeat_stop.wait(0.1):
            heartbeat(base)

    heartbeat_thread = threading.Thread(
        target=heartbeat_loop, name="htfx-heartbeat", daemon=True
    )
    try:
        validate_header(base)
        write_u32(base, p.OFF_WORKER_PID, os.getpid())
        write_u32(base, p.OFF_WORKER_HANDLE_START, get_handle_count())
        write_i32(base, p.OFF_STATUS_CODE, 0)
        set_last_error(base, "")
        heartbeat(base)
        heartbeat_thread.start()

        write_u32(base, p.OFF_STATE, p.STATE_LOADING)
        active_sources = read_u32(base, p.OFF_SOURCES)
        segment_frames = read_u32(base, p.OFF_SEGMENT_FRAMES)
        hop_frames = read_u32(base, p.OFF_HOP_FRAMES)
        overlap_frames = segment_frames - hop_frames
        if models_directory is not None:
            model = load_demucs_registry_model(
                model_name,
                models_directory,
                DEMUCS_REPO,
                DEPENDENCY_DIR,
            )
        elif checkpoint is not None:
            model = load_demucs_checkpoint(
                checkpoint,
                DEMUCS_REPO,
                DEPENDENCY_DIR,
            )
            validate_eager_model(model, HTDEMUCS_SPEC)
        else:
            raise ValueError("either --models-dir or --checkpoint is required")
        source_names = tuple(str(source) for source in model.sources)
        if len(source_names) != active_sources:
            raise ValueError(
                f"model {model_name} has {len(source_names)} sources, "
                f"client configured {active_sources}"
            )
        if int(model.samplerate) != p.SAMPLE_RATE:
            raise ValueError(
                f"model {model_name} sample rate {model.samplerate} != {p.SAMPLE_RATE}"
            )
        if int(model.audio_channels) != p.CHANNELS:
            raise ValueError(
                f"model {model_name} channels {model.audio_channels} != {p.CHANNELS}"
            )
        spec = StreamSpec(
            sample_rate=p.SAMPLE_RATE,
            input_channels=p.CHANNELS,
            source_names=source_names,
            segment_samples=segment_frames,
            hop_samples=hop_frames,
            overlap_samples=overlap_frames,
        )
        model.to(device)

        from demucs.apply import apply_model

        def run_inference(batch: torch.Tensor) -> torch.Tensor:
            return apply_model(
                model,
                batch,
                shifts=0,
                split=False,
                segment=spec.segment_seconds,
                device=device,
            )

        write_u32(base, p.OFF_STATE, p.STATE_WARMING)
        if device.type == "cuda":
            torch.backends.cuda.matmul.allow_tf32 = False
            torch.backends.cudnn.allow_tf32 = False
            torch.backends.cudnn.benchmark = False
        with torch.inference_mode():
            _ = run_inference(
                torch.zeros(
                    1,
                    p.CHANNELS,
                    spec.segment_samples,
                    device=device,
                    dtype=torch.float32,
                )
            )
        synchronize_device(device)
        resolved_backend = {
            "cuda": p.BACKEND_CUDA,
            "mps": p.BACKEND_MPS,
            "cpu": p.BACKEND_CPU,
        }[device.type]
        write_u32(base, p.OFF_COMPUTE_BACKEND, resolved_backend)
        write_u32(base, p.OFF_CUDA_DEVICE_INDEX, (device.index or 0) if device.type == "cuda" else 0)
        set_fixed_text(
            base,
            p.OFF_GPU_NAME,
            p.GPU_NAME_BYTES,
            accelerator_name(device),
        )
        engine = StreamingOLAEngine(
            model,
            device,
            spec,
            validate_finite=False,
            inference=run_inference,
        )
        engine.reset()

        input_slots = [
            float_view(
                base + p.INPUT_BASE_OFFSET + slot * p.INPUT_SLOT_STRIDE,
                (p.CHANNELS, p.MAX_FRAMES),
            )
            for slot in range(p.SLOT_COUNT)
        ]
        output_slots = [
            float_view(
                base + p.OUTPUT_BASE_OFFSET + slot * p.OUTPUT_SLOT_STRIDE,
                (p.MAX_SOURCES, p.CHANNELS, p.MAX_FRAMES),
            )
            for slot in range(p.SLOT_COUNT)
        ]
        accelerated = device.type in ("cuda", "mps")
        if accelerated:
            staging_input = torch.empty(
                (p.CHANNELS, spec.hop_samples),
                dtype=torch.float32,
                pin_memory=device.type == "cuda",
            )
            staging_output = torch.empty(
                (active_sources, p.CHANNELS, spec.hop_samples),
                dtype=torch.float32,
                pin_memory=device.type == "cuda",
            )
            device_input = torch.empty(
                (p.CHANNELS, spec.hop_samples),
                dtype=torch.float32,
                device=device,
            )
            staging_input_np = staging_input.numpy()
            staging_output_np = staging_output.numpy()
        else:
            device_input = torch.empty(
                (p.CHANNELS, spec.hop_samples),
                dtype=torch.float32,
                device=device,
            )
            staging_input = device_input
            staging_output = None
            staging_input_np = device_input.numpy()
            staging_output_np = None

        active_epoch = read_u64(base, p.OFF_EPOCH)
        last_sequence = 0
        mps_max_allocated = 0
        mps_max_driver = 0
        write_u32(base, p.OFF_STATE, p.STATE_READY)
        if not kernel32.SetEvent(ready):
            raise win_error("SetEvent ready")

        should_stop = False
        while not should_stop:
            if kernel32.WaitForSingleObject(shutdown, 0) == WAIT_OBJECT_0:
                break
            wait_result = kernel32.WaitForSingleObject(request, 100)
            if wait_result == WAIT_TIMEOUT:
                continue
            if wait_result != WAIT_OBJECT_0:
                raise win_error("WaitForSingleObject request")

            sequence = read_u64(base, p.OFF_REQUEST_SEQUENCE)
            command = read_u32(base, p.OFF_COMMAND)
            epoch = read_u64(base, p.OFF_EPOCH)
            try:
                if sequence <= last_sequence:
                    raise ValueError(
                        f"non-monotonic sequence {sequence} <= {last_sequence}"
                    )
                write_i32(base, p.OFF_STATUS_CODE, 0)
                set_last_error(base, "")
                if command == p.COMMAND_PROCESS:
                    if epoch != active_epoch:
                        raise ValueError(
                            f"PROCESS epoch {epoch} != active epoch {active_epoch}"
                        )
                    slot = read_u32(base, p.OFF_ACTIVE_SLOT)
                    frames = read_u32(base, p.OFF_VALID_FRAMES)
                    if slot >= p.SLOT_COUNT or frames != spec.hop_samples:
                        raise ValueError(
                            f"invalid PROCESS slot={slot} frames={frames}"
                        )
                    write_u32(base, p.OFF_STATE, p.STATE_PROCESSING)
                    inference_started_ns = time.perf_counter_ns()
                    np.copyto(
                        staging_input_np,
                        input_slots[slot][:, :frames],
                        casting="no",
                    )
                    if accelerated:
                        device_input.copy_(
                            staging_input,
                            non_blocking=device.type == "cuda",
                        )
                    with torch.inference_mode():
                        output = engine.process_hop(device_input)
                    if accelerated:
                        assert staging_output is not None
                        assert staging_output_np is not None
                        staging_output.copy_(
                            output,
                            non_blocking=device.type == "cuda",
                        )
                        synchronize_device(device)
                        np.copyto(
                            output_slots[slot][:active_sources, :, :frames],
                            staging_output_np,
                            casting="no",
                        )
                    else:
                        np.copyto(
                            output_slots[slot][:active_sources, :, :frames],
                            output.numpy(),
                            casting="no",
                        )
                    write_u64(
                        base,
                        p.OFF_LAST_INFERENCE_US,
                        (time.perf_counter_ns() - inference_started_ns) // 1_000,
                    )
                    if device.type == "cuda":
                        write_u64(
                            base,
                            p.OFF_CUDA_ALLOCATED_BYTES,
                            torch.cuda.memory_allocated(device),
                        )
                        write_u64(
                            base,
                            p.OFF_CUDA_RESERVED_BYTES,
                            torch.cuda.memory_reserved(device),
                        )
                        write_u64(
                            base,
                            p.OFF_CUDA_MAX_ALLOCATED_BYTES,
                            torch.cuda.max_memory_allocated(device),
                        )
                        write_u64(
                            base,
                            p.OFF_CUDA_MAX_RESERVED_BYTES,
                            torch.cuda.max_memory_reserved(device),
                        )
                    elif device.type == "mps":
                        allocated = int(torch.mps.current_allocated_memory())
                        driver = int(torch.mps.driver_allocated_memory())
                        mps_max_allocated = max(mps_max_allocated, allocated)
                        mps_max_driver = max(mps_max_driver, driver)
                        for offset, value in (
                            (p.OFF_CUDA_ALLOCATED_BYTES, allocated),
                            (p.OFF_CUDA_RESERVED_BYTES, driver),
                            (p.OFF_CUDA_MAX_ALLOCATED_BYTES, mps_max_allocated),
                            (p.OFF_CUDA_MAX_RESERVED_BYTES, mps_max_driver),
                        ):
                            write_u64(base, offset, value)
                    else:
                        for offset in (
                            p.OFF_CUDA_ALLOCATED_BYTES,
                            p.OFF_CUDA_RESERVED_BYTES,
                            p.OFF_CUDA_MAX_ALLOCATED_BYTES,
                            p.OFF_CUDA_MAX_RESERVED_BYTES,
                        ):
                            write_u64(base, offset, 0)
                    write_u64(base, p.OFF_OUTPUT_CHECKSUM, 0)
                    write_u64(
                        base,
                        p.OFF_PROCESSES_COMPLETED,
                        read_u64(base, p.OFF_PROCESSES_COMPLETED) + 1,
                    )
                elif command == p.COMMAND_RESET:
                    if epoch <= active_epoch:
                        raise ValueError(
                            f"RESET epoch {epoch} <= active epoch {active_epoch}"
                        )
                    active_epoch = epoch
                    write_u32(base, p.OFF_STATE, p.STATE_PRIMING)
                    engine.reset()
                    write_u64(base, p.OFF_OUTPUT_CHECKSUM, 0)
                    write_u64(
                        base,
                        p.OFF_RESETS_COMPLETED,
                        read_u64(base, p.OFF_RESETS_COMPLETED) + 1,
                    )
                elif command == p.COMMAND_SHUTDOWN:
                    should_stop = True
                else:
                    raise ValueError(f"unknown command {command}")
                last_sequence = sequence
                write_u64(base, p.OFF_RESPONSE_SEQUENCE, sequence)
                write_u32(base, p.OFF_STATE, p.STATE_READY)
            except Exception as exc:
                write_i32(base, p.OFF_STATUS_CODE, -1)
                write_u32(base, p.OFF_STATE, p.STATE_ERROR)
                write_u64(
                    base,
                    p.OFF_PROTOCOL_ERRORS,
                    read_u64(base, p.OFF_PROTOCOL_ERRORS) + 1,
                )
                write_u64(base, p.OFF_RESPONSE_SEQUENCE, sequence)
                set_last_error(base, f"{type(exc).__name__}: {exc}")
                should_stop = True
            heartbeat(base)
            if not kernel32.SetEvent(response):
                raise win_error("SetEvent response")

        write_u32(base, p.OFF_WORKER_HANDLE_END, get_handle_count())
        write_u32(base, p.OFF_STATE, p.STATE_STOPPED)
        heartbeat(base)
        return 0 if read_i32(base, p.OFF_STATUS_CODE) == 0 else 2
    except Exception as exc:
        # Keep startup failures visible to the client even though the worker has
        # no console window. The manual-reset READY event also wakes a client
        # that is still waiting for model load/warm-up to finish.
        write_i32(base, p.OFF_STATUS_CODE, -1)
        write_u32(base, p.OFF_STATE, p.STATE_ERROR)
        set_last_error(base, f"{type(exc).__name__}: {exc}")
        heartbeat(base)
        kernel32.SetEvent(ready)
        kernel32.SetEvent(response)
        raise
    finally:
        heartbeat_stop.set()
        if heartbeat_thread.is_alive():
            heartbeat_thread.join(timeout=1.0)
        for handle in reversed(handles):
            kernel32.CloseHandle(handle)
        kernel32.UnmapViewOfFile(view)
        kernel32.CloseHandle(mapping)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--session")
    parser.add_argument("--checkpoint", type=Path)
    parser.add_argument("--models-dir", type=Path)
    parser.add_argument("--model", default="htdemucs")
    parser.add_argument("--device", default="auto")
    parser.add_argument("--self-test-json", type=Path)
    parser.add_argument("--install-model")
    parser.add_argument("--status-json", type=Path)
    args = parser.parse_args()
    if args.install_model is not None:
        if args.models_dir is None or args.status_json is None:
            parser.error("--install-model requires --models-dir and --status-json")
        try:
            return install_registry_model(
                args.models_dir,
                args.install_model,
                args.status_json,
            )
        except Exception as exc:
            _write_download_status(
                args.status_json,
                {
                    "schema_version": 1,
                    "state": "error",
                    "model": args.install_model,
                    "message": f"{type(exc).__name__}: {exc}",
                },
            )
            print(
                f"gpu_ipc_worker model install fatal: {type(exc).__name__}: {exc}",
                file=sys.stderr,
                flush=True,
            )
            return 5
    if args.self_test_json is not None:
        if args.models_dir is None or args.checkpoint is not None:
            parser.error("--self-test-json requires --models-dir and does not accept --checkpoint")
        try:
            return runtime_self_test(
                args.self_test_json,
                args.models_dir,
                args.model,
                args.device,
            )
        except Exception as exc:
            print(
                f"gpu_ipc_worker self-test fatal: {type(exc).__name__}: {exc}",
                file=sys.stderr,
                flush=True,
            )
            return 4
    if args.session is None:
        parser.error("--session is required unless --self-test-json is used")
    if (args.checkpoint is None) == (args.models_dir is None):
        parser.error("provide exactly one of --checkpoint or --models-dir")
    try:
        return run(
            args.session,
            args.checkpoint,
            args.models_dir,
            args.model,
            args.device,
        )
    except Exception as exc:
        print(f"gpu_ipc_worker fatal: {type(exc).__name__}: {exc}", file=sys.stderr, flush=True)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
