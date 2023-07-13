FROM fedora:latest as builder
RUN dnf -y update && dnf -y install git cmake gcc g++ python python-devel nodejs-npm && dnf clean all
ADD . /s
RUN python /s/r.py --work-dir /app

FROM fedora:latest
WORKDIR /app
VOLUME /data
VOLUME /cache
COPY --from=builder /s/papertrail.py /app/papertrail.py
COPY --from=builder /app/svelte/dist /app/dist
COPY --from=builder /app/venv /app/venv
COPY --from=builder /app/typesense-server /app/typesense-server
ENTRYPOINT ["/app/venv/bin/python", "/app/papertrail.py", "/data", "--work-dir=/cache"]