// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
// TypeScript mirrors of the backend's schema v1 (backend/livepi_backend/
// validation.py) and the effects manifest. Keep in sync by hand -- one
// small schema, two owners, per docs/videosynth-backend.md.

export type BlendMode = "normal" | "add" | "screen" | "multiply";
export type AudioBand = "low" | "mid" | "high";

// WiFi provisioning (backend/livepi_backend/network.py).
export interface NetworkStatus {
    hostname: string;
    ethernet: { connected: boolean; ip: string | null };
    ap: { active: boolean; ssid: string | null; ip: string | null };
    wifiClient: { connected: boolean; ssid: string | null; ip: string | null };
    online: boolean;
}

export interface WifiNetwork {
    ssid: string;
    signal: number; // 0..100
    security: string; // "open", "WPA2", ...
    inUse: boolean;
}

export interface MappingTrigger {
    type: "cc" | "note" | "audioBand";
    number?: number;
    band?: AudioBand;
}

// A MIDI note/CC bound to a scene action (learned in the gear menu).
export interface SceneTrigger {
    type: "cc" | "note";
    number: number;
}

// Device-global settings from the gear menu (backend/livepi_backend/settings.py).
export interface Settings {
    /** Whether the on-screen setup/QR card appears on every boot. */
    showCardOnBoot: boolean;
    /**
     * Bootloader network-install prompt (the pink QR screen at power-on).
     * null where the board has no such setting -- the gear menu hides the
     * control rather than offering something that cannot work. Stored in the
     * board's EEPROM, so it survives re-flashing the card, and only takes
     * effect at the next power-on.
     */
    netInstallPrompt?: boolean | null;
    /**
     * Boot splash image, as a path relative to the data dir ("" = none, plain
     * black). Set by picking a still image from the clip library. Cleared
     * automatically if that image is deleted.
     */
    splashImage?: string;
    /**
     * Show switching bound to a key/pad or a Program Change. Device-global,
     * unlike the per-SCENE selectors that live in the show: a show name means
     * something across the whole box, a scene id only inside its own show.
     * Bindings to shows that no longer exist are filtered out by the backend.
     */
    showSelect?: ShowBinding[];
    /** Control that advances to the next scene, or null if unbound. */
    sceneAdvance?: SceneTrigger | null;
    /** Control that jumps to the first scene, or null if unbound. */
    sceneBack?: SceneTrigger | null;
    /** Global thermal-rescue toggle: cap render scale when the SoC overheats. */
    thermalRescue?: boolean;
    /** Mask a mid-scene thermal resolution drop with the scene's transition
     *  (else it resizes silently). Default off. */
    thermalTransition?: boolean;
    /** Overall audio-level one-pole smoothing, 0 (snappiest) .. ~0.95 (steadiest). */
    audioSmoothing?: number;
    /** Adaptive audio gain: on = auto-normalize (good for a mic), off = fixed
     *  reference (ride a line source with the Pisound's own gain knob). */
    audioAutoGain?: boolean;
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
    transition?: { style: "none" | "fade" | "tear" | "shatter" | "static"; duration: number } | null;
    /** Internal render resolution, 0.25..1.0 of the display (fps vs sharpness). */
    renderScale?: number;
    /** Timed auto-advance: when true, the scene automatically advances to the
     *  next scene after autoAdvanceSeconds. */
    autoAdvance?: boolean;
    autoAdvanceSeconds?: number;
    /**
     * Direct MIDI selection of THIS scene -- a note/pad, or a Program Change.
     * Stored on the scene (not device-wide) because a scene id only means
     * something inside its own show. null/absent = not directly selectable.
     * CC ranges are deliberately unsupported: sweeping a knob past intermediate
     * scenes would load and tear down each one on the way.
     */
    sceneSelect?: { type: "note" | "program"; number: number } | null;
}

export interface Show {
    schemaVersion: 1;
    scenes: Scene[];
}

export interface Clip {
    id: string;
    /** "clip" (video) or "image" (png/jpg still). Absent on old entries == clip. */
    kind?: "clip" | "image";
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
    /** Image only: downscaled to <=1080 via the optimize action. */
    optimized?: boolean;
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
    /**
     * The box the backend detected at runtime. Advisory: the layerBudget above
     * stays the Pi 4's measured one on every tier, so a show authored on a
     * Pi 5 still plays on a Pi 4. Here so per-model values have somewhere to
     * live, and so the UI can name the hardware.
     */
    hardware: HardwareInfo;
}

/** Mirrors backend/livepi_backend/hardware.py and src/util/Platform.h. */
export interface HardwareInfo {
    /** "desktop" | "pi3" | "pi4" | "pi5" | "pi" (a Pi with no tier of its own). */
    tier: string;
    /** Raw /proc/device-tree/model, e.g. "Raspberry Pi 5 Model B Rev 1.0". */
    model: string;
    /** True when LIVEPI_HARDWARE_TIER forced the tier instead of detection. */
    forced: boolean;
}

export interface Telemetry {
    lastControl: { kind: "cc" | "note" | "none"; number: number; value: number; ts: number };
    frameTimeMs: number;
    fps: number;
    currentSceneId: string;
    currentSceneName: string;
    ts: number;
    /** What the RENDERER detected. Absent from a status file written by an
     *  older bundle, so treat it as optional. */
    hardware?: Pick<HardwareInfo, "tier" | "model">;
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

// In-app updater (appliance only). `available` is false on a dev box with no
// /data/app partition, in which case the UI hides the panel entirely.
export interface UpdateManifest {
    version: string | null;
    gitHash?: string | null;
    builtAt?: string | null;
    channel?: string | null;
}

export interface UpdateStatus {
    available: boolean;
    current?: UpdateManifest | null;
    lastGood?: UpdateManifest | null;
    factory?: UpdateManifest | null;
    pending?: string | null;
    lastApply?: { status: string; message: string; version?: string } | null;
    /** The box's original device code -- its unix/SSH/sudo login and hotspot
     *  key. Unchanged by a web-password change; surfaced here for the owner. */
    deviceCode?: string | null;
}

/** Still images in the clip library that can be used as the boot splash. */
export interface SplashCandidates {
    clips: { id: string; path: string }[];
    selectedId: string | null;
}

/** One show-switch binding: a key/pad or Program Change that loads a show. */
export interface ShowBinding {
    show: string;
    trigger: { type: "note" | "program"; number: number };
}
