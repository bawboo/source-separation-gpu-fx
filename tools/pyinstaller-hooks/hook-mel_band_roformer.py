"""PyInstaller hook for the MelBand RoFormer inference package.

The package loads model classes and its bundled model registry dynamically, so
static analysis alone misses both. Data files include the manifest JSON that the
downloader consults, which must ship with the frozen worker.
"""

from PyInstaller.utils.hooks import collect_data_files, collect_submodules

hiddenimports = collect_submodules("mel_band_roformer")
datas = collect_data_files("mel_band_roformer")
