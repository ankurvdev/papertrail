FROM fedora:latest
USER root
VOLUME ["/output"]
RUN \
    --mount=type=cache,id=cache,target=/var/lib/dnf \
    --mount=type=cache,id=cache,target=/var/cache/dnf \
    --mount=type=cache,id=cache,target=/var/lib/libdnf5 \
    --mount=type=cache,id=cache,target=/var/cache/libdnf5 \
    echo "keepcache=1" >> /etc/dnf/dnf.conf && \
    dnf -y --skip-file-locks install curl python3.12 python3-uv libglvnd-glx
RUN --mount=type=cache,id=cache,target=/root/.cache \
    --mount=type=cache,id=cache,target=/root/.paddlex \
    --mount=type=bind,source=.,target=/app \
    /app/prepare_machine.sh
