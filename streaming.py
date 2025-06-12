from flask import Flask, Response
import cv2
import numpy as np
from picamera2 import Picamera2
import time
from collections import deque
import serial
WINDOW_SIZE = 5
steer_buffer = deque(maxlen=WINDOW_SIZE)
prev_to_send = None  # 마지막으로 보낸 값 저장


# ▸ Arduino 시리얼 초기화 (한 번만)
# ================================
SERIAL_PORT   = '/dev/ttyACM0'  # ▶ 실제 연결된 포트명으로 바꾸세요
BAUDRATE      = 9600            # ▶ Arduino 코드에서 Serial.begin(9600)이라면 9600으로 맞춰야 합니다

try:
    arduino = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
    time.sleep(2)  # ▶ Arduino가 리셋되고 시리얼 연결 준비되는 시간을 줍니다
except Exception as e:
    print(f"Error: 시리얼 포트 열기 실패: {e}")
    exit(1)



app = Flask(__name__)

def detect_edges_and_slope_30_80(frame):
    """
    입력: BGR 이미지 frame
    처리:
      1) 화면 세로 30%~80% 구간을 ROI로 잡아 Canny 엣지 계산
      2) ROI 내 엣지 픽셀들의 x 중앙값(center_x) 계산
      3) HoughLinesP로 평균 기울기(slope_avg) 계산 (dy/dx)
      4) 전체 크기 흑백 마스크(ROI 30~80% 영역에만 엣지 실루엣, 나머지 흰색) 반환
      5) ROI 시작 y(=30% 위치) 반환
    """
    h, w = frame.shape[:2]

    # ROI: 화면 세로 0%~80%
    y30 = int(h * 0)
    y80 = int(h * 0.8)
    roi = frame[y30:y80, 0:w]

    gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
    blurred = cv2.GaussianBlur(gray, (5, 5), 0)
    edges = cv2.Canny(blurred, 50, 150)

    # 모폴로지 확장 연산 적용 (dilate)
    kernel = np.ones((5, 5), np.uint8)
    edges = cv2.dilate(edges, kernel, iterations=1)

    # edges의 y좌표 240~270 영역을 0으로
    edges[200:231, :] = 0

    # HoughLinesP로 평균 기울기 계산
    lines = cv2.HoughLinesP(
        edges,
        rho=1,
        theta=np.pi / 180,
        threshold=50,
        minLineLength=50,
        maxLineGap=1
    )

    # “선만 남긴” 흑백 마스크 ROI
    mask_roi = np.ones_like(gray, dtype=np.uint8) * 255
    mask_roi[edges > 0] = 0
    # mask_roi = edges.copy()

    imsi_daepyo = []
    if lines is not None:
        for l in lines:
            x1, y1, x2, y2 = l[0]
            # y좌표가 200을 넘는 점이 포함된 직선만 선택
            if y1 > 200 or y2 > 200:
                # y1이 y2보다 크도록 정렬
                if y1 < y2:
                    x1, y1, x2, y2 = x2, y2, x1, y1
                imsi_daepyo.append((x1, y1, x2, y2))
    b_daepyo = np.mean(imsi_daepyo, axis=0) if imsi_daepyo else None
    imsi_daepyo = []
    if lines is not None:
        for l in lines:
            x1, y1, x2, y2 = l[0]
            # y좌표가 200을 넘는 점이 포함된 직선만 선택
            if y1 <= 200 or y2 <= 200:
                # y1이 y2보다 크도록 정렬
                if y1 < y2:
                    x1, y1, x2, y2 = x2, y2, x1, y1
                imsi_daepyo.append((x1, y1, x2, y2))
    t_daepyo = np.mean(imsi_daepyo, axis=0) if imsi_daepyo else None

    # 전체 크기로 복원 (ROI 외곽은 흰색)
    full_mask = np.ones((h, w), dtype=np.uint8) * 255
    full_mask[y30:y80, 0:w] = mask_roi
    # full_mask를 3채널(RGB)로 변환
    full_mask = cv2.cvtColor(full_mask, cv2.COLOR_GRAY2BGR)

    return full_mask, b_daepyo, t_daepyo

# Picamera2 설정 & 워밍업
picam2 = Picamera2()
camera_config = picam2.create_preview_configuration(
    main={"format": "BGR888", "size": (640, 480)}
)
picam2.configure(camera_config)
picam2.start()
time.sleep(2)

