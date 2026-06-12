# Control Panel hardware checklist

This checklist covers behavior that QEMU cannot fully prove without real or
passed-through hardware.

## QEMU coverage

- Run `make test-network` to boot with an explicit virtual Ethernet NIC and
  verify that the kernel network modules load, DHCP returns `applied:true`, and
  `/api/system/network` reports `online:true`.
- Regular `make test` and `make test-full` still verify graceful
  `available:false` responses for Wi-Fi/Bluetooth when no real adapter is
  visible in the guest.

## Network

- Boot with a visible Ethernet interface and verify `/api/system/network` lists
  a non-loopback interface.
- Run DHCP through `/api/system/network/configure?mode=dhcp&interface=<iface>`
  and verify `applied:true`, an address on the interface, DNS in
  `/etc/resolv.conf`, and a default route.
- Verify `/api/system/network` includes per-interface `ipv4`, `dnsServers`, and
  `routesText`.
- Run `/api/system/network/reconnect` after unplug/replug or rfkill recovery
  and verify the saved DHCP/static config is applied again.
- Run static config with `address`, optional `netmask`, `gateway`, and `dns`;
  verify the interface, route, and resolver state.
- Reboot and verify saved network config is reapplied when `/data/suvos`
  persistence is available.

## Wi-Fi

- Pass through a USB Wi-Fi adapter or run on target hardware with a wireless
  interface.
- Verify `/api/system/wifi/scan` returns nearby SSIDs.
- Connect with `/api/system/wifi/connect` and verify `associated:true`,
  `applied:true`, DHCP, default route, and no PSK in any API response.
- Verify `/api/system/wifi/saved` lists saved SSIDs and never returns PSKs.
- Run `/api/system/wifi/disconnect` and verify `wpa_supplicant` stops without
  deleting the saved network.
- Reboot and verify saved Wi-Fi config is reapplied.
- Run `/api/system/wifi/forget` and verify `wpa_supplicant` stops and the saved
  config is removed.

## Bluetooth

- Verify `/api/system/bluetooth` reports the controller and rfkill state.
- Verify `/api/system/bluetooth/devices` lists known devices.
- Run scan, pair, trust, connect, disconnect, and remove through the gateway
  actions with a test device.

## Audio, Brightness, Time

- Verify `/api/system/audio` and `/api/system/audio/devices` see ALSA playback
  and capture devices.
- Verify output volume/mute through `/api/system/audio/set`,
  `/api/system/audio/mute`, `/api/system/audio/unmute` with optional
  `device=<card>` and `control=<mixer-control>`.
- Verify microphone mute through `/api/system/audio/mute-input` and
  `/api/system/audio/unmute-input`.
- Run `/api/system/audio/test` and confirm the test tone is audible.
- Verify `/api/system/brightness` lists the expected backlight device and
  `/api/system/brightness/set` changes the real panel brightness.
- Verify `/api/system/datetime/timezone` persists the timezone and survives the
  next boot when `/data/suvos` persistence is available.
- Verify `/api/system/datetime/ntp` reports `running:true` after enabling NTP
  on an image with `ntpd`, and that `/tmp/suvos-ntpd.log` has useful errors if
  sync fails.
- Verify `/api/system/datetime/format` switches between `12h` and `24h`.

## Display and GPU

- Verify `/api/system/display` reports DRM outputs, status, and available modes.
- Run `/api/system/display/configure` with `width`, `height`, `scale`, and
  `orientation`; on a Wayland image with `wlr-randr`, verify `applied:true`.
- Verify `/api/system/render` includes the requested `renderProfile`,
  `/dev/dri` devices, framebuffer state, and loaded DRM/GPU modules.

## Power

- Verify `/api/system/power` reports battery supplies and saved power settings.
- Run `/api/system/power/configure` with `profile`,
  `sleepTimeoutSeconds`, and `blankScreenTimeoutSeconds`; on supported hardware,
  verify `powerprofilesctl` and blanking behavior apply.

## Notifications

- Verify `/api/notifications/unread-count` changes when unread/quiet
  notifications are created/read/dismissed.
- Set `/api/notifications/policy/set` for a site/app/source and verify blocked
  notifications return `created:false` and do not write records.
- Enable `/api/notifications/quiet` and verify new notifications are retained
  with `status:"quiet"` until quiet mode is disabled or cleared by status.

## System Diagnostics and Cleanup

- Verify `/api/system/storage` reports `/`, `/system/suvos`, `/data/suvos`, and
  `/tmp` usage.
- Create disposable files under `/data/suvos/cache`, `/data/suvos/tmp`, or
  `/data/suvos/logs`; verify `/api/system/storage/cleanup` removes only those
  safe generated directories.
- Verify `/api/system/versions` reports SuvOS build profile/arch, kernel,
  Chromium version when present, and AEC status when present.
- Set `/api/system/logging?level=debug` before hardware testing. Include
  `/api/system/logs`, `/tmp/suvos-*.log`, serial output, and relevant endpoint
  JSON when reporting hardware failures.
