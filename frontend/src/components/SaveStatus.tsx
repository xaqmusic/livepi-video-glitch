// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
// Auto-save state + last save's warnings (the layer-budget heaviness
// notes) -- shown wherever a show document is being edited.

import { useShowStore } from "../state/showStore";

export default function SaveStatus() {
    const { dirty, saving, warnings, saveError } = useShowStore();

    return (
        // Fixed min-width: this cycles "saved" -> "…" -> "saving…" on EVERY
        // keystroke, and without a reserved width the whole row reflows as you
        // type -- which reads as the controls jumping around under the cursor.
        <span className="row" style={{ fontSize: 12, gap: 6, minWidth: 62 }}>
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
