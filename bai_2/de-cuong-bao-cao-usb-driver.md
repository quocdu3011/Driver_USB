# Đề cương báo cáo

## Tên đề tài

**Phát triển module nhân Linux trên Ubuntu 64-bit để bổ sung chức năng quản lý, giám sát ổ USB, minh họa với USB Flash, không thay thế driver mặc định của hệ điều hành**

---

## 1. Mở đầu

### 1.1. Lý do chọn đề tài

Trong các hệ điều hành hiện đại, thiết bị USB là nhóm thiết bị ngoại vi được sử dụng rất phổ biến nhờ tính linh hoạt, khả năng cắm nóng và tính tương thích cao. Trên Ubuntu 64-bit, các thiết bị lưu trữ USB thông thường đã được hệ điều hành hỗ trợ sẵn thông qua các driver chuẩn như `usb-storage` và các lớp block device. Tuy nhiên, trong nhiều bài toán thực tế, người dùng hoặc quản trị viên hệ thống vẫn cần bổ sung thêm các chức năng mới như giám sát thiết bị cắm vào, ghi nhật ký hoạt động, nhận diện thiết bị hợp lệ, áp dụng chính sách kiểm soát truy cập hoặc xây dựng giao diện trao đổi thông tin riêng giữa nhân và không gian người dùng.

Việc nghiên cứu và phát triển một module nhân Linux để mở rộng chức năng quản lý đối với ổ USB, thay vì viết lại driver có sẵn của hệ điều hành, là một hướng tiếp cận phù hợp cả về kỹ thuật lẫn thực tiễn. Đề tài giúp người học hiểu rõ cơ chế hoạt động của USB trong Linux, quy trình phát triển driver, đồng thời bảo đảm tuân thủ nguyên tắc tận dụng hạ tầng sẵn có của kernel.

### 1.2. Mục tiêu nghiên cứu

- Tìm hiểu kiến trúc driver trong Linux, đặc biệt là driver cho thiết bị USB.
- Nghiên cứu cơ chế phát hiện, nhận dạng và quản lý thiết bị USB trong Ubuntu 64-bit.
- Thiết kế và xây dựng một module nhân Linux bổ sung chức năng mới cho ổ USB.
- Minh họa việc triển khai và thử nghiệm trên một USB Flash thực tế.
- Đánh giá khả năng mở rộng và ứng dụng thực tế của giải pháp.

### 1.3. Đối tượng và phạm vi nghiên cứu

#### Đối tượng nghiên cứu

- Hệ điều hành Ubuntu 64-bit.
- Linux kernel module.
- Hệ thống USB core của Linux.
- Ổ nhớ USB Flash dùng để minh họa thử nghiệm.

#### Phạm vi nghiên cứu

- Chỉ phát triển module hoặc driver mở rộng để bổ sung tính năng mới.
- Không thay thế, không chỉnh sửa, không viết lại driver mặc định của hệ điều hành như `usb-storage`.
- Tập trung vào các chức năng giám sát, nhận diện, quản lý hoặc kiểm soát truy cập logic đối với thiết bị USB.
- Không đi sâu vào việc xây dựng lại toàn bộ stack truyền dữ liệu mức thấp của USB mass storage.

### 1.4. Phương pháp nghiên cứu

- Nghiên cứu lý thuyết từ giáo trình, slide môn học và tài liệu kỹ thuật Linux kernel.
- Khảo sát thông tin thiết bị USB bằng các công cụ hệ thống như `lsusb`, `dmesg`, `modinfo`.
- Phân tích kiến trúc driver USB của Linux.
- Thiết kế, cài đặt, biên dịch và nạp thử module nhân.
- Thử nghiệm thực tế với USB Flash và đánh giá kết quả.

### 1.5. Ý nghĩa của đề tài

#### Ý nghĩa khoa học

Đề tài giúp làm rõ mối quan hệ giữa Linux kernel, USB core, bus driver và device driver; đồng thời minh họa quy trình phát triển một module nhân theo hướng mở rộng chức năng.

#### Ý nghĩa thực tiễn

Kết quả của đề tài có thể áp dụng cho các bài toán quản lý thiết bị ngoại vi, tăng cường giám sát an toàn thông tin hoặc xây dựng các cơ chế kiểm soát USB trong môi trường Linux.

---

## 2. Cơ sở lý thuyết

### 2.1. Tổng quan về hệ điều hành Linux

- Khái niệm nhân hệ điều hành.
- Sự khác nhau giữa `kernel space` và `user space`.
- `kernel mode` và `user mode`.
- Vai trò của system call trong giao tiếp giữa ứng dụng và kernel.

### 2.2. Tổng quan về Linux kernel module

