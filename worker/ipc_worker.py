from __future__ import annotations

import argparse
import ctypes
import os
import sys
import time
from ctypes import wintypes
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(WORKER_DIR))

import ipc_protocol as p  # noqa: E402

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

FILE_MAP_ALL_ACCESS = 0x000F001F
EVENT_MODIFY_STATE = 0x0002
SYNCHRONIZE = 0x00100000
WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 258

kernel32.OpenFileMappingW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
kernel32.OpenFileMappingW.restype = wintypes.HANDLE
kernel32.MapViewOfFile.argtypes = [
    wintypes.HANDLE,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.DWORD,
    ctypes.c_size_t,
]
kernel32.MapViewOfFile.restype = ctypes.c_void_p
kernel32.OpenEventW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
kernel32.OpenEventW.restype = wintypes.HANDLE
kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.WaitForSingleObject.restype = wintypes.DWORD
kernel32.SetEvent.argtypes = [wintypes.HANDLE]
kernel32.SetEvent.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL
kernel32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
kernel32.UnmapViewOfFile.restype = wintypes.BOOL
kernel32.GetCurrentProcess.restype = wintypes.HANDLE
kernel32.GetProcessHandleCount.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.GetProcessHandleCount.restype = wintypes.BOOL


def win_error(label: str) -> OSError:
    return ctypes.WinError(ctypes.get_last_error(), label)


def read_u32(base: int, offset: int) -> int:
    return ctypes.c_uint32.from_address(base + offset).value


def write_u32(base: int, offset: int, value: int) -> None:
    ctypes.c_uint32.from_address(base + offset).value = value


def read_i32(base: int, offset: int) -> int:
    return ctypes.c_int32.from_address(base + offset).value


def write_i32(base: int, offset: int, value: int) -> None:
    ctypes.c_int32.from_address(base + offset).value = value


def read_u64(base: int, offset: int) -> int:
    return ctypes.c_uint64.from_address(base + offset).value


def write_u64(base: int, offset: int, value: int) -> None:
    ctypes.c_uint64.from_address(base + offset).value = value


def set_last_error(base: int, message: str) -> None:
    encoded = message.encode("utf-8", errors="replace")[: p.LAST_ERROR_BYTES - 1]
    ctypes.memset(base + p.OFF_LAST_ERROR, 0, p.LAST_ERROR_BYTES)
    if encoded:
        ctypes.memmove(base + p.OFF_LAST_ERROR, encoded, len(encoded))


def get_handle_count() -> int:
    count = wintypes.DWORD()
    if not kernel32.GetProcessHandleCount(kernel32.GetCurrentProcess(), ctypes.byref(count)):
        raise win_error("GetProcessHandleCount")
    return int(count.value)


def heartbeat(base: int) -> None:
    write_u64(base, p.OFF_HEARTBEAT_COUNTER, read_u64(base, p.OFF_HEARTBEAT_COUNTER) + 1)
    write_u64(base, p.OFF_HEARTBEAT_MONOTONIC_MS, int(time.monotonic() * 1000.0))


def fnv1a_ranges(ranges: list[tuple[int, int]]) -> int:
    value = 14_695_981_039_346_656_037
    for address, size in ranges:
        data = ctypes.string_at(address, size)
        for byte in data:
            value ^= byte
            value = (value * 1_099_511_628_211) & 0xFFFFFFFFFFFFFFFF
    return value


def input_ranges(base: int, slot: int, frames: int) -> list[tuple[int, int]]:
    slot_base = base + p.INPUT_BASE_OFFSET + slot * p.INPUT_SLOT_STRIDE
    return [
        (slot_base + channel * p.MAX_FRAMES * 4, frames * 4)
        for channel in range(p.CHANNELS)
    ]


def output_ranges(base: int, slot: int, frames: int) -> list[tuple[int, int]]:
    slot_base = base + p.OUTPUT_BASE_OFFSET + slot * p.OUTPUT_SLOT_STRIDE
    return [
        (
            slot_base + (source * p.CHANNELS + channel) * p.MAX_FRAMES * 4,
            frames * 4,
        )
        for source in range(p.SOURCES)
        for channel in range(p.CHANNELS)
    ]


def process_fake(base: int, slot: int, frames: int, sequence: int, epoch: int) -> int:
    if not 0 <= slot < p.SLOT_COUNT:
        raise ValueError(f"invalid slot {slot}")
    if not 1 <= frames <= p.MAX_FRAMES:
        raise ValueError(f"invalid valid_frames {frames}")
    actual_input_checksum = fnv1a_ranges(input_ranges(base, slot, frames))
    expected_input_checksum = read_u64(base, p.OFF_INPUT_CHECKSUM)
    if actual_input_checksum != expected_input_checksum:
        raise ValueError(
            f"input checksum mismatch {actual_input_checksum:#x} != {expected_input_checksum:#x}"
        )

    input_base = base + p.INPUT_BASE_OFFSET + slot * p.INPUT_SLOT_STRIDE
    output_base = base + p.OUTPUT_BASE_OFFSET + slot * p.OUTPUT_SLOT_STRIDE
    float_array = ctypes.c_float * frames
    gains = (0.25, 0.5, 0.75, 1.0)
    offset = (sequence % 1000) * 1.0e-7 + (epoch % 1000) * 1.0e-6
    for source, gain in enumerate(gains):
        for channel in range(p.CHANNELS):
            input_view = float_array.from_address(
                input_base + channel * p.MAX_FRAMES * 4
            )
            output_view = float_array.from_address(
                output_base
                + (source * p.CHANNELS + channel) * p.MAX_FRAMES * 4
            )
            for frame in range(frames):
                output_view[frame] = input_view[frame] * gain + offset
    return fnv1a_ranges(output_ranges(base, slot, frames))