def generate_frames():
    global prev_to_send, arduino
    while True:
        frame = picam2.capture_array()
        h, w = frame.shape[:2]

        # 엣지 + 기울기 계산(30~80% ROI)
        mask_color, near_line, far_line = detect_edges_and_slope_30_80(frame)

        # near_line과 far_line 그리기
        if near_line is not None:
            x1b, y1b, x2b, y2b = map(int, near_line)
            cv2.line(mask_color, (x1b, y1b), (x2b, y2b), (0, 255, 0), 2)
        if far_line is not None:
            x1t, y1t, x2t, y2t = map(int, far_line)
            cv2.line(mask_color, (x1t, y1t), (x2t, y2t), (0, 255, 0), 2)

        # 각 라인의 x좌표와 각도 계산
        line_info = []
        for line in [near_line, far_line]:
            if line is not None:
                x1, y1, x2, y2 = map(float, line)
                x_center = (x1 + x2) / 2
                dx = x1 - x2
                dy = y1 - y2
                angle_deg = np.degrees(np.arctan2(dy, dx)) if dx != 0 else (90.0 if dy > 0 else -90.0)
                x_center = x_center - 320
                angle_deg = angle_deg - 90.0
                line_info.append({'x_center': x_center, 'angle_deg': angle_deg})
            else:
                line_info.append({'x_center': None, 'angle_deg': None})

        # ————————— 여기부터 수정된 부분 —————————
        # Near Line 레이블 생성
        if not line_info or line_info[0]['x_center'] is None or line_info[0]['angle_deg'] is None:
            near_label = "Near Line: no data"
        else:
            x0 = line_info[0]['x_center']
            a0 = line_info[0]['angle_deg']
            near_label = f"Near Line: x={x0:.1f}, angle={a0:.1f} deg"

        # Far Line 레이블 생성
        if not line_info or line_info[1]['x_center'] is None or line_info[1]['angle_deg'] is None:
            far_label = "Far Line: no data"
        else:
            x1 = line_info[1]['x_center']
            a1 = line_info[1]['angle_deg']
            far_label = f"Far Line: x={x1:.1f}, angle={a1:.1f} deg"
        # ————————————————————————————————

        # 생성한 레이블을 화면에 출력
        cv2.putText(
            mask_color,
            near_label,
            (10, 430), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 255), 1
        )
        cv2.putText(
            mask_color,
            far_label,
            (10, 450), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 255), 1
        )

        # 조향 변수 계산
        steer = 0.0
        if (line_info[0]['x_center'] is not None and
            line_info[0]['angle_deg'] is not None and
            line_info[1]['angle_deg'] is not None):
            steer = (
                0.7 * line_info[0]['x_center'] +
                0.2 * line_info[0]['angle_deg'] +
                0.1 * line_info[1]['angle_deg']
            )
        cv2.putText(
            mask_color,
            f"Steer: {steer:.1f}",
            (10, 470), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2
        )

        # ================================
        # ▸ 여기에 Arduino로 “avg_steer” 값을 보내는 코드 추가
        #    – prev_to_send가 None 또는 이전과 다를 때만 전송하도록 함
        # ================================
        if steer is not None:
            to_send = int(round(steer))
        else:
            to_send = None

        if to_send != prev_to_send:
            if to_send is not None:
                arduino.write(f"{to_send}\n".encode())
            else:
                arduino.write("N/A\n".encode())
            prev_to_send = to_send    # 숫자와 None 모두 공통으로 갱신


        if arduino.in_waiting > 0:
            response = arduino.readline().decode(errors='ignore').strip()
            print(f"[Arduino echo] {response}")
            
        # steer 값을 버퍼에 추가        
        steer_buffer.append(steer)


        # JPEG 인코딩 & 스트리밍
        ret, buffer = cv2.imencode('.jpg', mask_color)
        if not ret:
            continue
        frame_bytes = buffer.tobytes()
        yield (
            b'--frame\r\n'
            b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n'
        )


@app.route('/video_feed')
def video_feed():
    return Response(
        generate_frames(),
        mimetype='multipart/x-mixed-replace; boundary=frame'
    )

@app.route('/')
def index():
    return """
    <html>
      <head><title>기울기에 맞춘 80px 초록 선</title></head>
      <body style="margin:0; padding:0; text-align:center; background-color:#222;">
        <h2 style="color:#fff;">
          화면 세로 30~80%에서 엣지 검출 →
          화면 세로 60% 위치에서 80px 크기로 기울기 일치 초록 선
        </h2>
        <img src="/video_feed" style="max-width:100%; height:auto;" />
      </body>
    </html>
    """

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)
