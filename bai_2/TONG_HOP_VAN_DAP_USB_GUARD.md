# Tổng Hợp Vấn Đáp Dự Án USB Guard

## 1. Mục tiêu đề tài

Đề tài xây dựng một hệ thống giám sát và quản lý USB trên Ubuntu 64-bit theo đúng ràng buộc:

- không viết lại driver `usb-storage` của Linux;
- không thay thế stack USB sẵn có của hệ điều hành;
- chỉ mở rộng chức năng quản lý, kiểm soát và giám sát đối với USB mass storage.

Tên gọi ngắn gọn của sản phẩm là `usb_guard`.

## 2. Thành phần của dự án

Dự án gồm 3 thành phần chính:

### 2.1. Kernel module `usb_guard.ko`

File mã nguồn: `usb_guard.c`

Vai trò:

- theo dõi sự kiện cắm và rút USB mass storage;
- đọc thông tin nhận dạng của USB;
- đối chiếu với whitelist;
- tự động chặn USB lạ nếu bật chế độ chặn;
- cung cấp trạng thái ra `procfs` để user space đọc và điều khiển.

### 2.2. Ứng dụng console/TUI `usb_guard_monitor`

File mã nguồn: `usb_guard_monitor.cpp`

Vai trò:

- đọc trạng thái từ `/proc/usb_guard/status`;
- hiển thị ở dạng text, JSON hoặc TUI;
- hỗ trợ theo dõi vận hành nhanh trong terminal.

### 2.3. Ứng dụng GUI `usb_guard_gui`

File mã nguồn: `usb_guard_gui.cpp`

Vai trò:

- cung cấp giao diện tiếng Việt;
- hiển thị trạng thái USB đang cắm, lịch sử, nhật ký;
- thêm/xóa whitelist trực tiếp;
- quản lý quyền truy cập `chỉ đọc` hoặc `đọc/ghi` theo từng USB;
- lưu cấu hình bền vững ra file để dùng lại ở lần mở sau.

## 3. Kiến trúc tổng thể

Luồng tổng quát của hệ thống:

1. USB được cắm vào máy.
2. USB core của Linux nhận diện thiết bị.
3. `usb_guard.ko` nhận sự kiện `USB_DEVICE_ADD`.
4. Module đọc `VID`, `PID`, `serial`, `manufacturer`, `product`, `bus`, `devpath`.
5. Module kiểm tra xem đây có phải là USB mass storage hay không.
6. Module so khớp với whitelist.
7. Nếu thiết bị hợp lệ:
   - giữ thiết bị hoạt động bình thường;
   - cập nhật danh sách `active`;
   - ghi lịch sử và log.
8. Nếu thiết bị không hợp lệ và chế độ chặn đang bật:
   - module tự ghi `authorized=0` cho thiết bị đó;
   - USB bị deauthorize, coi như bị hệ điều hành ngắt quyền sử dụng.
9. User space đọc trạng thái từ `/proc/usb_guard/status`.
10. GUI tiếp tục làm giàu thông tin bằng `sysfs`, `mount table`, block stats.

Điểm quan trọng:

- hệ thống **không thay thế** `usb-storage`;
- driver chuẩn của Linux vẫn chịu trách nhiệm mount, block device, I/O bình thường;
- `usb_guard` chỉ thêm lớp giám sát và chính sách.

## 4. Các tính năng hiện có

### 4.1. Ở mức kernel module

- Phát hiện cắm/rút USB mass storage.
- Lọc đúng class `USB_CLASS_MASS_STORAGE`.
- Đọc thông tin nhận dạng:
  - `VID`
  - `PID`
  - `serial`
  - `manufacturer`
  - `product`
  - `bus/dev/devpath`
- Ghi log vào `dmesg`.
- Quản lý whitelist theo:
  - `VID:PID`
  - `VID:PID:SERIAL`
- Hỗ trợ 2 chế độ:
  - `observe`
  - `whitelist`
