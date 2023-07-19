FROM fedora:latest as builder
RUN dnf -y update && dnf -y install git cmake gcc g++ python python-devel nodejs-npm libglvnd-glx cairo pango libffi && dnf clean all
ADD . /s
RUN python /s/r.py --work-dir /app
RUN /app/venv/bin/python /s/papertrail.py --warm-up-doctr-cache=/app

FROM fedora:latest
RUN dnf -y update && dnf -y install python libglvnd-glx cairo pango libffi && dnf clean all
WORKDIR /app
VOLUME /data
VOLUME /cache
COPY --from=builder /s/papertrail.py /app/papertrail.py
COPY --from=builder /app/svelte/dist /app/svelte/dist
COPY --from=builder /app/doctr-cache /app/doctr-cache
COPY --from=builder /app/venv /app/venv
COPY --from=builder /app/typesense-server /app/typesense-server
ENTRYPOINT ["/app/venv/bin/python", "/app/papertrail.py", "/data", "--work-dir=/cache"]
