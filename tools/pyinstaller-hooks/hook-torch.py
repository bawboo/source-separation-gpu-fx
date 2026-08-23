"""Focused PyInstaller hook for the HTDemucs inference worker.

The upstream hook deliberately collects every torch submodule, including its
test, distributed, TensorBoard and optional ecosystem integrations. The worker
uses eager inference only, so static dependency analysis plus Torch's data and
DLL trees are both safer and substantially smaller.
"""

from PyInstaller.utils.hooks import collect_data_files, collect_dynamic_libs

module_collection_mode = "pyz+py"
warn_on_missing_hiddenimports = False

datas = collect_data_files(
    "torch",
    excludes=[
        "**/*.h",
        "**/*.hpp",
        "**/*.cuh",
        "**/*.lib",
        "**/*.cpp",
        "**/*.pyi",
        "**/*.cmake",
        "bin/**",
    ],
)
binaries = collect_dynamic_libs("torch")

# These modules are selected dynamically by Torch on Windows or by the Demucs
# model loader, so static analysis cannot see every one of them.
hiddenimports = [
    "torch._C",
    "torch.cuda",
    "torch.fft",
    "torch.jit",
    "torch.nn",
    "torch.nn.functional",
    "torch.serialization",
    "torch.utils.data",
]
