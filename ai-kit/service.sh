#!/usr/bin/env bash
# Tự chạy gateway khi đăng nhập vào Mac và tự bật lại nếu bị tắt (launchd).
#   ./service.sh install | uninstall | restart | logs
set -euo pipefail
cd "$(dirname "$0")"
DIR=$(pwd)
LABEL=com.local.tts-gateway
PLIST=$HOME/Library/LaunchAgents/$LABEL.plist
DOMAIN=gui/$(id -u)

case "${1:-}" in
  install)
    mkdir -p logs "$HOME/Library/LaunchAgents"
    cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array>
    <string>$DIR/bin/tts-gateway</string><string>-config</string><string>$DIR/config.yaml</string>
  </array>
  <key>WorkingDirectory</key><string>$DIR</string>
  <key>EnvironmentVariables</key><dict>
    <key>PATH</key><string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin</string>
  </dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>$DIR/logs/gateway.log</string>
  <key>StandardErrorPath</key><string>$DIR/logs/gateway.log</string>
</dict></plist>
EOF
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    launchctl bootstrap "$DOMAIN" "$PLIST"
    echo "Đã cài. Log: $DIR/logs/gateway.log  (./service.sh logs)";;
  uninstall)
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    rm -f "$PLIST"; echo "Đã gỡ.";;
  restart)
    launchctl kickstart -k "$DOMAIN/$LABEL"; echo "Đã khởi động lại.";;
  logs)
    tail -n 100 -f logs/gateway.log;;
  *) echo "usage: $0 install|uninstall|restart|logs"; exit 1;;
esac
