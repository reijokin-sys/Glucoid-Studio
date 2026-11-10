// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "wireguard.h"
#include <QAbstractSocket>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QRegularExpression>
#ifdef Q_OS_LINUX
#include <sys/socket.h>
#include <unistd.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#endif

namespace Studio {
QString wireguardNameError(const QString& name) {
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.-]{1,15}$"));
    return valid.match(name).hasMatch() ? QString() : QCoreApplication::translate("Studio", "Anna WireGuard-liittymän nimi (esimerkiksi wg0).");
}
namespace {
#ifdef Q_OS_LINUX
bool isActiveWireguard(const QString& name) {
    if (!wireguardNameError(name).isEmpty()) return false;
    const auto interface = QNetworkInterface::interfaceFromName(name);
    if (!interface.isValid() || !interface.flags().testFlag(QNetworkInterface::IsUp)) return false;
    const int descriptor = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (descriptor < 0) return false;
    struct Close {
        int descriptor;
        ~Close() { ::close(descriptor); }
    } close{descriptor};
    const timeval timeout{0, 250000};
    if (::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) return false;
    struct {
        nlmsghdr header;
        ifinfomsg interface;
    } request{};
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
    request.header.nlmsg_type = RTM_GETLINK;
    request.header.nlmsg_flags = NLM_F_REQUEST;
    request.header.nlmsg_seq = 1;
    request.interface.ifi_family = AF_UNSPEC;
    request.interface.ifi_index = interface.index();
    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;
    if (::sendto(descriptor, &request, request.header.nlmsg_len, 0, reinterpret_cast<sockaddr*>(&kernel), sizeof(kernel)) < 0) return false;
    alignas(nlmsghdr) char buffer[16384];
    int remaining = int(::recv(descriptor, buffer, sizeof(buffer), 0));
    for (auto* message = reinterpret_cast<nlmsghdr*>(buffer); NLMSG_OK(message, remaining); message = NLMSG_NEXT(message, remaining)) {
        if (message->nlmsg_type != RTM_NEWLINK || message->nlmsg_seq != 1 || message->nlmsg_len < NLMSG_LENGTH(sizeof(ifinfomsg))) continue;
        const auto* info = reinterpret_cast<ifinfomsg*>(NLMSG_DATA(message));
        if (info->ifi_index != interface.index()) continue;
        int length = IFLA_PAYLOAD(message);
        for (auto* attribute = IFLA_RTA(info); RTA_OK(attribute, length); attribute = RTA_NEXT(attribute, length)) {
            if ((attribute->rta_type & NLA_TYPE_MASK) != IFLA_LINKINFO) continue;
            int nestedLength = RTA_PAYLOAD(attribute);
            for (auto* nested = reinterpret_cast<rtattr*>(RTA_DATA(attribute)); RTA_OK(nested, nestedLength); nested = RTA_NEXT(nested, nestedLength)) {
                if ((nested->rta_type & NLA_TYPE_MASK) == IFLA_INFO_KIND) {
                    const QByteArray kind(static_cast<const char*>(RTA_DATA(nested)), RTA_PAYLOAD(nested));
                    return kind == QByteArray("wireguard\0", 10);
                }
            }
        }
    }
    return false;
}
#endif
const QString unavailable = QCoreApplication::translate("Studio", "Vaadittu WireGuard-yhteys ei ole käytettävissä. Tarkista tunneli ja liittymän nimi.");
} // namespace
QString bindWireguardSocket(QAbstractSocket& socket, const QString& name, bool ipv6) {
#ifdef Q_OS_LINUX
    if (!isActiveWireguard(name)) return unavailable;
    // A numeric destination, no proxy and a family-specific socket prevent Qt
    // from resolving or replacing this socket for another address family.
    socket.setProxy(QNetworkProxy::NoProxy);
    if (!socket.bind(ipv6 ? QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4, 0)) return unavailable;
    const auto encoded = name.toLatin1();
    if (::setsockopt(int(socket.socketDescriptor()), SOL_SOCKET, SO_BINDTODEVICE, encoded.constData(), socklen_t(encoded.size() + 1)) != 0) {
        socket.abort();
        return QCoreApplication::translate("Studio", "WireGuard-sidonta ei onnistunut. Hakua ei lähetetty.");
    }
    return verifyWireguardSocket(socket, name);
#else
    Q_UNUSED(socket);
    Q_UNUSED(name);
    Q_UNUSED(ipv6);
    return QCoreApplication::translate("Studio", "WireGuard-verkkohaku vaatii Linuxin liittymäsidonnan.");
#endif
}
QString verifyWireguardSocket(const QAbstractSocket& socket, const QString& name) {
#ifdef Q_OS_LINUX
    char bound[256]{};
    socklen_t size = sizeof(bound);
    if (::getsockopt(int(socket.socketDescriptor()), SOL_SOCKET, SO_BINDTODEVICE, bound, &size) != 0 ||
        QByteArray(bound, int(size)).split('\0').value(0) != name.toLatin1() || !isActiveWireguard(name))
        return unavailable;
    return {};
#else
    Q_UNUSED(socket);
    Q_UNUSED(name);
    return unavailable;
#endif
}
} // namespace Studio
