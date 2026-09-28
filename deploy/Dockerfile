FROM ubuntu:24.04 AS backend-build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake libpq-dev libssl-dev ninja-build \
    && rm -rf /var/lib/apt/lists/*

COPY deploy/or-tools.tar.gz /tmp/or-tools.tar.gz
RUN mkdir -p /opt/or-tools \
    && tar -xzf /tmp/or-tools.tar.gz -C /opt/or-tools --strip-components=1 \
    && rm /tmp/or-tools.tar.gz

WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DCMAKE_PREFIX_PATH=/opt/or-tools \
    && cmake --build build --target timetable_solver --parallel 2

FROM node:22-bookworm-slim AS frontend-build
WORKDIR /frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend ./
RUN npm run build

FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libpq5 libssl3t64 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=backend-build /opt/or-tools /opt/or-tools
COPY --from=backend-build /src/build/timetable_solver /usr/local/bin/timetable_solver
COPY --from=frontend-build /frontend/dist /opt/frontend-dist

ENV LD_LIBRARY_PATH=/opt/or-tools/lib
ENV TIMETABLE_HOST=0.0.0.0
WORKDIR /app
USER ubuntu
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/timetable_solver"]
