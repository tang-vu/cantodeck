# CantoDeck — hát qua máy Windows

**Đây là bản phát triển, chưa đạt để hát trực tiếp qua loa ngoài.** Người dùng đã phản hồi giọng bị trễ và chất lượng kém tự nhiên. Kiểm thử phần mềm đã chạy nhưng không thay thế đánh giá nghe hát thực tế. Xem [trạng thái kiểm thử](docs/STATUS.md) và [lộ trình](docs/ROADMAP.md).

## Mở ứng dụng

Chế độ **Giảm trễ** giữ mức đệm nhỏ lúc kết nối. Nếu thực sự thiếu mẫu mic, ứng dụng
có thể tăng mức đệm dự phòng tối đa hai bước để phục hồi; xem `target` trong chẩn đoán.
Mức này không tự giảm cho đến khi kết nối lại. Đây là đánh đổi thêm độ trễ để giảm
gián đoạn khi máy giao dữ liệu không đều, không bảo đảm hết giật hay hát không trễ.

Danh sách tối đa 128 bài WAV và độ lệch lời được lưu trong phiên làm việc trên máy. Mở lại ứng dụng chỉ khôi phục danh sách: bạn chọn bài để tải, không tự phát nhạc hay bật mic. **Xóa DS** không xóa file và không dừng bài đang tải/phát. Đường dẫn danh sách không được đưa vào preset giọng xuất ra; file session vẫn chứa đường dẫn cục bộ, nên kiểm tra trước khi chia sẻ.

Lời TXT có thể cuộn trong khung chính và cửa sổ lời; dòng chứa dấu ngoặc vuông trong TXT được giữ nguyên. File LRC/TXT tối đa 256 KiB. LRC hỗ trợ timestamp phút:giây với phần thập phân đến ba chữ số và thẻ `[offset:+500]` (mili giây; số dương làm lời sớm hơn). Offset trong file được cộng với thanh chỉnh tay, không thay đổi thanh này hay độ trễ âm thanh. Nhận số nguyên tối đa sáu chữ số trong khoảng ±600.000 ms; thẻ sai bị bỏ qua, thẻ hợp lệ cuối cùng được dùng. Mở file lời khác sẽ đặt lại offset từ file; TXT không diễn giải thẻ.

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

Bản mới có **Giọng gốc** (mặc định bật): giữ gain nhưng bỏ màu giọng và hiệu ứng. Tắt tùy chọn này khi muốn nghe Echo/Room. Trong Nâng cao, **Native** là backend WASAPI thử nghiệm; chọn rồi nhấn Kết nối để áp dụng. Chưa xác nhận nó giải quyết độ trễ trên bộ mic/loa của bạn. Nút **Đo trễ · phát thử** chỉ phát khi bạn chủ động nhấn; tạm dừng YouTube trước khi đo. Phép đo đã được kiểm thử bằng tín hiệu tổng hợp, chưa xác minh trên thiết bị thật. Xem [chi tiết thay đổi và giới hạn](docs/AUDIO_REWORK.md).

Nếu giọng bị trễ: mở **Nâng cao**, bật **Giảm trễ**, chọn **128 samples**, rồi nhấn **Kết nối** và chủ động bật nghe mic lại. Đây là WASAPI shared low-latency, vẫn cho phép ứng dụng khác phát nhạc. Thiết bị quyết định buffer thực; xem chẩn đoán. Nếu lách tách, tăng buffer hoặc tắt Giảm trễ. Tắt Echo/Room lúc so sánh để tránh nhầm tiếng echo chủ ý với độ trễ giọng trực tiếp.

Với DGM20, thử cắm tai nghe có dây vào cổng tai nghe của mic và chọn **Headphones (DGM20 USB Microphone)**. Lượt mở thử im lặng trên máy này nhận được 128 mẫu cho cả input/output ở 48 kHz; đầu ra Realtek vẫn dùng 480 mẫu trong lượt thử. Đây là số liệu buffer, chưa phải đo độ trễ từ mic đến tai nghe. Nếu nghe hai đường giọng đồng thời, kiểm tra tính năng nghe trực tiếp của phần cứng; nó khác đường hiệu ứng qua máy.

Có thể bật YouTube trong trình duyệt rồi nghe mic qua CantoDeck. Windows trộn âm thanh hai ứng dụng. Fader Music của CantoDeck không điều khiển YouTube và bản thu không chứa âm thanh trình duyệt.

