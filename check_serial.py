import serial
import time

# Replace COMx with the port shown by your operating system.
ser = serial.Serial('COMx', 115200, timeout=1)
time.sleep(0.5)

print("=== Reading ESP32 Serial Output ===")
start = time.time()
while time.time() - start < 10:
    if ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            print(line)

ser.close()
print("\n=== Serial read complete ===")
