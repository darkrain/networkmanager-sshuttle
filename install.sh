#!/usr/bin/env bash
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if (( EUID == 0 )); then
  echo 'Run as a regular user; this script invokes sudo for system files.' >&2
  exit 1
fi
for program in make gcc pkg-config sshuttle ssh sudo nmcli systemctl dbus-send; do
  command -v "$program" >/dev/null || { echo "Missing command: $program" >&2; exit 1; }
done
pkg-config --exists libnm gtk4 gio-2.0 || {
  echo 'Missing development files for libnm, GTK 4 or GLib/GIO.' >&2
  exit 1
}

libdir=$(pkg-config --variable=libdir libnm)
if [[ $libdir != /* || ! -d $libdir/NetworkManager/VPN ]]; then
  echo "NetworkManager VPN plugin directory not found under $libdir" >&2
  exit 1
fi

make -C "$here" all
sed "s|@LIBDIR@|$libdir|g" "$here/src/nm-sshuttle-service.name.in" > "$here/build/nm-sshuttle-service.name"
sudo -v
sudo install -m 0755 "$here/build/nm-sshuttle-service" "$libdir/nm-sshuttle-service.new"
sudo mv -f "$libdir/nm-sshuttle-service.new" "$libdir/nm-sshuttle-service"
sudo install -m 0644 "$here/build/libnm-vpn-plugin-sshuttle.so" "$libdir/NetworkManager/libnm-vpn-plugin-sshuttle.so.new"
sudo mv -f "$libdir/NetworkManager/libnm-vpn-plugin-sshuttle.so.new" "$libdir/NetworkManager/libnm-vpn-plugin-sshuttle.so"
sudo install -m 0644 "$here/build/nm-sshuttle-service.name" "$libdir/NetworkManager/VPN/nm-sshuttle-service.name"
sudo install -m 0644 "$here/src/nm-sshuttle-service.conf" /etc/dbus-1/system.d/nm-sshuttle-service.conf

if [[ $(systemctl show -p CanReload --value dbus.service) == yes ]]; then
  sudo systemctl reload dbus.service
else
  sudo dbus-send --system --dest=org.freedesktop.DBus --type=method_call / org.freedesktop.DBus.ReloadConfig
fi
sudo systemctl reload NetworkManager

echo 'Installed. Reopen GNOME Settings → Network → VPN → + → sshuttle over SSH.'