Loa Bluetooth phải được ghép đôi trong Windows trước. Chọn endpoint nếu Windows cung cấp. Chất lượng, độ trễ và chuyển profile chưa được thử; nên dùng dây/USB để hát trực tiếp. Không có lời hứa “không trễ”.

Nếu hú, nhấn **TẮT TẤT CẢ**, giảm âm lượng loa, đưa mic xa và tránh chĩa vào loa; ưu tiên tai nghe. Bộ chặn đỉnh số không đảm bảo chống hú hay mức âm thanh an toàn ngoài phòng.

## Khi không có tiếng

Room đã có thêm tầng khuếch tán đuôi vang; không đặt bộ trễ đó lên nhánh giọng trực tiếp. Đây vẫn là reverb mono cơ bản, chưa có đánh giá nghe thực tế. Muốn nghe Echo/Room, tắt **Giọng gốc** và bật **Echo / Room**; tăng từng chút, nhất là khi dùng loa ngoài.

Trong **Nâng cao → EQ 3 bands**, mỗi dải có Frequency (tần số), Gain (tăng/giảm dB) và Q (Q càng cao, dải càng hẹp). Muốn nghe tác dụng, tắt **Giọng gốc** và bật **Vocal EQ**. Các dải mặc định 0 dB; đổi preset có sẵn sẽ đưa gain EQ về 0. Thông số được lưu cùng preset/session. Tăng dB có thể làm lớn tiếng nền hoặc gây hú; EQ này không tự chống hú và chưa được đánh giá nghe hát trên bộ loa của bạn.

Nếu mic nhỏ: bản mới có **Mic boost dB** ngay trên màn hình chính. Thử +6 dB, rồi +12 dB nếu cần; thanh Mic có thể tăng tới 8 lần. Tăng từng chút khi dùng tai nghe, vì gain phần mềm cũng tăng tiếng nền. Mức MIC hiển thị vẫn là tín hiệu thô trước boost; nhìn MASTER để thấy thay đổi sau xử lý. Master 0,5 giảm biên độ đầu ra một nửa. Có thể thử tắt Expander trong Nâng cao khi tín hiệu đầu vào quá nhỏ. CantoDeck không tự thay đổi âm lượng đầu vào Windows hay gain vật lý của mic.

- MIC không nhúc nhích: kiểm tra mic, dây, quyền Windows, channel 1/2 trong Nâng cao và gain phần cứng.
- MIC có tín hiệu nhưng MASTER không: kiểm tra nút nghe mic, MUTE ALL, Mic và Master.
- MASTER có tín hiệu nhưng không nghe: kiểm tra đúng loa, âm lượng Windows và âm lượng loa vật lý.
- Mất/rút thiết bị: nghe mic bị tắt; nhấn Quét thiết bị, chọn lại, Kết nối và chủ động bật nghe mic lại.
- Khi đã nhận lỗi thiết bị, mix và nhánh thu dry/wet cùng im; giao diện sẽ dừng và đóng phiên thu đang chạy, chờ xong thao tác tải nhạc nếu có. Kiểm thử báo lỗi bằng phần mềm đã chạy; thao tác rút/cắm thiết bị thật vẫn cần xác minh.
- Lách tách: thử buffer 1024 trong Nâng cao, kết nối lại. Buffer lớn có thể tăng độ trễ.
- Thu bị lỗi: nhấn Dừng thu để đóng file; kiểm tra dung lượng đĩa, quyền thư mục và chẩn đoán. Không thu quá ba giờ mỗi file.

Trong Nâng cao có tốc độ/buffer thực, mức tải callback, bộ đệm đổi tốc độ, số lỗi và nút lưu chẩn đoán. Các con số buffer không phải độ trễ đo từ mic đến loa.

Chỉ WAV mono/stereo. Nhạc được đọc từng khối ở luồng nền, không giữ cả bài trong RAM. Khi tua hoặc đọc không kịp, nhạc có thể tạm im và vị trí bài chờ dữ liệu; mic và thu âm vẫn tiếp tục. Lỗi đọc sẽ dừng nhạc và báo trên giao diện. File lớn/ổ đĩa chậm vẫn cần thử thực tế. Khi đang mở bài, nút tắt toàn bộ vẫn dùng được. Preset và thiết bị lưu trong `%APPDATA%\CantoDeck\session.json`; không tự kết nối hoặc tự bật nghe mic lần sau. Giao diện đã có VI/EN cho thao tác chính; thông số kỹ thuật/lỗi còn một phần tiếng Anh.
