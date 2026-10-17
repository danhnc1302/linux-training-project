# Sensor Gateway – Hệ thống giám sát cảm biến smart home

Sensor gateway viết bằng C trên Linux. Gateway nhận dữ liệu từ nhiều sensor node qua TCP, giải mã bốn giao thức khác nhau, xử lý và cảnh báo theo từng loại sensor, lưu phép đo vào SQLite và ghi nhật ký tập trung qua một log process riêng.

---

## 1. Mục tiêu

- **Thu nhận:** nhận phép đo từ nhiều sensor node qua TCP. Số lượng node không cố định và có thể thay đổi khi đang chạy.
- **Xử lý và cảnh báo:** tính giá trị trung bình của 5 phép đo gần nhất (nhiệt độ, độ ẩm), so ngưỡng tức thời (khói/gas), ghi nhận sự kiện (chuyển động, cửa), phát hiện sensor ID không hợp lệ.
- **Lưu trữ:** ghi mọi phép đo vào SQLite. Khi mất kết nối cơ sở dữ liệu thì thử lại tối đa 3 lần, sau đó kết thúc có kiểm soát.
- **Ghi nhật ký:** gom log-event của toàn hệ thống qua FIFO về một log process, ghi vào `gateway.log`.

---

## 2. Kiến trúc

```text
 sensor_node (TBP)  ──TCP P────┐
 sensor_node (HLP)  ──TCP P+1──┤
 sensor_node (EVP)  ──TCP P+2──┼──► Connection manager ──► sbuffer ──┬──► Data manager
 sensor_node (SGP)  ──TCP P+3──┘    (protocol adapter)              └──► Storage manager ──► SQLite
                                           │                                │
                                           └──────── logFifo ◄──────────────┘
                                                        │
                                                   Log process ──► gateway.log
```

| Thành phần | Dạng thực thi | Vai trò |
|---|---|---|
| Main process | Process chính | Tạo FIFO, fork log process, tạo sbuffer và 3 thread, điều phối kết thúc |
| Connection manager | Thread | Lắng nghe 4 cổng, mỗi kết nối một thread phụ, giải mã bằng protocol adapter, ghi vào sbuffer |
| Data manager | Thread | Nạp file room–sensor, kiểm tra sensor ID và loại, xử lý theo loại sensor, ghi log cảnh báo |
| Storage manager | Thread | Ghi phép đo vào bảng `SensorData` của SQLite, xử lý mất kết nối |
| sbuffer | Cấu trúc dùng chung | Hàng đợi duy nhất giữa 3 thread, bảo vệ bằng mutex + condition variable |
| Log process | Process con (`fork`) | Đọc FIFO, ghi `gateway.log` theo định dạng cố định |

---

## 3. Build và chạy

### 3.1 Build gateway

```bash
make            # build ./server
make DEBUG=1    # build có in từng phép đo ra màn hình (chế độ debug, -DDEBUG)
make clean      # xóa file .o, server, gateway.log, logFifo
```

Khi đổi giữa chế độ thường và `DEBUG=1`, chạy `make clean` trước.

### 3.2 Chạy gateway

```bash
./server <cổng gốc P> <cổng HTTP>
./server 1234 8080
```

Gateway lắng nghe bốn cổng TCP liên tiếp:

| Cổng | Giao thức | Loại sensor |
|---|---|---|
| P (1234) | TBP | Temperature |
| P+1 (1235) | HLP | Humidity |
| P+2 (1236) | EVP | Motion, Door |
| P+3 (1237) | SGP | Smoke/Gas |

Gateway cần file `room_sensor.map` ở thư mục chạy chương trình. Nhấn **Ctrl+C** để dừng.

### 3.3 Build và chạy sensor mô phỏng

```bash
cd sensor_node && make && cd ..

./sensor_node/sensor_node <loại> <id> <host> <cổng gốc P> <chu kỳ ms> [tùy chọn]
```

- `loại`: `temperature` | `humidity` | `motion` | `door` | `smoke_gas`
- Sensor tự cộng offset theo loại, nên luôn truyền **cổng gốc P**.
- Với `motion` và `door`, chu kỳ là khoảng thời gian **trung bình** giữa hai sự kiện (ngẫu nhiên).

| Tùy chọn | Ý nghĩa |
|---|---|
| `--fragment` | Chia mỗi frame thành nhiều lần `send()`, có khoảng nghỉ |
| `--glue` | Gộp nhiều frame vào một lần `send()` |
| `--corrupt` | Thỉnh thoảng gửi frame hỏng (sai CRC, sai magic, sai giá trị, dòng quá dài) |

Ví dụ chạy đủ năm loại sensor:

```bash
S=./sensor_node/sensor_node
$S temperature 10 127.0.0.1 1234 1000
$S humidity    21 127.0.0.1 1234 1000 --glue
$S motion      30 127.0.0.1 1234 15000
$S door        40 127.0.0.1 1234 20000 --corrupt
$S smoke_gas   50 127.0.0.1 1234 2000  --corrupt --fragment
$S temperature 99 127.0.0.1 1234 1000          # ID không có trong room_sensor.map
```

