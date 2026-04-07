# BÁO CÁO ĐỀ TÀI

## Phát triển module nhân Linux trên Ubuntu 64-bit để bổ sung chức năng quản lý, giám sát ổ USB, minh họa với USB Flash, không thay thế driver mặc định của hệ điều hành

---

## MỤC LỤC

1. Mở đầu
2. Cơ sở lý thuyết
3. Phân tích đề tài và định hướng giải pháp
4. Thiết kế hệ thống
5. Cài đặt và triển khai
6. Thử nghiệm và đánh giá
7. Ưu điểm, hạn chế và hướng phát triển
8. Kết luận
9. Tài liệu tham khảo
10. Phụ lục

---

## 1. Mở đầu

### 1.1. Lý do chọn đề tài

Trong các hệ thống máy tính hiện nay, USB là chuẩn giao tiếp rất phổ biến nhờ ưu điểm dễ sử dụng, hỗ trợ cắm nóng, tốc độ truyền dữ liệu tương đối cao và khả năng tương thích rộng. Đặc biệt, USB Flash là thiết bị lưu trữ được sử dụng thường xuyên trong học tập, làm việc và trao đổi dữ liệu. Trên hệ điều hành Ubuntu 64-bit, phần lớn các thao tác với USB Flash như phát hiện thiết bị, gắn kết thiết bị lưu trữ và truy cập dữ liệu đã được hỗ trợ sẵn bởi hệ điều hành thông qua các driver mặc định.

Tuy nhiên, trong nhiều tình huống thực tế, các chức năng mặc định vẫn chưa đáp ứng đủ các nhu cầu quản lý hệ thống. Ví dụ, người quản trị có thể cần theo dõi lịch sử cắm hoặc rút USB, phân biệt thiết bị hợp lệ và không hợp lệ, ghi log nhận dạng thiết bị, hoặc xây dựng một giao diện để ứng dụng người dùng đọc trạng thái thiết bị ngoại vi theo yêu cầu riêng. Những nhu cầu này không nhất thiết đòi hỏi phải viết lại driver gốc của hệ điều hành, mà có thể được thực hiện bằng cách phát triển thêm một module nhân Linux để bổ sung chức năng.

Đề tài "Phát triển driver trên Ubuntu 64 bit – minh họa với ổ USB" vì vậy cần được tiếp cận theo hướng hợp lý là xây dựng một module nhân mở rộng chức năng quản lý, giám sát đối với thiết bị USB Flash, đồng thời giữ nguyên driver mặc định của hệ điều hành. Đây là hướng làm phù hợp với kiến thức môn học, sát thực tế triển khai, đồng thời giúp người học hiểu sâu hơn về Linux kernel module, USB subsystem và mô hình driver trong Linux.

### 1.2. Mục tiêu của đề tài

Mục tiêu tổng quát của đề tài là nghiên cứu và xây dựng một module nhân Linux hoạt động trên Ubuntu 64-bit nhằm bổ sung chức năng quản lý và giám sát thiết bị USB Flash, nhưng không thay thế hay viết lại driver mặc định của hệ điều hành.

Các mục tiêu cụ thể bao gồm:

- Tìm hiểu kiến trúc hệ điều hành Linux liên quan đến driver thiết bị.
- Nghiên cứu cơ chế hoạt động của giao thức USB và hạ tầng USB trong Linux.
- Tìm hiểu cách hệ điều hành Ubuntu nhận diện và quản lý ổ USB Flash.
- Thiết kế một giải pháp mở rộng chức năng quản lý USB theo hướng an toàn, không xung đột với driver sẵn có.
- Xây dựng module nhân có khả năng phát hiện thiết bị USB, thu thập thông tin nhận dạng, ghi log và cung cấp giao diện cho user space.
- Xây dựng cơ chế làm việc theo hai chế độ `observe` và `whitelist`.
- Xây dựng ứng dụng user space bằng C++ để đọc dữ liệu từ `procfs`, kết hợp `sysfs` và theo dõi trạng thái USB trong terminal.
- Thử nghiệm giải pháp trên Ubuntu 64-bit với thiết bị USB Flash thực tế.

### 1.3. Đối tượng nghiên cứu

Đối tượng nghiên cứu của đề tài gồm:

- Hệ điều hành Ubuntu 64-bit.
- Linux kernel module.
- Hệ thống USB core trong Linux kernel.
- Thiết bị USB Flash dùng trong thử nghiệm.
- Các công cụ quan sát và kiểm thử như `lsusb`, `dmesg`, `modinfo`, `insmod`, `rmmod`.

### 1.4. Phạm vi nghiên cứu

Đề tài giới hạn trong các nội dung sau:

- Chỉ phát triển thêm tính năng mới cho việc quản lý, giám sát USB.
- Không thay thế, không can thiệp trực tiếp để viết lại cơ chế hoạt động của driver lưu trữ mặc định như `usb-storage`.
- Tập trung minh họa với USB Flash trên Ubuntu 64-bit.
- Không triển khai lại cơ chế block device hay toàn bộ chu trình đọc ghi mức thấp của thiết bị lưu trữ USB.
- Không đi sâu vào chỉnh sửa mã nguồn Linux kernel gốc.

### 1.5. Phương pháp nghiên cứu

Để thực hiện đề tài, các phương pháp sau được áp dụng:

- Nghiên cứu tài liệu lý thuyết từ slide môn học về lập trình driver.
- Phân tích kiến trúc USB và mô hình driver của Linux.
- Khảo sát thông tin thiết bị USB bằng các lệnh hệ thống.
- Thiết kế mô hình hoạt động của module nhân.
- Viết mã nguồn, biên dịch, nạp thử và kiểm tra module trên Ubuntu.
- Đánh giá kết quả thực nghiệm theo các kịch bản đã xây dựng.

