FROM --platform=${BUILDPLATFORM} tonistiigi/xx:latest@sha256:c64defb9ed5a91eacb37f96ccc3d4cd72521c4bd18d5442905b95e2226b0e707 AS xx

FROM --platform=${BUILDPLATFORM} alpine:3.24.1@sha256:28bd5fe8b56d1bd048e5babf5b10710ebe0bae67db86916198a6eec434943f8b AS build-base
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

FROM --platform=${BUILDPLATFORM} alpine:3.24.1@sha256:28bd5fe8b56d1bd048e5babf5b10710ebe0bae67db86916198a6eec434943f8b AS release-dynamic-base
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

FROM alpine:3.24.1@sha256:28bd5fe8b56d1bd048e5babf5b10710ebe0bae67db86916198a6eec434943f8b AS release-dynamic
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
    apk add --no-cache cmake gnupg && \
    xx-apk add --no-cache zlib-dev openssl-dev openssl-libs-static zlib-static && \
    if [ "$(xx-info alpine-arch)" = "ppc64le" ]; then export XX_CC_PREFER_LINKER=ld; fi && \
    xx-clang --setup-target-triple

WORKDIR /usr/src
RUN \
    set -eu; \
    LIBSSH_VERSION=0.12.0; \
    LIBSSH_RELEASE_KEY=88A228D89B07C2C77D0C780903D5DF8CFDD3E8E7; \
    wget "https://www.libssh.org/files/0.12/libssh-${LIBSSH_VERSION}.tar.xz" -O "libssh-${LIBSSH_VERSION}.tar.xz"; \
    wget "https://www.libssh.org/files/0.12/libssh-${LIBSSH_VERSION}.tar.xz.asc" -O "libssh-${LIBSSH_VERSION}.tar.xz.asc"; \
    wget "https://keyserver.ubuntu.com/pks/lookup?op=get&options=mr&search=0x${LIBSSH_RELEASE_KEY}" -O libssh-release-key.asc; \
    GNUPGHOME="$(mktemp -d)"; \
    export GNUPGHOME; \
    gpg --batch --show-keys --with-colons libssh-release-key.asc | grep -q "^fpr:::::::::${LIBSSH_RELEASE_KEY}:"; \
    gpg --batch --import libssh-release-key.asc; \
    gpg --batch --verify "libssh-${LIBSSH_VERSION}.tar.xz.asc" "libssh-${LIBSSH_VERSION}.tar.xz"; \
    tar -xa --strip-components=1 -f "libssh-${LIBSSH_VERSION}.tar.xz"; \
    rm -rf "$GNUPGHOME" libssh-release-key.asc "libssh-${LIBSSH_VERSION}.tar.xz.asc"
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
COPY --from=build-static --chown=10000:10000 /src/ssh-honeypotd/keys/ /
EXPOSE 22
USER 10000:10000
ENTRYPOINT [ "/ssh-honeypotd" ]
CMD [ "-k", "/ssh_host_rsa_key", "-k", "/ssh_host_ecdsa_key", "-k", "/ssh_host_ed25519_key" ]
