"""Schema v1 validation + cross-checks, run before any show write. A bad
save gets a 422 here; the renderer never sees invalid data (it still
degrades gracefully if something slips through -- defense in depth, not a
substitute). Returns non-blocking warnings (the layer budget) alongside."""

from typing import Literal, Optional

from pydantic import BaseModel, Field, field_validator

from . import storage
from .effects import load_manifest


class MappingTrigger(BaseModel):
    type: Literal["cc", "note", "audioBand"]
    number: Optional[int] = Field(default=None, ge=0, le=127)
    band: Optional[Literal["low", "mid", "high"]] = None


class MappingTarget(BaseModel):
    layerId: Optional[str] = None
    param: str
    min: float = 0.0
    max: float = 1.0


class Mapping(BaseModel):
    trigger: MappingTrigger
    targets: list[MappingTarget]


class Layer(BaseModel):
    id: str
    kind: Literal["clip", "generator"]
    source: str
    blendMode: Literal["normal", "add", "screen", "multiply"] = "normal"
    opacity: float = Field(default=1.0, ge=0.0, le=1.0)
    layerEffects: dict[str, float] = {}
    params: dict[str, float] = {}


class Scene(BaseModel):
    id: str
    name: str
    layers: list[Layer] = []
    mappings: list[Mapping] = []
    postEffects: dict[str, float] = {}


class Show(BaseModel):
    schemaVersion: Literal[1]
    scenes: list[Scene]

    @field_validator("scenes")
    @classmethod
    def unique_scene_ids(cls, scenes):
        ids = [s.id for s in scenes]
        if len(ids) != len(set(ids)):
            raise ValueError("Scene ids must be unique")
        return scenes


class ValidationProblem(Exception):
    def __init__(self, errors: list[str]):
        self.errors = errors
        super().__init__("; ".join(errors))


def validate_show(document: dict) -> tuple[Show, list[str]]:
    """Full validation: schema (raises pydantic ValidationError), then
    sanitization of stale references, then cross-checks (raises
    ValidationProblem). Returns (show, warnings) -- callers should persist
    show.model_dump(), which is the SANITIZED document.

    Sanitize-don't-reject for anything that goes stale when the effect
    catalog evolves (a param that used to exist, a mapping bound to it):
    rejecting those bricks every save of an existing show the moment the
    manifest changes -- exactly what happened when stutter moved from the
    post chain to the layers. Structural problems (unknown clips,
    generators, layer ids; out-of-range values of KNOWN params) still
    reject."""
    show = Show.model_validate(document)

    manifest = load_manifest()
    post_specs = manifest.get("postEffects", {})
    layer_specs = manifest.get("layerEffects", {})
    generator_names = set(manifest.get("generators", {}).keys())
    library = storage.read_library()
    clip_ids = {c["id"] for c in library.get("clips", [])}
    clip_meta = {c["id"]: c for c in library.get("clips", [])}
    budget = manifest.get("layerBudget", {})

    errors: list[str] = []
    warnings: list[str] = []

    for scene in show.scenes:
        layer_ids = [l.id for l in scene.layers]
        if len(layer_ids) != len(set(layer_ids)):
            errors.append(f'Scene "{scene.name}": duplicate layer ids')

        clip_layers = []
        for layer in scene.layers:
            if layer.kind == "clip":
                clip_layers.append(layer)
                if layer.source not in clip_ids:
                    errors.append(
                        f'Scene "{scene.name}" layer "{layer.id}": unknown clipId "{layer.source}"'
                    )
            else:
                if layer.source not in generator_names:
                    errors.append(
                        f'Scene "{scene.name}" layer "{layer.id}": unknown generator "{layer.source}"'
                    )

        for layer in scene.layers:
            for key in [k for k in layer.layerEffects if k not in layer_specs]:
                layer.layerEffects.pop(key)
                warnings.append(
                    f'Scene "{scene.name}" layer "{layer.id}": dropped retired param "{key}"'
                )
            for key, value in layer.layerEffects.items():
                spec = layer_specs[key]
                if not (spec["min"] <= value <= spec["max"]):
                    errors.append(
                        f'Scene "{scene.name}" layer "{layer.id}": {key}={value} outside '
                        f'[{spec["min"]}, {spec["max"]}]'
                    )

        for key in [k for k in scene.postEffects if k not in post_specs]:
            scene.postEffects.pop(key)
            warnings.append(f'Scene "{scene.name}": dropped retired param "{key}"')
        for key, value in scene.postEffects.items():
            spec = post_specs[key]
            if not (spec["min"] <= value <= spec["max"]):
                errors.append(
                    f'Scene "{scene.name}": {key}={value} outside [{spec["min"]}, {spec["max"]}]'
                )

        for mapping in scene.mappings:
            if mapping.trigger.type in ("cc", "note") and mapping.trigger.number is None:
                errors.append(f'Scene "{scene.name}": {mapping.trigger.type} mapping missing "number"')
            if mapping.trigger.type == "audioBand" and mapping.trigger.band is None:
                errors.append(f'Scene "{scene.name}": audioBand mapping missing "band"')

            kept_targets = []
            for target in mapping.targets:
                if target.layerId and target.layerId not in layer_ids:
                    errors.append(
                        f'Scene "{scene.name}": mapping targets unknown layerId "{target.layerId}"'
                    )
                elif target.layerId and target.param != "opacity" and target.param not in layer_specs:
                    warnings.append(
                        f'Scene "{scene.name}": dropped mapping target to retired param "{target.param}"'
                    )
                    continue
                elif not target.layerId and target.param.removeprefix("postEffects.") not in post_specs:
                    warnings.append(
                        f'Scene "{scene.name}": dropped mapping target to retired param "{target.param}"'
                    )
                    continue
                kept_targets.append(target)
            mapping.targets = kept_targets
        scene.mappings = [m for m in scene.mappings if m.targets]

        # Layer budget: soft warning, never a rejection -- it's art.
        max_clips = budget.get("maxClipLayers", 2)
        if len(clip_layers) > max_clips:
            warnings.append(
                f'Scene "{scene.name}": {len(clip_layers)} clip layers exceeds the measured '
                f"Pi 4 decode budget ({max_clips}) -- expect dropped frames or worse"
            )
        elif len(clip_layers) == 2:
            comfortable = budget.get("comfortableMaxHeight", 720)
            tall = [
                l for l in clip_layers
                if clip_meta.get(l.source, {}).get("height", 0) > comfortable
            ]
            if tall:
                warnings.append(
                    f'Scene "{scene.name}": two clip layers with one above {comfortable}p '
                    f"runs at the 30fps floor on the Pi 4 (heavy scene)"
                )

    if errors:
        raise ValidationProblem(errors)
    return show, warnings