### 1.6. Ý nghĩa của đề tài

Về mặt học thuật, đề tài giúp sinh viên hiểu rõ mối liên hệ giữa driver, kernel module, USB core và thiết bị USB trong Linux. Về mặt thực tiễn, đề tài cho thấy cách bổ sung chức năng quản lý thiết bị ngoại vi mà không làm phá vỡ kiến trúc sẵn có của hệ điều hành. Đây là cách tiếp cận phù hợp khi xây dựng các cơ chế theo dõi, kiểm soát hoặc bảo mật USB trong môi trường Linux.

---

## 2. Cơ sở lý thuyết

### 2.1. Tổng quan về hệ điều hành Linux

Hệ điều hành là phần mềm chịu trách nhiệm quản lý tài nguyên phần cứng và cung cấp dịch vụ cho các chương trình ứng dụng. Trong Linux, phần lõi của hệ điều hành là nhân hệ điều hành, hay còn gọi là kernel. Kernel chịu trách nhiệm quản lý tiến trình, bộ nhớ, thiết bị, hệ thống tệp và các dịch vụ cơ bản khác.

Theo nội dung môn học, nhân hệ điều hành đảm nhiệm nhiều nhóm chức năng quan trọng như:

- quản lý tiến trình;
- quản lý bộ nhớ;
- quản lý thiết bị;
- quản lý hệ thống tệp;
- quản lý mạng;
- cung cấp giao diện system call.

Trong đó, quản lý thiết bị là nội dung liên quan trực tiếp đến đề tài này. Nhân Linux phải giám sát trạng thái thiết bị, điều khiển thiết bị và tổ chức trao đổi dữ liệu giữa CPU với phần cứng.

### 2.2. Không gian người dùng và không gian nhân

Trong Linux, hệ thống được tách thành hai vùng thực thi chính:

- `user space`: nơi các ứng dụng người dùng chạy;
- `kernel space`: nơi kernel và các module nhân hoạt động.

Khi một ứng dụng cần sử dụng dịch vụ hệ thống, nó sẽ gọi system call. Từ đó CPU chuyển sang `kernel mode` để thực thi mã của kernel. Driver thiết bị nói chung và module nhân nói riêng đều hoạt động trong kernel space, do đó có quyền truy cập sâu tới phần cứng và tài nguyên hệ thống. Chính vì vậy, khi lập trình driver cần đặc biệt chú ý tới độ an toàn, tránh lỗi bộ nhớ hoặc lỗi logic có thể làm treo toàn bộ hệ thống.

### 2.3. Linux kernel module

Linux kernel module là thành phần có thể được nạp vào hoặc gỡ ra khỏi kernel khi cần thiết. Module thường có đuôi `.ko`. Hình thức module động mang lại nhiều lợi ích:

- không cần biên dịch lại toàn bộ kernel;
- không cần khởi động lại hệ thống khi thêm mới driver;
- giảm kích thước kernel cơ sở;
- thuận tiện cho thử nghiệm và phát triển.

Quy trình làm việc với kernel module thường gồm các bước:

1. Viết mã nguồn module.
2. Tạo Makefile theo cơ chế Kbuild.
3. Biên dịch tạo file `.ko`.
4. Nạp module bằng `insmod`.
5. Theo dõi hoạt động bằng `dmesg`.
6. Gỡ module bằng `rmmod` khi không cần dùng nữa.

### 2.4. Khái niệm driver trong Linux

Driver là phần mềm đóng vai trò hướng dẫn CPU tương tác với thiết bị phần cứng. Thiết bị ngoại vi không kết nối trực tiếp với CPU mà thường thông qua một bộ điều khiển hoặc một bus trung gian. Vì vậy, để thiết bị làm việc được, hệ điều hành cần có driver phù hợp.

Theo phân loại trong Linux, có thể chia driver thành các nhóm chính:

- **Character device driver**: dữ liệu trao đổi theo từng byte hoặc luồng byte.
- **Block device driver**: dữ liệu trao đổi theo từng khối.
- **Network device driver**: dữ liệu trao đổi theo từng gói tin mạng.
- **Bus driver**: chịu trách nhiệm cho giao tiếp trên bus như USB, PCI, I2C.

Đối với thiết bị USB Flash, bản chất của nó là thiết bị lưu trữ nên thường gắn với mô hình block device. Tuy nhiên, trong đề tài này, mục tiêu không phải là viết lại block driver, mà là bổ sung chức năng mới ở mức quản lý hoặc giám sát.

### 2.5. Giao thức USB

USB là chuẩn giao tiếp nối tiếp dùng rộng rãi trong các thiết bị ngoại vi. Theo nội dung slide, USB sử dụng bốn đường tín hiệu chính:

- VBUS 5V;
- GND;
- D+;
- D-.

Hệ thống USB có ba thành phần cơ bản:

- **USB Host**: điều khiển toàn bộ mạng USB.
- **USB Device**: các thiết bị ngoại vi như USB Flash, chuột, bàn phím.
- **USB Hub**: thiết bị mở rộng cổng USB.

Khi một thiết bị được cắm vào bus USB, host sẽ tiến hành quá trình điểm danh để phát hiện thiết bị, cấp địa chỉ và đọc descriptor. Quá trình này giúp hệ điều hành biết thiết bị là gì, có những giao diện nào và sử dụng các endpoint nào.

