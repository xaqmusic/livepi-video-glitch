// Last-resort catch so a render crash shows a reload prompt instead of a
// silently black page (which is indistinguishable from "the Pi died" when
// you're standing at a rig). Shows the COMPONENT STACK too -- for loop
// crashes like "Maximum update depth exceeded" the message alone never
// names the culprit, the stack does -- plus a copy button so the whole
// report can be pasted from a phone/tablet without devtools.

import { Component, type ErrorInfo, type ReactNode } from "react";

interface State {
    error: Error | null;
    componentStack: string;
}

export default class ErrorBoundary extends Component<{ children: ReactNode }, State> {
    state: State = { error: null, componentStack: "" };

    static getDerivedStateFromError(error: Error) {
        return { error };
    }

    componentDidCatch(_error: Error, info: ErrorInfo) {
        this.setState({ componentStack: info.componentStack ?? "" });
    }

    render() {
        if (this.state.error) {
            const report = `${String(this.state.error)}\n\nComponent stack:${this.state.componentStack}`;
            return (
                <div className="page" style={{ maxWidth: 640, marginTop: "10vh", textAlign: "center" }}>
                    <div className="card">
                        <h2>Something broke in the UI</h2>
                        <p className="dim" style={{ wordBreak: "break-word" }}>{String(this.state.error)}</p>
                        {this.state.componentStack && (
                            <pre
                                style={{
                                    textAlign: "left",
                                    fontSize: 11,
                                    maxHeight: 220,
                                    overflow: "auto",
                                    background: "var(--bg)",
                                    padding: 8,
                                    borderRadius: 6,
                                }}
                            >
                                {this.state.componentStack.trim()}
                            </pre>
                        )}
                        <div className="row" style={{ justifyContent: "center" }}>
                            <button onClick={() => navigator.clipboard?.writeText(report)}>Copy report</button>
                            <button className="primary" onClick={() => location.reload()}>
                                Reload
                            </button>
                        </div>
                    </div>
                </div>
            );
        }
        return this.props.children;
    }
}
