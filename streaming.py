from flask import Flask, Response
import cv2

app = Flask(__name__)

# OpenCV로 영상 스트림 열기
camera = cv2.VideoCapture("tcp://127.0.0.1:8888", cv2.CAP_FFMPEG)

def generate_frames():
    while True:
        success, frame = camera.read()
        if not success:
            break
        _, buffer = cv2.imencode('.jpg', frame)
        frame_bytes = buffer.tobytes()
        yield (
            b'--frame\r\n'
            b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n'
        )

@app.route('/')
def index():
    return Response(
        generate_frames(),
        mimetype='multipart/x-mixed-replace; boundary=frame'
    )

# 웹 서버 시작
app.run(host='0.0.0.0', port=5000)
