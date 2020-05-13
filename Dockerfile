FROM alpine:latest as builddeps
RUN apk add --no-cache gcc make libc-dev libssh-dev openssh-keygen

FROM alpine:latest as deps
RUN apk add --no-cache libssh

FROM builddeps as build
WORKDIR /src/ssh-honeypotd
COPY . .
ENV CFLAGS="-Os -g0"
RUN make docker-build LIBFLAGS="-lssh -pthread"

FROM deps
COPY --from=build /src/ssh-honeypotd/ssh-honeypotd /usr/bin/ssh-honeypotd
COPY --from=build /src/ssh-honeypotd/keys/ /etc/ssh-honeypotd/
EXPOSE 22
CMD ["/usr/bin/ssh-honeypotd", "-k", "/etc/ssh-honeypotd/ssh_host_dsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_rsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_ecdsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_ed25519_key", "-f", "-x"]
