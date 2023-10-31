FROM registry.fedoraproject.org/fedora-minimal:latest as builder
RUN microdnf -y update && microdnf -y install git cmake gcc g++ python python-devel nodejs-npm libglvnd-glx cairo pango libffi && microdnf clean all
RUN python -m venv /buildvenv && python -m venv /app/venv
ADD requirements.txt /
RUN /app/venv/bin/python -m pip install --upgrade pip -r /requirements.txt

ADD . /s
RUN /buildvenv/bin/python /s/ci/run.py --work-dir /app build

FROM registry.fedoraproject.org/fedora-minimal:latest
RUN microdnf -y update && microdnf -y install python libglvnd-glx cairo pango libffi && microdnf clean all
WORKDIR /app
VOLUME /data
VOLUME /cache
COPY --from=builder /app/venv /app/venv
COPY --from=builder /app/doctr-cache /app/doctr-cache
COPY --from=builder /app/typesense-server /app/typesense-server
COPY --from=builder /s/papertrail.py /app/papertrail.py
COPY --from=builder /app/svelte/dist /app/svelte/dist
ENTRYPOINT ["/app/venv/bin/python", "/app/papertrail.py", "/data", "--work-dir=/cache"]