- Tự động chặn USB ngoài whitelist bằng `authorized=0` nếu bật `block_untrusted=1`.
- Duy trì danh sách thiết bị đang hoạt động `active`.
- Duy trì lịch sử thiết bị `history`.
- Thống kê:
  - số lần cắm `connect_count`
  - thời gian đang kết nối `connected_for_ms`
  - tổng thời gian kết nối `total_connected_ms`
- Xuất trạng thái ra `/proc/usb_guard/status`.
- Nhận lệnh runtime qua `/proc/usb_guard/control`.

### 4.2. Ở mức GUI

- Giao diện tiếng Việt.
- Hiển thị trạng thái module, số USB đang cắm, số USB tin cậy, số USB ngoài whitelist.
- Hiển thị danh sách USB đang kết nối.
- Hiển thị lịch sử USB đã ghi nhận.
- Hiển thị nhật ký sự kiện/chính sách.
- Hiển thị lưu lượng đọc/ghi và tốc độ đọc/ghi của từng USB ở mức block layer.
- Thêm whitelist trực tiếp từ GUI.
- Xóa whitelist bằng cách chọn rule từ danh sách hiện có, không bắt nhập tay.
- Quản lý quyền truy cập riêng cho từng USB:
  - đặt `chỉ đọc`
  - cho phép `đọc/ghi`
- Lưu cấu hình bền vững vào file cấu hình.
- Tự đồng bộ whitelist và policy truy cập khi mở lại GUI.

## 5. Cơ chế whitelist

Whitelist là danh sách USB được phép hoạt động.

Hai kiểu rule đang hỗ trợ:

- `VID:PID`
- `VID:PID:SERIAL`

Ý nghĩa:

- `VID:PID` dùng khi chỉ cần nhận diện theo loại thiết bị.
- `VID:PID:SERIAL` dùng khi muốn phân biệt đúng một thiết bị cụ thể.

Ví dụ:

- `346d:5678`
- `346d:5678:8124311165564264629`

So khớp:

- nếu có serial trong rule thì phải khớp cả `VID`, `PID`, `serial`;
- nếu rule chỉ có `VID:PID` thì chỉ cần khớp hai trường này.

## 6. Cơ chế chặn USB lạ ở mức driver

Đây là phần quan trọng để trả lời vấn đáp.

### 6.1. Hệ thống có “viết lại driver USB” không?

Không.

Hệ thống chỉ đăng ký module giám sát và điều khiển chính sách. Driver chuẩn `usb-storage` của Linux vẫn tồn tại nguyên vẹn.

### 6.2. Hệ thống chặn USB lạ như thế nào?

Khi USB được cắm vào:

1. `usb_guard` nhận sự kiện thêm thiết bị.
2. Module đọc thông tin nhận dạng.
3. Module so khớp whitelist.
4. Nếu USB không nằm trong whitelist và `block_untrusted=1`:
   - module đưa yêu cầu chặn vào `workqueue`;
   - worker ghi `0` vào `authorized` của thiết bị;
   - USB bị deauthorize.

### 6.3. Vì sao dùng `authorized=0`?

Vì đây là cơ chế chuẩn của Linux để vô hiệu hóa một USB sau khi đã nhận diện được thiết bị.

Ưu điểm:

- không phải sửa `usb-storage`;
- không phải vá sâu vào USB core;
- phù hợp với module ngoài cây kernel;
- đạt được hiệu ứng “thiết bị lạ bị ngắt ngay sau khi xác định”.

### 6.4. Có phải chặn tuyệt đối từ trước khi nhận diện không?

Không hoàn toàn.

Thiết bị phải được hệ thống enumerate một khoảng rất ngắn để lấy thông tin nhận dạng. Sau đó `usb_guard` mới quyết định cho phép hay chặn.

Vì vậy, cách đúng để mô tả là:

- “phát hiện thiết bị, đọc thông tin, nếu không nằm trong whitelist thì tự động deauthorize”.

Không nên mô tả là:

- “USB chưa kịp được hệ thống nhìn thấy”.

