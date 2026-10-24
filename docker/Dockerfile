# Imprint development environment.
# Image = rebuildable tools only (compilers, NDS/WASM toolchains, gh, X/shot
# tools, optional dsh CLI). The repo is bind-mounted by docker/compose.yaml.
# No credentials are baked or mounted — install/auth inside the container.
#
# Build context is the repository root (see docker/dev.sh).

# ---- NDS toolchain (baked into the image) ---------------------------------
FROM devkitpro/devkitarm:20260610 AS devkit

# ---- WASM toolchain (baked into the image) --------------------------------
FROM emscripten/emsdk:5.0.1 AS emsdk

# ---- development image ----------------------------------------------------
FROM ubuntu:24.04

ARG USERNAME=dev
ARG UID=1000
ARG GID=1000

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# Build/test toolchain + X11/screenshot tools + gh + python extras
# (g++-mingw-w64-x86-64: the X-21 Windows-TU probe, `win-probe`/README
# "Windows TU probe" -- it compiles imshell/src/win/* locally, which the
# MSVC-only CI job cannot do for the GCC warning families)
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        make \
        ninja-build \
        g++-mingw-w64-x86-64 \
        git \
        ca-certificates \
        curl \
        wget \
        gnupg \
        unzip \
        xz-utils \
        python3 \
        python3-pip \
        python3-venv \
        python3-dev \
        python3-pygame \
        libx11-dev \
        libx11-6 \
        libxext6 \
        libxrender1 \
        libxi6 \
        libxrandr2 \
        libxcursor1 \
        libxinerama1 \
        libgl1 \
        libglu1-mesa \
        libegl1 \
        mesa-utils \
        libfontconfig1 \
        libfreetype6 \
        libdbus-1-3 \
        libnss3 \
        libatk1.0-0t64 \
        libatk-bridge2.0-0t64 \
        libcups2t64 \
        libdrm2 \
        libgbm1 \
        libasound2t64 \
        libxkbcommon0 \
        libxcomposite1 \
        libxdamage1 \
        libxfixes3 \
        xauth \
        x11-utils \
        x11-apps \
        xdotool \
        xterm \
        imagemagick \
        scrot \
        ffmpeg \
        xvfb \
        xdg-utils \
        file \
        jq \
        less \
        vim-tiny \
        sudo \
        locales \
    && rm -rf /var/lib/apt/lists/*

# GitHub CLI
RUN mkdir -p -m 755 /etc/apt/keyrings \
    && curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
        -o /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
        > /etc/apt/sources.list.d/github-cli.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends gh \
    && rm -rf /var/lib/apt/lists/*

# Node.js 22 (dsh / npx / wasm smoke)
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# Non-root user matching host uid/gid (bind-mount ownership).
# Ubuntu 24.04 may already own GID/UID 1000 (e.g. group "ubuntu").
RUN if getent group "${GID}" >/dev/null; then \
        EXISTING_GNAME="$(getent group "${GID}" | cut -d: -f1)"; \
        if [ "${EXISTING_GNAME}" != "${USERNAME}" ]; then \
            groupmod -n "${USERNAME}" "${EXISTING_GNAME}"; \
        fi; \
    else \
        groupadd -g "${GID}" "${USERNAME}"; \
    fi \
    && if id -u "${UID}" >/dev/null 2>&1; then \
        EXISTING_UNAME="$(getent passwd "${UID}" | cut -d: -f1)"; \
        if [ "${EXISTING_UNAME}" != "${USERNAME}" ]; then \
            usermod -l "${USERNAME}" -d "/home/${USERNAME}" -m "${EXISTING_UNAME}" 2>/dev/null \
            || usermod -l "${USERNAME}" "${EXISTING_UNAME}"; \
        fi; \
        usermod -u "${UID}" -g "${GID}" "${USERNAME}" 2>/dev/null || true; \
    else \
        useradd -m -u "${UID}" -g "${GID}" -s /bin/bash "${USERNAME}"; \
    fi \
    && mkdir -p "/home/${USERNAME}" \
    && chown "${UID}:${GID}" "/home/${USERNAME}" \
    && echo "${USERNAME} ALL=(root) NOPASSWD:ALL" > "/etc/sudoers.d/${USERNAME}" \
    && chmod 0440 "/etc/sudoers.d/${USERNAME}"

# NDS: /opt/devkitpro + env expected by cmake/nds.toolchain.cmake
COPY --from=devkit /opt/devkitpro /opt/devkitpro
ENV DEVKITPRO=/opt/devkitpro \
    DEVKITARM=/opt/devkitpro/devkitARM \
    LIBNDS=/opt/devkitpro/libnds \
    PATH=/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:${PATH}

# WASM: emsdk tree + PATH (em++, node bundled with emsdk)
COPY --from=emsdk /emsdk /emsdk
ENV EMSDK=/emsdk \
    EM_CONFIG=/emsdk/.emscripten \
    PATH=/emsdk:/emsdk/upstream/emscripten:/emsdk/node/24.19.0_64bit/bin:${PATH}

# melonDS AppImage staged by docker/dev.sh into docker/context/
# (static-pie; no FUSE in container — force extract-and-run)
COPY --chmod=0755 docker/context/melonDS-x86_64.AppImage /usr/local/bin/melonDS.real
RUN printf '%s\n' '#!/bin/sh' 'export APPIMAGE_EXTRACT_AND_RUN=1' 'exec /usr/local/bin/melonDS.real "$@"' \
    > /usr/local/bin/melonDS && chmod 0755 /usr/local/bin/melonDS

# dsh + opencode CLI (avoids cold npx download each start; opencode auth lives
# in the /root named volume, see compose.yaml)
RUN npm install -g @deepseek-ai/dsh opencode-ai

# Playwright browsers for headless WASM/HTML screenshots (root system install;
# runtime still runs as ${USERNAME})
RUN python3 -m pip install --break-system-packages playwright \
    && python3 -m playwright install --with-deps firefox chromium

# /src -> /workspace/imprint so demo/wasm/build.sh (cd /src) works under bind
RUN mkdir -p /workspace/imprint \
    && ln -sfn /workspace/imprint /src

# Home scaffolding: avoid root-owned .local under the non-root home.
RUN mkdir -p "/home/${USERNAME}/.local/state" "/home/${USERNAME}/.local/share" \
    && chown -R "${UID}:${GID}" "/home/${USERNAME}"

# melonDS: prefer extract-and-run (no FUSE in container)
ENV APPIMAGE_EXTRACT_AND_RUN=1

COPY --chmod=0755 docker/entrypoint.sh /usr/local/bin/imprint-entrypoint

# git: trust every repo path system-wide. Bind-mounted repos are owned by the
# host uid; any container uid other than that owner (the root override, or a
# future uid change) trips git's dubious-ownership check on every repo,
# including nested ones (<repo>/.ai). /etc/gitconfig applies to all users, so
# no per-repo whitelist is needed and this survives container recreation.
RUN git config --system --add safe.directory '*'

USER ${USERNAME}
WORKDIR /workspace/imprint
ENTRYPOINT ["/usr/local/bin/imprint-entrypoint"]
CMD ["sleep", "infinity"]