def open_handle(call, name: str):
    handle = call(EVENT_MODIFY_STATE | SYNCHRONIZE, False, name)
    if not handle:
        raise win_error(f"open {name}")
    return handle


def run(session: str) -> int:
    mapping = kernel32.OpenFileMappingW(FILE_MAP_ALL_ACCESS, False, p.mapping_name(session))
    if not mapping:
        raise win_error("OpenFileMappingW")
    view = kernel32.MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0)
    if not view:
        kernel32.CloseHandle(mapping)
        raise win_error("MapViewOfFile")
    base = int(view)
    handles: list[int] = []
    try:
        request = open_handle(kernel32.OpenEventW, p.event_name(session, "request"))
        response = open_handle(kernel32.OpenEventW, p.event_name(session, "response"))
        ready = open_handle(kernel32.OpenEventW, p.event_name(session, "ready"))
        shutdown = open_handle(kernel32.OpenEventW, p.event_name(session, "shutdown"))
        handles.extend((request, response, ready, shutdown))

        expected = {
            p.OFF_MAGIC: p.MAGIC,
            p.OFF_ABI_VERSION: p.ABI_VERSION,
            p.OFF_HEADER_BYTES: p.HEADER_BYTES,
            p.OFF_TOTAL_BYTES: p.TOTAL_BYTES,
            p.OFF_SAMPLE_RATE: p.SAMPLE_RATE,
            p.OFF_CHANNELS: p.CHANNELS,
            p.OFF_SOURCES: p.SOURCES,
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

        write_u32(base, p.OFF_WORKER_PID, os.getpid())
        write_u32(base, p.OFF_WORKER_HANDLE_START, get_handle_count())
        write_i32(base, p.OFF_STATUS_CODE, 0)
        write_u32(base, p.OFF_STATE, p.STATE_READY)
        set_last_error(base, "")
        heartbeat(base)
        if not kernel32.SetEvent(ready):
            raise win_error("SetEvent ready")

        last_sequence = 0
        last_epoch = read_u64(base, p.OFF_EPOCH)
        should_stop = False
        while not should_stop:
            if kernel32.WaitForSingleObject(shutdown, 0) == WAIT_OBJECT_0:
                break
            wait_result = kernel32.WaitForSingleObject(request, 100)
            if wait_result == WAIT_TIMEOUT:
                heartbeat(base)
                continue
            if wait_result != WAIT_OBJECT_0:
                raise win_error("WaitForSingleObject request")

            sequence = read_u64(base, p.OFF_REQUEST_SEQUENCE)
            command = read_u32(base, p.OFF_COMMAND)
            epoch = read_u64(base, p.OFF_EPOCH)
            try:
                if sequence <= last_sequence:
                    raise ValueError(f"non-monotonic sequence {sequence} <= {last_sequence}")
                write_u32(base, p.OFF_STATE, p.STATE_PROCESSING)
                write_i32(base, p.OFF_STATUS_CODE, 0)
                set_last_error(base, "")
                if command == p.COMMAND_PROCESS:
                    if epoch != last_epoch:
                        raise ValueError(f"PROCESS epoch {epoch} != active epoch {last_epoch}")
                    slot = read_u32(base, p.OFF_ACTIVE_SLOT)
                    frames = read_u32(base, p.OFF_VALID_FRAMES)
                    checksum = process_fake(base, slot, frames, sequence, epoch)
                    write_u64(base, p.OFF_OUTPUT_CHECKSUM, checksum)
                    write_u64(
                        base,
                        p.OFF_PROCESSES_COMPLETED,
                        read_u64(base, p.OFF_PROCESSES_COMPLETED) + 1,
                    )
                elif command == p.COMMAND_RESET:
                    if epoch <= last_epoch:
                        raise ValueError(f"RESET epoch {epoch} <= active epoch {last_epoch}")
                    last_epoch = epoch
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
                set_last_error(base, str(exc))
                should_stop = True
            heartbeat(base)
            if not kernel32.SetEvent(response):
                raise win_error("SetEvent response")

        write_u32(base, p.OFF_WORKER_HANDLE_END, get_handle_count())
        write_u32(base, p.OFF_STATE, p.STATE_STOPPED)
        heartbeat(base)
        return 0 if read_i32(base, p.OFF_STATUS_CODE) == 0 else 2
    finally:
        for handle in reversed(handles):
            kernel32.CloseHandle(handle)
        kernel32.UnmapViewOfFile(view)
        kernel32.CloseHandle(mapping)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--session", required=True)
    args = parser.parse_args()
    try:
        return run(args.session)
    except Exception as exc:
        print(f"ipc_worker fatal: {exc}", file=sys.stderr, flush=True)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