- Khái niệm module nhân Linux.
- Ưu điểm của dạng module nạp động (`.ko`).
- Quy trình biên dịch, nạp và gỡ module:
  - `make`
  - `insmod`
  - `rmmod`
  - `lsmod`
  - `dmesg`

### 2.3. Khái niệm driver trong Linux

- Khái niệm driver và vai trò của driver trong hệ điều hành.
- Mối quan hệ giữa thiết bị, controller, bus và CPU.
- Phân loại driver:
  - bus driver
  - character driver
  - block driver
  - network driver

### 2.4. Tổng quan về giao thức USB

- Khái niệm USB và các thành phần cơ bản:
  - USB Host
  - USB Device
  - USB Hub
- Quá trình điểm danh thiết bị USB.
- Khái niệm `configuration`, `interface`, `endpoint`.
- Các kiểu truyền dữ liệu của USB:
  - control transfer
  - interrupt transfer
  - bulk transfer
  - isochronous transfer

### 2.5. Kiến trúc hỗ trợ USB trong Linux

- Vai trò của USB host controller.
- Vai trò của USB core trong kernel.
- Cơ chế phát hiện thiết bị cắm nóng.
- Cơ chế gắn driver cho thiết bị dựa trên `idVendor`, `idProduct`, class hoặc interface.

### 2.6. Đặc điểm của ổ USB Flash trong Linux

- Ổ USB Flash thường thuộc lớp thiết bị lưu trữ.
- Driver mặc định thường được hệ điều hành hỗ trợ sẵn.
- Mỗi interface thường chỉ gắn với một driver.
- Hệ quả: không nên thay thế driver chuẩn nếu yêu cầu đề tài chỉ là bổ sung chức năng mới.

---

## 3. Phân tích bài toán và định hướng giải pháp

### 3.1. Mô tả bài toán

Đề tài yêu cầu phát triển trên Ubuntu 64-bit một driver hoặc module liên quan đến USB, minh họa bằng ổ USB Flash. Tuy nhiên, yêu cầu quan trọng là không viết lại driver mặc định của hệ điều hành. Do đó, bài toán không phải là xây dựng lại cơ chế đọc ghi dữ liệu cho USB mass storage, mà là bổ sung một tính năng mới hoạt động song song hoặc kế thừa hạ tầng có sẵn của Linux.

### 3.2. Các ràng buộc của đề tài

- Không thay thế `usb-storage`.
- Không làm thay đổi cơ chế mount và truy xuất dữ liệu mặc định của hệ điều hành.
- Phải hoạt động trên Ubuntu 64-bit.
- Phải có minh họa với thiết bị USB thực tế.

### 3.3. Các hướng chức năng có thể bổ sung

- Ghi log khi thiết bị USB được cắm vào hoặc tháo ra.
- Đọc và lưu thông tin định danh thiết bị như VID, PID, serial, manufacturer.
- Xây dựng danh sách USB hợp lệ và cảnh báo khi phát hiện thiết bị lạ.
- Cung cấp giao diện điều khiển qua character device, `procfs` hoặc `sysfs`.
- Hỗ trợ thống kê số lần cắm thiết bị hoặc thời gian sử dụng.

### 3.4. Hướng giải pháp lựa chọn

Trong báo cáo này, giải pháp được lựa chọn là xây dựng một module nhân Linux có chức năng:

- phát hiện sự kiện kết nối hoặc ngắt kết nối USB Flash;
- thu thập và lưu trữ thông tin cơ bản của thiết bị;
- xuất thông tin đó ra log kernel và một giao diện trao đổi với không gian người dùng;
- không can thiệp vào driver lưu trữ mặc định của hệ điều hành.

### 3.5. Tính đúng đắn của hướng tiếp cận

Hướng tiếp cận này phù hợp vì:

- bảo đảm đúng yêu cầu “chỉ phát triển tính năng mới”;
- vẫn thể hiện được quy trình làm việc với USB trong Linux;
- tránh xung đột với driver chuẩn của hệ điều hành;
- dễ thử nghiệm, đánh giá và mở rộng.

---

## 4. Thiết kế hệ thống

### 4.1. Kiến trúc tổng thể

Hệ thống gồm các thành phần chính:

- **USB Core của Linux**: phát hiện thiết bị USB và cung cấp hạ tầng chung.
- **Driver mặc định của hệ điều hành**: tiếp tục đảm nhiệm việc truy cập dữ liệu của USB Flash.
- **Module nhân do đề tài phát triển**: bổ sung chức năng giám sát và quản lý.
- **Ứng dụng người dùng hoặc lệnh shell**: đọc thông tin từ log, `procfs`, `sysfs` hoặc file thiết bị do module tạo ra.

