# Security and Local Configuration

This repository intentionally contains placeholders instead of real Wi-Fi credentials or LAN addresses.

Before compiling a firmware sketch:

1. Replace `YOUR_WIFI_SSID` and `YOUR_WIFI_PASSWORD` in the selected sketch, or in `firmware/include/config.h` for the PlatformIO project.
2. Replace the example daemon host in `WEBHOOK_BASE` or `DAEMON_BASE` with the local machine running `daemon/app.py`.
3. Do not commit those local edits. Restore the placeholders before creating a commit, or keep local-only changes in an ignored file.

The daemon listens on the local network without authentication by default. Do not expose port 8899 directly to the public Internet. Restrict access with a firewall or add authentication before using it outside a trusted LAN.

If real credentials were ever committed to a repository, rotate them even after removing the files from the working tree because Git history may retain them.
