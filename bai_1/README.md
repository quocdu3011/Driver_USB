# Secure File Manager for Ubuntu / Zorin OS

Project nay la mot he thong quan ly file bao mat hoan chinh gom:

1. Linux kernel module dang character device (`/dev/secure_aes`) thuc hien AES-CBC trong kernel.
2. CLI app de demo, test va script tu dong.
3. GUI GTK3 de chay tren Zorin OS / Ubuntu Desktop, cho phep browse thu muc, chon file, tao folder, xoa file/folder, encrypt/decrypt qua driver, va theo doi log thao tac.

## Tinh nang chinh

- AES-CBC + PKCS#7 padding trong kernel driver.
- User-space khong tu ma hoa AES, chi giao tiep voi driver bang `ioctl + read/write`.
- Ho tro AES-128, AES-192, AES-256 qua key hex 16/24/32 byte.
- GUI GTK3 native cho Zorin OS:
  - browse thu muc hien tai
  - xem danh sach file/folder
  - double-click folder de di chuyen
  - chon file lam input
  - tao folder moi
  - xoa file hoac folder de quy co xac nhan
  - chon input/output bang dialog
  - nhap key/IV hex
  - encrypt/decrypt qua driver
  - xem operation log
- CLI van duoc giu lai de test va demo nhanh.

## Lua chon ky thuat

- Driver su dung **Linux kernel crypto API** voi transform `cbc(aes)`.
- Ly do:
  - phu hop hon cho bai hoc kernel module,
  - giam rui ro sai sot so voi tu viet AES block cipher,
  - van dap ung dung yeu cau vi AES duoc thuc hien trong driver, khong nam o user-space.
- Phan padding PKCS#7, session theo file descriptor, char device, `ioctl`, `read`, `write`, file browser GUI, va logic app deu duoc project nay tu cai dat.

## Cau truc thu muc

```text
project-root/
|-- .gitignore
|-- README.md
|-- app/
|   |-- Makefile
|   |-- driver_client.c
|   |-- driver_client.h
|   |-- file_io.c
|   |-- file_io.h
|   |-- file_manager.c
|   |-- file_manager.h
|   |-- gui_main.c
|   |-- hex_utils.c
|   |-- hex_utils.h
|   |-- main.c
|   |-- secure_file_service.c
|   `-- secure_file_service.h
|-- desktop/
|   `-- secure-file-manager.desktop
|-- driver/
|   |-- Makefile
|   |-- aes_core.c
|   |-- aes_core.h
|   |-- ioctl_defs.h
|   |-- pkcs7.c
|   |-- pkcs7.h
|   `-- secure_aes_driver.c
|-- scripts/
|   |-- build_all.sh
|   |-- install_deps_zorin.sh
|   |-- install_launcher.sh
|   |-- load_driver.sh
|   |-- run_demo.sh
|   |-- run_gui.sh
|   `-- unload_driver.sh
`-- tests/
    |-- sample.txt
    `-- verify.sh
```

## Yeu cau moi truong

- Zorin OS hoac Ubuntu 64-bit.
- Khong dat project trong duong dan co dau cach. Nen dung vi du `~/bai_1`.
- Da cai kernel headers, build toolchain va GTK3 dev package.

Cach nhanh nhat:

```bash
bash scripts/install_deps_zorin.sh
```

Hoac cai tay:

```bash
sudo apt update
sudo apt install -y \
  build-essential gcc make pkg-config libgtk-3-dev \
  linux-headers-$(uname -r) mokutil openssl
```

## Build toan bo du an

```bash
cd ~/bai_1
bash scripts/build_all.sh
```

Ket qua build:

- Driver: `driver/secure_aes.ko`
- CLI: `app/secure_file_app`
- GUI: `app/secure_file_gui`

## Nap driver

```bash
bash scripts/load_driver.sh
```

Script se:

- `insmod` module `secure_aes.ko`
- kiem tra `/dev/secure_aes`
- neu can thi `mknod` thu cong

### Neu gap loi `Key was rejected by service`

Day thuong la do **Secure Boot** dang bat va module chua duoc ky.

Kiem tra nhanh:

```bash
mokutil --sb-state
```

Neu Secure Boot dang `enabled`, co 2 cach:

1. Cach nhanh de demo mon hoc: tat Secure Boot trong BIOS/UEFI.
2. Cach dung chuan hon: ky `driver/secure_aes.ko` va enroll MOK truoc khi load module.

## Chay CLI

Encrypt:

```bash
./app/secure_file_app encrypt tests/sample.txt tests/sample.enc \
  --key 00112233445566778899aabbccddeeff \
  --iv 0102030405060708090a0b0c0d0e0f10
```

Decrypt:

```bash
./app/secure_file_app decrypt tests/sample.enc tests/restored.txt \
  --key 00112233445566778899aabbccddeeff \
  --iv 0102030405060708090a0b0c0d0e0f10
```

## Kiem thu end-to-end bang CLI

```bash
bash tests/verify.sh
```

Hoac chay full demo:

```bash
bash scripts/run_demo.sh
```

## Chay GUI tren Zorin OS

Sau khi build va load driver:

```bash
bash scripts/run_gui.sh
```

### Luong su dung GUI

1. O panel trai, chon thu muc can lam viec.
2. Double-click folder de di chuyen vao thu muc con.
3. Chon mot file roi bam `Use Selected as Input`, hoac dung `Browse File`.
4. Chon `Encrypt` hoac `Decrypt`.
5. Nhap AES key hex va IV hex.
6. Kiem tra duong dan output, co the bam `Suggest Output` hoac `Choose Output`.
7. Bam `Process via Driver`.
8. Theo doi ket qua trong `Operation Log`.

GUI duoc viet bang GTK3, nen giao dien se chay native tren Zorin OS Desktop.

## Cai launcher vao menu ung dung

```bash
bash scripts/install_launcher.sh
```

Launcher se duoc tao tai:

```text
~/.local/share/applications/secure-file-manager.desktop
```

## Unload driver

```bash
bash scripts/unload_driver.sh
```

## Giao tiep app-driver

Luong xu ly chung cho ca CLI va GUI:

1. App gui `SECURE_AES_IOCTL_SET_CONFIG` de truyen mode, key length, key, IV.
2. App `write()` noi dung file vao driver.
3. App goi `SECURE_AES_IOCTL_PROCESS`.
4. App lay `SECURE_AES_IOCTL_GET_STATUS` de biet kich thuoc output.
5. App `read()` du lieu da xu ly va ghi ra file dich.

Moi `open()` tao mot session rieng. Driver luu context theo file descriptor de de giai thich trong bao cao mon hoc.

## Gioi han hien tai

- Driver hien tai xu ly theo kieu **in-memory one shot**.
- Gioi han mac dinh la **8 MiB** moi request (`SECURE_AES_MAX_BUFFER_SIZE`).
- AES-CBC + PKCS#7 chi dam bao tinh bi mat, khong dam bao xac thuc/toan ven. Trong thuc te nen bo sung HMAC hoac AEAD nhu AES-GCM.
- Xoa folder trong GUI la xoa de quy, da co hop thoai xac nhan nhung van can su dung can than.

## Ghi chu bao cao

Neu can thuyet trinh hoac viet bao cao, ban co the nhan manh cac diem sau:

- Driver dung char device `/dev/secure_aes`.
- Giao tiep user-space / kernel-space qua `ioctl + read/write`.
- AES nam trong kernel driver, user-space chi dieu khien va quan ly file.
- PKCS#7 padding duoc kiem tra khi decrypt.
- Session duoc tach theo file descriptor.
- GUI Zorin OS chi la frontend, khong thay the driver.