### 4.2. Mô hình hoạt động

1. Người dùng cắm USB Flash vào máy.
2. Hệ thống Ubuntu phát hiện thiết bị thông qua USB host controller và USB core.
3. Driver mặc định vẫn nhận thiết bị để phục vụ mount và truy xuất dữ liệu.
4. Module do đề tài phát triển thu thập thông tin cần thiết về thiết bị.
5. Module ghi log và xuất trạng thái ra giao diện quản lý.
6. Người dùng đọc thông tin và quan sát kết quả thử nghiệm.

### 4.3. Thiết kế dữ liệu

Các thông tin cần quản lý có thể bao gồm:

- mã hãng sản xuất (`idVendor`);
- mã sản phẩm (`idProduct`);
- tên nhà sản xuất;
- tên sản phẩm;
- số serial;
- thời điểm cắm thiết bị;
- thời điểm rút thiết bị;
- trạng thái hợp lệ hoặc không hợp lệ theo chính sách quản lý.

### 4.4. Thiết kế chức năng

#### Chức năng 1: phát hiện sự kiện USB

- nhận biết khi USB được cắm hoặc rút;
- cập nhật trạng thái thiết bị trong module.

#### Chức năng 2: thu thập thông tin nhận dạng

- lấy dữ liệu mô tả thiết bị từ hạ tầng USB của Linux;
- lưu vào cấu trúc dữ liệu của module.

#### Chức năng 3: ghi log hệ thống

- xuất các thông tin cần thiết ra `dmesg`;
- hỗ trợ phục vụ kiểm thử và quan sát hoạt động.

#### Chức năng 4: cung cấp giao diện cho user space

- tạo file thiết bị ký tự hoặc điểm truy cập trong `procfs/sysfs`;
- cho phép ứng dụng người dùng đọc trạng thái thiết bị.

#### Chức năng 5: kiểm tra chính sách quản lý

- đối chiếu VID/PID hoặc serial với danh sách cho phép;
- đưa ra cảnh báo hoặc trạng thái tương ứng.

---

## 5. Cài đặt và xây dựng chương trình

### 5.1. Môi trường thực hiện

- Hệ điều hành: Ubuntu 64-bit.
- Trình biên dịch: GCC.
- Bộ công cụ build kernel module: `make`, Kbuild.
- Thiết bị thử nghiệm: USB Flash.

### 5.2. Cấu trúc chương trình

- File mã nguồn chính của module.
- Makefile hoặc Kbuild để biên dịch.
- Tệp mô tả thông tin module.
- Tệp hỗ trợ kiểm thử nếu có.

### 5.3. Các thành phần chính trong mã nguồn

- Khai báo giấy phép và thông tin module.
- Khai báo cấu trúc dữ liệu lưu thông tin thiết bị.
- Khai báo bảng nhận diện thiết bị hoặc cơ chế theo dõi USB.
- Hàm khởi tạo module.
- Hàm kết thúc module.
- Hàm xử lý khi phát hiện thiết bị.
- Hàm xử lý khi ngắt kết nối thiết bị.
- Nhóm hàm giao tiếp với user space.

### 5.4. Quy trình biên dịch và nạp module

1. Chuẩn bị mã nguồn và Makefile.
2. Biên dịch module bằng `make`.
3. Kiểm tra thông tin module nếu cần.
4. Nạp module vào kernel bằng `insmod`.
5. Theo dõi log bằng `dmesg`.
6. Gỡ module bằng `rmmod` khi kết thúc thử nghiệm.

### 5.5. Giao diện và công cụ hỗ trợ kiểm tra

- `lsusb`: xem thông tin thiết bị USB.
- `dmesg`: theo dõi log của kernel.
- `lsmod`: kiểm tra module đã được nạp.
- `cat` trên `procfs/sysfs` hoặc đọc file thiết bị để xem dữ liệu xuất ra từ module.

---

## 6. Thử nghiệm và đánh giá

### 6.1. Mục tiêu thử nghiệm

- Xác nhận module hoạt động ổn định trên Ubuntu 64-bit.
- Xác nhận USB Flash vẫn hoạt động bình thường với driver mặc định.
- Xác nhận module bổ sung được chức năng mới như thiết kế.

### 6.2. Kịch bản thử nghiệm

#### Kịch bản 1: cắm USB hợp lệ

- Cắm USB Flash vào hệ thống.
- Quan sát log của kernel.
- Kiểm tra thông tin nhận dạng được ghi nhận đầy đủ.

#### Kịch bản 2: rút USB khỏi hệ thống

- Tháo USB Flash ra khỏi cổng.
- Kiểm tra log ngắt kết nối.
- Kiểm tra trạng thái thiết bị được cập nhật.

