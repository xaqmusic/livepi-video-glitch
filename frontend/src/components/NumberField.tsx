// A number input you can actually TYPE in.
//
// The obvious form -- value={n} onChange={e => set(clamp(parseFloat(e.target.value) || fallback))}
// -- cannot be edited by backspacing. Clearing the field makes parseFloat
// return NaN, and a partial entry like "0." parses to 0, which is falsy; either
// way the `|| fallback` snaps the field back before the next keystroke arrives.
//
// So the field keeps a local DRAFT string while it is being edited: partial and
// empty text is allowed to sit there, a value is committed upstream as soon as
// it parses inside range, and the draft is dropped on blur (clamping whatever
// was typed). The committed value shows through whenever there is no draft, so
// external changes still display.
import { useState } from "react";

export default function NumberField({
    value,
    min,
    max,
    step = 1,
    width = 64,
    title,
    onCommit,
}: {
    value: number;
    min: number;
    max: number;
    step?: number;
    width?: number;
    title?: string;
    onCommit: (n: number) => void;
}) {
    const [draft, setDraft] = useState<string | null>(null);

    return (
        <input
            type="number"
            min={min}
            max={max}
            step={step}
            style={{ width }}
            title={title}
            value={draft ?? String(value)}
            onChange={(e) => {
                setDraft(e.target.value);
                const n = parseFloat(e.target.value);
                // Commit only a genuinely valid value. Anything else stays in
                // the draft, so half-typed input is never written to the show.
                if (Number.isFinite(n) && n >= min && n <= max) onCommit(n);
            }}
            onBlur={() => {
                const n = parseFloat(draft ?? "");
                setDraft(null);
                if (Number.isFinite(n)) onCommit(Math.min(max, Math.max(min, n)));
            }}
        />
    );
}
