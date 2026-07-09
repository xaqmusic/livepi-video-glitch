// Scene-wide post effects (the CRT-decay chain), manifest-driven: every
// param in the manifest renders a MappableControl -- a new pass added to
// the manifest shows up here with Learn support, zero frontend changes.

import type { EffectsManifest, Mapping, Scene } from "../../api/types";
import MappableControl, { makePreviewSender } from "../../components/MappableControl";
import { useShowStore } from "../../state/showStore";

export default function PostEffectsPanel({ scene, manifest }: { scene: Scene; manifest: EffectsManifest }) {
    const edit = useShowStore((s) => s.edit);

    const mappingFor = (param: string) =>
        scene.mappings.find((m) => m.targets.some((t) => !t.layerId && t.param.replace(/^postEffects\./, "") === param)) ?? null;

    const bind = (param: string, trigger: Mapping["trigger"]) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (!s) return;
            s.mappings = s.mappings.filter(
                (m) => !m.targets.some((t) => !t.layerId && t.param.replace(/^postEffects\./, "") === param),
            );
            const spec = manifest.postEffects[param];
            s.mappings.push({
                trigger,
                targets: [{ param: `postEffects.${param}`, min: spec?.min ?? 0, max: spec?.max ?? 1 }],
            });
        });
    };

    const unbind = (param: string) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (!s) return;
            s.mappings = s.mappings
                .map((m) => ({
                    ...m,
                    targets: m.targets.filter((t) => !(!t.layerId && t.param.replace(/^postEffects\./, "") === param)),
                }))
                .filter((m) => m.targets.length > 0);
        });
    };

    return (
        <div className="card" style={{ display: "flex", flexDirection: "column", gap: 10 }}>
            <h3 style={{ margin: 0 }}>Post effects <span className="dim" style={{ fontWeight: 400 }}>(whole frame)</span></h3>
            {Object.entries(manifest.postEffects).map(([param, spec]) => (
                <MappableControl
                    key={param}
                    label={spec.label}
                    spec={spec}
                    value={scene.postEffects[param] ?? spec.default}
                    onChange={(v) =>
                        edit((draft) => {
                            const s = draft.scenes.find((x) => x.id === scene.id);
                            if (s) s.postEffects[param] = v;
                        })
                    }
                    mapping={mappingFor(param)}
                    onBind={(trigger) => bind(param, trigger)}
                    onUnbind={() => unbind(param)}
                    sendPreview={makePreviewSender(scene.id, `postEffects.${param}`)}
                />
            ))}
        </div>
    );
}
