import http.server
import socketserver
import os
import mimetypes

PORT = 8080
ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WASM_DIR = os.path.join(ROOT_DIR, "build-wasm")
DIRECTORY = WASM_DIR if os.path.isdir(WASM_DIR) else ROOT_DIR

# Ensure proper MIME types for WASM (critical for Safari)
mimetypes.add_type('application/wasm', '.wasm')
mimetypes.add_type('application/javascript', '.js')

class MyHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def end_headers(self):
        # Cross-Origin Isolation headers (required for SharedArrayBuffer if used)
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        # Prevent caching issues on Safari
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()

    def guess_type(self, path):
        """Override to ensure .wasm gets correct MIME type"""
        if path.endswith('.wasm'):
            return 'application/wasm'
        return super().guess_type(path)

# SO_REUSEADDR prevents "Address already in use" from TIME_WAIT sockets;
# threading allows concurrent asset requests without head-of-line blocking.
class AcuteSimServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

with AcuteSimServer(("", PORT), MyHTTPRequestHandler) as httpd:
    print(f"Serving {DIRECTORY} at http://localhost:{PORT}")
    print("Safari users: Clear browser cache and hard refresh (Cmd+Shift+R)")
    httpd.serve_forever()