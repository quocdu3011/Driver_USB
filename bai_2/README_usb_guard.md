# USB Guard

`usb_guard` là kernel module Linux dùng để giám sát USB mass storage trên Ubuntu 64-bit mà không thay thế `usb-storage`.

## Chức năng hiện tại

- Phát hiện sự kiện cắm và rút USB mass storage.
- Ghi log chi tiết bằng `dmesg`.
- Whitelist theo `VID:PID` hoặc `VID:PID:SERIAL`.
- Hỗ trợ `policy_mode=observe|whitelist`.
- Cảnh báo USB ngoài whitelist.
- Lưu trạng thái `active/history`.
- Xuất dữ liệu qua `/proc/usb_guard/status`.
- Có ứng dụng user space `usb_guard_monitor` viết bằng C++17 để theo dõi ở dạng text, JSON và TUI.
- Có ứng dụng GUI desktop `usb_guard_gui` viết bằng C++17 + GTK 3, giao diện tiếng Việt.
- GUI hiển thị thêm lưu lượng đọc/ghi và tốc độ đọc/ghi của USB ở mức block layer.
- GUI cho phép thêm và xóa rule whitelist runtime trực tiếp từ giao diện.
- GUI cho phép đặt từng USB đang chọn sang chế độ `chỉ đọc` hoặc `đọc/ghi` bằng thao tác thật trên block device và mountpoint.
- GUI lưu bền vững whitelist và policy truy cập theo USB vào file cấu hình để tự nạp lại ở lần mở sau.
- Kernel module có thể tự động chặn USB ngoài whitelist bằng cách ghi `authorized=0` vào `/sys/bus/usb/devices/<devpath>/authorized`.

## Biên dịch

Thực hiện trong đúng thư mục bài 2:

```bash
cd /media/Data/Ki_2_nam_4/Lap_trinh_driver/Bai_tap_lon/bai_2
make
```

Lệnh này tạo:

- `usb_guard.ko`
- `usb_guard_monitor`
- `usb_guard_gui`

## Nạp module

Chế độ chỉ giám sát:

```bash
sudo insmod ./usb_guard.ko
```

Whitelist theo `VID:PID`:

```bash
sudo insmod ./usb_guard.ko whitelist=346d:5678 block_untrusted=1
```

Whitelist theo `VID:PID:SERIAL`:

```bash
sudo insmod ./usb_guard.ko whitelist=346d:5678:8124311165564264629 block_untrusted=1
```

Nếu chỉ muốn bật whitelist nhưng chưa tự động chặn:

```bash
sudo insmod ./usb_guard.ko whitelist=346d:5678 block_untrusted=0
```

Kiểm tra:

```bash
cat /proc/usb_guard/status
dmesg | tail -n 50
```

Gỡ module:

```bash
sudo rmmod usb_guard
```

## Ứng dụng dòng lệnh

Chạy một lần:

```bash
./usb_guard_monitor
```

Theo dõi liên tục:

```bash
./usb_guard_monitor --watch 2
```

Xuất JSON:

```bash
./usb_guard_monitor --json
```

TUI realtime:

```bash
./usb_guard_monitor --tui --watch 1
```

Tự động chặn USB ngoài whitelist:

```bash
sudo ./usb_guard_monitor --watch 1 --deauthorize-untrusted
```

Hoặc:

```bash
make tui
```

Phím điều khiển TUI:

```text
q    thoát
j    xuống USB tiếp theo
k    lên USB trước đó
h    sang tab bên trái
l    sang tab bên phải
Tab  chuyển tab tiếp theo
x    đóng popup action
r    làm mới ngay
```

## Ứng dụng GUI

Chạy giao diện desktop:

```bash
./usb_guard_gui
```

Hoặc:

```bash
make gui-run
```

Chạy với file cấu hình riêng:

```bash
./usb_guard_gui --config-file /đường/dẫn/usb_guard.conf
```

Tính năng chính của GUI:

- Bảng điều khiển tiếng Việt có khối tổng quan trạng thái.
- Danh sách USB đang kết nối với thông tin:
  - thiết bị `devpath | VID:PID`
  - trạng thái tin cậy
  - tên sản phẩm
  - tốc độ I/O hiện tại
  - tổng lưu lượng đọc/ghi
  - mountpoint