### 2.6. Interface và Endpoint

Hai khái niệm quan trọng nhất trong USB là `interface` và `endpoint`.

- `interface` biểu diễn một chức năng mà thiết bị cung cấp;
- `endpoint` là điểm cuối dữ liệu dùng để truyền nhận giữa thiết bị và host.

Một thiết bị USB có thể có nhiều interface, mỗi interface có thể có nhiều endpoint. Trong Linux, driver USB thường gắn với một interface cụ thể, không nhất thiết chiếm toàn bộ thiết bị. Đây là điểm quan trọng khi phân tích ràng buộc của đề tài: nếu một interface của USB Flash đã được driver mặc định quản lý, thì không nên viết driver mới để giành lại interface đó nếu mục tiêu chỉ là bổ sung chức năng.

### 2.7. Các kiểu truyền dữ liệu USB

USB hỗ trợ bốn kiểu truyền dữ liệu chính:

- `control transfer`: truyền điều khiển;
- `interrupt transfer`: truyền ngắt;
- `bulk transfer`: truyền khối, phù hợp với thiết bị lưu trữ;
- `isochronous transfer`: truyền theo thời gian thực.

USB Flash chủ yếu sử dụng cơ chế truyền theo khối do đặc điểm cần độ chính xác dữ liệu cao.

### 2.8. Hỗ trợ USB trong Linux

Linux cung cấp một hạ tầng chung cho USB bao gồm:

- USB host controller driver;
- USB core;
- các driver cho từng lớp hoặc từng loại thiết bị.

Khi cắm thiết bị, USB core sẽ nhận thông tin đặc tả, dò tìm driver phù hợp và gắn driver vào interface tương ứng. Thông tin thiết bị có thể được xem thông qua lệnh `lsusb`, trong khi thông tin log có thể theo dõi bằng `dmesg`.

Đối với USB Flash, hệ điều hành Linux thường sử dụng driver lưu trữ mặc định. Điều này có nghĩa là việc đọc ghi tệp dữ liệu đã được hệ thống xử lý sẵn, và đề tài chỉ nên mở rộng chức năng thay vì can thiệp thay thế driver gốc.

---

## 3. Phân tích đề tài và định hướng giải pháp

### 3.1. Nội dung thực chất của đề tài

Đề tài được nêu là: "Phát triển driver trên Ubuntu 64 bit – minh họa với ổ USB. Chỉ phát triển driver với tính năng mới không viết lại driver sẵn có của hệ điều hành."

Nếu hiểu theo nghĩa hẹp, người thực hiện có thể dễ nhầm sang hướng viết một USB driver mới để thay thế hoàn toàn driver đang quản lý USB Flash. Tuy nhiên, điều này mâu thuẫn với chính yêu cầu "không viết lại driver sẵn có". Do đó, đề tài cần được hiểu đúng là:

- nghiên cứu cách Linux làm việc với thiết bị USB;
- xây dựng một module nhân mới có liên quan đến USB Flash;
- module đó bổ sung thêm một tính năng mới;
- còn chức năng truy xuất dữ liệu cơ bản của USB Flash vẫn do hệ điều hành đảm nhiệm.

### 3.2. Bài toán cần giải quyết

Bài toán đặt ra là xây dựng một cơ chế trong kernel để:

- phát hiện sự kiện cắm hoặc rút USB Flash;
- nhận diện các thuộc tính quan trọng của thiết bị;
- xuất hoặc lưu các thông tin này phục vụ quản lý;
- bảo đảm USB Flash vẫn hoạt động bình thường dưới driver mặc định.

Như vậy, thay vì "thay thế", module của đề tài đóng vai trò "mở rộng" hoặc "bổ sung".

### 3.3. Các yêu cầu chức năng

Hệ thống dự kiến cần đáp ứng các yêu cầu chức năng sau:

- Phát hiện khi USB Flash được cắm vào hệ thống.
- Phát hiện khi USB Flash bị rút khỏi hệ thống.
- Đọc được các thông tin nhận dạng như VID, PID, tên sản phẩm, nhà sản xuất, số serial nếu có.
- Ghi log trạng thái thiết bị vào kernel log.
- Cung cấp dữ liệu ra một giao diện để chương trình user space có thể đọc.
- Hỗ trợ whitelist theo `VID:PID` hoặc `VID:PID:SERIAL`.
- Phân biệt rõ chế độ `observe` và `whitelist`.
- Có thể đánh dấu thiết bị hợp lệ hoặc không hợp lệ khi whitelist được bật.

### 3.4. Các yêu cầu phi chức năng

- Không gây ảnh hưởng đến hoạt động bình thường của hệ điều hành.
- Không gây treo kernel.
- Không làm hỏng chức năng mount và truy cập dữ liệu của USB Flash.
- Có thể nạp và gỡ bằng module động.
- Dễ thử nghiệm trên Ubuntu 64-bit.

### 3.5. Các hướng tiếp cận có thể có

Có thể xem xét ba hướng tiếp cận:

**Hướng 1: Viết lại driver cho USB Flash**

Hướng này không phù hợp vì vi phạm yêu cầu đề tài. Ngoài ra, đây là công việc phức tạp và dễ gây xung đột với driver chuẩn.

**Hướng 2: Tạo một USB driver chiếm interface của thiết bị**

Hướng này chỉ phù hợp cho bài toán học thuật minh họa cơ chế USB driver, nhưng nếu interface đã do `usb-storage` sử dụng thì vẫn có nguy cơ xung đột và trái với mục tiêu không thay thế driver có sẵn.

