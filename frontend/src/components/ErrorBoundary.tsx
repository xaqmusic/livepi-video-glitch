// Last-resort catch so a render crash shows a reload prompt instead of a
// silently black page (which is indistinguishable from "the Pi died" when
// you're standing at a rig).

import { Component, type ReactNode } from "react";

export default class ErrorBoundary extends Component<{ children: ReactNode }, { error: Error | null }> {
    state = { error: null as Error | null };

    static getDerivedStateFromError(error: Error) {
        return { error };
    }

    render() {
        if (this.state.error) {
            return (
                <div className="page" style={{ maxWidth: 480, marginTop: "18vh", textAlign: "center" }}>
                    <div className="card">
                        <h2>Something broke in the UI</h2>
                        <p className="dim" style={{ wordBreak: "break-word" }}>{String(this.state.error)}</p>
                        <button className="primary" onClick={() => location.reload()}>
                            Reload
                        </button>
                    </div>
                </div>
            );
        }
        return this.props.children;
    }
}
