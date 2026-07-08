from http.server import BaseHTTPRequestHandler, HTTPServer


class HelloHandler(BaseHTTPRequestHandler):
    def send_text_response(self, status_code, body):
        self.send_response(status_code)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write(body.encode("utf-8"))

    def do_GET(self):
        if self.path == "/hello":
            print("Hello World", flush=True)
            self.send_text_response(200, "Hello World from Python to TypeScript\n")
            return

        self.send_text_response(404, "Not Found\n")


def main():
    server = HTTPServer(("localhost", 8000), HelloHandler)
    print("Listening on http://localhost:8000", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down", flush=True)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
