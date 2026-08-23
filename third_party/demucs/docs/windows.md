# Windows support for Demucs

## Installation and usage

If you don't have much experience with python or the shell, here are more detailed
instructions. Note that **Demucs is not supported on 32bits systems** (as Pytorch
is not available there).

- First install [uv](https://docs.astral.sh/uv/getting-started/installation/), e.g.
  with `winget install --id=astral-sh.uv -e` from a terminal (press `Win + R`,
  type `cmd` and press enter to open one). uv takes care of installing Python for
  you, there is nothing else to install.
- Optionally, install [ffmpeg](https://ffmpeg.org/) (`winget install ffmpeg`): it is
  required for flac output and for reading the more exotic audio formats.

<details>
  <summary>I have no coding experience and these are too difficult for me</summary>

> Then a GUI is suitable for you. See [Demucs GUI](https://github.com/CarlGao4/Demucs-Gui)

</details>

### Usage

To use Demucs, just run from a terminal:
```cmd
uvx demucs "PATH_TO_AUDIO_FILE_1" ["PATH_TO_AUDIO_FILE_2" ...]
```
The `"` around the filename are required if the path contains spaces. A simple way to
input these paths is draging a file from a folder into the terminal.

To find out the separated files, you can run this command and open the folders:
```cmd
explorer separated
```

### If you want to use your GPU

If you have a graphic card produced by NVIDIA with more than 2GiB of memory, you can
separate tracks with GPU acceleration. On Windows, the default PyTorch package is CPU
only, so you must instruct uv to fetch it from the PyTorch CUDA index instead:

```cmd
uv tool install demucs --index https://download.pytorch.org/whl/cu126
demucs "PATH_TO_AUDIO_FILE_1"
```

(see the [PyTorch home page](https://pytorch.org/get-started/locally/) for the index
url matching your CUDA version).

### Separating an entire folder

You can use the following command to separate an entire folder of mp3s for instance
(replace the extension `.mp3` if needs be for other file types)
```cmd
cd FOLDER
for %i in (*.mp3) do (uvx demucs "%i")
```
