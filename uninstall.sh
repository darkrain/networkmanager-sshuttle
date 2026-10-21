#!/usr/bin/env bash
set -euo pipefail

if (( EUID == 0 )); then
  echo 'Run as a regular user; this script invokes sudo for system files.' >&2
  exit 1
fi
libdir=$(pkg-config --variable=libdir libnm)
if [[ $libdir != /* || ! -d $libdir/NetworkManager/VPN ]]; then
  echo "NetworkManager VPN plugin directory not found under $libdir" >&2
  exit 1
fi

echo 'Disconnect any active sshuttle connection before continuing.'
sudo -v
sudo rm -f -- "$libdir/nm-sshuttle-service" \
  "$libdir/NetworkManager/libnm-vpn-plugin-sshuttle.so" \
  "$libdir/NetworkManager/VPN/nm-sshuttle-service.name" \
  /etc/dbus-1/system.d/nm-sshuttle-service.conf
if [[ $(systemctl show -p CanReload --value dbus.service) == yes ]]; then
  sudo systemctl reload dbus.service
else
  sudo dbus-send --system --dest=org.freedesktop.DBus --type=method_call / org.freedesktop.DBus.ReloadConfig
fi
sudo systemctl reload NetworkManager
echo 'Plugin removed. Saved VPN profiles can be deleted in GNOME Settings.'
