import time
import json
import serial
import os
import threading
from msgpackrpc import Address as RpcAddress, Client as RpcClient
import socket
import hashlib
import struct
from dataclasses import dataclass

# --- Global Configuration ---
UDP_PORT = 17751
GIGA_UART_PORT = "/dev/ttymxc1"
GIGA_BAUD_RATE = 115200
M4_PROXY_ADDRESS = 'm4-proxy'
M4_PROXY_PORT = 5001
CONFIG_FILE_PATH = "config.json"
POLL_INTERVAL = 0.05      # 20 Hz
BROADCAST_INTERVAL = 0.2  # 5 Hz

# --- Global State ---
WEB_CLIENTS = set()
DATA_LOCK = threading.RLock()
RPC_LOCK = threading.RLock()
ser = None
CURRENT_MODE = "local" # Default to local mode
M4_DATA_SNAPSHOT = {}

# --- Signal Database (populated from config.json) ---
SIGNAL_DB = {}
SIGNALS_BY_ID = {}
GIGA_TO_RPC_MAP = {}

# --- Sockets & Protocol ---
udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
udp_sock.bind(("0.0.0.0", UDP_PORT))
udp_sock.setblocking(False)
DEFAULT_SIGN_KEY = bytes.fromhex(('57 4F 4F 44 52 55 46 46 ' * 16).replace(' ', ''))
ACK_OK = 0xA0

@dataclass
class Signal:
    name: str; id: int; source: str; type: str
    rpc_set_func: str | None = None
    value: float | int | bool = 0
    def __post_init__(self):
        if self.type == "float": self.value = 0.0
        elif self.type == "int": self.value = 0
        elif self.type == "bool": self.value = False

def load_config_and_build_tables(config_path: str):
    global CURRENT_MODE
    print(f"[Config] Loading configuration from {config_path}")
    try:
        with open(config_path, 'r') as f: config = json.load(f)
    except Exception as e:
        print(f"[Config] CRITICAL ERROR: {e}"); exit(1)

    SIGNAL_DB.clear(); SIGNALS_BY_ID.clear(); GIGA_TO_RPC_MAP.clear()
    for name, props in config.get("signals", {}).items():
        try:
            sig = Signal(name=name, id=int(str(props["id"]),0), source=props["source"],
                         type=props["type"], rpc_set_func=props.get("rpc_set_func"),
                         value=props.get("default", 0))
            SIGNAL_DB[name] = sig; SIGNALS_BY_ID[sig.id] = sig
            if sig.source == "linux" and sig.rpc_set_func:
                GIGA_TO_RPC_MAP[name] = sig.rpc_set_func
        except Exception as e: print(f"[Config] ERROR processing signal '{name}': {e}")
    
    mode_val = get_signal_value("mode_set")
    CURRENT_MODE = "remote" if mode_val else "local"
    print(f"[Config] Loaded {len(SIGNAL_DB)} signals. Initial mode: {CURRENT_MODE}")
    return config

def get_signal_value(name: str):
    with DATA_LOCK: return SIGNAL_DB.get(name).value if name in SIGNAL_DB else 0

def set_signal_value(name: str, value, src="unknown"):
    global CURRENT_MODE
    with DATA_LOCK:
        if name not in SIGNAL_DB:
            print(f"[Error] Set unknown signal: {name}"); return
        if SIGNAL_DB[name].source == "m4" and src != "rpc": return
        
        SIGNAL_DB[name].value = value
        print(f"-> set [{src}] {name} = {value}")
        
        if name == "mode_set":
            CURRENT_MODE = "remote" if value else "local"
            print(f"[Logic] Mode changed to: {CURRENT_MODE}")

        giga_value = 1 if value else 0 if SIGNAL_DB[name].type == "bool" else value
        broadcast_binary_value(name, value)
        send_to_giga({"display_event": {"type": "set_value", "name": name, "value": giga_value, "src": src}})

