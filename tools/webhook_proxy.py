#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
企业微信Webhook代理服务器
用于将CH32V208的HTTP请求转发到企业微信的HTTPS接口

使用方法:
    python webhook_proxy.py

运行后监听 0.0.0.0:8080
CH32V208 发送 HTTP POST 请求到此服务器，服务器转发到企业微信
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import requests
import json
import sys

# 监听端口
LISTEN_PORT = 8080

# 企业微信Webhook地址
WEIXIN_WEBHOOK_URL = "https://qyapi.weixin.qq.com/cgi-bin/webhook/send"


class WebhookProxyHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        try:
            # 读取请求体
            MAX_BODY = 10 * 1024
            content_length = int(self.headers.get('Content-Length', 0))
            if content_length > MAX_BODY:
              self.send_response(413)
              self.end_headers()
              return
            body = self.rfile.read(content_length)

            # 解析路径中的key参数
            # 请求格式: POST /cgi-bin/webhook/send?key=xxx
            if '/cgi-bin/webhook/send' in self.path:
                # 构造完整的企业微信URL
                target_url = WEIXIN_WEBHOOK_URL + self.path.split('/cgi-bin/webhook/send')[1]

                print(f"[PROXY] 收到请求: {self.path}")
                print(f"[PROXY] 请求体: {body.decode('utf-8', errors='ignore')}")
                print(f"[PROXY] 转发到: {target_url}")

                # 转发到企业微信
                response = requests.post(
                    target_url,
                    data=body,
                    headers={'Content-Type': 'application/json'},
                    timeout=10
                )

                print(f"[PROXY] 企业微信响应: {response.status_code} - {response.text}")

                # 返回响应给CH32V208
                self.send_response(response.status_code)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(response.content)
            else:
                # 未知路径
                self.send_response(404)
                self.end_headers()
                self.wfile.write(b'{"errcode": 404, "errmsg": "Not Found"}')

        except Exception as e:
            print(f"[PROXY] 错误: {e}")
            self.send_response(500)
            self.end_headers()
            self.wfile.write(f'{{"errcode": 500, "errmsg": "{str(e)}"}}'.encode())

    def do_GET(self):
        """健康检查接口"""
        if self.path == '/health' or self.path == '/':
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(b'Webhook Proxy OK')
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        """自定义日志格式"""
        print(f"[HTTP] {self.address_string()} - {format % args}")


def main():
    print("=" * 50)
    print("企业微信Webhook代理服务器")
    print("=" * 50)
    print(f"监听地址: 0.0.0.0:{LISTEN_PORT}")
    print(f"转发目标: {WEIXIN_WEBHOOK_URL}")
    print()
    print("CH32V208 配置:")
    print(f"  HTTP_PROXY_IP   = 本机IP地址")
    print(f"  HTTP_PROXY_PORT = {LISTEN_PORT}")
    print()
    print("按 Ctrl+C 停止服务器")
    print("=" * 50)

    try:
        server = HTTPServer(('0.0.0.0', LISTEN_PORT), WebhookProxyHandler)
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[PROXY] 服务器已停止")
        sys.exit(0)


if __name__ == '__main__':
    main()