**Hướng 3: Xây dựng module mở rộng quản lý, giám sát**

Đây là hướng hợp lý nhất. Module không làm thay nhiệm vụ của driver lưu trữ, mà chỉ theo dõi, thu thập thông tin và cung cấp thêm chức năng quản lý.

### 3.6. Giải pháp lựa chọn

Trong phạm vi báo cáo này, giải pháp được lựa chọn là xây dựng một module nhân Linux có chức năng:

- theo dõi trạng thái kết nối của USB Flash;
- đọc thông tin nhận dạng từ thiết bị USB;
- lưu thông tin vào cấu trúc dữ liệu trong kernel;
- ghi log bằng `printk`;
- cung cấp giao diện để đọc trạng thái thiết bị từ user space.

Phiên bản sản phẩm đã hoàn thiện mở rộng thêm các điểm sau:

- hỗ trợ whitelist theo `VID:PID` hoặc `VID:PID:SERIAL`;
- công bố rõ `policy_mode` với hai giá trị `observe` và `whitelist`;
- lưu lịch sử thiết bị đã theo dõi và thống kê số lần cắm, tổng thời gian kết nối;
- cung cấp ứng dụng `usb_guard_monitor` viết bằng C++ để hiển thị thông tin ở dạng text, JSON và TUI.

Giải pháp này tận dụng được kiến thức trong slide về:

- quá trình phát hiện thiết bị USB;
- vai trò của USB core;
- quy trình phát triển driver trong Linux;
- cách tổ chức module nhân và cơ chế giao tiếp giữa kernel với user space.

### 3.7. Tính phù hợp của giải pháp

Giải pháp đã chọn có các ưu điểm:

- đúng yêu cầu đề tài;
- tránh can thiệp trực tiếp vào driver mặc định;
- đủ thể hiện kiến thức về driver và kernel module;
- dễ mở rộng cho mục đích giám sát và bảo mật.

---

## 4. Thiết kế hệ thống

### 4.1. Kiến trúc tổng thể

Kiến trúc hệ thống được đề xuất gồm bốn lớp chính:

1. **Thiết bị USB Flash**
2. **Hệ thống USB của Linux**
3. **Module nhân do đề tài xây dựng**
4. **Không gian người dùng**

Trong đó:

- Thiết bị USB Flash là đối tượng được giám sát.
- Hệ thống USB của Linux bao gồm host controller, USB core và driver mặc định.
- Module nhân mở rộng `usb_guard` sẽ lấy thông tin về thiết bị, duy trì trạng thái active/history và xuất dữ liệu qua `procfs`.
- Ứng dụng `usb_guard_monitor` ở user space sẽ đọc kết quả từ `/proc/usb_guard/status`, kết hợp thêm thông tin từ `sysfs` để hiển thị block device, dung lượng, mountpoint và chế độ chỉ đọc.

### 4.2. Mô hình hoạt động tổng quát

Quá trình làm việc của hệ thống có thể mô tả như sau:

1. Người dùng cắm USB Flash vào cổng USB.
2. USB host controller phát hiện sự thay đổi trên bus.
3. USB core của Linux tiến hành điểm danh thiết bị.
4. Driver mặc định của hệ điều hành nhận diện USB Flash để phục vụ truy cập dữ liệu.
5. Module mở rộng thu thập thông tin nhận dạng của thiết bị.
6. Module ghi log và cập nhật trạng thái nội bộ.
7. User space có thể đọc trạng thái này qua giao diện đã thiết kế.

### 4.3. Thiết kế các thành phần chính

#### 4.3.1. Thành phần lưu thông tin thiết bị

Module cần một cấu trúc dữ liệu để lưu thông tin của USB đang theo dõi. Một số trường cơ bản gồm:

- `vendor_id`
- `product_id`
- `manufacturer`
- `product_name`
- `serial_number`
- `connect_time`
- `disconnect_time`
- `status`

Cấu trúc này cho phép module giữ được trạng thái hiện tại của thiết bị và hỗ trợ việc xuất dữ liệu ra bên ngoài.

#### 4.3.2. Thành phần xử lý sự kiện kết nối

Khi USB được cắm vào, module sẽ:

- đọc thông tin cơ bản của thiết bị;
- xác định đây có phải USB Flash hoặc thiết bị thuộc nhóm quan tâm hay không;
- cập nhật cấu trúc dữ liệu;
- ghi log thông báo phát hiện thiết bị.

#### 4.3.3. Thành phần xử lý sự kiện ngắt kết nối

Khi USB bị rút ra, module sẽ:

- cập nhật trạng thái ngắt kết nối;
- ghi nhận thời điểm thiết bị rời khỏi hệ thống;
- ghi log tương ứng;
- giải phóng hoặc làm sạch dữ liệu liên quan nếu cần.

#### 4.3.4. Thành phần giao tiếp với user space

Để người dùng có thể quan sát dữ liệu từ module, có thể chọn một trong các phương án:

- tạo file trong `procfs`;
- tạo file trong `sysfs`;
- đăng ký một character device;
- chỉ dùng `dmesg` để xem log.

Trong phạm vi đề tài, phương án phù hợp là kết hợp giữa `dmesg` và một giao diện đơn giản như `procfs` hoặc character device. Cách này vừa dễ triển khai, vừa đủ để chứng minh module có cung cấp thêm tính năng mới.

Trong sản phẩm hiện tại, lựa chọn cuối cùng là dùng `dmesg` kết hợp với `procfs`. Đây là cầu nối chính giữa kernel space và ứng dụng user space. Ứng dụng C++ sẽ đọc `/proc/usb_guard/status` rồi tiếp tục đối chiếu với `sysfs` để có thêm các thông tin thực tế của block device.

