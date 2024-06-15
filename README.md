# ssh-honeypotd

[![Coverity Scan Build Status](https://scan.coverity.com/projects/3318/badge.svg)](https://scan.coverity.com/projects/3318)
![Build](https://github.com/sjinks/ssh-honeypotd/workflows/Build/badge.svg)

A low-interaction SSH honeypot written in C.

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

`-k` option must be specified at least once if ssh-honeypots is compiled against libssh prior to 0.8.0 (note that in Ubuntu (and possibly Debian), libssh 0.8.0 is detected as 0.7.0 because of a bug in `libssh.h`). For newer libssh versions, the host key is generated automatically (RSA 2048 bits).

The minimum supported `libssh` version is 0.7.0.

## Usage with Docker

```bash
docker run -d \
    --network=host \
    --cap-add=NET_BIND_SERVICE \
    --restart=always \
    --read-only \
    --name=ssh-honeypotd \
    -e ADDRESS=0.0.0.0 \
    -e PORT=22 \
    wildwildangel/ssh-honeypotd:latest
```

```bash
docker run -d \
    --network=host \
    --cap-add=NET_BIND_SERVICE \
    --restart=always \
    --read-only \
    wildwildangel/ssh-honeypotd-min:latest
```

## Usage with Kubernetes

`ssh-honeypotd.yaml`:
```yaml
---
apiVersion: v1
kind: Namespace
metadata:
  name: honeypots
---
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: ssh-honeypotd
  namespace: honeypots
spec:
  selector:
    matchLabels:
      name: ssh-honeypotd
  template:
    metadata:
      labels:
        name: ssh-honeypotd
    spec:
      hostNetwork: true
      containers:
        - name: ssh-honeypotd
          image: wildwildangel/ssh-honeypotd-min # or wildwildangel/ssh-honeypotd
          resources:
            limits:
              cpu: 100m
              memory: 12Mi
            requests:
              cpu: 100m
              memory: 12Mi
          securityContext:
            capabilities:
              drop:
                - all
              add:
                - NET_BIND_SERVICE
            readOnlyRootFilesystem: true
            allowPrivilegeEscalation: false
            seccompProfile:
              type: RuntimeDefault
          ports:
            - containerPort: 22
              hostPort: 22
              protocol: TCP
```

```bash
kubectl apply -f ssh-honeypotd.yaml
```

You can control ssh-honeypotd's behavior in the container (wildwildangel/ssh-honeypotd image) with the following environment variables:
  * `ADDRESS` (default: 0.0.0.0): the IP address to bind to;
  * `PORT` (default: 22): the port to bind to.

These variables make it easy to have several ssh-honeypotd's running on the same machine, should the need arise.

## Docker Image Variants

ssh-honeypotd's Docker image comes in two flavors:

  1. A standard image based on the latest stable Alpine: [wildwildangel/ssh-honeypotd](https://hub.docker.com/repository/docker/wildwildangel/ssh-honeypotd).
  2. A minimalistic image based on the `scratch` Docker image: [wildwildangel/ssh-honeypotd-min](https://hub.docker.com/repository/docker/wildwildangel/ssh-honeypotd-min)

The `ssh-honeypotd-min` image contains only the statically linked `ssh-honeypotd` binary and the set of the pre-generated SSH keys. This image is a bit smaller than `ssh-honeypotd` but is experimental at the moment. The `ssh-honeypotd` binary in the `ssh-honeypotd-min` image does not support the following command-line options: `--pid`, `--name`, `--user`, `--group`, `--no-syslog`, `--foreground`.
