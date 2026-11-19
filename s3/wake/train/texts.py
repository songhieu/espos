"""Texts for TTS data. Kinds:
  w1/w2/w3  wake word said 1/2/3 times (positives; the label end is the clip's speech end)
  req       requests said after the wake word (glued after a wake clip of the same voice)
  conf      confusable syllables and phrases (hard negatives)
  tone      'gâu' with another tone, said alone (kept out of training: an emphatic bark can sound like these)
  neg       ordinary sentences (negatives)
"""

WAKE = {
    "w1": ["Gâu.", "Gâu!", "Gâu?", "gâu", "Gâu!!", "GÂU!"],
    "w2": ["Gâu gâu.", "Gâu gâu!", "Gâu, gâu!", "Gâu gâu?", "gâu gâu", "Gâu! Gâu!"],
    "w3": ["Gâu gâu gâu!", "Gâu gâu gâu.", "Gâu, gâu, gâu!"],
}

REQ = [
    "mấy giờ rồi?", "đặt báo thức sáu giờ sáng mai.", "hôm nay trời thế nào?", "ngày mai có mưa không?",
    "bật nhạc đi.", "kể chuyện cười đi.", "nhắc tôi uống nước sau ba mươi phút.", "hôm nay là thứ mấy?",
    "tắt màn hình đi.", "to tiếng lên một chút.", "nhỏ tiếng thôi.", "đọc tin tức sáng nay.",
    "hẹn giờ năm phút.", "em ăn gì chưa?", "hôm nay âm lịch ngày bao nhiêu?", "giá vàng hôm nay bao nhiêu?",
    "một đô la bằng bao nhiêu tiền Việt?", "dịch giúp tôi câu này sang tiếng Anh.", "bây giờ ở Tokyo mấy giờ?",
    "còn bao nhiêu ngày nữa đến Tết?", "kể cho bé nghe một câu chuyện.", "hát một bài đi.", "chào buổi sáng!",
    "chúc ngủ ngon nhé.", "em có nhớ anh không?", "tắt báo thức đi.", "ngày mai mấy giờ mặt trời mọc?",
    "nhiệt độ ngoài trời bao nhiêu?", "có nên mang ô không?", "lịch hôm nay của tôi có gì?", "thêm sữa vào danh sách mua sắm.",
    "ghi chú: gọi cho mẹ lúc tám giờ.", "đọc lại ghi chú cho tôi.", "tính giúp tôi mười hai nhân mười bảy.",
    "từ đây đến Hà Nội bao xa?", "hôm nay có gì vui không?", "mở ảnh của tôi.", "chuyển sang chế độ ngủ.",
    "đừng làm phiền tôi một tiếng.", "pin còn bao nhiêu?", "kết nối wifi chưa?", "cập nhật phần mềm đi.",
    "nói tiếng Anh với tôi.", "đố vui đi.", "con thích ăn gì?", "đi dạo không?", "em tên là gì?",
    "hôm qua tôi ngủ mấy tiếng?", "đặt lời nhắc họp lúc hai giờ chiều.", "bật đèn phòng khách.",
    "tắt đèn đi.", "mấy giờ thì trời tối?", "tuần này có ngày lễ nào không?", "chơi trò chơi với tôi.",
    "cho tôi một lời khuyên.", "trời có lạnh không?", "nhớ uống thuốc lúc chín giờ tối.", "buồn quá.",
    "chán quá, nói chuyện với tôi đi.", "cảm ơn nhé.", "giỏi lắm!", "đi ngủ thôi.", "dậy đi!",
    "what time is it?", "set an alarm for seven.", "tell me a joke.", "what's the weather like?",
    "bao giờ thì mưa tạnh?", "kể về con chó của tôi.", "đếm từ một đến mười.", "hôm nay ngày mấy?",
    "tỷ giá euro hôm nay?", "có tin gì mới về bóng đá không?", "Việt Nam đá với Thái Lan khi nào?",
    "cách nấu phở bò?", "luộc trứng mấy phút?", "nhắc tôi tưới cây vào chiều nay.", "hẹn giờ mười phút để nấu cơm.",
    "đọc bài thơ đi.", "dạy tôi một từ tiếng Anh mới.", "ngày này năm xưa có gì?", "em có buồn không?",
]

