#!/usr/bin/env python3
"""常驻 Whisper STT 服务: 供小豆丁后端 voice.js 调用
POST /transcribe (body = WAV/PCM音频) -> {"text": "识别文字"}
监听 127.0.0.1:8794 (仅本机, 不暴露局域网)
模型 base 常驻内存, 避免每次重建 (~省1.5s)
"""
import os, sys, glob, json, tempfile, wave, io
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# 复用 dell_voice.py 的 whisper 加载方式
os.environ['HF_ENDPOINT'] = 'https://hf-mirror.com'
os.environ['HF_HUB_DISABLE_XET'] = '1'
sp = glob.glob(os.path.expanduser("~/whisper-env/lib/python*/site-packages"))
if sp:
    sys.path.insert(0, sp[0])
from faster_whisper import WhisperModel

PORT = 8796
MODEL = os.environ.get('WHISPER_MODEL', 'base')

# 模型常驻
print("加载 Whisper %s 模型 (常驻)..." % MODEL, flush=True)
MODEL_OBJ = WhisperModel(MODEL, device='cpu', compute_type='int8')
print("Whisper 就绪, 监听 :%d" % PORT, flush=True)


class H(BaseHTTPRequestHandler):
    def _parse_audio(self, body, content_type):
        # 支持 WAV (带header) 或 裸 PCM (需指定率)
        # 优先当 WAV 解析
        try:
            with wave.open(io.BytesIO(body)) as w:
                rate = w.getframerate()
                nch = w.getnchannels()
                pcm = w.readframes(w.getnframes())
                return pcm, rate, nch
        except Exception:
            # 不是WAV, 当作 16kHz mono 裸 PCM
            return body, 16000, 1

    def do_POST(self):
        if self.path != '/transcribe':
            self.send_response(404); self.end_headers(); return
        try:
            n = int(self.headers.get('Content-Length', 0) or 0)
            body = self.rfile.read(n)
            if not body:
                self._json(422, {'error': 'empty'})
                return
            pcm, rate, nch = self._parse_audio(body, self.headers.get('Content-Type',''))
            # faster-whisper 需要文件或 numpy
            # 写到临时 WAV
            with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as f:
                tmp = f.name
            with wave.open(tmp, 'wb') as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
                w.writeframes(pcm)
            segments, _ = MODEL_OBJ.transcribe(tmp, language='zh')
            text = ''.join(s.text for s in segments).strip()
            os.remove(tmp)
            self._json(200, {'text': text})
        except Exception as e:
            self._json(500, {'error': str(e)[:200]})

    def _json(self, code, obj):
        b = json.dumps(obj, ensure_ascii=False).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def log_message(self, *a):
        pass


if __name__ == '__main__':
    ThreadingHTTPServer(('127.0.0.1', PORT), H).serve_forever()
