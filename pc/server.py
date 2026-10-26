# server.py - a stand-in for Juxt for the PC test build: serves the saved
# pages in pages/, answers sign-in, Yeahs, follows and posts, and draws
# simple pictures for any image address. Plain HTTP on 127.0.0.1:8099.
import http.server, io, os, random, re, sys, urllib.parse, json
from PIL import Image, ImageDraw
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pages')
LOG = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'server.log'), 'a')
def page(name):
    return open(os.path.join(D, name), 'rb').read()
def png(path):
    r = random.Random(path)
    if '/paintings/' in path:
        im = Image.new('RGB', (320, 120), 'white'); d = ImageDraw.Draw(im)
        pts = [(r.randint(10, 310), r.randint(10, 110)) for _ in range(7)]
        d.line(pts, fill='black', width=3)
        cx, cy = r.randint(40, 280), r.randint(30, 90); d.ellipse((cx-14, cy-14, cx+14, cy+14), outline='black', width=3)
    elif '/screenshots/' in path:
        im = Image.new('RGB', (400, 240), (r.randint(40, 200), r.randint(60, 200), r.randint(80, 220))); d = ImageDraw.Draw(im)
        for _ in range(6):
            x, y = r.randint(0, 380), r.randint(0, 220); d.rectangle((x, y, x+r.randint(20, 120), y+r.randint(20, 80)), fill=(r.randint(0,255), r.randint(0,255), r.randint(0,255)))
    elif '/icons/' in path:
        im = Image.new('RGB', (128, 128), (r.randint(30, 230), r.randint(30, 230), r.randint(30, 230))); d = ImageDraw.Draw(im)
        d.rounded_rectangle((24, 24, 104, 104), 18, fill='white'); d.ellipse((44, 44, 84, 84), fill=(r.randint(0,200), r.randint(0,200), r.randint(0,200)))
    else:  # a Mii-ish face
        im = Image.new('RGB', (96, 96), (240, 243, 250)); d = ImageDraw.Draw(im)
        skin = r.choice([(255, 220, 190), (240, 200, 160), (200, 150, 110)]); hair = (r.randint(20, 120),) * 3
        d.ellipse((14, 10, 82, 90), fill=hair); d.ellipse((18, 22, 78, 88), fill=skin)
        d.ellipse((34, 46, 42, 56), fill='black'); d.ellipse((54, 46, 62, 56), fill='black'); d.arc((36, 54, 60, 74), 20, 160, fill=(150, 60, 60), width=3)
    b = io.BytesIO(); im.save(b, 'PNG'); return b.getvalue()
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *a): pass
    def send(self, code, body=b'', ctype='text/html; charset=utf-8', extra=()):
        self.send_response(code)
        for k, v in extra: self.send_header(k, v)
        self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        p = self.path; LOG.write('GET %s\n' % p); LOG.flush()
        u = urllib.parse.urlparse(p); path = u.path
        if re.search(r'\.(png|jpg|jpeg)$', path): return self.send(200, png(path), 'image/png')
        if path.startswith('/feed'): return self.send(200, page('feed_more.html' if 'offset' in u.query else 'feed.html'))
        if path in ('/titles', '/titles/all'): return self.send(200, page('communities.html'))
        if path.startswith('/titles/'): return self.send(200, page('feed_more.html' if 'offset' in u.query else 'community.html'))
        if path.startswith('/posts/'): return self.send(200, page('post.html'))
        if path == '/users/notifications.json': return self.send(200, b'{"message_count":0,"notification_count":3}', 'application/json')
        if path.startswith('/users/'): return self.send(200, page('user.html'))
        if path.startswith('/news'): return self.send(200, page('news.html'))
        self.send(404, b'not found')
    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0)); body = self.rfile.read(n)
        LOG.write('POST %s %d\n' % (self.path, n)); LOG.flush()
        path = self.path
        if path == '/login':
            f = urllib.parse.parse_qs(body.decode())
            if f.get('password', [''])[0] == 'bad':
                return self.send(200, page('login_bad.html'))
            return self.send(302, b'', extra=[('Location', '/users/me'), ('Set-Cookie', 'access_token=test123; Path=/; HttpOnly'), ('Set-Cookie', 'refresh_token=r456; Path=/')])
        if path == '/posts/empathy': return self.send(200, b'{"status":200,"id":"x","count":13}', 'application/json')
        if path.endswith('/follow'): return self.send(200, b'{"status":200,"action":"follow","count":27}', 'application/json')
        if path.startswith('/posts'):
            open(os.path.join(os.path.dirname(D), 'last_post.bin'), 'wb').write(body)
            return self.send(302, b'', extra=[('Location', '/titles/2551084080/new')])
        self.send(404, b'')
http.server.ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1]) if len(sys.argv) > 1 else 8099), H).serve_forever()