### 4.4. Thiết kế chức năng cụ thể

#### Chức năng phát hiện USB

Chức năng này giúp module biết được lúc nào thiết bị được cắm hoặc rút. Đây là chức năng nền tảng để các xử lý khác được kích hoạt.

#### Chức năng nhận diện thiết bị

Module cần đọc các thông tin nhận dạng để phục vụ việc ghi log, thống kê và áp dụng chính sách sau này.

#### Chức năng ghi log

Ghi log vào kernel log giúp người phát triển dễ quan sát hoạt động của module, đồng thời chứng minh hệ thống đã phản ứng đúng với sự kiện USB.

#### Chức năng xuất trạng thái

Cho phép người dùng ở user space đọc thông tin hiện tại của USB. Đây là điểm thể hiện tính "bổ sung chức năng mới" rõ ràng nhất.

#### Chức năng kiểm tra chính sách

 Module so sánh `VID:PID` hoặc `VID:PID:SERIAL` với danh sách thiết bị được phép.

- Nếu người dùng truyền whitelist cho module, hệ thống làm việc ở chế độ `whitelist`.
- Nếu không truyền whitelist, hệ thống làm việc ở chế độ `observe`.
- Trong chế độ `observe`, thiết bị chỉ bị theo dõi và ứng dụng user space hiển thị nhãn `monitor-only`.
- Trong chế độ `whitelist`, thiết bị không nằm trong danh sách sẽ bị đánh dấu `UNTRUSTED` và sinh cảnh báo.

 Việc chuyển block device sang trạng thái chỉ đọc không do kernel module chiếm quyền điều khiển driver `usb-storage`, mà được thực hiện ở ứng dụng user space bằng cách ghi vào `sysfs` khi chính sách phù hợp.

### 4.5. Sơ đồ luồng hoạt động

Có thể mô tả luồng hoạt động của hệ thống theo dạng sau:

1. Bắt đầu
2. Nạp module vào kernel
3. Chờ sự kiện thiết bị USB
4. Nếu phát hiện cắm USB:
   - đọc thông tin thiết bị
   - cập nhật trạng thái
   - ghi log
   - xuất dữ liệu cho user space
5. Nếu phát hiện rút USB:
   - cập nhật trạng thái
   - ghi log
   - làm sạch dữ liệu liên quan
6. Tiếp tục chờ sự kiện mới
7. Khi kết thúc, gỡ module khỏi kernel

---

## 5. Cài đặt và triển khai

### 5.1. Môi trường thực hiện

Hệ thống được triển khai trên môi trường Ubuntu 64-bit. Đây là môi trường phù hợp để phát triển và thử nghiệm Linux kernel module nhờ có đầy đủ công cụ biên dịch, cơ chế quản lý module và hỗ trợ tốt cho USB subsystem.

Các thành phần môi trường chính gồm:

- Hệ điều hành Ubuntu 64-bit
- Trình biên dịch GCC
- Bộ công cụ `make`
- Header tương ứng với phiên bản kernel đang sử dụng
- Thiết bị USB Flash dùng để minh họa

### 5.2. Công cụ sử dụng

Trong quá trình thực hiện đề tài, các công cụ sau được dùng:

- `lsusb`: liệt kê thiết bị USB, xem VID và PID.
- `dmesg`: theo dõi log của kernel.
- `lsmod`: xem danh sách module đang nạp.
- `insmod`: nạp module.
- `rmmod`: gỡ module.
- `modinfo`: xem thông tin module.
- `make`: biên dịch mã nguồn module.

### 5.3. Khảo sát thiết bị USB

Trước khi viết module, cần khảo sát thông tin thiết bị USB Flash trên hệ thống. Thông qua lệnh `lsusb`, có thể thu được:

- mã nhà sản xuất `idVendor`;
- mã sản phẩm `idProduct`;
- tên thiết bị;
- thông tin bus và thiết bị.

Việc khảo sát này giúp người phát triển hiểu được thiết bị đang làm việc với hệ thống ra sao, đồng thời hỗ trợ xây dựng logic nhận diện hoặc kiểm tra chính sách.

### 5.4. Tổ chức mã nguồn

Mã nguồn của module có thể được tổ chức gồm:

- một file nguồn chính chứa logic của module;
- một Makefile phục vụ biên dịch;
- một ứng dụng user space bằng C++;
- các phần khai báo cấu trúc dữ liệu, hàm khởi tạo và kết thúc module;
- các hàm ghi log và xuất thông tin ra giao diện user space.

Trong phiên bản hiện tại, các file chính của sản phẩm gồm:

- `usb_guard.c`: kernel module thực hiện giám sát USB;
- `Makefile`: biên dịch cả module và ứng dụng user space;
- `usb_guard_monitor.cpp`: ứng dụng C++ để đọc `/proc/usb_guard/status`, làm giàu dữ liệu bằng `sysfs` và hiển thị theo nhiều chế độ;
- `README_usb_guard.md`: hướng dẫn build, nạp module và chạy ứng dụng.

### 5.5. Các thành phần chính trong mã chương trình

#### 5.5.1. Khai báo thông tin module

Phần đầu chương trình thường khai báo:

- tên module;
- tác giả;
- giấy phép;
- mô tả chức năng.

Điều này giúp hệ thống và người dùng hiểu được mục đích của module khi kiểm tra bằng `modinfo`.

#### 5.5.2. Cấu trúc dữ liệu thiết bị

