# 部署指南

## 1. 运行环境要求

- Linux（x86_64；arm64 未实测）
- 最低内存 64 MB（纯静态服务）；数据库连接池启用后按需增加
- 依赖：MariaDB 客户端库（`libmariadb3`）；仅静态服务可不启动数据库

## 2. 文件描述符与内核参数

高并发场景需要提高进程文件描述符上限：

```bash
# 临时
ulimit -n 65535

# 持久（/etc/security/limits.conf）
# webserver soft nofile 65535
# webserver hard nofile 65535
```

可选内核参数（/etc/sysctl.d/99-tinywebserver.conf）：

```ini
# 增大 SYN 队列（高连接建立速率）
net.core.somaxconn = 4096
net.ipv4.tcp_max_syn_backlog = 8192
# 允许更多 TIME_WAIT 端口复用
net.ipv4.tcp_tw_reuse = 1
```

```bash
sudo sysctl --system
```

## 3. systemd 服务

`/etc/systemd/system/tinywebserver.service`：

```ini
[Unit]
Description=TinyWebServer (static HTTP server)
After=network.target

[Service]
Type=simple
User=webserver
Group=webserver
WorkingDirectory=/srv/tinywebserver
ExecStart=/usr/local/bin/webserver
# 优雅退出：SIGTERM -> 停止 accept、等待存量请求（受连接超时约束）后回收资源
KillSignal=SIGTERM
TimeoutStopSec=15
LimitNOFILE=65535
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now tinywebserver
sudo systemctl status tinywebserver
```

## 4. 目录与权限

建议部署布局（`/srv/tinywebserver`）：

```
/srv/tinywebserver/
├── webserver          # 二进制
├── dist/              # 静态网站根目录（config: site.path）
├── config/config.yml  # 配置（首次运行自动生成）
└── log/               # 日志目录（config: log.directory）
```

`webserver` 用户对 dist 目录只需**读**权限，对 log/config 目录需要**写**权限。

## 5. 健康检查

内置健康检查端点：`GET /healthz` 返回 `200 ok`（纯文本，不访问文件系统），
可用于负载均衡器、编排系统与 systemd 探活：

```bash
curl -fsS http://127.0.0.1:6666/healthz && echo healthy
# 输出: ok
```

systemd 内置健康检查：

```ini
# 在 [Service] 段添加
HealthCheckCommand=curl -fsS http://127.0.0.1:6666/healthz
HealthCheckIntervalSec=10
```

## 6. 容量与监控建议

- 单进程面向单机部署；`maxFd - 3` 即为理论上限连接数（默认 65536 上限）。
- 建议监控：进程存活、端口连通、`log/` 目录 ERROR 日志、连接数（`ss -s`）。
- 日志按天与大小滚动（`log.size` 单文件上限，默认 64 MB），无需外部 logrotate。

## 7. 安全边界（重要）

**本服务是一个静态文件 HTTP 服务器，不是 WAF，也不具备完整安全能力。**
在公网部署时，请将其置于反向代理之后：

- **TLS 终止**：Nginx / Caddy / Envoy 处理 HTTPS；本项目只支持明文 HTTP。
- **认证/授权**：内置登录仅用于演示（明文密码存储，未做哈希）；真实用户系统需自行实现。
- **DDoS / 限流**：无 IP 限速、无连接限速；由网关或防火墙层承担。
- **WAF**：本项目有基础输入校验（目录遍历、超长请求、畸形报文防御），但无规则引擎。

### 单机 vs 高可用

单个 WebServer 进程只能做到**较高的正确性、稳定性与可恢复性**（优雅退出、资源回收、
异常请求防御）。**系统级高可用**（故障转移、负载均衡、滚动升级）需要：

- 多实例部署（多进程/多机）
- 反向代理 / 负载均衡（Nginx/Envoy/HAProxy）
- 健康检查 + 监控告警
- 数据库（MariaDB）的高可用方案

本项目不承诺也无法提供系统级高可用。

## 8. 反向代理示例（Nginx）

```nginx
server {
    listen 443 ssl;
    server_name example.com;
    ssl_certificate     /etc/ssl/certs/example.com.crt;
    ssl_certificate_key /etc/ssl/private/example.com.key;

    location / {
        proxy_pass http://127.0.0.1:6666;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 10s;
    }
}
```
