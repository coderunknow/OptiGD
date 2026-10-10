# Hướng dẫn: Workflow tự động tạo GitHub Release (`release.yml`)

File workflow: [`.github/workflows/release.yml`](../.github/workflows/release.yml)

## 1. Workflow này làm gì?

Mỗi khi bạn push một tag có dạng `v*` (vd: `v0.1.0-beta1`, `v1.2.2`, `v1.1.0-rc1`), GitHub Actions sẽ:

| Bước | Job | Nội dung |
| :--- | :--- | :--- |
| 1 | `check-version` | Kiểm tra tag và `version` trong `mod.json`. Tag `v0.1.0-beta1` khớp `mod.json` `v0.1.0-beta.1` (dấu chấm trước số prerelease là cú pháp của Geode; tag Git không có dấu chấm). Lệch → **chỉ cảnh báo**, không làm fail workflow. Tag không hợp lệ hoặc `mod.json` hỏng → fail rõ ràng để không phát hành nhầm. |
| 2 | `build` | Build đúng một file `.geode` cho từng target **Win64, Android32, Android64** và upload thành artifact riêng. |
| 3 | `release` | Tải đủ ba artifact, kiểm tra không thiếu/thừa file, đổi tên theo target rồi tạo GitHub Release bằng `softprops/action-gh-release (pinned to a commit SHA)`. |

Release tạo ra có:

- **Tiêu đề** = tên tag (vd: `v1.2.2`).
- **Release Notes** tự sinh từ commits (`generate_release_notes: true`) **cộng thêm** section tương ứng trong `CHANGELOG.md` nếu section đó tồn tại.
- **Assets chính xác gồm ba file**:
  - `coderunknow.optigd-Win64.geode`
  - `coderunknow.optigd-Android32.geode`
  - `coderunknow.optigd-Android64.geode`
- Tag chứa `-alpha`, `-beta` hoặc `-rc` theo dạng hợp lệ (vd: `v1.1.0-rc1`) → tự đánh dấu **Prerelease**.
- `draft: false` → publish ngay, không cần bấm tay.

`release.yml` không dùng `combine: true`: tùy chọn đó tạo một package đa nền tảng duy nhất, trong khi Release này cần ba asset riêng theo target.

## 2. Cách cài đặt vào repository

1. Merge file `.github/workflows/release.yml` vào nhánh `main` trước khi push tag (workflow tag phải tồn tại trên nhánh mặc định).
2. Kiểm tra quyền Actions (chỉ cần làm 1 lần):
   - Vào **Settings → Actions → General → Workflow permissions**.
   - Chọn **Read and write permissions**, hoặc bảo đảm policy của organization không ghi đè quyền `contents: write` đã khai báo trong job `release`.
   - **Allow GitHub Actions to create and approve pull requests** không cần bật; workflow không tạo PR.

## 3. Cách test thử

### Bước 1 — Test an toàn với tag thử nghiệm

Chạy sau khi workflow mới đã được merge vào `main`:

```bash
git checkout main
git pull
git tag -a v0.0.1-test.2 -m "E2E test release workflow"
git push origin v0.0.1-test.2
```

Tag test này cố ý lệch với `mod.json` (`v0.1.0-beta.1`), nên mismatch là cảnh báo chứ không làm fail build.

### Bước 2 — Quan sát

- Tab **Actions** → workflow **Release** chạy `check-version`, ba job `build` và `release`.
- Job `check-version` phải hiển thị cảnh báo, nhưng vẫn thành công:

  ```text
  Tag 0.0.1-test.2 != mod.json version v0.1.0-beta.1
  ```

- Tab **Releases** → Release `v0.0.1-test.2` xuất hiện.
- Body phải có dòng cảnh báo với đầy đủ giá trị:

  ```text
  tag `0.0.1-test.2` không khớp version trong `mod.json` (`v0.1.0-beta.1`)
  ```

- Assets phải có đủ và chỉ có ba file `.geode` theo đúng tên target ở trên.

### Bước 3 — Dọn dẹp tag test và Release

```bash
gh release delete v0.0.1-test.2 --yes
git push origin :refs/tags/v0.0.1-test.2
git tag -d v0.0.1-test.2
```

Kiểm tra lại:

```bash
gh release list
git ls-remote --tags origin
```

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
| Workflow không chạy khi push tag | Kiểm tra tag đã tồn tại trên remote bằng `git ls-remote --tags origin`, tag phải bắt đầu bằng chữ thường `v`, và workflow phải có trên `main` trước khi push tag. |
| Lỗi tag không hợp lệ | `release.yml` chấp nhận dạng SemVer như `v0.1.0-beta1` hoặc `v1.1.0-rc1`; tag sai sẽ fail ở `check-version` thay vì tạo Release mơ hồ. |
| Cảnh báo mismatch làm job fail | Đây không phải hành vi mong muốn. Mismatch hợp lệ chỉ tạo annotation và Release vẫn tiếp tục; chỉ lỗi metadata/tag format mới làm fail. |
| Thiếu hoặc thừa file `.geode` | Mỗi matrix job phải tạo đúng một file. Các bước verify và collect sẽ fail rõ ràng nếu có 0 hoặc nhiều hơn 1 file cho bất kỳ target nào. |
| Lỗi `403` / `Resource not accessible by integration` ở job release | Vào **Settings → Actions → General → Workflow permissions**, chọn **Read and write permissions** và kiểm tra policy organization/repository có ghi đè không. |
| Muốn release lại tag đã tồn tại | Xóa Release/tag cũ rồi push lại, hoặc dùng một tag mới. |


## 3. Ghi chú phiên bản v0.1.0-beta1 (thêm bởi PR này)

- `mod.json` dùng `"version": "v0.1.0-beta.1"` vì bộ phân tích phiên bản của Geode yêu cầu dấu chấm trước số prerelease. Tag Git và tên Release là `v0.1.0-beta1`. `release.yml` so sánh hai dạng sau khi chuẩn hóa.
- `CMakeLists.txt` dùng `0.1.0` (chỉ số).
- SDK, CLI, bindings và các action được ghim bằng commit SHA trong `build.yml` và `release.yml`.
- **Workflow này chưa được chạy cho tag này.** Không tạo tag, không tạo Release trong PR này.
