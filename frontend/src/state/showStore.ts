// The draft show document being edited, with debounced auto-save: every
// edit marks dirty and schedules a whole-document PUT ~400ms later (the
// backend's atomic write + the renderer's per-frame poll turn that into a
// sub-second save->see loop; instant feedback additionally flows through
// /api/command from MappableControl). Warnings from the last save (the
// layer-budget heaviness notes) surface in the UI.

import { create } from "zustand";

import { api } from "../api/client";
import type { Scene, Show } from "../api/types";

const AUTOSAVE_MS = 400;

interface ShowState {
    name: string | null;
    show: Show | null;
    dirty: boolean;
    saving: boolean;
    warnings: string[];
    saveError: string | null;

    open: (name: string) => Promise<void>;
    close: () => void;
    /** Apply an edit to the draft and schedule an auto-save. */
    edit: (fn: (show: Show) => void) => void;
    saveNow: () => Promise<void>;
}

let saveTimer: ReturnType<typeof setTimeout> | null = null;

export const useShowStore = create<ShowState>((set, get) => ({
    name: null,
    show: null,
    dirty: false,
    saving: false,
    warnings: [],
    saveError: null,

    open: async (name) => {
        const show = await api.getShow(name);
        set({ name, show, dirty: false, warnings: [], saveError: null });
    },

    close: () => {
        if (saveTimer) clearTimeout(saveTimer);
        set({ name: null, show: null, dirty: false });
    },

    edit: (fn) => {
        const { show } = get();
        if (!show) return;
        // Structured clone keeps the store the single owner of the document.
        const next = structuredClone(show);
        fn(next);
        set({ show: next, dirty: true });
        if (saveTimer) clearTimeout(saveTimer);
        saveTimer = setTimeout(() => void get().saveNow(), AUTOSAVE_MS);
    },

    saveNow: async () => {
        const { name, show, saving } = get();
        if (!name || !show) return;
        if (saving) {
            // A save is in flight; reschedule behind it.
            if (saveTimer) clearTimeout(saveTimer);
            saveTimer = setTimeout(() => void get().saveNow(), AUTOSAVE_MS);
            return;
        }
        set({ saving: true });
        try {
            const result = await api.putShow(name, show);
            set({ dirty: false, warnings: result.warnings, saveError: null });
        } catch (e) {
            set({ saveError: e instanceof Error ? e.message : String(e) });
        } finally {
            set({ saving: false });
        }
    },
}));

export function findScene(show: Show, sceneId: string): Scene | undefined {
    return show.scenes.find((s) => s.id === sceneId);
}
