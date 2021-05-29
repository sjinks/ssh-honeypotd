FROM alpine:3.13 as builddeps
RUN apk add --no-cache gcc make libc-dev libssh-dev openssh-keygen

FROM builddeps as build
WORKDIR /src/ssh-honeypotd
COPY . .
ENV CFLAGS="-Os -g0"
RUN make docker-build
RUN strip ssh-honeypotd

FROM alpine:3.13 AS release-dynamic
RUN apk add --no-cache libssh
COPY --from=build /src/ssh-honeypotd/ssh-honeypotd /usr/bin/ssh-honeypotd
COPY --from=build /src/ssh-honeypotd/keys/ /etc/ssh-honeypotd/
COPY entrypoint.sh /entrypoint.sh
EXPOSE 22
ENTRYPOINT ["/entrypoint.sh"]
CMD ["-k", "/etc/ssh-honeypotd/ssh_host_dsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_rsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_ecdsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_ed25519_key", "-f", "-x"]

FROM alpine:latest as build-static
WORKDIR /usr/src
RUN apk add --no-cache gcc make libc-dev zlib-dev openssl-dev openssh-keygen openssl-libs-static zlib-static pkgconf cmake
RUN wget https://www.libssh.org/files/0.9/libssh-0.9.5.tar.xz -O libssh-0.9.5.tar.xz
RUN tar -xa --strip-components=1 -f libssh-0.9.5.tar.xz
RUN \
    mkdir build && \
    cd build && \
    cmake \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DWITH_GSSAPI=OFF \
        -DWITH_SFTP=OFF \
        -DWITH_PCAP=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DWITH_EXAMPLES=OFF \
        -DCMAKE_AR=/usr/bin/gcc-ar \
        -DCMAKE_RANLIB=/usr/bin/gcc-ranlib \
        -DCMAKE_C_FLAGS="-flto -fuse-linker-plugin -ffat-lto-objects" \
        .. \
    && \
    make && make install
WORKDIR /src/ssh-honeypotd
COPY . .
RUN make docker-build LDFLAGS="-static -flto -fuse-linker-plugin -ffat-lto-objects" LIBFLAGS="$(pkg-config --libs --static libssh openssl zlib)" CFLAGS="-Os -flto -fuse-linker-plugin -ffat-lto-objects" CPPFLAGS="-DMINIMALISTIC_BUILD -DLIBSSH_STATIC=1"
RUN strip ssh-honeypotd
RUN chmod 0444 /src/ssh-honeypotd/keys/*

FROM scratch AS release-static
COPY --from=build-static /src/ssh-honeypotd/ssh-honeypotd /ssh-honeypotd
COPY --from=build-static /src/ssh-honeypotd/keys/ /
EXPOSE 22
ENTRYPOINT [ "/ssh-honeypotd" ]
CMD [ "-k", "/ssh_host_dsa_key", "-k", "/ssh_host_rsa_key", "-k", "/ssh_host_ecdsa_key", "-k", "/ssh_host_ed25519_key" ]
