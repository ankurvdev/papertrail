FROM registry.fedoraproject.org/fedora-minimal:latest as builder
VOLUME /var/cache
RUN echo "keepcache=1" >> /etc/dnf/dnf.conf
RUN microdnf -y update \
    && microdnf -y install python3.12 python3-pip
RUN python3.12 -m venv /venv
RUN     /venv/bin/python3.12 -m pip install --upgrade pip \
    &&  /venv/bin/python3.12 -m pip install torch --index-url https://download.pytorch.org/whl/cpu
RUN  /venv/bin/python3.12 -m pip install surya_ocr --only-binar :all: --extra-index-url https://download.pytorch.org/whl/cpu
ADD ./surya_debug.py /generate.py
RUN /venv/bin/python3.12 /generate.py