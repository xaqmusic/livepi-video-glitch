// Auto-save state + last save's warnings (the layer-budget heaviness
// notes) -- shown wherever a show document is being edited.

import { useShowStore } from "../state/showStore";

export default function SaveStatus() {
    const { dirty, saving, warnings, saveError } = useShowStore();

    return (
        <span className="row" style={{ fontSize: 12, gap: 6 }}>
            {saveError ? (
                <span className="error" title={saveError}>save failed</span>
            ) : saving ? (
                <span className="dim">saving…</span>
            ) : dirty ? (
                <span className="dim">…</span>
            ) : (
                <span className="dim" style={{ color: "var(--ok)" }}>saved</span>
            )}
            {warnings.map((w, i) => (
                <span key={i} className="warn" title={w}>⚠ heavy</span>
            ))}
        </span>
    );
}