## 7. Cơ chế `/proc/usb_guard/status`

Đây là kênh để kernel module cung cấp dữ liệu cho user space.

Thông tin chính gồm:

- trạng thái module;
- `policy_mode`;
- whitelist hiện tại;
- sự kiện cuối cùng;
- số lượng USB đang kết nối;
- thông tin chi tiết của thiết bị cuối;
- danh sách `active[]`;
- danh sách `history[]`.

User space không hỏi trực tiếp USB core mà đọc trạng thái đã được tổng hợp sẵn từ driver.

## 8. Cơ chế `/proc/usb_guard/control`

Đây là kênh điều khiển runtime.

Các lệnh hỗ trợ:

- `add VVVV:PPPP[:SERIAL]`
- `remove VVVV:PPPP[:SERIAL]`
- `clear`
- `block on`
- `block off`

GUI dùng file này để:

- thêm whitelist;
- xóa whitelist;

Hai lệnh `block on` và `block off` hiện có thể dùng từ terminal để quản trị runtime khi cần.

## 9. Cơ chế quản lý chỉ đọc / đọc-ghi theo từng USB

Đây là phần rất dễ bị hỏi nhầm giữa driver và user mode.

### 9.1. Chức năng này nằm ở đâu?

Chức năng `chỉ đọc` hoặc `đọc/ghi` theo từng USB hiện nằm ở **user space**, cụ thể là trong GUI, không nằm trong kernel module.

### 9.2. Vì sao không đưa xuống driver?

Vì `usb_guard` là module giám sát/chính sách, không thay thế block driver hay `usb-storage`.

Nếu ép quyền truy cập sâu trong kernel ở tầng block cho từng USB sẽ phức tạp hơn nhiều, dễ can thiệp quá sâu vào stack lưu trữ.

Giải pháp hiện tại là thực tế hơn:

- kernel module nhận diện và quản lý whitelist/chặn USB lạ;
- GUI xử lý chính sách truy cập trên đúng block device tương ứng với USB đã chọn.

### 9.3. GUI áp `chỉ đọc` hoặc `đọc/ghi` như thế nào?

Khi người dùng chọn một USB trong danh sách:

1. GUI dò block device tương ứng qua `sysfs`.
2. GUI xác định các phân vùng và mountpoint của đúng USB đó.
3. Nếu chọn `chỉ đọc`:
   - remount mountpoint sang `ro`;
   - dùng `ioctl(BLKROSET)` để đặt block device và các partition sang read-only.
4. Nếu chọn `đọc/ghi`:
   - bỏ cờ read-only trên block device và partition;
   - remount mountpoint về `rw`.

Đây là thao tác **thật**, không phải mô phỏng.

### 9.4. Chính sách này có áp cho tất cả USB không?

Không.

Nó chỉ áp cho **USB đang được chọn** trong GUI. Đây là quản lý theo từng thiết bị.

## 10. Cơ chế lưu bền vững cấu hình

File cấu hình mặc định:

`/etc/usb_guard.conf`

Nội dung được lưu:

- whitelist runtime;
- policy truy cập theo từng USB:
  - `readonly`
  - `readwrite`

Khóa nhận dạng của policy:

- `VID:PID`
- hoặc `VID:PID:SERIAL`

Khi mở lại GUI:

1. GUI đọc file cấu hình.
2. Nếu chạy bằng `root`, GUI đồng bộ lại whitelist vào kernel module.
3. Nếu USB đang cắm khớp với policy đã lưu, GUI tự áp lại chế độ truy cập tương ứng.

Lưu ý quan trọng:

- dữ liệu bền vững nằm ở file cấu hình;
- còn dữ liệu runtime trong kernel module sẽ mất khi `rmmod` hoặc reboot.

## 11. Cơ chế thống kê lưu lượng và tốc độ I/O

GUI hiển thị:

- tổng byte đã đọc;
- tổng byte đã ghi;
- tốc độ đọc hiện tại;
- tốc độ ghi hiện tại.

