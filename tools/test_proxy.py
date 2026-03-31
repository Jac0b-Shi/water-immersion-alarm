#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
HTTP代理服务器测试脚本
用于验证代理服务器是否能正常转发HTTPS请求到企业微信webhook

使用方法:
    # 自动从config.env加载配置测试
    python test_proxy.py
    
    # 手动指定参数
    python test_proxy.py --proxy-ip 192.168.1.1 --proxy-port 8080 \
        --webhook "https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=YOUR_KEY"
    
    # 测试代理连通性（不发送实际消息）
    python test_proxy.py --test-connect-only
"""

import socket
import ssl
import sys
import json
import time
import argparse
import os
import re
from urllib.parse import urlparse


def load_config_from_env(config_path=None):
    """从config.env文件加载配置"""
    if config_path is None:
        possible_paths = [
            'config.env',
            '../config.env',
            os.path.join(os.path.dirname(__file__), '..', 'config.env'),
            os.path.join(os.path.dirname(__file__), 'config.env')
        ]
        for path in possible_paths:
            if os.path.exists(path):
                config_path = path
                break
    
    if not config_path or not os.path.exists(config_path):
        return {}
    
    config = {}
    print(f"[*] 加载配置文件: {os.path.abspath(config_path)}")
    
    try:
        with open(config_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if '=' in line:
                    key, value = line.split('=', 1)
                    config[key.strip()] = value.strip()
        print(f"[+] 配置文件加载成功")
    except Exception as e:
        print(f"[-] 加载配置文件失败: {e}")
    
    return config


def get_webhook_url_from_config(config):
    """从配置构建webhook URL"""
    webhook_key = config.get('WEBHOOK_KEY', '').strip()
    if webhook_key:
        return f"https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key={webhook_key}"
    return None


def parse_webhook_url(webhook_url):
    """解析webhook URL"""
    parsed = urlparse(webhook_url)
    is_https = parsed.scheme == 'https'
    host = parsed.hostname
    port = parsed.port or (443 if is_https else 80)
    path = parsed.path
    if parsed.query:
        path += '?' + parsed.query
    return host, port, path, is_https


def test_tcp_connect(proxy_ip, proxy_port, timeout=5):
    """测试TCP连接到代理服务器"""
    print(f"\n[1] 测试TCP连接到代理服务器 {proxy_ip}:{proxy_port}...")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        result = sock.connect_ex((proxy_ip, proxy_port))
        sock.close()
        
        if result == 0:
            print(f"[+] TCP连接成功 (端口{proxy_port}开放)")
            return True
        else:
            print(f"[-] TCP连接失败 (错误码: {result})")
            return False
    except Exception as e:
        print(f"[-] TCP连接异常: {e}")
        return False


def test_http_proxy(proxy_ip, proxy_port, target_host, target_port, timeout=10):
    """测试HTTP代理（CONNECT方法）"""
    print(f"\n[2] 测试HTTP代理CONNECT方法...")
    print(f"    代理: {proxy_ip}:{proxy_port}")
    print(f"    目标: {target_host}:{target_port}")
    
    try:
        # 建立到代理的连接
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((proxy_ip, proxy_port))
        print(f"[+] 已连接到代理服务器")
        
        # 发送CONNECT请求
        connect_req = f"CONNECT {target_host}:{target_port} HTTP/1.1\r\n"
        connect_req += f"Host: {target_host}:{target_port}\r\n"
        connect_req += "\r\n"
        
        print(f"[*] 发送CONNECT请求...")
        sock.sendall(connect_req.encode('utf-8'))
        
        # 接收响应
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
        
        response_str = response.decode('utf-8', errors='ignore')
        print(f"[*] 代理响应:\n{response_str[:500]}")
        
        if "200" in response_str and "Connection established" in response_str:
            print("[+] CONNECT方法成功，隧道已建立")
            sock.close()
            return True
        elif "200" in response_str:
            print("[+] 代理返回200响应")
            sock.close()
            return True
        else:
            print("[-] CONNECT方法失败，代理可能不支持CONNECT")
            sock.close()
            return False
            
    except Exception as e:
        print(f"[-] 测试失败: {e}")
        return False


def test_https_through_proxy(proxy_ip, proxy_port, target_host, target_port, timeout=15):
    """测试通过代理访问HTTPS"""
    print(f"\n[3] 测试通过代理建立HTTPS连接...")
    
    try:
        # 建立到代理的连接
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((proxy_ip, proxy_port))
        
        # 发送CONNECT请求
        connect_req = f"CONNECT {target_host}:{target_port} HTTP/1.1\r\n"
        connect_req += f"Host: {target_host}:{target_port}\r\n"
        connect_req += "\r\n"
        sock.sendall(connect_req.encode('utf-8'))
        
        # 读取CONNECT响应
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
        
        response_str = response.decode('utf-8', errors='ignore')
        
        if "200" not in response_str:
            print(f"[-] CONNECT失败: {response_str[:200]}")
            sock.close()
            return False
        
        print("[+] CONNECT成功，建立SSL隧道...")
        
        # 包装SSL
        context = ssl.create_default_context()
        ssl_sock = context.wrap_socket(sock, server_hostname=target_host)
        
        print("[+] SSL握手成功")
        print(f"[+] 使用的TLS版本: {ssl_sock.version()}")
        print(f"[+] 服务器证书: {ssl_sock.getpeercert().get('subject', 'N/A')}")
        
        ssl_sock.close()
        return True
        
    except ssl.SSLError as e:
        print(f"[-] SSL错误: {e}")
        return False
    except Exception as e:
        print(f"[-] 测试失败: {e}")
        return False


def send_wechat_message_via_app_proxy(proxy_ip, proxy_port, webhook_url, message, timeout=20):
    """
    通过应用层代理发送企业微信消息
    
    这种代理模式适用于 webhook_proxy.py 类型的代理服务器
    代理接收 HTTP 请求，然后使用 requests 转发到 HTTPS
    """
    print(f"\n[4] 发送测试消息到企业微信（应用层代理模式）...")
    
    # 解析webhook URL
    host, port, path, is_https = parse_webhook_url(webhook_url)
    
    try:
        # 建立到代理的连接
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((proxy_ip, proxy_port))
        print(f"[+] 已连接到代理 {proxy_ip}:{proxy_port}")
        
        # 构建HTTP请求（直接发送到代理，不是CONNECT）
        payload = {
            "msgtype": "text",
            "text": {
                "content": message
            }
        }
        body = json.dumps(payload, ensure_ascii=False)
        
        # 直接向代理发送POST请求，路径包含完整的webhook路径
        request = f"POST {path} HTTP/1.1\r\n"
        request += f"Host: {host}\r\n"
        request += "Content-Type: application/json\r\n"
        request += f"Content-Length: {len(body.encode('utf-8'))}\r\n"
        request += "Connection: close\r\n"
        request += "\r\n"
        request += body
        
        print(f"[*] 发送HTTP POST请求到代理...")
        print(f"[*] 请求路径: {path}")
        sock.sendall(request.encode('utf-8'))
        
        # 接收响应
        response = b""
        while True:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
            except socket.timeout:
                break
        
        sock.close()
        
        response_str = response.decode('utf-8', errors='ignore')
        print(f"[*] 代理响应:\n{response_str[:800]}")
        
        # 解析响应
        if "200" in response_str:
            # 提取JSON响应体
            parts = response_str.split("\r\n\r\n", 1)
            if len(parts) > 1:
                body = parts[1]
                try:
                    result = json.loads(body)
                    if result.get('errcode') == 0:
                        print("[+] 消息发送成功!")
                        return True
                    else:
                        print(f"[-] 企业微信返回错误: {result}")
                        return False
                except:
                    print("[+] HTTP 200 OK，但无法解析响应体")
                    return True
            else:
                print("[+] HTTP 200 OK")
                return True
        elif "404" in response_str:
            print("[-] 代理返回404，路径可能不正确")
            return False
        else:
            print("[-] 发送失败")
            return False
            
    except Exception as e:
        print(f"[-] 发送失败: {e}")
        import traceback
        traceback.print_exc()
        return False


def send_wechat_message_via_connect_proxy(proxy_ip, proxy_port, webhook_url, message, timeout=20):
    """
    通过CONNECT隧道代理发送企业微信消息
    
    这种代理模式适用于支持CONNECT方法的HTTP隧道代理
    """
    print(f"\n[4] 发送测试消息到企业微信（CONNECT隧道模式）...")
    
    # 解析webhook URL
    host, port, path, is_https = parse_webhook_url(webhook_url)
    
    if not is_https:
        print(f"[-] 仅支持HTTPS webhook")
        return False
    
    try:
        # 建立到代理的连接
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((proxy_ip, proxy_port))
        print(f"[+] 已连接到代理 {proxy_ip}:{proxy_port}")
        
        # 发送CONNECT请求
        connect_req = f"CONNECT {host}:{port} HTTP/1.1\r\n"
        connect_req += f"Host: {host}:{port}\r\n"
        connect_req += "\r\n"
        sock.sendall(connect_req.encode('utf-8'))
        
        # 读取CONNECT响应
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
        
        response_str = response.decode('utf-8', errors='ignore')
        
        if "200" not in response_str:
            print(f"[-] CONNECT失败: {response_str[:200]}")
            sock.close()
            return False
        
        print("[+] CONNECT成功")
        
        # 包装SSL
        context = ssl.create_default_context()
        ssl_sock = context.wrap_socket(sock, server_hostname=host)
        print("[+] SSL握手成功")
        
        # 构建HTTP请求
        payload = {
            "msgtype": "text",
            "text": {
                "content": message
            }
        }
        body = json.dumps(payload, ensure_ascii=False)
        
        request = f"POST {path} HTTP/1.1\r\n"
        request += f"Host: {host}\r\n"
        request += "Content-Type: application/json\r\n"
        request += f"Content-Length: {len(body.encode('utf-8'))}\r\n"
        request += "Connection: close\r\n"
        request += "\r\n"
        request += body
        
        print(f"[*] 发送HTTP请求...")
        ssl_sock.sendall(request.encode('utf-8'))
        
        # 接收响应
        response = b""
        while True:
            try:
                chunk = ssl_sock.recv(4096)
                if not chunk:
                    break
                response += chunk
            except socket.timeout:
                break
        
        ssl_sock.close()
        
        response_str = response.decode('utf-8', errors='ignore')
        print(f"[*] 服务器响应:\n{response_str[:800]}")
        
        # 解析响应
        if "200" in response_str:
            # 提取JSON响应体
            parts = response_str.split("\r\n\r\n", 1)
            if len(parts) > 1:
                body = parts[1]
                try:
                    result = json.loads(body)
                    if result.get('errcode') == 0:
                        print("[+] 消息发送成功!")
                        return True
                    else:
                        print(f"[-] 企业微信返回错误: {result}")
                        return False
                except:
                    print("[+] HTTP 200 OK，但无法解析响应体")
                    return True
            else:
                print("[+] HTTP 200 OK")
                return True
        elif "errcode" in response_str:
            # 尝试解析错误
            try:
                match = re.search(r'\{[^}]*\}', response_str)
                if match:
                    err = json.loads(match.group())
                    print(f"[-] 企业微信错误: {err}")
            except:
                pass
            return False
        else:
            print("[-] 发送失败")
            return False
            
    except Exception as e:
        print(f"[-] 发送失败: {e}")
        import traceback
        traceback.print_exc()
        return False


def main():
    parser = argparse.ArgumentParser(
        description='HTTP代理服务器测试工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 自动从config.env加载配置并测试
  python test_proxy.py
  
  # 仅测试代理连通性（不发送消息）
  python test_proxy.py --test-connect-only
  
  # 手动指定参数
  python test_proxy.py --proxy-ip x.x.x.x --proxy-port 8080 \\
      --webhook "https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=YOUR_KEY"
        """
    )
    
    parser.add_argument('--proxy-ip', help='代理服务器IP')
    parser.add_argument('--proxy-port', type=int, help='代理服务器端口')
    parser.add_argument('--webhook', help='企业微信Webhook完整URL')
    parser.add_argument('--config', help='指定config.env路径')
    parser.add_argument('--auto-config', action='store_true',
                        help='自动从config.env加载代理配置（使用HTTP_PROXY_IP和HTTP_PROXY_PORT）')
    parser.add_argument('--test-connect-only', action='store_true',
                        help='仅测试代理连通性，不发送实际消息')
    parser.add_argument('--message',
                        help='要发送的测试消息（默认自动生成，包含代理服务器信息）')
    
    args = parser.parse_args()
    
    print("="*60)
    print("HTTP代理服务器测试工具")
    print("="*60)
    
    # 加载配置
    config = load_config_from_env(args.config)
    
    # 确定代理IP和端口
    proxy_ip = args.proxy_ip
    proxy_port = args.proxy_port
    
    if args.auto_config and config:
        # 自动配置模式：使用内网代理配置（HTTP_PROXY_IP/HTTP_PROXY_PORT）
        proxy_ip = config.get('HTTP_PROXY_IP', '').strip()
        if proxy_ip:
            print(f"[+] 从配置加载代理IP (HTTP_PROXY_IP): {proxy_ip}")
        
        port_str = config.get('HTTP_PROXY_PORT', '').strip()
        if port_str:
            try:
                proxy_port = int(port_str)
                print(f"[+] 从配置加载代理端口 (HTTP_PROXY_PORT): {proxy_port}")
            except ValueError:
                pass
    elif not args.proxy_ip and config:
        # 非自动配置但无手动指定：尝试使用BC260代理配置
        proxy_ip = config.get('BC260_PROXY_IP', '').strip()
        if proxy_ip:
            print(f"[+] 从配置加载代理IP (BC260_PROXY_IP): {proxy_ip}")
        
        port_str = config.get('BC260_PROXY_PORT', '').strip()
        if port_str:
            try:
                proxy_port = int(port_str)
                print(f"[+] 从配置加载代理端口 (BC260_PROXY_PORT): {proxy_port}")
            except ValueError:
                pass
    
    if not proxy_ip:
        proxy_ip = '127.0.0.1'
        print(f"[*] 使用默认代理IP: {proxy_ip}")
    
    if not proxy_port:
        proxy_port = 8080
        print(f"[*] 使用默认代理端口: {proxy_port}")
    
    # 确定webhook
    webhook_url = args.webhook
    if not webhook_url and config:
        webhook_key = config.get('WEBHOOK_KEY', '').strip()
        if webhook_key:
            webhook_url = f"https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key={webhook_key}"
            print(f"[+] 从配置加载webhook")
    
    print("\n" + "-"*60)
    print("测试配置:")
    print(f"  代理服务器: {proxy_ip}:{proxy_port}")
    if webhook_url:
        masked_url = webhook_url[:60] + "..." if len(webhook_url) > 60 else webhook_url
        print(f"  Webhook: {masked_url}")
    print("-"*60)
    
    # 执行测试
    results = {}
    
    # 测试1: TCP连接
    results['tcp_connect'] = test_tcp_connect(proxy_ip, proxy_port)
    if not results['tcp_connect']:
        print("\n[-] TCP连接失败，请检查:")
        print("    1. 代理服务器是否运行")
        print("    2. 代理IP和端口是否正确")
        print("    3. 网络是否可达")
        sys.exit(1)
    
    # 测试2: 应用层代理发送消息
    if webhook_url and not args.test_connect_only:
        # 如果没有指定消息，自动生成包含代理服务器信息的消息
        if args.message:
            message = args.message
        else:
            message = (
                f"【代理测试】\n"
                f"代理服务器: {proxy_ip}:{proxy_port}\n"
                f"状态: 连接测试成功\n"
                f"时间: {time.strftime('%Y-%m-%d %H:%M:%S')}"
            )
        results['send_message'] = send_wechat_message_via_app_proxy(
            proxy_ip, proxy_port, webhook_url, message
        )
    
    # 打印测试摘要
    print("\n" + "="*60)
    print("测试摘要")
    print("="*60)
    
    test_names = {
        'tcp_connect': 'TCP连接',
        'send_message': '发送消息'
    }
    
    for key, name in test_names.items():
        if key in results:
            status = "[OK] 通过" if results[key] else "[FAIL] 失败"
            print(f"  {name}: {status}")
    
    print("="*60)
    
    if all(results.values()):
        print("\n[+] 所有测试通过！代理服务器工作正常")
        sys.exit(0)
    else:
        print("\n[-] 部分测试失败")
        sys.exit(1)


if __name__ == '__main__':
    main()
