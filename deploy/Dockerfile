FROM ubuntu:24.04 AS backend-build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake curl libpq-dev libssl-dev ninja-build python3 \
    && rm -rf /var/lib/apt/lists/*

ARG ORTOOLS_URL=https://github.com/google/or-tools/releases/download/v9.15/or-tools_amd64_ubuntu-24.04_cpp_v9.15.6755.tar.gz
ARG ORTOOLS_SHA256=6f389320672cee00b78aacefb2bde33fef0bb988c3b2735573b9fffd1047fbda
RUN test "$(dpkg --print-architecture)" = amd64 \
    && curl -fL --retry 3 "$ORTOOLS_URL" -o /tmp/or-tools.tar.gz \
    && echo "$ORTOOLS_SHA256  /tmp/or-tools.tar.gz" | sha256sum -c - \
    && mkdir -p /opt/or-tools \
    && tar -xzf /tmp/or-tools.tar.gz -C /opt/or-tools --strip-components=1 \
    && rm /tmp/or-tools.tar.gz

WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY tests ./tests
COPY scripts ./scripts
RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DCMAKE_PREFIX_PATH=/opt/or-tools \
    && cmake --build build --parallel 2
RUN ctest --test-dir build --output-on-failure

FROM node:22-bookworm-slim AS frontend-build
WORKDIR /frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend ./
RUN npm test && npm run build
RUN npm prune --omit=dev --offline --ignore-scripts --no-audit --no-fund

FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libpq5 libssl3t64 tzdata \
    && rm -rf /var/lib/apt/lists/*

COPY --from=backend-build /opt/or-tools /opt/or-tools
COPY --from=backend-build /src/build/timetable_solver /usr/local/bin/timetable_solver
COPY --from=backend-build /src/build/quota_optimizer /usr/local/bin/quota_optimizer
COPY --from=frontend-build /usr/local/bin/node /usr/local/bin/node
COPY --from=frontend-build /frontend/dist /app/frontend/dist
COPY --from=frontend-build /frontend/node_modules /app/frontend/node_modules
COPY frontend/package.json /app/frontend/package.json
COPY frontend/src/utils /app/frontend/src/utils
COPY frontend/public/templates /app/frontend/public/templates
COPY site /app/site

ENV LD_LIBRARY_PATH=/opt/or-tools/lib
ENV TIMETABLE_HOST=127.0.0.1
ENV SITE_HOST=0.0.0.0
ENV SITE_PORT=8080
ENV SITE_API_PORT=8081
ENV SITE_BACKEND_PATH=/usr/local/bin/timetable_solver
ENV TZ=Asia/Yekaterinburg
WORKDIR /app
USER ubuntu
EXPOSE 8080
ENTRYPOINT ["node", "/app/site/server.mjs"]
