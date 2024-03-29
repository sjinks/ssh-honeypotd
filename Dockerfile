# syntax = docker/dockerfile:1.7@sha256:dbbd5e059e8a07ff7ea6233b213b36aa516b4c53c645f1817a4dd18b83cbea56
FROM --platform=amd64 wildwildangel/linux-musl-cross-compilers@sha256:c8e3cfdc2dfae66f0c63e9567d417fb453eae46eadc981ae838dc32d0da95322 AS build-base
RUN apk add --no-cache openssh-keygen
COPY toolchain /toolchain

FROM build-base AS build-deps
ARG TARGETPLATFORM
RUN \
    $(setvars ${TARGETPLATFORM}) && \
    apk add --root "${APK_ROOT}" --arch "${APK_ARCH}" --no-cache libssh-dev zlib-dev openssl-dev openssl-libs-static zlib-static

FROM build-deps as build-dynamic
ARG TARGETPLATFORM
WORKDIR /src/ssh-honeypotd
ENV CFLAGS="-Os -g0"
COPY . .
RUN \
    $(setvars ${TARGETPLATFORM}) && \
    make all keys && \
    strip ssh-honeypotd

FROM alpine:3.19.1@sha256:c5b1261d6d3e43071626931fc004f70149baeba2c8ec672bd4f27761f8e1ad6b AS release-dynamic
RUN apk add --no-cache libssh
COPY --from=build-dynamic /src/ssh-honeypotd/ssh-honeypotd /usr/bin/ssh-honeypotd
COPY --from=build-dynamic /src/ssh-honeypotd/keys/ /etc/ssh-honeypotd/
COPY entrypoint.sh /entrypoint.sh
EXPOSE 22
ENTRYPOINT ["/entrypoint.sh"]
CMD ["-k", "/etc/ssh-honeypotd/ssh_host_dsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_rsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_ecdsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_ed25519_key", "-f", "-x"]

FROM build-deps AS build-static
ARG TARGETPLATFORM
WORKDIR /usr/src
RUN wget https://www.libssh.org/files/0.10/libssh-0.10.4.tar.xz -O libssh-0.10.4.tar.xz
RUN tar -xa --strip-components=1 -f libssh-0.10.4.tar.xz
RUN \
    $(setvars ${TARGETPLATFORM}) && \
    cmake \
        -B build \
        -DCMAKE_TOOLCHAIN_FILE=/toolchain \
        -DCMAKE_INSTALL_PREFIX="/${HOSTSPEC}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DWITH_GSSAPI=OFF \
        -DWITH_SFTP=OFF \
        -DWITH_PCAP=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DWITH_EXAMPLES=OFF \
        -DCMAKE_AR=/usr/bin/gcc-ar \
        -DCMAKE_RANLIB=/usr/bin/gcc-ranlib \
        -DCMAKE_C_FLAGS="-flto -ffat-lto-objects" \
    && \
    cmake --build build && \
    cmake --install build
WORKDIR /src/ssh-honeypotd
COPY . .
RUN \
    $(setvars ${TARGETPLATFORM}) && \
    make all keys LDFLAGS="-static -flto -ffat-lto-objects" LIBFLAGS="$(pkg-config --libs --static libssh openssl zlib)" CFLAGS="-Os -flto -ffat-lto-objects" CPPFLAGS="-DMINIMALISTIC_BUILD -DLIBSSH_STATIC=1" && \
    strip ssh-honeypotd && \
    chmod 0444 /src/ssh-honeypotd/keys/*

FROM scratch AS release-static
COPY --from=build-static /src/ssh-honeypotd/ssh-honeypotd /ssh-honeypotd
COPY --from=build-static /src/ssh-honeypotd/keys/ /
EXPOSE 22
ENTRYPOINT [ "/ssh-honeypotd" ]
CMD [ "-k", "/ssh_host_dsa_key", "-k", "/ssh_host_rsa_key", "-k", "/ssh_host_ecdsa_key", "-k", "/ssh_host_ed25519_key" ]
