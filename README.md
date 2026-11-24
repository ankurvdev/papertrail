# Overview
Papertrail is a document management search engine that allows you to index your documents (OCR) and search both filenames and content
It uses onnxruntime PPOCR-v6 models for OCR and xapian for indexing and search

# Quick Start

## Docker command line

`docker create --name papertrail ghcr.io/ankurvdev/papertrail:main -v ./cache:/run -v ./docs:/data`

The website can be accessed via http://localhost:5000


## Docker compose
```
version: "3"
services:
  papertrail:
    image: ghcr.io/ankurvdev/papertrail:main
    restart: unless-stopped
    ports:
      - 5000:5000
    volumes:
      - ./test:/run
      - ./docs:/data
```
