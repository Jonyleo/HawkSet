# HawkSet

This repo contains the source code for HawkSet, an automatic, application-agnostic, and efficient concurrent PM bug detector

## Requirements

- Docker

## Step 1: Building de docker container

```sh
docker build . -t hawkset --build-arg PMDK_VERSION=tags/1.12.1
```

## Step 2: Set a PM pool (TODO)

## Step 3: Running HawkSet (TODO)

```sh
./docker.sh
```

## Scripts

Inside the scripts folder you can find several useful scripts for running HawkSet

- build.sh & clean.sh
- HawkSet: main scripted used to invoke HawkSet