CONF = [
    # single syllables that differ from gâu in one sound
    "Câu.", "Cau.", "Cao.", "Đâu?", "Sau.", "Sâu.", "Nâu.", "Ngâu.", "Dâu.", "Râu.", "Trâu.", "Lâu.", "Khâu.",
    "Bâu.", "Tâu.", "Mau!", "Màu.", "Rau.", "Gạo.", "Gào.", "Ngao.", "Âu.", "Au!", "Gà.", "Gõ.", "Gu.", "Gân.",
    "Gần.", "Gầy.", "Gay.", "Cầu.", "Giàu.", "Dao.", "Hâu.", "Thâu.", "Châu.", "Phâu.", "Vâu.", "Kêu.", "Cứu!",
    "Câu câu.", "Đâu đâu?", "Lâu lâu.", "Mau mau!", "Ngâu ngâu.", "Cau cau.", "Gà gà.", "Trâu trâu.", "Au au!",
    "Go!", "Wow!", "Ciao!", "How?", "Now!", "Cow.", "Gown.", "Gaul.", "Ouch!", "Ok Google.", "Alexa.",
    # phrases with those syllables and with gâu's tone variants
    "con gấu bông", "gấu trúc", "chú gấu nâu", "cái gầu múc nước", "gầu dai", "câu cá ngoài sông", "câu hỏi hay quá",
    "ở đâu vậy?", "đâu có đâu", "lát nữa rồi sau", "màu nâu đất", "mưa ngâu tháng bảy", "quả dâu tây", "con trâu cày ruộng",
    "bao lâu rồi?", "may khâu vá", "cây cau trước nhà", "rau muống luộc", "cao quá trời", "gạo nếp cái hoa vàng",
    "gào khóc ầm ĩ", "sâu róm", "ngồi đằng sau", "râu tóc bạc phơ", "lâu lắm rồi", "trâu bò đầy đồng", "con gà gáy sáng",
    "gõ cửa đi", "gu thẩm mỹ", "Châu Âu", "giàu sang phú quý", "cầu thang", "kêu cứu", "con dao sắc",
    "con chó nhà tôi", "chó con dễ thương", "cún cưng ơi", "meo meo", "ò ó o", "ụt ịt", "quạc quạc", "ụm bò",
    "gâm gừ", "cẩu thả", "gậy gộc", "gàu nước", "ngao du", "cao gầy", "gấu ơi", "đâu rồi?", "sao thế?",
]

TONE = ["Gấu.", "Gầu.", "Gẫu.", "Gậu.", "Gẩu.", "Gấu!", "Gấu gấu!", "Gầu gầu."]

NEG = [
    "Hôm nay trời nắng đẹp, chúng ta đi chơi nhé.", "Tôi đang làm việc, lát nữa nói chuyện sau.",
    "Bản tin thời sự lúc mười chín giờ.", "Giá xăng tăng nhẹ từ chiều nay.", "Mẹ ơi, con đói bụng rồi.",
    "Anh có biết đường đến bưu điện không?", "Cuối tuần này cả nhà về quê thăm ông bà.",
    "Đội tuyển Việt Nam đã giành chiến thắng.", "Hãy nhớ đóng cửa sổ trước khi đi ngủ.",
    "Cô giáo khen bạn Lan học rất chăm.", "Thời tiết miền Bắc chuyển lạnh vào cuối tuần.",
    "Chương trình khuyến mãi kéo dài đến hết tháng.", "Tối nay ăn cơm với cá kho nhé.",
    "Em đi học về rồi đây.", "Xin quý khách vui lòng thắt dây an toàn.", "Tắt bếp đi kẻo cháy nồi canh.",
    "Con mèo đang ngủ trên ghế sofa.", "Ngày mai tôi phải dậy sớm để đi họp.", "Bạn đã xem phim đó chưa?",
    "Ông ấy là một người rất tốt bụng.", "Chúng ta cần mua thêm rau và trứng.", "Điện thoại của ai đang reo vậy?",
]
