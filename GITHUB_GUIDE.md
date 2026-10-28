# 🚀 GITHUB_GUIDE.md — Đưa project lên GitHub từ A đến Z

> Hướng dẫn này dành cho chính bạn — 3 cách, chọn 1 cách bạn thích nhất.
> Repo nên đặt tên: **`esp8266-pomodoro-clock`** (Public).

---

## Bước 0 — Chuẩn bị (làm 1 lần)

1. Tạo tài khoản tại [github.com](https://github.com) nếu chưa có.
2. Quyết định tên repo: `esp8266-pomodoro-clock`.
3. Thư mục project trên máy bạn hiện nằm ở:
   `C:\Users\Admin\Documents\PlatformIO\Projects\promo`
   → bạn sẽ dùng nội dung thư mục **`esp8266-pomodoro-clock`** trong file zip đã tải về để thay thế/bổ sung vào đó (README.md, LICENSE, .gitignore, .gitattributes, docs/wiring.png, GITHUB_GUIDE.md). File `src/main.cpp` trong zip đã đổi Wi-Fi AP thành `TEST PROMO` — nếu muốn giữ bản cũ của bạn thì giữ nguyên `main.cpp` cũ cũng được.

---

## Cách 1 — GitHub Desktop (dễ nhất, không gõ lệnh) ⭐

1. Tải **[GitHub Desktop](https://desktop.github.com/)** → cài → đăng nhập tài khoản GitHub.
2. Tạo repo trên web trước (trống):
   - Vào github.com → nút **`+`** góc phải trên → **New repository**.
   - Repository name: `esp8266-pomodoro-clock` · chọn **Public** · **KHÔNG** tích "Add a README" (vì mình đã có sẵn) → **Create repository**.
3. Quay lại GitHub Desktop:
   - **File → Add local repository…** → chọn thư mục `esp8266-pomodoro-clock` (hoặc thư mục `promo` của bạn sau khi đã chép các file mới vào).
   - Nếu nó báo "not a repository" → chọn **Create a repository here** nhưng **bỏ qua** bước tạo trên web (dùng 1 trong 2, không cần cả hai).
   - Kiểm tra danh sách file sẽ commit (sẽ thấy `src/main.cpp`, `platformio.ini`, `README.md`, `LICENSE`, `.gitignore`, `docs/wiring.png`… — thư mục `.pio/` đã bị `.gitignore` chặn, đúng rồi!).
   - Gõ tiêu đề commit: `First commit: HANHGRIN ESP8266 Pomodoro Clock` → **Commit to main**.
   - Nút **Publish repository** trên thanh trên → giữ tên `esp8266-pomodoro-clock` → bỏ tick "Keep this code private" (muốn Public) → **Publish**.
4. Xong! Vào `github.com/<tên-bạn>/esp8266-pomodoro-clock` kiểm tra.

---

## Cách 2 — Lệnh Git A-Z (PowerShell trên Windows) ⌨️

Mở **PowerShell** trong thư mục project (chuột phải thư mục → "Open in Terminal", hoặc dùng lệnh `cd`):

```powershell
# 1. Tạo repo trống trên github.com trước (xem Cách 1 bước 2, KHÔNG tạo README)

# 2. Vào thư mục project
cd "C:\Users\Admin\Documents\PlatformIO\Projects\promo"

# 3. Khởi tạo git + chào súng
git init
git branch -M main

# 4. Báo danh cho git (làm 1 lần trên máy)
git config --global user.name  "Hanh Grin"
git config --global user.email "email-cua-ban@example.com"

# 5. Thêm toàn bộ file (tự động bỏ .pio/ nhờ .gitignore)
git add .
git status          # kiểm tra: KHÔNG được thấy .pio/ trong danh sách

# 6. Commit lần đầu
git commit -m "First commit: HANHGRIN ESP8266 Pomodoro Clock"

# 7. Nối với repo trên GitHub (thay <tên-bạn>)
git remote add origin https://github.com/<tên-bạn>/esp8266-pomodoro-clock.git

# 8. Đẩy lên
git push -u origin main
```

> 🔐 **Khi push lần đầu**, Windows sẽ hiện cửa sổ đăng nhập GitHub (Git Credential Manager) — đăng nhập bằng trình duyệt là xong, git tự nhớ. Nếu không hiện, tạo **Personal Access Token** tại github.com → Settings → Developer settings → Personal access tokens → dùng token thay mật khẩu.

**Các lần sau, chỉ cần 3 lệnh:**

```powershell
git add .
git commit -m "Mô tả thay đổi"
git push
```

---

## Cách 3 — Kéo–thả qua web (nhanh nhất, không cần cài gì) 🌐

1. Tạo repo trống như Cách 1 bước 2.
2. Vào trang repo → nút **uploading an existing file**.
3. Kéo–thả toàn bộ file + thư mục (`src`, `docs`, `platformio.ini`, `README.md`, `LICENSE`, `.gitignore`, `.gitattributes`…).
   - Lưu ý: cách này **không upload được thư mục rỗng** (`include/lib/test`) — không sao, PlatformIO tự tạo khi build.
4. Gõ **Commit changes** → xong.

> ⚠️ Cách 3 không có lịch sử git và không tiện cập nhật về sau — nên dùng Cách 1 hoặc 2.

---

## Sau khi push — "make up" cho repo 💅

1. **Thêm ảnh thật:** chụp máy, chụp web dashboard → đặt vào `docs/` → sửa README chỗ "Photos coming soon" (phần 📸 nếu bạn thêm) hoặc thay hình mẫu bằng:
   ```markdown
   ![Máy thật](docs/device.jpg)
   ```
2. **Thêm Topics** (giúp người khác tìm thấy): vào trang repo → ⚙️ bên phải (About) → thêm: `esp8266`, `nodemcu`, `pomodoro`, `oled`, `tetris`, `dht22`, `arduino`, `platformio`, `clock`.
3. **Tạo Release** (tùy chọn): Releases → Draft a new release → tag `v1.0` → đính kèm `firmware.bin` để người khác nạp trực tiếp không cần build.
4. **Kiểm tra badge license** trên README hiện xanh là đạt.

---

## Lỗi hay gặp & cách chữa 🚑

| Lỗi | Nguyên nhân | Chữa |
|---|---|---|
| `error: Repository not found` khi push | Sai URL remote / chưa tạo repo trên web | `git remote -v` kiểm tra; sửa bằng `git remote set-url origin <url>` |
| `Updates were rejected` | Repo trên web có file (README tạo sẵn) | `git pull origin main --allow-unrelated-histories` rồi `git push` lại |
| Yêu cầu mật khẩu nhưng nhập đúng vẫn bị chặn | GitHub đã bỏ đăng nhập bằng mật khẩu | Dùng cửa sổ Credential Manager, hoặc Personal Access Token |
| Push lên được nhưng `.pio/` cũng lên theo | Thiếu `.gitignore` hoặc thêm sau khi đã commit | `git rm -r --cached .pio` → `git add .` → commit lại |
| Tên hiển thị commit sai | Chưa config user.name/email | Xem lại bước 4 Cách 2 |

---

*Chúc repo của bạn nhiều sao ⭐ — Hanh Grin*
