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
- Reboot and verify saved Wi-Fi config is reapplied.
- Run `/api/system/wifi/forget` and verify `wpa_supplicant` stops and the saved
  config is removed.

## Bluetooth

- Verify `/api/system/bluetooth` reports the controller and rfkill state.
- Verify `/api/system/bluetooth/devices` lists known devices.
- Run scan, pair, trust, connect, disconnect, and remove through the gateway
  actions with a test device.

## Audio, Brightness, Time

- Verify `/api/system/audio` sees ALSA cards and can set/mute/unmute the Master
  control when a sound device is present.
- Verify `/api/system/brightness` lists the expected backlight device and
  `/api/system/brightness/set` changes the real panel brightness.
- Verify `/api/system/datetime/timezone` persists the timezone and survives the
  next boot when `/data/suvos` persistence is available.
