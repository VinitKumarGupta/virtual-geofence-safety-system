import cv2
import socket
import numpy as np
import time

# ================= CONFIGURATION =================
UDP_IP = "0.0.0.0"
UDP_PORT = 1234
MUSCLE_IP = "10.114.170.XYZ"  # <--- ESP32 (Server) IP
MUSCLE_PORT = 3333

SCALE_FACTOR = 2.0  
# Center the box (roughly) for a 320x240 feed scaled by 2x
DANGER_ZONE_X = 150 
DANGER_ZONE_Y = 100
DANGER_ZONE_W = 300
DANGER_ZONE_H = 200

# SENSITIVITY
MIN_AREA = 500  # Minimum size of object to trigger (to filter noise)
# =================================================

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
print(f"Listening on {UDP_PORT} (Motion Mode)...")

frame_buffer = {}
current_frame_id = -1
last_command_time = 0

# Background Subtraction Variables
background_model = None
frame_count = 0

while True:
    try:
        data, addr = sock.recvfrom(65535)
        if len(data) < 4: continue 

        # Packet Assembly
        frame_id = data[0]
        chunk_idx = data[1]
        total_chunks = data[2]
        img_data = data[3:]

        if frame_id != current_frame_id:
            frame_buffer = {}
            current_frame_id = frame_id

        frame_buffer[chunk_idx] = img_data

        if len(frame_buffer) == total_chunks:
            sorted_chunks = [frame_buffer[i] for i in range(total_chunks)]
            full_data = b''.join(sorted_chunks)
            np_arr = np.frombuffer(full_data, dtype=np.uint8)
            frame = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)

            if frame is not None:
                # 1. Flip and Resize
                frame = cv2.flip(frame, 1)
                height, width = frame.shape[:2]
                new_dim = (int(width * SCALE_FACTOR), int(height * SCALE_FACTOR))
                frame = cv2.resize(frame, new_dim, interpolation=cv2.INTER_LINEAR)
                
                # 2. Convert to Grayscale and Blur
                gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
                gray = cv2.GaussianBlur(gray, (21, 21), 0)

                # --- Calibrates the first 30 frames ---
                if frame_count < 30:
                    if background_model is None:
                        # Convertig to FLOAT for accumulateWeighted
                        background_model = gray.copy().astype("float")
                    
                    # Accumulate background
                    cv2.accumulateWeighted(gray, background_model, 0.5)
                    frame_count += 1
                    
                    cv2.putText(frame, f"CALIBRATING... {frame_count}/30", (50, 50), 
                               cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 255), 2)
                    cv2.imshow("Smart Safety System", frame)
                    
                    # Clear buffer and skip the rest of the loop
                    frame_buffer = {}
                    if cv2.waitKey(1) & 0xFF == ord('q'): break
                    continue
                
                # --- RUNNING PHASE ---
                
                # Update background slowly (to handle lighting changes)
                cv2.accumulateWeighted(gray, background_model, 0.05)
                
                # Compute Difference (Must convert background back to uint8)
                frame_delta = cv2.absdiff(gray, cv2.convertScaleAbs(background_model))
                thresh = cv2.threshold(frame_delta, 25, 255, cv2.THRESH_BINARY)[1]
                thresh = cv2.dilate(thresh, None, iterations=2)

                # Crop to Danger Zone
                dz_x = int(DANGER_ZONE_X)
                dz_y = int(DANGER_ZONE_Y)
                dz_w = int(DANGER_ZONE_W)
                dz_h = int(DANGER_ZONE_H)
                
                roi = thresh[dz_y:dz_y+dz_h, dz_x:dz_x+dz_w]
                
                # Find contours
                cnts, _ = cv2.findContours(roi.copy(), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
                
                danger_detected = False
                color = (0, 255, 0) # Green

                for c in cnts:
                    if cv2.contourArea(c) < MIN_AREA:
                        continue
                    
                    danger_detected = True
                    # Draw object
                    c_shifted = c + np.array([dz_x, dz_y])
                    cv2.drawContours(frame, [c_shifted], -1, (0, 0, 255), 2)

                # Action TRiggering
                if danger_detected:
                    color = (0, 0, 255) # Red
                    cv2.putText(frame, "OBJECT DETECTED!", (50, 50), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 255), 3)
                    
                    if time.time() - last_command_time > 0.1:
                        sock.sendto(b'STOP', (MUSCLE_IP, MUSCLE_PORT))
                        last_command_time = time.time()

                cv2.rectangle(frame, (dz_x, dz_y), (dz_x + dz_w, dz_y + dz_h), color, 2)
                cv2.imshow("Smart Safety System", frame)
            
            frame_buffer = {}

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    except Exception as e:
        # print(f"Error: {e}") 
        continue

sock.close()
cv2.destroyAllWindows()