Phần này định nghĩa cấu trúc lưu trữ thông tin nhận dạng và trạng thái của USB. Tùy mức độ yêu cầu, cấu trúc có thể đơn giản hoặc mở rộng.

#### 5.5.3. Hàm khởi tạo module

Hàm khởi tạo thực hiện các tác vụ:

- đăng ký các thành phần cần thiết với kernel;
- khởi tạo dữ liệu;
- chuẩn bị giao diện xuất thông tin cho user space;
- in log xác nhận module đã được nạp.

#### 5.5.4. Hàm kết thúc module

Hàm kết thúc thực hiện:

- giải phóng tài nguyên;
- hủy đăng ký giao diện đã tạo;
- in log xác nhận module đã được gỡ.

#### 5.5.5. Hàm xử lý thiết bị USB

Nhóm hàm này có nhiệm vụ:

- nhận biết sự kiện kết nối hoặc ngắt kết nối;
- đọc thuộc tính của thiết bị;
- cập nhật thông tin vào cấu trúc quản lý;
- gọi các hàm ghi log hoặc xuất trạng thái.

#### 5.5.6. Hàm giao tiếp với user space

Nếu sử dụng `procfs`, module cần cung cấp hàm đọc để xuất chuỗi văn bản mô tả trạng thái thiết bị. Nếu dùng character device, module cần cài đặt `file_operations` như `open`, `read`, và có thể thêm `write` nếu muốn cấu hình động từ user space.

Với phiên bản đã cài đặt, `/proc/usb_guard/status` xuất các nhóm thông tin chính sau:

- `whitelist_enabled`;
- `policy_mode` với hai giá trị `observe` hoặc `whitelist`;
- `last_event`;
- `active_count` và `history_count`;
- `last_device`;
- các dòng `active[i]` mô tả thiết bị USB đang hoạt động;
- các dòng `history[i]` mô tả lịch sử thiết bị đã được ghi nhận.

### 5.6. Quy trình biên dịch

Sau khi hoàn thiện mã nguồn, module được biên dịch bằng cơ chế Kbuild. Quy trình cơ bản như sau:

1. Tạo Makefile.
2. Chạy lệnh `make`.
3. Hệ thống tạo ra file module có đuôi `.ko`.

Sau đó có thể dùng `modinfo` để kiểm tra thông tin module nếu cần.

### 5.7. Quy trình nạp và gỡ module

Khi muốn chạy thử module:

1. Nạp module bằng `sudo insmod <ten_module>.ko`
2. Dùng `dmesg` để kiểm tra thông báo khởi tạo
3. Cắm hoặc rút USB Flash để quan sát log
4. Đọc giao diện xuất thông tin mà module cung cấp
5. Gỡ module bằng `sudo rmmod <ten_module>`

### 5.8. Mô tả hoạt động mong đợi

Khi module được nạp thành công:

- hệ thống không bị ảnh hưởng đến cơ chế truy cập USB Flash mặc định;
- khi cắm USB, log xuất hiện thông tin nhận diện thiết bị;
- khi rút USB, log ghi nhận thiết bị đã ngắt kết nối;
- nếu có giao diện `procfs` hoặc character device, người dùng đọc được dữ liệu tương ứng.

### 5.9. Các tính năng đã hoàn thiện trong sản phẩm hiện tại

Phiên bản hiện tại của đề tài đã cài đặt được các tính năng chính sau:

- phát hiện sự kiện cắm và rút USB mass storage bằng USB notifier;
- ghi log chi tiết vào `dmesg` với các trường `bus`, `dev`, `devpath`, `VID`, `PID`, trạng thái whitelist, chuỗi nhận dạng và thống kê thời gian;
- hỗ trợ whitelist theo `VID:PID` hoặc `VID:PID:SERIAL`;
- xuất `policy_mode` để phân biệt rõ `observe mode` và `whitelist mode`;
- lưu danh sách thiết bị đang hoạt động và lịch sử thiết bị đã ghi nhận;
- thống kê `connect_count`, `connected_for_ms`, `total_connected_ms`;
- với thiết bị đã cắm sẵn trước lúc nạp module, thời gian được tính từ lúc `usb_guard` bắt đầu theo dõi;
- ứng dụng `usb_guard_monitor` đọc `/proc/usb_guard/status`, làm giàu thông tin từ `sysfs` và hiển thị ở dạng text hoặc JSON;
- ứng dụng có giao diện TUI trong terminal với các tab `Overview`, `History`, `Actions`;
- trong `observe mode`, ứng dụng hiển thị nhãn `monitor-only` thay vì `UNTRUSTED`;
- tùy chọn `--readonly-untrusted` chỉ áp chính sách với thiết bị ngoài whitelist khi hệ thống đang ở `whitelist mode`;
- tùy chọn `--dry-run` cho phép mô phỏng trước mà không ghi vào `sysfs`;
- thông báo của ứng dụng đã được phân biệt rõ thành `dry-run`, `applied`, `failed` hoặc `skipped`, phản ánh đúng kết quả xử lý thực tế.

---

## 6. Thử nghiệm và đánh giá

### 6.1. Mục tiêu thử nghiệm

Việc thử nghiệm nhằm kiểm tra ba nội dung chính:

- module có hoạt động đúng chức năng đã thiết kế hay không;
- USB Flash có còn được hệ điều hành hỗ trợ bình thường hay không;
- giải pháp có bám đúng ràng buộc không thay thế driver mặc định hay không.

### 6.2. Kịch bản thử nghiệm

