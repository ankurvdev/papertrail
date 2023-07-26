# Overview
Papertrail is a document management search engine that allows you to index your documents (OCR) and search both filenames and content
The OCR is performed using [doctr](https://mindee.github.io/doctr/) and [typesense](https://typesense.org/) is used for the search engine functionality

# Quick Start

## Docker command line

`docker create --name papertrail ghcr.io/ankurvdev/papertrail:main -v ./cache:/cache -v ./docs:/data`

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
      - ./test:/cache
      - ./docs:/data
```
