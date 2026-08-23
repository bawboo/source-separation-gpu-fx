from __future__ import annotations

from pathlib import Path
from typing import Any, Callable

import torch
from torch import Tensor, nn

from .constants import HTDEMUCS_SPEC, StreamSpec


class ModelContractError(RuntimeError):
    """The .nm archive or separator does not match the expected HTDemucs ABI."""


def load_nm_archive(model_path: str | Path, device: str | torch.device) -> Any:
    """Load the Neutone archive as a CPU oracle.

    The traced HTDemucs graph captured explicit CPU device constants. Loading it
    with CUDA ``map_location`` moves weights but not all intermediate tensors and
    fails at inference. The production GPU path must use the eager checkpoint.
    """

    path = Path(model_path)
    if not path.is_file():
        raise FileNotFoundError(path)
    target = torch.device(device)
    if target.type != "cpu":
        raise ModelContractError(
            "this .nm graph is CPU-traced; use it as a CPU oracle and run the eager checkpoint on CUDA"
        )
    archive = torch.jit.load(str(path), map_location=target).eval()
    validate_nm_archive(archive, target)
    return archive


def validate_nm_archive(
    archive: Any,
    device: str | torch.device,
    spec: StreamSpec = HTDEMUCS_SPEC,
) -> None:
    target = torch.device(device)
    if not hasattr(archive, "nrb") or not hasattr(archive.nrb, "model"):
        raise ModelContractError("archive must expose nrb.model")

    nrb = archive.nrb
    native_rates = tuple(int(v) for v in nrb.get_native_sample_rates())
    native_buffers = tuple(int(v) for v in nrb.get_native_buffer_sizes())
    model_delay = int(nrb.calc_model_delay_samples())
    if native_rates != (spec.sample_rate,):
        raise ModelContractError(f"native sample rates {native_rates} != {(spec.sample_rate,)}")
    if native_buffers != (spec.hop_samples,):
        raise ModelContractError(f"native buffers {native_buffers} != {(spec.hop_samples,)}")
    if model_delay != spec.overlap_samples:
        raise ModelContractError(f"model delay {model_delay} != {spec.overlap_samples}")

    expected_shapes = {
        "in_buf": (spec.input_channels, spec.segment_samples),
        "out_buf": (len(spec.source_names), spec.input_channels, spec.segment_samples),
        "fade_up": (spec.overlap_samples,),
        "fade_down": (spec.overlap_samples,),
    }
    for name, expected_shape in expected_shapes.items():
        value = getattr(nrb, name, None)
        if not isinstance(value, Tensor):
            raise ModelContractError(f"nrb.{name} is not a tensor")
        if tuple(value.shape) != expected_shape:
            raise ModelContractError(
                f"nrb.{name} shape {tuple(value.shape)} != {expected_shape}"
            )
        if value.device != target:
            raise ModelContractError(f"nrb.{name} is on {value.device}, expected {target}")


def reset_neutone_stream_state(nrb: Any) -> None:
    """Reset only the state that participates in the wrapper's streaming OLA."""

    for name in ("in_buf", "in_buf_tmp", "out_buf"):
        value = getattr(nrb, name, None)
        if isinstance(value, Tensor):
            value.zero_()


def neutone_process_hop(nrb: Any, block_index: int, hop: Tensor) -> Tensor:
    """Run the archive wrapper and stack its four stereo outputs."""

    outputs = nrb.forward(block_index, [hop], None, None, None)
    if len(outputs) != len(HTDEMUCS_SPEC.source_names):
        raise ModelContractError(f"wrapper returned {len(outputs)} sources")
    return torch.stack(list(outputs), dim=0)


