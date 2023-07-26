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
