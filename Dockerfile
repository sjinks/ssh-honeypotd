# syntax = docker/dockerfile:1.8@sha256:d6d396f3780b1dd56a3acbc975f57bd2fc501989b50164c41387c42d04e780d0
FROM --platform=amd64 wildwildangel/linux-musl-cross-compilers@sha256:c8e3cfdc2dfae66f0c63e9567d417fb453eae46eadc981ae838dc32d0da95322 AS build-base
RUN apk add --no-cache openssh-keygen
COPY toolchain /toolchain

FROM build-base AS build-deps
ARG TARGETPLATFORM
RUN \
    $(setvars ${TARGETPLATFORM}) && \
    apk add --root "${APK_ROOT}" --arch "${APK_ARCH}" --no-cache libssh-dev zlib-dev openssl-dev openssl-libs-static zlib-static

FROM build-deps AS build-dynamic
ARG TARGETPLATFORM
WORKDIR /src/ssh-honeypotd
ENV CFLAGS="-Os -g0"
COPY . .
RUN \
    $(setvars ${TARGETPLATFORM}) && \
    make all keys && \
    strip ssh-honeypotd

FROM alpine:3.20.0@sha256:77726ef6b57ddf65bb551896826ec38bc3e53f75cdde31354fbffb4f25238ebd AS release-dynamic
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
RUN wget https://www.libssh.org/files/0.10/libssh-0.10.6.tar.xz -O libssh-0.10.6.tar.xz
RUN tar -xa --strip-components=1 -f libssh-0.10.6.tar.xz
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
