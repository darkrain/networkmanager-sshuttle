# NetworkManager sshuttle plugin for GNOME

Add, edit, delete and toggle SSH based sshuttle connections in **GNOME Settings → Network → VPN**. Each server is a standard NetworkManager VPN profile. The plugin provides a GTK 4 editor and a NetworkManager D-Bus service that starts and stops sshuttle.

The project is currently tested on Manjaro with GNOME 50, NetworkManager 1.58 and sshuttle 1.3.2. It uses Linux nftables and systemd. Other distributions with the same interfaces may work but have not been tested.

## Requirements

- NetworkManager, GNOME Settings with GTK 4, sshuttle and OpenSSH
- C compiler, `make`, `pkg-config`, development files for `libnm` and GTK 4
- `sudo` and systemd for installation
- An SSH server with Python and a private key that can authenticate without interaction

Check the build dependencies with `pkg-config --modversion libnm gtk4`.

## Install

```bash
git clone https://github.com/darkrain/networkmanager-sshuttle.git
cd networkmanager-sshuttle
./install.sh
```

Run the script as your regular user. It builds locally, asks for `sudo` for the system files, then reloads D-Bus and NetworkManager. It creates **no VPN profiles** and does not start a connection. Reopen GNOME Settings after installation. Run `./install.sh` again to update the plugin.

In GNOME Settings, add **sshuttle over SSH** under Network → VPN. Enter the server's IPv4 address, SSH user and port, and the absolute path to a private key. The server field starts empty. The editor suggests your local SSH user and an existing `~/.ssh/id_ed25519` or `~/.ssh/id_rsa` key when found; change these to match your server.

On the first connection, OpenSSH automatically accepts the server's host key and stores it in `/var/lib/NetworkManager/sshuttle/known_hosts`, owned by root. Later connections reject a changed key. This removes the separate first-time `ssh` command, but the first connection uses trust on first use: if someone intercepts that first connection, the wrong key could be stored. Verify the server fingerprint through an independent channel when that risk matters to you.

SSH authentication remains noninteractive (`BatchMode=yes`). The current service does not use your desktop SSH agent, so a passphrase protected private key still needs separate support. A dedicated key that can authenticate without a prompt works now. Profiles created by earlier versions with a `known-hosts` setting keep using that file and strict host-key checking.

## How it works

- NetworkManager controls the profile and shows its status in GNOME.
- GNOME Shell's authentication helper confirms that this SSH key based profile has no VPN password, so saved profiles open in Settings without waiting for a secret prompt.
- The service runs sshuttle as root and lets sshuttle manage its own nftables rules and DNS interception. Host keys for new profiles are stored in a root-owned file.
- The server's own IPv4 address and private IPv4 ranges are excluded from forwarding to keep SSH and local network access working.
- The plugin reports the connection active after sshuttle logs a successful server handshake. When sshuttle exits, NetworkManager sees a failure.

This is a transparent **TCP** tunnel with ordinary DNS interception, not a full IP VPN. Arbitrary UDP and IPv6 traffic are outside the tunnel. Applications using DNS over HTTPS may resolve names outside sshuttle's DNS interception. There is no kill switch. Do not rely on it to tunnel those traffic types.

The initial release supports **IPv4 literal server addresses**. SSH aliases and DNS names are not accepted in the server field. Only one sshuttle profile should be active at a time.

## Build and test

```bash
make
make test
```

`make test` needs a graphical GTK 4 display. It checks that the editor loads, a new profile has an empty server field, and an existing profile retains its saved server address. To inspect service behavior, use `journalctl -b | grep nm-sshuttle` and `nmcli connection show --active`.

## Uninstall

Disconnect sshuttle profiles, then run `./uninstall.sh`. It removes the installed service, editor library, `.name` metadata and D-Bus policy. Saved NetworkManager profiles and the root-owned host-key file are left in place so you can review or remove them separately.

## License

MIT. See [LICENSE](LICENSE).
