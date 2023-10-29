FROM registry.fedoraproject.org/fedora-minimal:latest as builder
RUN microdnf -y update && microdnf -y install git cmake gcc g++ python python-devel nodejs-npm libglvnd-glx cairo pango libffi && microdnf clean all
RUN python -m venv /buildvenv && python -m venv /app/venv
ADD requirements.txt /
RUN /app/venv/bin/python -m pip install --upgrade pip -r /requirements.txt
RUN /buildvenv/bin/python -m pip install --upgrade pip  buildverse==0.0.6

ADD . /s
RUN bash -c 'if [[ -e /s/buildverse ]]; then /buildvenv/bin/python -m pip install /s/buildverse --force-reinstall; fi'
RUN /buildvenv/bin/python /s/r.py --work-dir /app
RUN /app/venv/bin/python /s/papertrail.py --warm-up-doctr-cache=/app

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
