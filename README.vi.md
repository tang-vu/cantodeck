# CantoDeck — hát qua máy Windows

**Đây là bản phát triển, chưa đạt để hát trực tiếp qua loa ngoài.** Người dùng đã phản hồi giọng bị trễ và chất lượng kém tự nhiên. Kiểm thử phần mềm đã chạy nhưng không thay thế đánh giá nghe hát thực tế. Xem [trạng thái kiểm thử](docs/STATUS.md) và [lộ trình](docs/ROADMAP.md).

## Mở ứng dụng

Chạy `CantoDeck.exe` trong thư mục portable, hoặc từ mã nguồn:

```powershell
& .\build\CantoDeck_artefacts\Release\CantoDeck.exe
```

1. Cắm mic USB và tai nghe có dây vào laptop. Hoặc cắm mic vào sound card/interface USB, tai nghe/loa vào interface đó. Mic XLR thụ động cần preamp/interface phù hợp; phần mềm không cấp phantom power.
2. Mở CantoDeck, chọn mic và loa/tai nghe, nhấn **Kết nối**. Hạ âm lượng loa vật lý trước. Nói vào mic, kiểm tra mức MIC thay đổi; nếu không có tín hiệu, kiểm tra quyền Microphone cho ứng dụng desktop trong Windows Settings.
3. Nhấn **Bật / tắt nghe mic**. Tăng Mic/Master từ từ. Chọn Warm Karaoke hoặc chỉnh Echo/Room. Nếu interface bật direct monitoring, hãy tắt đường đó khi muốn nghe hiệu ứng từ máy để tránh nghe hai giọng.
4. Nhấn **Mở nhạc WAV**, rồi **Phát / Tạm dừng**. Có thể thêm các bài khác và chọn trong danh sách; hết bài sẽ dừng. Mở LRC cùng tên tự động, hoặc dùng **Mở LRC / TXT**. Cửa sổ lời hỗ trợ F11 toàn màn hình, Esc thoát toàn màn hình.
5. Nhấn **Thu / Dừng thu**, chọn tên WAV mới. Nhấn lại để đóng file đúng cách. Bật “Thu thêm giọng dry / wet” trước khi thu nếu cần ba file. Mix chứa nhạc WAV và giọng đang nghe; dry/wet vẫn thu mic khi tắt nghe mic. Nút **TẮT TẤT CẢ** tắt cả đầu ra và các stem, không kết thúc phiên thu.

Nút thử loa chỉ phát khi bạn nhấn, trong nửa giây ở mức nhỏ. Không thử loa trong lúc thu nếu không muốn tiếng thử nằm trong file.

## YouTube, loa Bluetooth và chống hú

Nếu giọng bị trễ: mở **Nâng cao**, bật **Giảm trễ**, chọn **128 samples**, rồi nhấn **Kết nối** và chủ động bật nghe mic lại. Đây là WASAPI shared low-latency, vẫn cho phép ứng dụng khác phát nhạc. Thiết bị quyết định buffer thực; xem chẩn đoán. Nếu lách tách, tăng buffer hoặc tắt Giảm trễ. Tắt Echo/Room lúc so sánh để tránh nhầm tiếng echo chủ ý với độ trễ giọng trực tiếp.

Với DGM20, thử cắm tai nghe có dây vào cổng tai nghe của mic và chọn **Headphones (DGM20 USB Microphone)**. Lượt mở thử im lặng trên máy này nhận được 128 mẫu cho cả input/output ở 48 kHz; đầu ra Realtek vẫn dùng 480 mẫu trong lượt thử. Đây là số liệu buffer, chưa phải đo độ trễ từ mic đến tai nghe. Nếu nghe hai đường giọng đồng thời, kiểm tra tính năng nghe trực tiếp của phần cứng; nó khác đường hiệu ứng qua máy.

Có thể bật YouTube trong trình duyệt rồi nghe mic qua CantoDeck. Windows trộn âm thanh hai ứng dụng. Fader Music của CantoDeck không điều khiển YouTube và bản thu không chứa âm thanh trình duyệt.

Loa Bluetooth phải được ghép đôi trong Windows trước. Chọn endpoint nếu Windows cung cấp. Chất lượng, độ trễ và chuyển profile chưa được thử; nên dùng dây/USB để hát trực tiếp. Không có lời hứa “không trễ”.

Nếu hú, nhấn **TẮT TẤT CẢ**, giảm âm lượng loa, đưa mic xa và tránh chĩa vào loa; ưu tiên tai nghe. Bộ chặn đỉnh số không đảm bảo chống hú hay mức âm thanh an toàn ngoài phòng.

## Khi không có tiếng

Nếu mic nhỏ: bản mới có **Mic boost dB** ngay trên màn hình chính. Thử +6 dB, rồi +12 dB nếu cần; thanh Mic có thể tăng tới 8 lần. Tăng từng chút khi dùng tai nghe, vì gain phần mềm cũng tăng tiếng nền. Mức MIC hiển thị vẫn là tín hiệu thô trước boost; nhìn MASTER để thấy thay đổi sau xử lý. Master 0,5 giảm biên độ đầu ra một nửa. Có thể thử tắt Expander trong Nâng cao khi tín hiệu đầu vào quá nhỏ. CantoDeck không tự thay đổi âm lượng đầu vào Windows hay gain vật lý của mic.

- MIC không nhúc nhích: kiểm tra mic, dây, quyền Windows, channel 1/2 trong Nâng cao và gain phần cứng.
- MIC có tín hiệu nhưng MASTER không: kiểm tra nút nghe mic, MUTE ALL, Mic và Master.
- MASTER có tín hiệu nhưng không nghe: kiểm tra đúng loa, âm lượng Windows và âm lượng loa vật lý.
- Mất/rút thiết bị: nghe mic bị tắt; nhấn Quét thiết bị, chọn lại, Kết nối và chủ động bật nghe mic lại.
- Lách tách: thử buffer 1024 trong Nâng cao, kết nối lại. Buffer lớn có thể tăng độ trễ.
- Thu bị lỗi: nhấn Dừng thu để đóng file; kiểm tra dung lượng đĩa, quyền thư mục và chẩn đoán. Không thu quá ba giờ mỗi file.

Trong Nâng cao có tốc độ/buffer thực, mức tải callback, bộ đệm đổi tốc độ, số lỗi và nút lưu chẩn đoán. Các con số buffer không phải độ trễ đo từ mic đến loa.

Chỉ WAV mono/stereo; file lớn cần nhiều RAM và được đọc bằng luồng riêng. Khi đang đọc bài, nút tắt toàn bộ vẫn dùng được. Preset và tên thiết bị lưu trong `%APPDATA%\CantoDeck\session.json`; không tự kết nối hoặc tự bật nghe mic lần sau. Giao diện đã có VI/EN cho thao tác chính; thông số kỹ thuật/lỗi còn một phần tiếng Anh.
