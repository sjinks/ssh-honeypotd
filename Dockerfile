FROM --platform=${BUILDPLATFORM} tonistiigi/xx:latest@sha256:923441d7c25f1e2eb5789f82d987693c47b8ed987c4ab3b075d6ed2b5d6779a3 AS xx

FROM --platform=${BUILDPLATFORM} alpine:3.22.0@sha256:8a1f59ffb675680d47db6337b49d22281a139e9d709335b492be023728e11715 AS build-base
ARG TARGETPLATFORM
COPY --from=xx / /
RUN \
    apk add --no-cache clang llvm lld make openssh-keygen libcap-setcap pkgconf && \
    xx-apk add --no-cache gcc musl-dev libssh-dev

FROM --platform=${BUILDPLATFORM} build-base AS build-dynamic
ARG TARGETPLATFORM
WORKDIR /src/ssh-honeypotd
COPY . .
RUN \
    xx-clang --setup-target-triple && \
    make PKGCONFIG="$(xx-clang --print-prog-name=pkg-config)" CFLAGS="-Os -g0" CC="xx-clang" all keys && \
    $(xx-info triple)-strip ssh-honeypotd && \
    setcap cap_net_bind_service=ep ssh-honeypotd

FROM --platform=${BUILDPLATFORM} alpine:3.22.0@sha256:8a1f59ffb675680d47db6337b49d22281a139e9d709335b492be023728e11715 AS release-dynamic-base
ARG TARGETPLATFORM
COPY --from=xx / /
RUN xx-apk add --no-cache libssh
RUN \
    if xx-info is-cross; then \
        ln -sf "/$(xx-info triple)" /target; \
    else \
        mkdir /target; \
        ln -sf /lib /target/lib; \
        ln -sf /usr /target/usr; \
    fi

FROM alpine:3.22.0@sha256:8a1f59ffb675680d47db6337b49d22281a139e9d709335b492be023728e11715 AS release-dynamic
COPY --from=release-dynamic-base /target/lib /lib
COPY --from=release-dynamic-base /target/usr/lib /usr/lib
COPY --from=build-dynamic /src/ssh-honeypotd/ssh-honeypotd /usr/bin/ssh-honeypotd
COPY --from=build-dynamic /src/ssh-honeypotd/keys/ /etc/ssh-honeypotd/
COPY entrypoint.sh /entrypoint.sh
EXPOSE 22
ENTRYPOINT ["/entrypoint.sh"]
CMD ["-k", "/etc/ssh-honeypotd/ssh_host_rsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_ecdsa_key", "-k", "/etc/ssh-honeypotd/ssh_host_ed25519_key", "-f", "-x"]


FROM --platform=${BUILDPLATFORM} build-base AS build-static
ARG TARGETPLATFORM
WORKDIR /src/ssh-honeypotd
RUN \
    apk add --no-cache cmake && \
    xx-apk add --no-cache zlib-dev openssl-dev openssl-libs-static zlib-static && \
    if [ "$(xx-info alpine-arch)" = "ppc64le" ]; then export XX_CC_PREFER_LINKER=ld; fi && \
    xx-clang --setup-target-triple

WORKDIR /usr/src
RUN wget https://www.libssh.org/files/0.10/libssh-0.10.6.tar.xz -O libssh-0.10.6.tar.xz
RUN tar -xa --strip-components=1 -f libssh-0.10.6.tar.xz
RUN \
    if xx-info is-cross; then EXTRA="-DCMAKE_SYSROOT=/$(xx-info triple) -DCMAKE_INSTALL_PREFIX=/$(xx-info triple)/usr"; else EXTRA=; fi && \
    cmake -B build \
        $(xx-clang --print-cmake-defines) ${EXTRA} \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DWITH_GSSAPI=OFF \
        -DWITH_SFTP=OFF \
        -DWITH_DEBUG_CALLTRACE=OFF \
        -DWITH_PCAP=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DWITH_EXAMPLES=OFF \
        -DWITH_NACL=OFF \
    && \
    cmake --build build && \
    cmake --install build

WORKDIR /src/ssh-honeypotd
COPY . .
RUN \
    make \
        CFLAGS="-Os -g0" \
        CC="xx-clang" \
        CPPFLAGS="-DMINIMALISTIC_BUILD -DLIBSSH_STATIC=1" \
        LIBFLAGS="$($(xx-clang --print-prog-name=pkg-config) --libs --static libssh openssl zlib)" \
        LDFLAGS="-static" \
        all keys && \
    $(xx-info triple)-strip ssh-honeypotd && \
    setcap cap_net_bind_service=ep ssh-honeypotd

FROM scratch AS release-static
COPY --from=build-static /src/ssh-honeypotd/ssh-honeypotd /ssh-honeypotd
COPY --from=build-static /src/ssh-honeypotd/keys/ /
EXPOSE 22
ENTRYPOINT [ "/ssh-honeypotd" ]
CMD [ "-k", "/ssh_host_rsa_key", "-k", "/ssh_host_ecdsa_key", "-k", "/ssh_host_ed25519_key" ]
