# USB Guard

`usb_guard` la mot Linux kernel module dung de giam sat USB mass storage tren Ubuntu 64-bit ma khong thay the `usb-storage`.

## Chuc nang hien tai

- Phat hien su kien cam/rut USB mass storage
- Ghi log chi tiet bang `dmesg`
- Whitelist theo `VID:PID` hoac `VID:PID:SERIAL`
- Ho tro `policy_mode=observe|whitelist`
- Canh bao USB ngoai whitelist
- Luu thong tin active/history
- Xuat du lieu qua `/proc/usb_guard/status`
- Co ung dung user space viet bang C++17 de theo doi va lam giau thong tin block device
- Co giao dien TUI terminal realtime, khong can phu thuoc Python hay ncurses
- TUI co mau trang thai, tab Overview/History/Actions va popup khi policy tao action moi
- `--dry-run` chi mo phong va khong can quyen root

## Build module

```bash
cd /media/Data/Ki_2_nam_4/Lap_trinh_driver/Bai_tap_lon
make
```

## Nap module

Whitelist theo `VID:PID`:

```bash
sudo insmod ./usb_guard.ko whitelist=346d:5678
```

Whitelist theo `VID:PID:SERIAL`:

```bash
sudo insmod ./usb_guard.ko whitelist=346d:5678:8124311165564264629
```

Kiem tra:

```bash
cat /proc/usb_guard/status
dmesg | tail -n 50
```

Go module:

```bash
sudo rmmod usb_guard
```

## Ung dung user space

Chay mot lan:

```bash
./usb_guard_monitor
```

Theo doi lien tuc:

```bash
./usb_guard_monitor --watch 2
```

TUI realtime:

```bash
./usb_guard_monitor --tui --watch 1
```

Hoac:

```bash
make tui
```

Phim dieu khien TUI:

```text
q    thoat
j    xuong USB tiep theo
k    len USB truoc do
h    sang tab ben trai
l    sang tab ben phai
Tab  chuyen tab tiep theo
x    dong popup action
mui ten len/xuong cung ho tro
r    refresh ngay
```

Xuat JSON:

```bash
./usb_guard_monitor --json
```

Mo phong chinh sach read-only voi USB ngoai whitelist:

```bash
./usb_guard_monitor --readonly-untrusted --dry-run
```

Ap read-only that su cho USB ngoai whitelist:

```bash
sudo ./usb_guard_monitor --readonly-untrusted
```

## Luu y

- Module nay chi mo rong giam sat va quan ly, khong thay the driver mac dinh cua he dieu hanh.
- Neu khong truyen whitelist, he thong chay o `observe mode` va app se hien thi USB la `monitor-only`.
- Che do `--readonly-untrusted` duoc thuc hien o user space bang cach ghi vao sysfs cua block device.
- `--readonly-untrusted` chi ap dung thuc su khi module dang o `whitelist mode`.
- Neu USB dang duoc mount read-write, ban nen unmount truoc khi ap chinh sach read-only de tranh loi he thong tep.
