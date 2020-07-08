#!/bin/sh

LISTEN_ADDRESS=${ADDRESS:-0.0.0.0}
LISTEN_PORT=${PORT:-22}

exec /usr/bin/ssh-honeypotd -b "${LISTEN_ADDRESS}" -p "${LISTEN_PORT}" $@