Nguồn dữ liệu:

- thống kê ở mức block layer qua `sysfs` hoặc `diskstats`.

Ý nghĩa:

- phản ánh hoạt động I/O logic của USB trên block device;
- phù hợp cho mục tiêu quản lý vận hành;
- không phải là lưu lượng điện tín hiệu thuần túy trên đường bus USB.

Nếu bị hỏi:

- “đây có phải tốc độ vật lý trên dây USB không?”

Thì trả lời:

- “không hoàn toàn, đây là thống kê I/O ở mức block layer, phù hợp để giám sát đọc ghi thực tế của ổ USB trong hệ điều hành”.

## 12. Luồng hoạt động hoàn chỉnh của dự án

### 12.1. Trường hợp USB nằm trong whitelist

1. Cắm USB.
2. `usb_guard` nhận sự kiện.
3. Đọc thông tin thiết bị.
4. So khớp whitelist.
5. Xác định là hợp lệ.
6. Cập nhật `active/history`.
7. Ghi log.
8. Cho phép hệ điều hành tiếp tục sử dụng USB bình thường.
9. GUI đọc trạng thái và hiển thị thiết bị.
10. Người dùng có thể đặt riêng USB đó sang `chỉ đọc` hoặc `đọc/ghi`.

### 12.2. Trường hợp USB không nằm trong whitelist

1. Cắm USB.
2. `usb_guard` nhận sự kiện.
3. Đọc thông tin nhận dạng.
4. So khớp whitelist và xác định là không hợp lệ.
5. Nếu `block_untrusted=1`:
   - module deauthorize USB bằng `authorized=0`;
   - USB bị ngắt quyền sử dụng.
6. Trạng thái, log và lịch sử vẫn được ghi nhận để người quản trị biết thiết bị nào đã bị chặn.

### 12.3. Trường hợp tắt GUI rồi mở lại

1. GUI đóng, nhưng module vẫn chạy nếu chưa `rmmod`.
2. Mở lại GUI.
3. GUI đọc `/etc/usb_guard.conf`.
4. GUI đọc `/proc/usb_guard/status`.
5. GUI đồng bộ whitelist và policy đã lưu.
6. Nếu có USB phù hợp đang cắm, GUI tự áp lại chính sách truy cập.

## 13. Điểm mạnh của dự án

- Bám đúng yêu cầu đề tài: không viết lại driver có sẵn.
- Có tách lớp rõ ràng:
  - kernel làm giám sát và chính sách nền;
  - user space làm hiển thị và thao tác nâng cao.
- Có cả CLI, TUI, GUI.
- Có runtime control qua `/proc`.
- Có khả năng chặn USB lạ tự động.
- Có quản lý truy cập riêng cho từng USB.
- Có lưu bền vững cấu hình phục vụ vận hành thực tế.

## 14. Giới hạn hiện tại của dự án

- Cơ chế `chỉ đọc/đọc-ghi` nằm ở user space, chưa ở mức kernel module.
- Tốc độ I/O đang là thống kê block layer, không phải lưu lượng tín hiệu bus USB thuần túy.
- Dữ liệu `active/history` trong kernel không tồn tại qua reboot hoặc khi gỡ module.
- Việc tự áp lại policy lưu bền hiện diễn ra khi mở GUI; nếu muốn tự áp từ lúc boot thì cần thêm daemon hoặc `systemd service`.

## 15. Các câu hỏi vấn đáp hay gặp và cách trả lời

### Câu 1. Đề tài này có viết lại driver USB của Linux không?

Trả lời:

Không. Đề tài chỉ xây dựng một kernel module mở rộng để giám sát và áp chính sách cho USB mass storage. Driver chuẩn `usb-storage` của Linux vẫn được giữ nguyên.

### Câu 2. Vì sao chọn hướng này thay vì viết lại driver?

Trả lời:

Vì yêu cầu đề tài là không viết lại driver sẵn có của hệ điều hành. Ngoài ra, giữ nguyên `usb-storage` giúp hệ thống ổn định hơn và tập trung vào chức năng quản lý, giám sát, whitelist và bảo mật.

