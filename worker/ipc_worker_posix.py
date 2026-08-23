"""POSIX shared-memory transport used by the macOS standalone worker.

The public names intentionally mirror the small Win32 helper surface consumed
by gpu_ipc_worker.py.  That keeps the model/inference code identical on both
platforms while the native C++ client uses shm_open(2) and named semaphores.
"""

from __future__ import annotations

import ctypes
import errno
import os
import time
from dataclasses import dataclass

import ipc_protocol as p

FILE_MAP_ALL_ACCESS = 0
EVENT_MODIFY_STATE = 0
SYNCHRONIZE = 0
WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 258
WAIT_FAILED = 0xFFFFFFFF

_libc = ctypes.CDLL(None, use_errno=True)
_libc.shm_open.argtypes = (ctypes.c_char_p, ctypes.c_int, ctypes.c_uint)
_libc.shm_open.restype = ctypes.c_int
_libc.mmap.argtypes = (
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_longlong,
)
_libc.mmap.restype = ctypes.c_void_p
_libc.munmap.argtypes = (ctypes.c_void_p, ctypes.c_size_t)
_libc.munmap.restype = ctypes.c_int
_libc.sem_open.restype = ctypes.c_void_p
_libc.sem_trywait.argtypes = (ctypes.c_void_p,)
_libc.sem_trywait.restype = ctypes.c_int
_libc.sem_post.argtypes = (ctypes.c_void_p,)
_libc.sem_post.restype = ctypes.c_int
_libc.sem_close.argtypes = (ctypes.c_void_p,)
_libc.sem_close.restype = ctypes.c_int

_PROT_READ = 0x1
_PROT_WRITE = 0x2
_MAP_SHARED = 0x0001
_MAP_FAILED = ctypes.c_void_p(-1).value


@dataclass
class _Mapping:
    fd: int


@dataclass
class _View:
    address: int

    def __int__(self) -> int:
        return self.address


@dataclass
class _Semaphore:
    value: int


def _os_error(label: str) -> OSError:
    code = ctypes.get_errno()
    return OSError(code, f"{label}: {os.strerror(code)}")


class _PosixApi:
    @staticmethod
    def OpenFileMappingW(_access: int, _inherit: bool, name: str):
        fd = _libc.shm_open(name.encode(), os.O_RDWR, 0)
        return None if fd < 0 else _Mapping(fd)

    @staticmethod
    def MapViewOfFile(mapping: _Mapping, *_args):
        address = _libc.mmap(
            None,
            p.TOTAL_BYTES,
            _PROT_READ | _PROT_WRITE,
            _MAP_SHARED,
            mapping.fd,
            0,
        )
        if address in (None, _MAP_FAILED):
            return None
        return _View(int(address))

    @staticmethod
    def OpenEventW(_access: int, _inherit: bool, name: str):
        ctypes.set_errno(0)
        value = _libc.sem_open(name.encode(), 0)
        if value in (None, _MAP_FAILED):
            return None
        return _Semaphore(int(value))

    @staticmethod
    def WaitForSingleObject(handle: _Semaphore, timeout_ms: int) -> int:
        deadline = time.monotonic() + max(timeout_ms, 0) / 1000.0
        while True:
            if _libc.sem_trywait(ctypes.c_void_p(handle.value)) == 0:
                return WAIT_OBJECT_0
            code = ctypes.get_errno()
            if code not in (errno.EAGAIN, errno.EINTR):
                return WAIT_FAILED
            if timeout_ms == 0 or time.monotonic() >= deadline:
                return WAIT_TIMEOUT
            time.sleep(min(0.001, max(0.0, deadline - time.monotonic())))

    @staticmethod
    def SetEvent(handle: _Semaphore) -> bool:
        return _libc.sem_post(ctypes.c_void_p(handle.value)) == 0

    @staticmethod
    def CloseHandle(handle) -> bool:
        if isinstance(handle, _Semaphore):
            return _libc.sem_close(ctypes.c_void_p(handle.value)) == 0
        if isinstance(handle, _Mapping):
            try:
                os.close(handle.fd)
                return True
            except OSError:
                return False
        return False

    @staticmethod
    def UnmapViewOfFile(view: _View) -> bool:
        return _libc.munmap(ctypes.c_void_p(view.address), p.TOTAL_BYTES) == 0


kernel32 = _PosixApi()


def win_error(label: str) -> OSError:
    return _os_error(label)


def open_handle(call, name: str):
    handle = call(0, False, name)
    if not handle:
        raise win_error(f"open {name}")
    return handle


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
    try:
        return len(os.listdir("/dev/fd"))
    except OSError:
        return 0


def heartbeat(base: int) -> None:
    write_u64(base, p.OFF_HEARTBEAT_COUNTER, read_u64(base, p.OFF_HEARTBEAT_COUNTER) + 1)
    write_u64(base, p.OFF_HEARTBEAT_MONOTONIC_MS, int(time.monotonic() * 1000.0))