def call_m4_rpc(function_name, *args, retries=1, timeout=0.05):
    with RPC_LOCK:
        for _ in range(retries + 1):
            client = None
            try:
                client = RpcClient(RpcAddress(M4_PROXY_ADDRESS, M4_PROXY_PORT), timeout=timeout, reconnect_limit=3)
                return client.call(function_name, *args)
            except Exception: pass
            finally:
                if client: client.close()
            time.sleep(0.05)
    return None

def poll_m4_signals():
    def sign_extend(v, b): s = 1 << (b - 1); return (v & (s - 1)) - (v & s)
    word = call_m4_rpc("get_poll_data", retries=0, timeout=0.5)
    if not isinstance(word, int): return
    pid = (word >> 63) & 1
    if pid == 0:
        M4_DATA_SNAPSHOT['flags']= (word >> 58) & 0x1F
        M4_DATA_SNAPSHOT['volt_act'] = sign_extend((word >> 38) & 0xFFFFF, 20)
        M4_DATA_SNAPSHOT['curr_act'] = sign_extend((word >> 18) & 0xFFFFF, 20)
    else:
        M4_DATA_SNAPSHOT['curr_set'] = sign_extend((word >> 43) & 0xFFFFF, 20)
        M4_DATA_SNAPSHOT['temp'] = sign_extend((word >> 23) & 0xFFFFF, 20)
    if all(k in M4_DATA_SNAPSHOT for k in ['flags','volt_act','curr_act','curr_set','temp']):
        set_signal_value("extern_enable", bool(M4_DATA_SNAPSHOT['flags'] & 1), src="rpc")
        set_signal_value("volt_act", round(M4_DATA_SNAPSHOT['volt_act']/100.0,2), src="rpc")
        set_signal_value("curr_act", round(M4_DATA_SNAPSHOT['curr_act']/100.0,2), src="rpc")
        set_signal_value("output_enable", get_signal_value("internal_enable") and get_signal_value("extern_enable"), src="logic")
        M4_DATA_SNAPSHOT.clear()

def apply_math_operation(current, increment, op="set"):
    return current + increment if op == "add" else increment

def process_giga_event(event_data):
    event = (event_data or {}).get("display_event") or event_data
    if not isinstance(event, dict) or event.get("type") != "button_press": return
    name, value, do_type = event.get("name"), event.get("value",0), event.get("do","set")
    
    # --- UPDATED LOGIC ---
    # If the event is to change the mode, always allow it to proceed.
    # For any other event, check if we are in remote mode and block it if so.
    if name == "mode_set":
        pass # Always allow mode changes
    elif CURRENT_MODE == "remote":
        print(f"[Logic] Ignoring Giga event '{name}' in remote mode.")
        return

    if not name or name not in SIGNAL_DB or SIGNAL_DB[name].source != "linux": return
    rpc_func = GIGA_TO_RPC_MAP.get(name)
    if not rpc_func and name != "mode_set": return

    new_val = apply_math_operation(get_signal_value(name), value, op=do_type)
    
    # --- UPDATED LOGIC ---
    # If the event is a mode change, always process it to allow for re-syncing the UI.
    # For all other events, ignore them if the value hasn't actually changed.
    if name == "mode_set":
        pass # Always process mode changes
    elif get_signal_value(name) == new_val:
        print(f"[Logic] Ignoring redundant Giga set for '{name}'.")
        return

    new_val = bool(new_val) if SIGNAL_DB[name].type == "bool" else new_val
    if rpc_func:
        print(f"[RPC] Calling {rpc_func}({new_val}) for Giga event '{name}'")
        call_m4_rpc(rpc_func, new_val)
    set_signal_value(name, new_val, src="giga")