#### 6.2.1. Kịch bản 1: nạp module

Thực hiện lệnh nạp module vào kernel. Quan sát bằng `lsmod` và `dmesg` để xác nhận:

- module được nạp thành công;
- không có lỗi khởi tạo;
- giao diện quản lý của module đã sẵn sàng.

#### 6.2.2. Kịch bản 2: cắm USB Flash

Sau khi module đang hoạt động, cắm USB Flash vào hệ thống. Kiểm tra:

- hệ điều hành vẫn nhận thiết bị bình thường;
- USB có thể mount và truy cập dữ liệu như trước;
- module ghi nhận được sự kiện kết nối;
- thông tin VID, PID và tên thiết bị được ghi log hoặc xuất ra giao diện đọc.

#### 6.2.3. Kịch bản 3: đọc dữ liệu từ module

Đọc thông tin do module cung cấp từ `procfs`, `sysfs` hoặc file thiết bị. Kết quả cần phản ánh đúng trạng thái hiện tại của USB.

#### 6.2.4. Kịch bản 4: rút USB Flash

Rút USB Flash ra khỏi hệ thống và theo dõi:

- log ngắt kết nối;
- dữ liệu trạng thái được cập nhật;
- không xuất hiện lỗi kernel.

#### 6.2.5. Kịch bản 5: kiểm tra nhiều lần cắm hoặc rút

Lặp lại thao tác cắm và rút nhiều lần để đánh giá tính ổn định. Mục tiêu là xác nhận module không bị sai trạng thái hoặc rò rỉ tài nguyên.

### 6.3. Kết quả mong đợi

Nếu hệ thống hoạt động đúng, có thể thu được các kết quả sau:

- Module được nạp và gỡ thành công.
- USB Flash vẫn hoạt động bình thường dưới driver mặc định.
- Hệ thống ghi nhận được sự kiện cắm hoặc rút thiết bị.
- Dữ liệu nhận dạng của USB được thu thập chính xác.
- User space có thể đọc thông tin do module cung cấp.
- `/proc/usb_guard/status` phản ánh đúng `policy_mode`.
- Khi không cấu hình whitelist, ứng dụng hiển thị thiết bị ở trạng thái `monitor-only`.
- Khi whitelist được bật, thiết bị ngoài danh sách sẽ bị đánh dấu `UNTRUSTED` và có thể được xử lý tiếp ở ứng dụng user space.

### 6.4. Đánh giá theo tiêu chí kỹ thuật

#### Tính đúng chức năng

Giải pháp đáp ứng đúng mục tiêu khi nó phát hiện được USB, lưu thông tin nhận dạng và cung cấp thêm khả năng giám sát.

#### Tính ổn định

Module cần không gây lỗi kernel, không làm treo máy, không tạo tác động phụ lên cơ chế truy cập USB Flash sẵn có.

#### Tính phù hợp với yêu cầu đề tài

Điểm quan trọng nhất là không thay thế driver hệ điều hành. Nếu USB Flash vẫn mount, đọc ghi dữ liệu bình thường dưới cơ chế mặc định của Ubuntu thì giải pháp được xem là đáp ứng tốt yêu cầu.

#### Tính mở rộng

Từ phiên bản cơ bản, module có thể tiếp tục được phát triển thành hệ thống quản lý USB nâng cao như danh sách thiết bị tin cậy, cảnh báo thời gian thực hoặc tích hợp với ứng dụng giám sát người dùng.

### 6.5. Nhận xét kết quả

Thông qua quá trình thử nghiệm, có thể nhận thấy cách tiếp cận theo hướng module mở rộng là khả thi và hợp lý. Giải pháp vừa tận dụng được hạ tầng mạnh của Linux kernel, vừa tránh được rủi ro khi can thiệp sâu vào driver lưu trữ chuẩn. Điều này đặc biệt phù hợp với các đề tài học phần, nơi mục tiêu là hiểu cơ chế driver và biết cách mở rộng hệ thống một cách có kiểm soát.

Kết quả triển khai thực tế cho thấy hệ thống đã hình thành một chuỗi xử lý khá hoàn chỉnh:

- kernel module giám sát sự kiện USB và duy trì trạng thái trong kernel;
- `procfs` đóng vai trò cầu nối giữa kernel space và user space;
- ứng dụng C++ hiển thị thông tin quản trị, thống kê và thử chính sách ở phía người dùng.

Việc bổ sung `policy_mode` là một điểm quan trọng vì nó giúp phân biệt rõ giữa trạng thái chỉ theo dõi và trạng thái kiểm tra whitelist. Nhờ đó, hệ thống tránh được việc hiểu sai rằng mọi thiết bị đều không tin cậy khi người quản trị chưa cấu hình danh sách cho phép.

---

## 7. Ưu điểm, hạn chế và hướng phát triển

### 7.1. Ưu điểm

Giải pháp được đề xuất có các ưu điểm sau:

- Bám sát kiến trúc chuẩn của Linux.
- Không thay thế hoặc phá vỡ driver mặc định.
- Dễ triển khai trên Ubuntu 64-bit.
- Phù hợp với yêu cầu học tập và nghiên cứu.
- Có khả năng mở rộng thành các chức năng quản lý và bảo mật USB thực tế.
- Có phân tách rõ giữa `observe mode` và `whitelist mode`.
- Có ứng dụng user space trực quan để giám sát và đánh giá chính sách.
- Có khả năng thống kê lịch sử cắm thiết bị và thời lượng hoạt động.

