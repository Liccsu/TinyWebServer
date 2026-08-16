# 开发构建镜像（Debian + CMake/Ninja/GCC + MariaDB 依赖）
FROM debian:13 AS build

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    cmake ninja-build pkg-config g++ make git curl \
    libmariadb-dev libmariadb-dev-compat libmariadb3 \
    libyaml-cpp-dev \
    libgtest-dev python3 \
    && rm -rf /var/lib/apt/lists/*

# 编译安装 GTest（Debian 包仅提供源码）
RUN cd /usr/src/googletest && cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja \
    && cmake --build build --parallel \
    && cmake --install build

WORKDIR /workspace
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja \
    && cmake --build build --parallel

# ---------------------------------------------------------------------------
# 运行镜像（仅运行时依赖）
# ---------------------------------------------------------------------------
FROM debian:13 AS runtime

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    libmariadb3 libyaml-cpp0.8 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -s /usr/sbin/nologin webserver

WORKDIR /srv/tinywebserver
COPY --from=build /workspace/build/webserver /usr/local/bin/webserver
# 内置默认配置与最小站点（生产使用 -v 挂载覆盖 dist/ 与 config/）
COPY config/config.example.yml ./config/config.yml
RUN mkdir -p ./dist ./log \
    && echo "<html>tinywebserver</html>" > ./dist/index.html \
    && chown -R webserver:webserver /srv/tinywebserver

# 健康检查：内置 /healthz 端点（默认端口 6666）
ENV TWS_PORT=6666
EXPOSE ${TWS_PORT}

USER webserver
CMD ["sh", "-c", "exec webserver"]
