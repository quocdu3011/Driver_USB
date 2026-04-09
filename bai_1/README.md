# Secure File Manager backed by a Linux AES driver

Du an nay da duoc chuyen thanh **chuong trinh quan ly file co bao mat** dung theo de tai:

- quan ly file trong mot khu luu tru rieng;
- tao file, doc file, sua file, xoa file;
- liet ke tat ca secure file dang co;
- du lieu **luon duoc ma hoa khi luu** vao dia;
- du lieu duoc **giai ma khi doc**;
- viec ma hoa/giai ma AES duoc thuc hien boi **driver kernel** su dung **Kernel Crypto API**.

## Mo hinh moi

He thong gom 2 phan:

1. `driver/secure_aes.ko`
   - character device `/dev/secure_aes`
   - thuc hien AES-CBC trong kernel
   - dung Linux Kernel Crypto API (`cbc(aes)`)

2. `app/`
   - CLI va GUI de quan ly secure file
   - user-space khong tu cai dat AES
   - moi lan luu/doc file deu goi driver bang `ioctl + write + read`

## Tinh nang hien tai

- Tao secure file moi trong kho luu tru rieng.
- Mo secure file va giai ma noi dung de doc/sua.
- Luu lai noi dung da sua duoi dang da ma hoa.
- Xoa secure file khoi kho luu tru.
- Hien danh sach tat ca secure file dang co.
- GUI GTK3 co editor text + danh sach secure file.
- CLI ho tro `create`, `update`, `read`, `delete`, `list`.
- Moi lan luu se sinh IV ngau nhien moi va ghi cung file ma hoa.

## Noi luu tru rieng

Mac dinh, secure file duoc luu trong:

```text
~/.secure_file_manager_storage
```

Moi file trong kho duoc luu duoi dang:

```text
<ten_file>.saes
```

Noi dung tren dia khong phai plaintext. File luu tru gom:

- secure file header (magic + IV)
- du lieu ciphertext da ma hoa qua driver

## Cau truc thu muc

```text
bai_1/
|-- app/
|   |-- main.c                  # CLI CRUD/list
|   |-- gui_main.c              # GUI quan ly secure file
|   |-- secure_file_service.c   # logic secure storage + goi driver AES
|   |-- driver_client.c         # giao tiep truc tiep voi /dev/secure_aes
|   |-- file_io.c
|   `-- hex_utils.c
|-- driver/
|   |-- secure_aes_driver.c     # char device driver
|   |-- aes_core.c              # AES-CBC bang Kernel Crypto API
|   |-- pkcs7.c                 # PKCS#7 padding
|   `-- ioctl_defs.h
|-- scripts/
|-- tests/
|   |-- sample.txt
|   `-- verify.sh
`-- README.md
```

## Build

```bash
cd ~/bai_1
bash scripts/build_all.sh
```

Ket qua:

- `driver/secure_aes.ko`
- `app/secure_file_app`
- `app/secure_file_gui`

## Nap driver

```bash
bash scripts/load_driver.sh
```

Neu `/dev/secure_aes` chua ton tai, CLI/GUI se khong the doc va luu secure file.

## CLI

### Liet ke secure file

```bash
./app/secure_file_app list
```

### Tao secure file moi tu mot file plaintext

```bash
./app/secure_file_app create note.txt \
  --key 00112233445566778899aabbccddeeff \
  --from-file tests/sample.txt
```

### Tao secure file moi tu text truc tiep

```bash
./app/secure_file_app create todo.txt \
  --key 00112233445566778899aabbccddeeff \
  --text "Noi dung bi mat"
```

### Doc secure file

```bash
./app/secure_file_app read note.txt \
  --key 00112233445566778899aabbccddeeff \
  --output /tmp/note.txt
```

### Cap nhat secure file

```bash
./app/secure_file_app update note.txt \
  --key 00112233445566778899aabbccddeeff \
  --text "Noi dung moi da duoc sua"
```

### Xoa secure file

```bash
./app/secure_file_app delete note.txt
```

### Dung kho luu tru tuy chon

```bash
./app/secure_file_app list --storage /tmp/demo_secure_store
```

## GUI

Chay GUI:

```bash
bash scripts/run_gui.sh
```

GUI moi gom:

- panel trai: danh sach secure file trong kho luu tru rieng
- panel phai: file name, AES key, editor noi dung, log thao tac

Luong su dung:

1. Nhap hoac giu nguyen AES key.
2. Bam `New File` de tao file moi, hoac chon mot file va bam `Open`.
3. Sua noi dung trong editor.
4. Bam `Save Encrypted` de luu vao secure storage.
5. Bam `Delete` neu muon xoa file da chon.

## Kiem thu

Test end-to-end:

```bash
bash tests/verify.sh
```

Test nay se kiem tra day du:

- create
- list
- read/decrypt
- update
- delete
- xac nhan file tren dia khong phai plaintext

## Luong du lieu khi luu/doc

### Luu file

1. App nhan plaintext tu editor hoac CLI.
2. App sinh IV ngau nhien.
3. App goi driver `/dev/secure_aes` de AES encrypt.
4. App ghi header + IV + ciphertext vao kho rieng.

### Doc file

1. App doc encrypted file trong kho rieng.
2. App tach header va IV.
3. App goi driver `/dev/secure_aes` de AES decrypt.
4. App tra plaintext cho CLI hoac hien len GUI.

## Ghi chu

- Khoa AES van do nguoi dung cung cap.
- Plaintext khong duoc luu ra kho rieng.
- Driver gioi han kich thuoc du lieu xu ly toi da 8 MiB moi request.
- Secure storage hien tai phu hop tot cho cac file text; GUI huong den bien tap noi dung text.
