# Build environment matching .github/workflows/continuous-integration-workflow.yml
FROM ghcr.io/extremscorner/libogc2:latest

RUN apt-get update && \
    apt-get install -y --no-install-recommends -t bookworm-backports \
        7zip gcc gcc-mingw-w64-i686 libc6-dev libc6-i386 xorriso git && \
    rm -rf /var/lib/apt/lists/*

RUN git config --system --add safe.directory '*'

WORKDIR /swiss
CMD ["make"]