def handle_udp_packet(data, addr):
    if len(data) != 14: return
    payload, signature = data[:10], data[10:]
    if hashlib.sha256(payload + DEFAULT_SIGN_KEY).digest()[-4:] != signature: return
    
    sig_id, sig_type = payload[4], payload[5]
    sig = SIGNALS_BY_ID.get(sig_id)
    if not sig: return
    
    # --- UPDATED LOGIC ---
    # If the command is to change mode, always allow it.
    # For any other command, check if we are in local mode and block it.
    if sig.name == "mode_set":
        pass # Always allow mode changes
    elif CURRENT_MODE == "local":
        return

    if sig_type == 0x10: # SET
        value = struct.unpack('>f', payload[6:10])[0]
        
        # --- UPDATED LOGIC ---
        # If the command is a mode change, always process it.
        # Otherwise, check for redundancy.
        if sig.name == "mode_set":
            pass # Always process
        elif get_signal_value(sig.name) == value:
            return

        set_signal_value(sig.name, value, src="udp")

def send_to_giga(json_data):
    if ser and ser.is_open:
        try: ser.write((json.dumps(json_data) + '\n').encode('utf-8'))
        except Exception as e: print(f"[UART] Error: {e}")

def read_from_giga():
    if ser and ser.is_open and ser.in_waiting:
        try: return json.loads(ser.readline().decode('utf-8').strip())
        except Exception: return None

def broadcast_all_values():
    with DATA_LOCK: signals_snapshot = list(SIGNAL_DB.values())
    for sig in signals_snapshot:
        giga_value = 1 if sig.value else 0 if sig.type == 'bool' else sig.value
        send_to_giga({"display_event": {"type":"set_value", "name":sig.name, "value":giga_value, "src":sig.source}})
        broadcast_binary_value(sig.name, sig.value)

def broadcast_binary_value(name, value, target_addr=None):
    with DATA_LOCK:
        sig = SIGNAL_DB.get(name)
        if not sig: return
        uid = os.urandom(4)
        val_bytes = struct.pack('>I',int(value)&0xFFFFFFFF) if sig.type=="int" else struct.pack('>f',float(value))
        payload = uid + bytes([sig.id, ACK_OK]) + val_bytes
        packet = payload + hashlib.sha256(payload + DEFAULT_SIGN_KEY).digest()[-4:]
    for client in ([target_addr] if target_addr else WEB_CLIENTS):
        if client:
            try: udp_sock.sendto(packet, client)
            except Exception: pass

def udp_listener():
    print(f"[UDP] Listening on port {UDP_PORT}")
    while True:
        try:
            data, addr = udp_sock.recvfrom(1024); WEB_CLIENTS.add(addr)
            handle_udp_packet(data, addr)
        except BlockingIOError: time.sleep(0.01)
        except Exception as e: print(f"[UDP] Error: {e}"); time.sleep(0.05)

if __name__ == "__main__":
    print("--- Portenta Linux Bridge (Config-Driven w/ Mode Control) ---")
    config = load_config_and_build_tables(CONFIG_FILE_PATH)
    threading.Thread(target=udp_listener, daemon=True).start()
    try:
        ser = serial.Serial(GIGA_UART_PORT, GIGA_BAUD_RATE, timeout=0.05)
        print(f"[Init] Serial port {GIGA_UART_PORT} opened.")
        if "display_config" in config:
            send_to_giga({"display_config": config["display_config"]})
    except serial.SerialException as e: print(f"[Init] CRITICAL: {e}"); exit(1)
    
    last_broadcast, last_poll = 0, 0
    try:
        while True:
            now = time.time()
            if event := read_from_giga(): process_giga_event(event)
            if now - last_poll > POLL_INTERVAL: poll_m4_signals(); last_poll = now
            if now - last_broadcast > BROADCAST_INTERVAL: broadcast_all_values(); last_broadcast = now
            time.sleep(0.01)
    except KeyboardInterrupt: print("\n[Shutdown] Exiting.")
    finally:
        if ser and ser.is_open: ser.close()

