# Hướng dẫn: Workflow tự động tạo GitHub Release (`release.yml`)

File workflow: [`.github/workflows/release.yml`](../.github/workflows/release.yml)

## 1. Workflow này làm gì?

Mỗi khi bạn push một tag có dạng `v*` (vd: `v1.0.0`, `v1.2.2`), GitHub Actions sẽ:

| Bước | Job | Nội dung |
| :--- | :--- | :--- |
| 1 | `check-version` | So sánh tag với `version` trong `mod.json`. Lệch → **chỉ cảnh báo** (annotation trong Actions + ghi chú trong Release body), không làm fail workflow. |
| 2 | `build` | Build file `.geode` cho **Win64, Android32, Android64** bằng `geode-sdk/build-geode-mod` (matrix song song, giống `build.yml`). |
| 3 | `release` | Gom các `.geode` → tạo **GitHub Release** bằng `softprops/action-gh-release@v2`. |

Release tạo ra có:

- **Tiêu đề** = tên tag (vd: `v1.2.2`).
- **Release Notes** tự sinh từ commits (`generate_release_notes: true`) **cộng thêm** section tương ứng trong `CHANGELOG.md`.
- **Assets** = các file `.geode` để người dùng cài trực tiếp qua Geode.
- Tag chứa `-alpha` / `-beta` / `-rc` (vd: `v1.1.0-rc1`) → tự đánh dấu **Prerelease**; tag thường → release chính thức.
- `draft: false` → **publish ngay**, không cần bấm tay.

## 2. Cách cài đặt vào repository

1. Merge file `.github/workflows/release.yml` vào nhánh `main` (xem PR của thay đổi này).
2. Kiểm tra quyền Actions (chỉ cần làm 1 lần):
   - Vào **Settings → Actions → General → Workflow permissions**.
   - Chọn **"Read and write permissions"** (hoặc giữ mặc định "Read" vẫn được, vì workflow đã khai báo `contents: write` tường minh — chính dòng này chống lỗi permission 403).
   - Đảm bảo **"Allow GitHub Actions to create and approve pull requests"** KHÔNG cần bật (workflow không tạo PR).

## 3. Cách test thử

### Bước 1 — Test an toàn với tag thử nghiệm (không ảnh hưởng bản chính thức)

```bash
git checkout main
git pull
git tag -a v0.0.1-test.1 -m "Test release workflow"
git push origin v0.0.1-test.1
```

### Bước 2 — Quan sát

- Tab **Actions** trên GitHub → workflow **Release** chạy 3 job (`check-version`, `build` ×3, `release`).
- Tab **Releases** → xuất hiện release `v0.0.1-test.1` kèm notes tự sinh và 3 file `.geode`.
- Vì tag lệch với `mod.json` (`1.0.0`), bạn sẽ thấy **cảnh báo vàng** trong log job `check-version` và dòng ghi chú ⚠️ trong body Release — đây là hành vi mong muốn.

### Bước 3 — Dọn dẹp tag test

```bash
git push origin :refs/tags/v0.0.1-test.1   # xóa tag trên GitHub
git tag -d v0.0.1-test.1                   # xóa tag local
```

(Xóa luôn release `v0.0.1-test.1` trong tab Releases nếu muốn.)

### Bước 4 — Release thật

```bash
# 1. Cập nhật version trong mod.json + CHANGELOG.md, commit vào main
# 2. Tạo tag đúng version và push
git tag -a v1.1.0 -m "Release v1.1.0"
git push origin v1.1.0
```

## 4. Xử lý sự cố nhanh

| Triệu chứng | Nguyên nhân / cách sửa |
| :--- | :--- |
| Lỗi `403` / `Resource not accessible by integration` | Workflow permissions của repo đang để Read-only và thiếu khai báo quyền. File này đã có `permissions: contents: write` — kiểm tra xem Settings → Actions có đang bị policy tổ chức (org) ghi đè không. |
| Workflow không chạy khi push tag | Tag phải khớp `v*` (`v1.0.0` ✅, `1.0.0` ❌) và file workflow phải tồn tại **trên nhánh mặc định (main)** trước khi push tag. |
| `fail_on_unmatched_files` báo lỗi | Không tìm thấy file `.geode` nào ở bước combine — xem log job `build` của từng target. |
| Muốn release lại tag đã tồn tại | Xóa tag + release cũ rồi push lại, hoặc đổi tag mới. |