Ngoài ra, cách tiếp cận bằng kernel module giúp quá trình phát triển, kiểm thử và chỉnh sửa thuận tiện hơn nhiều so với việc thay đổi mã nguồn kernel chính.

### 7.2. Hạn chế

Bên cạnh ưu điểm, giải pháp vẫn có một số hạn chế:

- Chưa can thiệp trực tiếp vào luồng đọc ghi dữ liệu mức thấp của USB Flash.
- Khả năng kiểm soát truy cập thực tế còn phụ thuộc vào cách tích hợp với user space và chính sách hệ thống.
- Phạm vi thử nghiệm có thể chỉ dừng ở mức thiết bị USB Flash phổ biến, chưa xét đến các thiết bị USB phức hợp.
- Chế độ `read-only` hiện được thực hiện ở user space bằng cách ghi vào `sysfs`, chưa phải là cơ chế chặn ở tầng kernel.
- Ứng dụng hiện chủ yếu theo dõi trạng thái theo chu kỳ đọc dữ liệu, chưa tích hợp hạ tầng sự kiện đầy đủ như `udev`.

Ngoài ra, vì hoạt động trong kernel space, mọi mở rộng sau này vẫn cần được kiểm thử cẩn thận để tránh gây ảnh hưởng đến toàn hệ thống.

### 7.3. Hướng phát triển

Trong tương lai, từ nền tảng của đề tài này có thể phát triển thêm các hướng sau:

- xây dựng danh sách thiết bị USB được phép sử dụng;
- cảnh báo khi phát hiện USB không thuộc danh sách tin cậy;
- ghi log chi tiết theo thời gian, người dùng hoặc phiên làm việc;
- tích hợp với ứng dụng giao diện đồ họa ở user space;
- xuất thống kê về số lần cắm thiết bị và thời lượng sử dụng;
- hỗ trợ thêm nhiều loại thiết bị USB khác ngoài USB Flash;
- kết hợp với các cơ chế bảo mật của hệ điều hành để nâng cao kiểm soát.
- tích hợp theo dõi sự kiện thời gian thực bằng `udev` hoặc netlink thay cho polling;
- lưu lịch sử thiết bị bền vững ra file hoặc cơ sở dữ liệu ở user space;
- bổ sung chế độ xuất báo cáo JSON hoặc CSV để phục vụ quản trị và viết báo cáo tự động.

---

## 8. Kết luận

Đề tài "Phát triển module nhân Linux trên Ubuntu 64-bit để bổ sung chức năng quản lý, giám sát ổ USB, minh họa với USB Flash, không thay thế driver mặc định của hệ điều hành" là một bài toán phù hợp với định hướng học tập về lập trình driver trong Linux.

Qua việc nghiên cứu lý thuyết về kernel, driver, USB và Linux kernel module, có thể thấy rằng việc viết lại driver mặc định cho USB Flash là không cần thiết và không phù hợp với ràng buộc của đề tài. Thay vào đó, hướng tiếp cận đúng đắn là xây dựng một module mở rộng có khả năng phát hiện thiết bị, thu thập thông tin nhận dạng, ghi log và cung cấp dữ liệu cho không gian người dùng.

Giải pháp này vừa khai thác được hạ tầng sẵn có của hệ điều hành, vừa đáp ứng được yêu cầu "phát triển tính năng mới". Đồng thời, nó cũng tạo ra một nền tảng rõ ràng để mở rộng sang các bài toán kiểm soát và bảo mật thiết bị USB trong môi trường Linux. Có thể khẳng định rằng đây là hướng triển khai khả thi, an toàn và có giá trị thực tiễn cao đối với việc học tập và nghiên cứu lập trình driver.

---

## 9. Tài liệu tham khảo

1. Slide môn học Lập trình driver, Học viện Kỹ thuật Mật mã.
2. Slide Chương 1: Khái niệm cơ bản về hệ điều hành, driver và module nhân Linux.
3. Slide Chương 6: Phát triển trình điều khiển cho thiết bị chuẩn USB.
4. Tài liệu Linux kernel module và USB subsystem trong Linux.
5. Tài liệu hướng dẫn sử dụng các lệnh hệ thống trên Ubuntu như `lsusb`, `dmesg`, `insmod`, `rmmod`, `modinfo`.

---

## 10. Phụ lục

### Phụ lục A. Một số lệnh sử dụng trong quá trình thử nghiệm

```bash
lsusb
lsusb -v
dmesg | tail -n 50
lsmod | grep usb
sudo insmod ten_module.ko
sudo rmmod ten_module
modinfo ten_module.ko
cat /proc/usb_guard/status
./usb_guard_monitor
./usb_guard_monitor --json
./usb_guard_monitor --tui --watch 1
./usb_guard_monitor --readonly-untrusted --dry-run
```

### Phụ lục B. Gợi ý hình minh họa trong báo cáo

- Hình kiến trúc tổng thể của Linux kernel và vị trí của driver.
- Hình sơ đồ USB Host, USB Device, USB Hub.
- Hình luồng phát hiện thiết bị USB trong Linux.
- Hình kết quả lệnh `lsusb`.
- Hình log `dmesg` khi cắm USB.
- Hình nội dung `/proc/usb_guard/status`.
- Hình giao diện `usb_guard_monitor` ở chế độ text.
- Hình giao diện TUI của `usb_guard_monitor`.

### Phụ lục C. Gợi ý bảng biểu

- Bảng mô tả thông tin nhận dạng của USB Flash.
- Bảng mô tả chức năng của module.
- Bảng kịch bản thử nghiệm và kết quả mong đợi.
- Bảng so sánh giữa driver mặc định và module mở rộng.
