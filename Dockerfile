FROM fedora:latest as builder
RUN dnf -y update && dnf -y install git  cmake gcc g++ python python-devel && dnf clean all
ADD . /s
RUN python /s/r.py --work-dir /work
    
FROM fedora:latest
WORKDIR /app
COPY --from=builder /app/dist/dhcpleases /app/dhcpleases
ENTRYPOINT ["/app/papertrail.py"]