### Câu 3. Dự án phân biệt USB hợp lệ và USB lạ bằng gì?

Trả lời:

Bằng whitelist theo `VID:PID` hoặc `VID:PID:SERIAL`.

### Câu 4. Nếu USB không nằm trong whitelist thì hệ thống làm gì?

Trả lời:

Nếu bật `block_untrusted`, kernel module sẽ tự động deauthorize thiết bị bằng `authorized=0`, làm thiết bị bị ngắt quyền sử dụng ngay sau khi nhận diện.

### Câu 5. Vì sao dùng `authorized=0`?

Trả lời:

Đây là cơ chế phù hợp và an toàn của Linux để vô hiệu hóa một USB sau khi hệ thống đã đọc được thông tin nhận dạng. Nó giúp chặn thiết bị lạ mà không cần viết lại `usb-storage`.

### Câu 6. Tính năng `chỉ đọc/đọc-ghi` nằm ở đâu?

Trả lời:

Tính năng này nằm ở GUI/user space, không nằm trong driver. GUI dò block device của đúng USB đang chọn rồi áp `BLKROSET` và remount `ro/rw`.

### Câu 7. Vì sao không làm `chỉ đọc` trong driver?

Trả lời:

Vì mục tiêu của driver là giám sát và chính sách nền, không thay thế block driver. Đưa quyền truy cập chi tiết xuống driver sẽ can thiệp sâu hơn vào stack lưu trữ và làm hệ thống phức tạp hơn.

### Câu 8. Dữ liệu có còn sau khi tắt chương trình không?

Trả lời:

Dữ liệu runtime trong kernel còn nếu module vẫn đang nạp. Riêng whitelist và policy truy cập theo USB được GUI lưu vào `/etc/usb_guard.conf`, nên khi mở lại GUI có thể nạp lại và áp lại.

### Câu 9. Dự án theo dõi tốc độ USB bằng cách nào?

Trả lời:

GUI đọc thống kê block I/O từ hệ thống để tính tốc độ đọc và ghi của block device tương ứng với USB. Đây là tốc độ ở mức block layer.

### Câu 10. Điểm mới của đề tài là gì?

Trả lời:

Điểm mới là kết hợp kernel module giám sát USB, cơ chế whitelist và chặn USB lạ, giao diện quản trị GUI tiếng Việt, thống kê I/O và quản lý quyền truy cập riêng cho từng USB mà không phải viết lại driver chuẩn của Linux.

## 16. Cách trình bày ngắn gọn khi bị hỏi bất ngờ

Bạn có thể trả lời theo mẫu 4 ý:

1. `usb_guard` là kernel module mở rộng cho USB mass storage trên Ubuntu 64-bit.
2. Module dùng để giám sát, whitelist và chặn USB lạ bằng `authorized=0`, không thay thế `usb-storage`.
3. GUI cho phép quản lý whitelist và quyền `chỉ đọc/đọc-ghi` theo từng USB, đồng thời hiển thị lịch sử và thống kê I/O.
4. Cấu hình được lưu bền vững để dùng lại ở lần mở sau.

## 17. Kết luận ngắn gọn

Sản phẩm hiện tại là một hệ thống quản lý USB hoàn chỉnh ở mức đề tài học phần:

- có kernel module thực thi chính sách nền;
- có công cụ theo dõi ở terminal;
- có GUI quản trị bằng tiếng Việt;
- có chặn USB lạ theo whitelist;
- có quản lý truy cập riêng cho từng USB;
- có lưu cấu hình để tái sử dụng.

Điểm quan trọng nhất khi trả lời vấn đáp là phải nhấn mạnh:

- **không viết lại driver chuẩn của Linux**;
- **driver làm nhiệm vụ giám sát và chặn USB lạ**;
- **quản lý `chỉ đọc/đọc-ghi` theo từng USB được thực hiện ở user space trên block device tương ứng**.
