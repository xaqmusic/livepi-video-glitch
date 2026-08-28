// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
// Scene-wide post effects (the CRT-decay chain), manifest-driven: every
// param in the manifest renders a MappableControl -- a new pass added to
// the manifest shows up here with MIDI + audio binding for free.

import type { AudioBand, EffectsManifest, Mapping, Scene } from "../../api/types";
import MappableControl, { makePreviewSender } from "../../components/MappableControl";
import { useShowStore } from "../../state/showStore";

const MIDI_TYPES = ["cc", "note"];

export default function PostEffectsPanel({ scene, manifest }: { scene: Scene; manifest: EffectsManifest }) {
    const edit = useShowStore((s) => s.edit);

    const targetsParam = (m: Mapping, param: string) =>
        m.targets.some((t) => !t.layerId && t.param.replace(/^postEffects\./, "") === param);

    // amount lives in the target SPAN (see LayerStack): an older binding written
    // as [spec.min, spec.max] reads back as 1.0, so nothing needs migrating.
    const midiMappingFor = (param: string) => {
        const m = scene.mappings.find((x) => MIDI_TYPES.includes(x.trigger.type) && targetsParam(x, param));
        if (!m) return null;
        const target = m.targets.find((t) => !t.layerId && t.param.replace(/^postEffects\./, "") === param)!;
        const spec = manifest.postEffects[param];
        const span = spec ? spec.max - spec.min : 1;
        return { trigger: m.trigger, amount: span ? (target.max - target.min) / span : 1 };
    };

    const audioMappingFor = (param: string) => {
        const m = scene.mappings.find((x) => x.trigger.type === "audioBand" && targetsParam(x, param));
        if (!m) return null;
        const target = m.targets.find((t) => !t.layerId && t.param.replace(/^postEffects\./, "") === param)!;
        return { band: m.trigger.band as AudioBand, amount: target.max };
    };

    const removeBindings = (draftScene: Scene, param: string, types: string[]) => {
        draftScene.mappings = draftScene.mappings
            .map((m) =>
                types.includes(m.trigger.type)
                    ? { ...m, targets: m.targets.filter((t) => !(!t.layerId && t.param.replace(/^postEffects\./, "") === param)) }
                    : m,
            )
            .filter((m) => m.targets.length > 0);
    };

    const bindMidi = (param: string, trigger: Mapping["trigger"], amount = 1) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (!s) return;
            removeBindings(s, param, MIDI_TYPES);
            const spec = manifest.postEffects[param];
            const span = (spec?.max ?? 1) - (spec?.min ?? 0);
            s.mappings.push({
                trigger,
                targets: [{ param: `postEffects.${param}`, min: 0, max: amount * span }],
            });
        });
    };

    const bindAudio = (param: string, band: AudioBand, amount: number) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (!s) return;
            removeBindings(s, param, ["audioBand"]);
            s.mappings.push({
                trigger: { type: "audioBand", band },
                targets: [{ param: `postEffects.${param}`, min: 0, max: amount }],
            });
        });
    };

    const unbind = (param: string, types: string[]) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (s) removeBindings(s, param, types);
        });
    };

    // Same contract as the layer-effect group resets: every post param back to
    // its manifest default (the no-effect state), bindings deliberately left
    // alone. The post chain is one flat group, so this is the whole panel.
    const resetAll = () => {
        const entries = Object.entries(manifest.postEffects);
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (!s) return;
            for (const [param, spec] of entries) s.postEffects[param] = spec.default;
        });
        for (const [param, spec] of entries) {
            makePreviewSender(scene.id, `postEffects.${param}`)(spec.default);
        }
    };

    return (
        <div className="card" style={{ display: "flex", flexDirection: "column", gap: 10 }}>
            <div className="row" style={{ justifyContent: "space-between", alignItems: "baseline" }}>
                <h3 style={{ margin: 0 }}>Post effects <span className="dim" style={{ fontWeight: 400 }}>(whole frame)</span></h3>
                <button
                    className="icon group-reset"
                    title="Reset every post effect to its default (no effect). MIDI and audio bindings are kept."
                    onClick={resetAll}
                >
                    reset
                </button>
            </div>
            {Object.entries(manifest.postEffects).map(([param, spec], i, entries) => (
                <div key={param} style={{ display: "contents" }}>
                {i > 0 && param.split(".")[0] !== entries[i - 1][0].split(".")[0] && <div className="fx-divider" />}
                <MappableControl
                    label={spec.label}
                    spec={spec}
                    value={scene.postEffects[param] ?? spec.default}
                    onChange={(v) =>
                        edit((draft) => {
                            const s = draft.scenes.find((x) => x.id === scene.id);
                            if (s) s.postEffects[param] = v;
                        })
                    }
                    midiMapping={midiMappingFor(param)}
                    audioMapping={audioMappingFor(param)}
                    onBindMidi={(trigger, amount) => bindMidi(param, trigger, amount)}
                    onUnbindMidi={() => unbind(param, MIDI_TYPES)}
                    onBindAudio={(band, amount) => bindAudio(param, band, amount)}
                    onUnbindAudio={() => unbind(param, ["audioBand"])}
                    sendPreview={makePreviewSender(scene.id, `postEffects.${param}`)}
                />
                </div>
            ))}
        </div>
    );
}