- Tab lịch sử thiết bị đã ghi nhận.
- Tab nhật ký sự kiện và chính sách.
- Giao diện đã được tinh gọn theo hướng dễ đọc hơn: bảng tóm tắt ngắn, khung chi tiết đặt cạnh bên và màu sắc sáng đồng nhất với nội dung dữ liệu.
- Nút `Thêm whitelist` và `Xóa whitelist` để cập nhật whitelist runtime trong kernel module.
- Khi xóa whitelist, GUI hiển thị danh sách rule hiện có để chọn trực tiếp thay vì yêu cầu nhập tay.
- Nút `Đặt chỉ đọc cho USB chọn` và `Cho phép đọc/ghi cho USB chọn`.
- Nút làm mới thủ công.
- Tự động làm mới theo chu kỳ cấu hình được.

## Lưu cấu hình bền vững

Mặc định GUI dùng file cấu hình:

```bash
/etc/usb_guard.conf
```

File này lưu:

- các rule whitelist đã được thêm/xóa từ GUI;
- policy `chỉ đọc` hoặc `đọc/ghi` theo từng USB, định danh bằng `VID:PID[:SERIAL]`.

Khi mở lại GUI:

- nếu file cấu hình tồn tại, GUI sẽ tự nạp lại cấu hình đã lưu;
- nếu GUI chạy bằng `root`, whitelist đã lưu sẽ được đồng bộ lại vào kernel module;
- nếu USB đang cắm khớp với policy đã lưu, GUI sẽ tự áp lại chế độ `chỉ đọc` hoặc `đọc/ghi`.

## Điều khiển whitelist runtime

Module hiện tạo thêm file:

```bash
/proc/usb_guard/control
```

File này hỗ trợ các lệnh:

```text
add VVVV:PPPP[:SERIAL]
remove VVVV:PPPP[:SERIAL]
clear
block on|off
```

Ví dụ thao tác bằng tay:

```bash
echo "add 346d:5678" | sudo tee /proc/usb_guard/control
echo "remove 346d:5678" | sudo tee /proc/usb_guard/control
echo "clear" | sudo tee /proc/usb_guard/control
echo "block on" | sudo tee /proc/usb_guard/control
echo "block off" | sudo tee /proc/usb_guard/control
```

GUI sử dụng chính giao diện điều khiển này để thêm hoặc xóa whitelist.

## Quản lý quyền truy cập từng USB

GUI cho phép đổi quyền truy cập thật cho đúng USB đang được chọn trong danh sách:

- `Đặt chỉ đọc cho USB chọn`: remount các mountpoint sang `ro` rồi đặt block device và phân vùng sang `read-only`.
- `Cho phép đọc/ghi cho USB chọn`: bỏ cờ `read-only` trên block device và phân vùng rồi remount mountpoint về `rw`.

Lưu ý:

- Chức năng này áp dụng riêng cho USB đang chọn, không ảnh hưởng các USB khác.
- Nên chạy GUI bằng `root` nếu muốn đổi quyền truy cập thật:

```bash
sudo ./usb_guard_gui
```

## Chặn USB ngoài whitelist bằng `authorized=0`

Nếu module được nạp với `block_untrusted=1`, việc chặn sẽ được thực hiện ngay trong `usb_guard.ko` sau khi module đọc xong thông tin nhận dạng và xác định thiết bị nằm ngoài whitelist.

Kiểm tra trạng thái:

```bash
cat /proc/usb_guard/status
cat /proc/usb_guard/control
dmesg | tail -n 50
```

Áp thật:

```bash
sudo ./usb_guard_monitor --deauthorize-untrusted
```

Theo dõi liên tục và tự động chặn:

```bash
sudo ./usb_guard_monitor --watch 1 --deauthorize-untrusted
```

Lưu ý:

- Khi `block_untrusted=1`, chính kernel module sẽ ghi `authorized=0` để deauthorize USB ngoài whitelist ngay sau khi nhận diện.
- CLI vẫn có thể deauthorize thủ công, nhưng không còn là nơi duy nhất thực thi chính sách chặn.
- Cơ chế này giữ nguyên `usb-storage`; USB ngoài whitelist sẽ bị hệ thống ngắt quyền sử dụng sau khi bị deauthorize.
- Nếu muốn chặn USB thật từ CLI, nên chạy bằng quyền `root`.
- Nếu muốn thêm hoặc xóa whitelist từ GUI, cũng nên chạy GUI bằng quyền `root`.
- Chính sách này chỉ có ý nghĩa khi module đang ở `whitelist mode`.

## Ghi chú kỹ thuật

- Module chỉ mở rộng giám sát và quản lý, không thay thế driver mặc định của hệ điều hành.
- Nếu không truyền whitelist, hệ thống chạy ở `observe mode`.
- Tốc độ đọc/ghi trong GUI là thống kê ước lượng theo `block layer`, không phải tốc độ tuyệt đối trên dây USB.
