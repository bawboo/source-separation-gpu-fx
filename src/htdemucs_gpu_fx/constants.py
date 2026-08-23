from __future__ import annotations

from dataclasses import dataclass
from math import ceil


DEFAULT_PROCESSING_GUARD_SAMPLES = 22_050


@dataclass(frozen=True)
class StreamSpec:
    """Audio geometry used by the shipped full-size HTDemucs model."""

    sample_rate: int = 44_100
    input_channels: int = 2
    source_names: tuple[str, ...] = ("drums", "bass", "other", "vocals")
    segment_samples: int = 343_980
    hop_samples: int = 257_985
    overlap_samples: int = 85_995

    def __post_init__(self) -> None:
        if self.segment_samples != self.hop_samples + self.overlap_samples:
            raise ValueError("segment_samples must equal hop_samples + overlap_samples")
        if self.sample_rate <= 0 or self.input_channels <= 0:
            raise ValueError("sample_rate and input_channels must be positive")
        if not self.source_names:
            raise ValueError("at least one source name is required")

    @property
    def segment_seconds(self) -> float:
        return self.segment_samples / self.sample_rate

    @property
    def hop_seconds(self) -> float:
        return self.hop_samples / self.sample_rate

    @property
    def overlap_seconds(self) -> float:
        return self.overlap_samples / self.sample_rate

    def block_quantized_algorithmic_latency(self, host_block_size: int) -> int:
        """Legacy boundary-only latency, excluding inference execution time."""

        if host_block_size <= 0:
            raise ValueError("host_block_size must be positive")
        buffered = ceil(self.hop_samples / host_block_size) * host_block_size
        return buffered + self.overlap_samples

    def reported_plugin_latency_samples(
        self,
        processing_guard_samples: int = DEFAULT_PROCESSING_GUARD_SAMPLES,
    ) -> int:
        """Production PDC: full model lookahead plus a fixed inference guard."""

        if processing_guard_samples < 0:
            raise ValueError("processing_guard_samples must be non-negative")
        return self.segment_samples + processing_guard_samples


HTDEMUCS_SPEC = StreamSpec()