#### Kịch bản 3: đọc thông tin từ giao diện user space

- Đọc dữ liệu từ `procfs`, `sysfs` hoặc file thiết bị.
- Đối chiếu với thông tin thực tế của USB.

#### Kịch bản 4: kiểm tra chính sách quản lý

- Thử với USB thuộc danh sách cho phép.
- Thử với USB không thuộc danh sách cho phép.
- Ghi nhận trạng thái cảnh báo hoặc chấp nhận.

### 6.3. Tiêu chí đánh giá

- Module nạp và gỡ thành công.
- Không gây treo hệ thống hoặc lỗi kernel.
- Không làm ảnh hưởng tới driver lưu trữ mặc định.
- Chức năng giám sát và xuất thông tin hoạt động đúng.
- Kết quả thử nghiệm phù hợp với thiết kế ban đầu.

### 6.4. Kết quả dự kiến

- Nhận diện được USB Flash khi cắm vào.
- Thu thập được các thông tin cơ bản của thiết bị.
- Xuất log rõ ràng, dễ theo dõi.
- Cung cấp được giao diện đọc dữ liệu cho user space.
- Duy trì được nguyên tắc không thay thế driver hệ điều hành.

---

## 7. Đánh giá ưu điểm, hạn chế và hướng phát triển

### 7.1. Ưu điểm

- Bám sát kiến trúc chuẩn của Linux.
- Không xung đột với driver mặc định.
- Dễ triển khai trên Ubuntu 64-bit.
- Phù hợp với bài toán giám sát, quản lý và nghiên cứu driver.
- Có thể mở rộng cho nhiều loại thiết bị USB khác.

### 7.2. Hạn chế

- Chưa can thiệp sâu vào luồng đọc ghi dữ liệu mức thấp của USB Flash.
- Chức năng quản lý phụ thuộc vào phạm vi hỗ trợ của USB core và kernel API.
- Một số tính năng kiểm soát nâng cao có thể cần tích hợp thêm ở user space.

### 7.3. Hướng phát triển

- Mở rộng danh sách chính sách kiểm soát USB.
- Kết hợp với cơ chế xác thực thiết bị tin cậy.
- Xây dựng ứng dụng người dùng để hiển thị trạng thái trực quan.
- Tích hợp cơ chế ghi log bảo mật hoặc cảnh báo thời gian thực.
- Mở rộng hỗ trợ cho nhiều lớp thiết bị USB khác ngoài USB Flash.

---

## 8. Kết luận

Đề tài tập trung nghiên cứu và phát triển một module nhân Linux trên Ubuntu 64-bit nhằm bổ sung chức năng quản lý và giám sát đối với ổ USB Flash mà không thay thế driver mặc định của hệ điều hành. Hướng tiếp cận này vừa phù hợp với nguyên tắc phát triển driver trong Linux, vừa có giá trị thực tiễn trong các bài toán theo dõi thiết bị ngoại vi và tăng cường kiểm soát hệ thống. Kết quả của đề tài không chỉ giúp củng cố kiến thức về lập trình driver mà còn tạo nền tảng để phát triển các giải pháp quản lý USB nâng cao trong tương lai.

---

## 9. Tài liệu tham khảo

1. Slide môn học Lập trình driver.
2. Tài liệu về Linux kernel module.
3. Tài liệu về USB subsystem trong Linux.
4. Tài liệu hướng dẫn sử dụng các công cụ `lsusb`, `dmesg`, `modinfo`, `insmod`, `rmmod`.
5. Tài liệu chính thức của Ubuntu và Linux kernel liên quan đến phát triển driver.

---

## 10. Phụ lục đề xuất

### Phụ lục A. Một số lệnh sử dụng trong quá trình thử nghiệm

```bash
lsusb
dmesg | tail
lsmod | grep <ten_module>
sudo insmod <ten_module>.ko
sudo rmmod <ten_module>
```

### Phụ lục B. Danh mục hình minh họa nên đưa vào báo cáo

- Sơ đồ kiến trúc Linux kernel và vị trí của driver.
- Sơ đồ thành phần USB Host, USB Device, USB Core.
- Sơ đồ luồng hoạt động của module khi cắm hoặc rút USB.
- Ảnh chụp kết quả `lsusb`.
- Ảnh chụp log `dmesg`.
- Ảnh chụp giao diện đọc dữ liệu từ module.

### Phụ lục C. Danh mục bảng biểu nên đưa vào báo cáo

- Bảng mô tả cấu trúc thông tin thiết bị USB.
- Bảng mô tả các chức năng của module.
- Bảng so sánh giữa driver mặc định và module mở rộng do đề tài xây dựng.
- Bảng kết quả thử nghiệm.