class StreamingOLAEngine:
    """Independent reference implementation of the .nm overlap-add algorithm.

    This class intentionally does not call ``nrb.forward``. It invokes only the raw
    separator and owns its input history, fade curves and previous prediction tail.
    It is therefore the reference that the future out-of-process GPU worker can use.
    """

    def __init__(
        self,
        separator: nn.Module,
        device: str | torch.device,
        spec: StreamSpec = HTDEMUCS_SPEC,
        validate_finite: bool = True,
        inference: Callable[[Tensor], Tensor] | None = None,
    ) -> None:
        self.separator = separator.eval()
        self.device = torch.device(device)
        self.spec = spec
        self.validate_finite = validate_finite
        self.inference = inference
        self._input_segment: Tensor
        self._previous_output_tail: Tensor
        self._fade_up: Tensor
        self._fade_down: Tensor
        self.reset()

    def reset(self) -> None:
        dtype = self._model_dtype()
        self._input_segment = torch.zeros(
            self.spec.input_channels,
            self.spec.segment_samples,
            dtype=dtype,
            device=self.device,
        )
        self._previous_output_tail = torch.zeros(
            len(self.spec.source_names),
            self.spec.input_channels,
            self.spec.overlap_samples,
            dtype=dtype,
            device=self.device,
        )
        self._fade_up = torch.linspace(
            0.0,
            1.0,
            self.spec.overlap_samples,
            dtype=dtype,
            device=self.device,
        )
        self._fade_down = torch.linspace(
            1.0,
            0.0,
            self.spec.overlap_samples,
            dtype=dtype,
            device=self.device,
        )

    def _model_dtype(self) -> torch.dtype:
        try:
            return next(self.separator.parameters()).dtype
        except StopIteration:
            return torch.float32

    @torch.inference_mode()
    def process_hop(self, hop: Tensor) -> Tensor:
        expected = (self.spec.input_channels, self.spec.hop_samples)
        if tuple(hop.shape) != expected:
            raise ValueError(f"hop shape {tuple(hop.shape)} != {expected}")
        if hop.device != self.device:
            raise ValueError(f"hop is on {hop.device}, expected {self.device}")
        if hop.dtype != self._input_segment.dtype:
            raise ValueError(f"hop dtype {hop.dtype} != {self._input_segment.dtype}")
        if self.validate_finite and not torch.isfinite(hop).all():
            raise ValueError("hop contains NaN or Inf")

        overlap = self.spec.overlap_samples
        hop_size = self.spec.hop_samples
        self._input_segment[:, :overlap].copy_(self._input_segment[:, -overlap:])
        self._input_segment[:, -hop_size:].copy_(hop)

        batch = self._input_segment.unsqueeze(0)
        prediction = (
            self.inference(batch) if self.inference is not None else self.separator(batch)
        ).squeeze(0)
        expected_output = (
            len(self.spec.source_names),
            self.spec.input_channels,
            self.spec.segment_samples,
        )
        if tuple(prediction.shape) != expected_output:
            raise ModelContractError(
                f"separator output shape {tuple(prediction.shape)} != {expected_output}"
            )
        if self.validate_finite and not torch.isfinite(prediction).all():
            raise ModelContractError("separator output contains NaN or Inf")

        # Preserve the new tail before changing the prediction head in place.
        next_tail = prediction[..., -overlap:].clone()
        prediction[..., :overlap].mul_(self._fade_up)
        self._previous_output_tail.mul_(self._fade_down)
        prediction[..., :overlap].add_(self._previous_output_tail)
        self._previous_output_tail.copy_(next_tail)
        return prediction[..., :hop_size]

    @torch.inference_mode()
    def separate_aligned(self, audio: Tensor) -> Tensor:
        """Simulate streaming, flush once, remove pre-roll, and return input length."""

        expected_channels = self.spec.input_channels
        if audio.ndim != 2 or audio.shape[0] != expected_channels:
            raise ValueError(f"audio must have shape ({expected_channels}, samples)")
        n_samples = int(audio.shape[-1])
        if n_samples == 0:
            return torch.empty(
                len(self.spec.source_names),
                expected_channels,
                0,
                dtype=self._input_segment.dtype,
                device=self.device,
            )

        self.reset()
        audio = audio.to(device=self.device, dtype=self._input_segment.dtype)
        hop_size = self.spec.hop_samples
        n_hops = (n_samples + hop_size - 1) // hop_size
        padded = torch.zeros(
            expected_channels,
            n_hops * hop_size,
            dtype=audio.dtype,
            device=self.device,
        )
        padded[:, :n_samples].copy_(audio)

        emitted = []
        for index in range(n_hops):
            emitted.append(self.process_hop(padded[:, index * hop_size : (index + 1) * hop_size]))
        emitted.append(
            self.process_hop(
                torch.zeros(
                    expected_channels,
                    hop_size,
                    dtype=audio.dtype,
                    device=self.device,
                )
            )
        )
        stream = torch.cat(emitted, dim=-1)
        start = self.spec.overlap_samples
        return stream[..., start : start + n_samples]
