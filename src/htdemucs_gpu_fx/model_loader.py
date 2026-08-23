from __future__ import annotations

import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import torch
from torch import Tensor, nn

from .constants import HTDEMUCS_SPEC, StreamSpec
from .engine import ModelContractError


@dataclass(frozen=True)
class WeightComparison:
    scripted_tensors: int
    checkpoint_tensors: int
    common_tensors: int
    scripted_numel: int
    checkpoint_numel: int
    missing_from_scripted: tuple[str, ...]
    missing_from_checkpoint: tuple[str, ...]
    different_tensors: tuple[str, ...]

    @property
    def exact(self) -> bool:
        return not (
            self.missing_from_scripted
            or self.missing_from_checkpoint
            or self.different_tensors
        )

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["exact"] = self.exact
        return result


def load_demucs_checkpoint(
    checkpoint_path: str | Path,
    demucs_repo: str | Path,
    dependency_dir: str | Path | None = None,
) -> nn.Module:
    """Load the eager architecture without downloading anything."""

    checkpoint = Path(checkpoint_path)
    repository = Path(demucs_repo)
    if not checkpoint.is_file():
        raise FileNotFoundError(checkpoint)
    if not (repository / "demucs" / "states.py").is_file():
        raise FileNotFoundError(f"not a Demucs source tree: {repository}")

    # torch is imported before this path is added. bench_deps contains an unused
    # CPU torch wheel, but supplies small pure-Python Demucs dependencies.
    if dependency_dir is not None:
        dependency = Path(dependency_dir)
        if dependency.is_dir() and str(dependency) not in sys.path:
            sys.path.insert(0, str(dependency))
    if str(repository) not in sys.path:
        sys.path.insert(0, str(repository))

    from demucs.states import load_model

    return load_model(str(checkpoint)).eval()


def load_demucs_registry_model(
    model_name: str,
    models_directory: str | Path,
    demucs_repo: str | Path,
    dependency_dir: str | Path | None = None,
) -> nn.Module:
    """Load a single model or BagOfModels from a fully local registry."""

    models = Path(models_directory)
    repository = Path(demucs_repo)
    if not models.is_dir():
        raise FileNotFoundError(f"not a Demucs model registry: {models}")
    if not (repository / "demucs" / "pretrained.py").is_file():
        raise FileNotFoundError(f"not a Demucs source tree: {repository}")
    if dependency_dir is not None:
        dependency = Path(dependency_dir)
        if dependency.is_dir() and str(dependency) not in sys.path:
            sys.path.insert(0, str(dependency))
    if str(repository) not in sys.path:
        sys.path.insert(0, str(repository))

    from demucs.pretrained import get_model

    return get_model(model_name, repo=models).eval()


def validate_eager_model(model: nn.Module, spec: StreamSpec = HTDEMUCS_SPEC) -> None:
    sources = tuple(str(source) for source in model.sources)
    if sources != spec.source_names:
        raise ModelContractError(f"checkpoint sources {sources} != {spec.source_names}")
    if int(model.samplerate) != spec.sample_rate:
        raise ModelContractError(f"checkpoint sample rate {model.samplerate} != {spec.sample_rate}")
    segment_samples = round(float(model.segment) * int(model.samplerate))
    if segment_samples != spec.segment_samples:
        raise ModelContractError(
            f"checkpoint segment {segment_samples} != {spec.segment_samples} samples"
        )


def compare_model_weights(scripted_model: nn.Module, checkpoint_model: nn.Module) -> WeightComparison:
    scripted_state = scripted_model.state_dict()
    checkpoint_state = checkpoint_model.state_dict()
    scripted_keys = set(scripted_state)
    checkpoint_keys = set(checkpoint_state)
    common = sorted(scripted_keys & checkpoint_keys)
    different = []
    for key in common:
        left: Tensor = scripted_state[key]
        right: Tensor = checkpoint_state[key]
        if left.shape != right.shape or not torch.equal(
            left.detach().cpu().float(), right.detach().cpu().float()
        ):
            different.append(key)
    return WeightComparison(
        scripted_tensors=len(scripted_state),
        checkpoint_tensors=len(checkpoint_state),
        common_tensors=len(common),
        scripted_numel=sum(value.numel() for value in scripted_state.values()),
        checkpoint_numel=sum(value.numel() for value in checkpoint_state.values()),
        missing_from_scripted=tuple(sorted(checkpoint_keys - scripted_keys)),
        missing_from_checkpoint=tuple(sorted(scripted_keys - checkpoint_keys)),
        different_tensors=tuple(different),
    )
