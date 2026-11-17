#!/usr/bin/env python3
import socket, socketserver, time, pathlib, datetime, struct

HOST, PORT = '0.0.0.0', 8766
OUT = pathlib.Path('/mnt/documents/桌面/web游戏/esp32-rlcd/voice_capture')
OUT.mkdir(exist_ok=True)

def header(data_len=0):
    return struct.pack('<4sI4s4sIHHIIHH4sI',
        b'RIFF', 36 + data_len, b'WAVE', b'fmt ', 16, 1, 1,
        16000, 32000, 2, 16, b'data', data_len)

def recv_exact(conn, n):
    buf = b''
    while len(buf) < n:
        d = conn.recv(n - len(buf))
        if not d:
            return None
        buf += d
    return buf

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        conn = self.request
        conn.settimeout(15)
        wf = None; path = None; total = 0
        print(f'[vad] connected {self.client_address}', flush=True)
        try:
            while True:
                hdr = recv_exact(conn, 4)
                if hdr is None: break
                ln = struct.unpack('<I', hdr)[0]
                if ln < 1 or ln > 4096: break
                body = recv_exact(conn, ln)
                if body is None: break
                typ = body[0]; data = body[1:]
                if typ == 1:  # START
                    if wf:
                        try: wf.close()
                        except Exception: pass
                    name = datetime.datetime.now().strftime('speech_%Y%m%d_%H%M%S.wav')
                    path = OUT / name
                    wf = open(path, 'wb', buffering=0)
                    wf.write(header(0)); total = 0
                    print(f'[vad] start -> {path}', flush=True)
                elif typ == 2 and wf:  # DATA
                    wf.write(data); total += len(data)
                elif typ == 3:  # END
                    if wf:
                        try:
                            wf.seek(4); wf.write(struct.pack('<I', 36 + total))
                            wf.seek(40); wf.write(struct.pack('<I', total))
                            wf.close()
                        except Exception: pass
                        print(f'[vad] end {path.name}, {total} bytes', flush=True)
                        wf = None; path = None; total = 0
                    try:
                        conn.sendall(struct.pack('<I', 1) + b'\x04')
                    except Exception:
                        pass
        except Exception as e:
            print(f'[vad] {e}', flush=True)
        finally:
            if wf:
                try:
                    wf.seek(4); wf.write(struct.pack('<I', 36 + total))
                    wf.seek(40); wf.write(struct.pack('<I', total))
                    wf.close()
                except Exception: pass
            print(f'[vad] closed {self.client_address}', flush=True)

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

if __name__ == '__main__':
    print(f'VAD capture server listening on {HOST}:{PORT}', flush=True)
    Server((HOST, PORT), Handler).serve_forever()
