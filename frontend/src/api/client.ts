// Thin typed fetch wrapper. Session is a cookie; a 401 anywhere routes to
// /login via the thrown error's status.

import type { Clip, EffectsManifest, JobSummary, Show, UploadJob } from "./types";

export class ApiError extends Error {
    status: number;
    detail: unknown;
    constructor(status: number, detail: unknown) {
        super(typeof detail === "string" ? detail : JSON.stringify(detail));
        this.status = status;
        this.detail = detail;
    }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
    const res = await fetch(path, {
        headers: init?.body instanceof FormData ? undefined : { "Content-Type": "application/json" },
        ...init,
    });
    if (!res.ok) {
        let detail: unknown = res.statusText;
        try {
            detail = (await res.json()).detail;
        } catch {
            /* keep statusText */
        }
        throw new ApiError(res.status, detail);
    }
    return res.json() as Promise<T>;
}

export const api = {
    login: (password: string) => request<{ ok: boolean }>("/api/login", { method: "POST", body: JSON.stringify({ password }) }),
    changePassword: (current: string, newPassword: string) =>
        request<{ ok: boolean }>("/api/auth/password", { method: "POST", body: JSON.stringify({ current, new: newPassword }) }),

    listShows: () => request<{ shows: string[]; active: string | null }>("/api/shows"),
    getShow: (name: string) => request<Show>(`/api/shows/${encodeURIComponent(name)}`),
    putShow: (name: string, show: Show) =>
        request<{ ok: boolean; warnings: string[] }>(`/api/shows/${encodeURIComponent(name)}`, {
            method: "PUT",
            body: JSON.stringify(show),
        }),
    createShow: (name: string, copyFrom?: string) =>
        request<{ ok: boolean; name: string }>("/api/shows", { method: "POST", body: JSON.stringify({ name, copyFrom }) }),
    deleteShow: (name: string) => request<{ ok: boolean }>(`/api/shows/${encodeURIComponent(name)}`, { method: "DELETE" }),
    setActiveShow: (name: string) =>
        request<{ ok: boolean; active: string }>("/api/shows-active", { method: "POST", body: JSON.stringify({ name }) }),

    listClips: () => request<{ clips: Clip[] }>("/api/clips"),
    listJobs: () => request<{ jobs: JobSummary[] }>("/api/clips/jobs"),
    uploadClip: (file: File) => {
        const form = new FormData();
        form.append("file", file);
        return request<{ jobId: string }>("/api/clips", { method: "POST", body: form });
    },
    jobStatus: (jobId: string) => request<UploadJob>(`/api/clips/jobs/${jobId}`),
    deleteClip: (clipId: string) => request<{ ok: boolean }>(`/api/clips/${clipId}`, { method: "DELETE" }),

    getEffects: () => request<EffectsManifest>("/api/effects"),

    command: (cmd: Record<string, unknown>) =>
        request<{ ok: boolean }>("/api/command", { method: "POST", body: JSON.stringify(cmd) }),
};

export function newId(prefix: string): string {
    // NOT crypto.randomUUID(): that API only exists in secure contexts
    // (https / localhost), and this UI is served over plain http on the
    // LAN -- it threw on the very first scene creation, silently eating
    // the edit. getRandomValues works in insecure contexts everywhere.
    const bytes = new Uint8Array(4);
    crypto.getRandomValues(bytes);
    return `${prefix}-${Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("")}`;
}
