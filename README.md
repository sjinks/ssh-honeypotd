# ssh-honeypotd

[![Coverity Scan Build Status](https://scan.coverity.com/projects/3318/badge.svg)](https://scan.coverity.com/projects/3318)
![Build](https://github.com/sjinks/ssh-honeypotd/workflows/Build/badge.svg)

A low-interaction SSH honeypot written in C

## Command Line Options

Usage: ssh-honeypotd [options]...

Mandatory arguments to long options are mandatory for short options too.
  * `-k`, `--host-key FILE`: the file containing the private host key (RSA, DSA, ECDSA, ED25519)
  * `-b`, `--address ADDRESS`: the IP address to bind to (default: `0.0.0.0`)
  * `-p`, `--port PORT`: the port to bind to (default: `22`)
  * `-P`, `--pid FILE`: the PID file (if not specified, the daemon will run in the foreground)
  * `-n`, `--name NAME`: the name of the daemon for syslog (default: `ssh-honeypotd`)
  * `-u`, `--user USER`: drop privileges and switch to this USER (default: `daemon` or `nobody`)
  * `-g`, `--group GROUP`: drop privileges and switch to this GROUP (default: `daemon` or `nogroup`)
  * `-x`, `--no-syslog`: log messages only to stderr (only works with `--foreground`)
  * `-f`, `--foreground`: do not daemonize
  * `-h`, `--help`: display help and exit
  * `-v`, `--version`: output version information and exit

`-k` option must be specified at least once.

Please note:
  * ECDSA keys are supported if ssh-honeypotd is compiled against libssh 0.6.4+
  * ED25519 keys are supported if ssh-honeypotd is compiled against libssh 0.7.0+

## Usage with Docker

```bash
docker run -d \
    --network=host \
    --cap-add=NET_ADMIN \
    --restart=always \
    --read-only \
    --name=ssh-honeypotd \
    -e ADDRESS=0.0.0.0 \
    -e PORT=22 \
    wildwildangel/ssh-honeypotd:latest
```

ssh-honeypotd's behavior in the container can be controlled with the following environment variables:
  * `ADDRESS` (default: 0.0.0.0): the IP address to bind to;
  * `PORT` (default: 22): the port to bind to.

These variables make it easy to have several ssh-honeypotd's running on the same machine, should the need arise.

## Docker Image Variants

ssh-honeypotd's Docker image comes in two flavors:

  1. A standard image based on Alpine 3.13 (or latest stable Alpine): [wildwildangel/ssh-honeypotd](https://hub.docker.com/repository/docker/wildwildangel/ssh-honeypotd).
  2. A minimalistic image based on the `scratch` Docker image: [ssh-honeypotd-min](https://hub.docker.com/repository/docker/wildwildangel/ssh-honeypotd-min)

The `ssh-honeypotd-min` image contains only the statically linked `ssh-honeypotd` binary and the set of the pre-generated SSH keys. This image is a bit smaller than `ssh-honeypotd` but is experimental at the moment. The `ssh-honeypotd` binary in the `ssh-honeypotd-min` image does not support the following command-line options: `--pid`, `--name`, `--user`, `--group`, `--no-syslog`, `--foreground`.
