// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
#pragma once

#include <string>
#include <vector>

// Renderer-side network reachability, for the on-screen debug overlay (and,
// later, the connection card the appliance paints onto the projector).
// Someone looking at the video output needs to know where to point a phone
// or laptop -- this enumerates the box's own non-loopback IPv4 interfaces.
// Pure getifaddrs(3): no shell-out, no blocking, safe to poll from the loop
// (throttled by the caller).
namespace NetInfo {

struct Iface {
    std::string name;   // "eth0", "wlan0", ...
    std::string ip;     // dotted IPv4
    bool isAp = false;  // heuristic: address in NetworkManager's shared range
};

// Up, non-loopback, IPv4-bearing interfaces, sorted by name.
std::vector<Iface> interfaces();

// One-line overlay summary, e.g. "eth0 10.0.0.241  wlan0 10.42.0.1 (AP)"
// or "no network".
std::string summary();

}  // namespace NetInfo
