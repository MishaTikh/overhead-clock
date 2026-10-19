## What this changes

## How it was tested
- [ ] Logic tests pass (`firmware/test/host_test.cpp`)
- [ ] Firmware builds (`pio run -e tc001`)
- [ ] Tried on a TC001 (say what you checked)

## Checklist
- [ ] New features are optional and off by default (unless core to showing planes)
- [ ] No new network calls, or they use HTTPS with pinned roots, size limits, and respect the service's terms
- [ ] The clock still tells time if this fails
- [ ] No personal data (addresses, coordinates, Wi-Fi names) in code, tests or screenshots