---

## 4. Kiểm thử

### 4.1 Unit test

```bash
make test
```

| Test | Nội dung |
|---|---|
| `test/test_parsers` | `parse()` của 4 giao thức với các ví dụ ở Mục 3.4, vector CRC `0x29B1` và `0xACF5`, các trường hợp thiếu byte, dính frame, frame hỏng, ACK/NAK |
| `test/test_datamgr` | Nạp file room–sensor, trung bình 5 phép đo, mỗi loại sensor có ca bình thường và ca cảnh báo, sensor ID lạ, loại không khớp |
| `test/test_storagemgr` | Tạo bảng, INSERT, đổi tên file DB, khóa file DB, thử lại 3 lần, dữ liệu được giữ trong sbuffer khi mất DB |

### 4.2 Kiểm tra bộ nhớ và luồng

```bash
make valgrind    # memcheck, chạy vài sensor rồi Ctrl+C
grep -E "ERROR SUMMARY|definitely|no leaks" valgrind_*.txt

make helgrind    # phát hiện data race / deadlock, chạy vài sensor rồi Ctrl+C
grep "ERROR SUMMARY" helgrind.txt
```

Kết quả đạt được: memcheck `0 errors`, helgrind `0 errors from 0 contexts` cho cả main process và log process.

### 4.3 Kiểm tra dữ liệu trong SQLite

```bash
sqlite3 gateway.db "SELECT type, count(*) FROM SensorData GROUP BY type;"
sqlite3 -header -column gateway.db \
  "SELECT id, type, value, datetime(ts,'unixepoch','localtime') AS time
   FROM SensorData ORDER BY rowid DESC LIMIT 10;"
```

### 4.4 Kiểm tra mất kết nối cơ sở dữ liệu

Khi gateway đang chạy, mở terminal khác:

```bash
mv gateway.db moved.db; sleep 3; mv moved.db gateway.db   # kết nối lại, không mất dữ liệu
mv gateway.db moved.db                                    # thử 3 lần rồi gateway tự kết thúc
```

---

## 5. Cấu trúc thư mục

```text
sensor_gateway/
├── include/
│   ├── adapter/            protocol_adapter.h, tbp/hlp/evp/sgp_adapter.h
│   ├── common.h            gateway_t, cờ kết thúc, console_printf
│   ├── sensor_types.h      sensor_data_t, sensor_type_t
│   ├── sensor_config.h     ngưỡng cảnh báo
│   ├── sbuffer.h
│   ├── connmgr.h  datamgr.h  storagemgr.h
│   └── logger.h  log_process.h
├── src/
│   ├── adapter/            parse() và build_reply() của 4 giao thức
│   ├── main.c              khởi động và kết thúc có kiểm soát
│   ├── gateway.c           cờ kết thúc (mutex), console_printf
│   ├── connmgr.c           4 listener + thread cho mỗi kết nối
│   ├── sbuffer.c           hàng đợi dùng chung
│   ├── datamgr.c           logic xử lý theo loại sensor
│   ├── datamgr_thread.c
│   ├── storagemgr.c        truy cập SQLite
│   ├── storagemgr_thread.c
│   ├── logger.c            ghi log-event vào FIFO
│   └── log_process.c       process ghi gateway.log
├── sensor_node/            chương trình mô phỏng sensor (Makefile riêng)
├── test/                   unit test
├── room_sensor.map         danh mục sensor hợp lệ
├── Makefile
└── README.md
```

Các file sinh ra khi chạy: `gateway.log`, `gateway.db`, `logFifo` (bị xóa khi gateway kết thúc).

---

## 6. File cấu hình và nhật ký

### room_sensor.map

Mỗi dòng khai báo một sensor hợp lệ; dòng bắt đầu bằng `#` là chú thích.

```text
# room  sensor_id  type
501     10         temperature
501     21         humidity
602     30         motion
703     40         door
804     50         smoke_gas
```

### Ngưỡng cảnh báo (`include/sensor_config.h`)

| Loại | Xử lý | Ngưỡng |
|---|---|---|
| Temperature | Trung bình 5 phép đo | Lạnh < 15 °C, nóng > 20 °C |
| Humidity | Trung bình 5 phép đo | Khô < 30 %, ẩm > 70 % |
| Smoke/Gas | So ngưỡng tức thời | Báo động khi ≥ 300 ppm |
| Motion | Ghi nhận mỗi sự kiện | – |
| Door | Ghi nhận mỗi lần đổi trạng thái | – |

### gateway.log

Định dạng: `<seq> <YYYY-MM-DD HH:MM:SS> <nguồn> <thông điệp>`

```text
1 2026-10-17 16:46:29 MAIN Gateway started (TCP base port 1234, HTTP port 8080)
5 2026-10-17 16:46:29 STOR Connection to SQL server established.
6 2026-10-17 16:46:38 CONN A sensor node with 10 has opened a new connection
9 2026-10-17 16:46:54 DATA The sensor node with 40 reports the door is open
```

---
