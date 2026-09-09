#!/bin/sh

APP="/usr/bin/rgb-led-app"
PIDFILE="/var/run/rgb-led-app.pid"

case "$1" in
    start)
        echo "start rgb-led-app-init.sh"

        ip addr add 192.168.10.3/24 dev eth0

        "$APP" &
        echo $! > "$PIDFILE"
        ;;
    stop)
        echo "stop rgb-led-app-init.sh"
        if [ -f "$PIDFILE" ]; then
            kill "$(cat $PIDFILE)"
            rm -f "$PIDFILE"
        fi

        ip addr del 192.168.10.3/24 dev eth0
        ;;
esac