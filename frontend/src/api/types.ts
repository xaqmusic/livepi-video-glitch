// TypeScript mirrors of the backend's schema v1 (backend/livepi_backend/
// validation.py) and the effects manifest. Keep in sync by hand -- one
// small schema, two owners, per docs/videosynth-backend.md.

export type BlendMode = "normal" | "add" | "screen" | "multiply";
export type AudioBand = "low" | "mid" | "high";

export interface MappingTrigger {
    type: "cc" | "note" | "audioBand";
    number?: number;
    band?: AudioBand;
}

export interface MappingTarget {
    layerId?: string | null;
    param: string;
    min: number;
    max: number;
}

export interface Mapping {
    trigger: MappingTrigger;
    targets: MappingTarget[];
}

export interface Layer {
    id: string;
    kind: "clip" | "generator";
    source: string;
    blendMode: BlendMode;
    opacity: number;
    layerEffects: Record<string, number>;
    params: Record<string, number>;
}

export interface Scene {
    id: string;
    name: string;
    layers: Layer[];
    mappings: Mapping[];
    postEffects: Record<string, number>;
    /** How this scene is ENTERED: effect ramps up over the old frame,
     *  holds while decoders spin up, ramps down over the new scene. */
    transition?: { style: "none" | "fade" | "tear" | "shatter"; duration: number } | null;
}

export interface Show {
    schemaVersion: 1;
    scenes: Scene[];
}

export interface Clip {
    id: string;
    path: string;
    name?: string;
    width?: number;
    height?: number;
    duration?: number;
    thumb?: string | null;
    thumbUrl?: string;
    exists?: boolean;
    /** All-intra re-encode done: tight loop-wrap seeks on trimmed clips. */
    intra?: boolean;
    /** Baked ping-pong boomerangs, as [startKey, endKey] pairs (pingpongKey). */
    pingpong?: [number, number][];
}

export interface ParamSpec {
    label: string;
    type: "float" | "toggle" | "enum";
    min: number;
    max: number;
    default: number;
    /** enum type: labels, mapped evenly onto min..max by index. */
    options?: string[];
    /** layerEffects: which collapsible section this control files into. */
    group?: string;
}

export interface GeneratorSpec {
    label: string;
    /** "notes": fires off played MIDI notes directly (no learn step). */
    trigger?: "notes";
    params: Record<string, ParamSpec>;
}

export interface EffectsManifest {
    postEffects: Record<string, ParamSpec>;
    layerEffects: Record<string, ParamSpec>;
    generators: Record<string, GeneratorSpec>;
    blendModes: BlendMode[];
    audioBands: AudioBand[];
    layerBudget: {
        maxClipLayers: number;
        comfortableMaxHeight: number;
        absoluteMaxHeight: number;
    };
}

export interface Telemetry {
    lastControl: { kind: "cc" | "note" | "none"; number: number; value: number; ts: number };
    frameTimeMs: number;
    fps: number;
    currentSceneId: string;
    currentSceneName: string;
    ts: number;
}

export interface UploadJob {
    state: "queued" | "probing" | "transcoding" | "done" | "error";
    progress: number;
    error?: string | null;
    clip?: Clip | null;
}

export interface JobSummary {
    id: string;
    name: string;
    state: "queued" | "probing" | "transcoding" | "done" | "error";
    progress: number;
    error: string | null;
}
