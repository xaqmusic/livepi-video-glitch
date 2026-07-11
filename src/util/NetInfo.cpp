#include "util/NetInfo.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>

namespace NetInfo {

std::vector<Iface> interfaces() {
    std::vector<Iface> out;
    struct ifaddrs* head = nullptr;
    if (getifaddrs(&head) != 0) return out;
    for (struct ifaddrs* ifa = head; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;   // IPv4 only
        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) continue;  // skip lo
        if ((ifa->ifa_flags & IFF_UP) == 0) continue;        // skip down links

        char buf[INET_ADDRSTRLEN] = {0};
        auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)) == nullptr) continue;

        Iface i;
        i.name = ifa->ifa_name ? ifa->ifa_name : "?";
        i.ip = buf;
        // NetworkManager's shared-mode hotspot hands the box the 10.42.x.1
        // gateway -- a good-enough tell that this interface is our own AP
        // rather than a normal client/uplink connection.
        i.isAp = i.ip.rfind("10.42.", 0) == 0;
        out.push_back(std::move(i));
    }
    freeifaddrs(head);
    std::sort(out.begin(), out.end(),
              [](const Iface& a, const Iface& b) { return a.name < b.name; });
    return out;
}

std::string summary() {
    const auto ifs = interfaces();
    if (ifs.empty()) return "no network";
    std::string s;
    for (size_t i = 0; i < ifs.size(); ++i) {
        if (i) s += "  ";
        s += ifs[i].name + " " + ifs[i].ip;
        if (ifs[i].isAp) s += " (AP)";
    }
    return s;
}

}  // namespace NetInfo
