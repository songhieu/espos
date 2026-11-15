// Cities for the weather block (with coordinates, so no lookup is needed) and time zones.

export interface Place {
  name: string;
  lat: number;
  lon: number;
}

export const CITY_GROUPS: { label: string; cities: Place[] }[] = [
  {
    label: "Miền Bắc",
    cities: [
      { name: "Hà Nội", lat: 21.0285, lon: 105.8542 },
      { name: "Hải Phòng", lat: 20.8449, lon: 106.6881 },
      { name: "Hạ Long", lat: 20.9599, lon: 107.0425 },
      { name: "Bắc Ninh", lat: 21.1861, lon: 106.0763 },
      { name: "Bắc Giang", lat: 21.2731, lon: 106.1946 },
      { name: "Thái Nguyên", lat: 21.5942, lon: 105.8482 },
      { name: "Việt Trì", lat: 21.3227, lon: 105.402 },
      { name: "Vĩnh Yên", lat: 21.3089, lon: 105.6049 },
      { name: "Hải Dương", lat: 20.9373, lon: 106.3146 },
      { name: "Hưng Yên", lat: 20.6464, lon: 106.0511 },
      { name: "Thái Bình", lat: 20.4463, lon: 106.3366 },
      { name: "Nam Định", lat: 20.42, lon: 106.1683 },
      { name: "Ninh Bình", lat: 20.2506, lon: 105.9745 },
      { name: "Phủ Lý", lat: 20.5411, lon: 105.9139 },
      { name: "Hòa Bình", lat: 20.8171, lon: 105.3376 },
      { name: "Mộc Châu", lat: 20.8456, lon: 104.6416 },
      { name: "Sơn La", lat: 21.3256, lon: 103.9188 },
      { name: "Điện Biên Phủ", lat: 21.386, lon: 103.023 },
      { name: "Lai Châu", lat: 22.3964, lon: 103.4582 },
      { name: "Lào Cai", lat: 22.4856, lon: 103.9707 },
      { name: "Sa Pa", lat: 22.3364, lon: 103.8438 },
      { name: "Yên Bái", lat: 21.7229, lon: 104.9113 },
      { name: "Hà Giang", lat: 22.8233, lon: 104.9836 },
      { name: "Tuyên Quang", lat: 21.8236, lon: 105.214 },
      { name: "Cao Bằng", lat: 22.6657, lon: 106.2522 },
      { name: "Lạng Sơn", lat: 21.8537, lon: 106.7615 },
      { name: "Bắc Kạn", lat: 22.147, lon: 105.8348 },
    ],
  },
  {
    label: "Miền Trung & Tây Nguyên",
    cities: [
      { name: "Thanh Hóa", lat: 19.8067, lon: 105.7852 },
      { name: "Vinh", lat: 18.6796, lon: 105.6813 },
      { name: "Hà Tĩnh", lat: 18.3428, lon: 105.9057 },
      { name: "Đồng Hới", lat: 17.4689, lon: 106.6223 },
      { name: "Đông Hà", lat: 16.8163, lon: 107.1003 },
      { name: "Huế", lat: 16.4637, lon: 107.5909 },
      { name: "Đà Nẵng", lat: 16.0544, lon: 108.2022 },
      { name: "Hội An", lat: 15.8801, lon: 108.338 },
      { name: "Tam Kỳ", lat: 15.5736, lon: 108.474 },
      { name: "Quảng Ngãi", lat: 15.1205, lon: 108.7923 },
      { name: "Quy Nhơn", lat: 13.782, lon: 109.2196 },
      { name: "Tuy Hòa", lat: 13.0882, lon: 109.0929 },
      { name: "Nha Trang", lat: 12.2388, lon: 109.1967 },
      { name: "Phan Rang", lat: 11.564, lon: 108.9886 },
      { name: "Phan Thiết", lat: 10.9289, lon: 108.1021 },
      { name: "Kon Tum", lat: 14.3497, lon: 108.0005 },
      { name: "Pleiku", lat: 13.9833, lon: 108.0 },
      { name: "Buôn Ma Thuột", lat: 12.6667, lon: 108.05 },
      { name: "Gia Nghĩa", lat: 11.981, lon: 107.69 },
      { name: "Đà Lạt", lat: 11.9404, lon: 108.4583 },
    ],
  },
  {
    label: "Miền Nam",
    cities: [
      { name: "TP Hồ Chí Minh", lat: 10.7769, lon: 106.7009 },
      { name: "Thủ Dầu Một", lat: 10.9804, lon: 106.6519 },
      { name: "Biên Hòa", lat: 10.9574, lon: 106.8426 },
      { name: "Vũng Tàu", lat: 10.346, lon: 107.0843 },
      { name: "Đồng Xoài", lat: 11.5349, lon: 106.8832 },
      { name: "Tây Ninh", lat: 11.31, lon: 106.0983 },
      { name: "Tân An", lat: 10.5359, lon: 106.4137 },
      { name: "Mỹ Tho", lat: 10.36, lon: 106.36 },
      { name: "Bến Tre", lat: 10.2434, lon: 106.3756 },
      { name: "Vĩnh Long", lat: 10.2537, lon: 105.9722 },
      { name: "Trà Vinh", lat: 9.9347, lon: 106.3453 },
      { name: "Cao Lãnh", lat: 10.46, lon: 105.6329 },
      { name: "Long Xuyên", lat: 10.3864, lon: 105.4352 },
      { name: "Châu Đốc", lat: 10.7, lon: 105.1167 },
      { name: "Rạch Giá", lat: 10.0125, lon: 105.0809 },
      { name: "Phú Quốc", lat: 10.2899, lon: 103.984 },
      { name: "Cần Thơ", lat: 10.0452, lon: 105.7469 },
      { name: "Vị Thanh", lat: 9.7845, lon: 105.4701 },
      { name: "Sóc Trăng", lat: 9.6025, lon: 105.9739 },
      { name: "Bạc Liêu", lat: 9.2941, lon: 105.7278 },
      { name: "Cà Mau", lat: 9.1769, lon: 105.1524 },
      { name: "Côn Đảo", lat: 8.6833, lon: 106.6 },
    ],
  },
];

export const ALL_CITIES = CITY_GROUPS.flatMap((g) => g.cities);

/** Whole-hour offsets (plus India), in minutes, with places that use them. No daylight saving. */
export const TIME_ZONES: { min: number; label: string }[] = [
  { min: 420, label: "GMT+7 · Việt Nam, Thái Lan, Lào, Campuchia, Jakarta" },
  { min: 480, label: "GMT+8 · Singapore, Malaysia, Philippines, Trung Quốc, Đài Loan, Hồng Kông, Perth" },
  { min: 540, label: "GMT+9 · Nhật Bản, Hàn Quốc" },
  { min: 600, label: "GMT+10 · Brisbane, Guam" },
  { min: 720, label: "GMT+12 · New Zealand (mùa đông)" },
  { min: 330, label: "GMT+5:30 · Ấn Độ" },
  { min: 240, label: "GMT+4 · Dubai" },
  { min: 180, label: "GMT+3 · Moskva, Ả Rập Xê Út, Thổ Nhĩ Kỳ" },
  { min: 120, label: "GMT+2 · Đông Âu (mùa đông), Ai Cập, Nam Phi" },
  { min: 60, label: "GMT+1 · Pháp, Đức, Ý (mùa đông)" },
  { min: 0, label: "GMT+0 · Anh, Bồ Đào Nha (mùa đông)" },
  { min: -180, label: "GMT−3 · Brazil, Argentina" },
  { min: -240, label: "GMT−4 · New York, Toronto (mùa hè)" },
  { min: -300, label: "GMT−5 · New York (mùa đông), Chicago (mùa hè)" },
  { min: -360, label: "GMT−6 · Chicago (mùa đông), Denver (mùa hè)" },
  { min: -420, label: "GMT−7 · Los Angeles (mùa hè), Denver (mùa đông), Arizona" },
  { min: -480, label: "GMT−8 · Los Angeles, Vancouver (mùa đông)" },
  { min: -600, label: "GMT−10 · Hawaii" },
];

/** Places matching a name anywhere in the world (Open-Meteo geocoding, called from the browser). */
export async function searchPlaces(q: string): Promise<(Place & { where: string })[]> {
  const url = "https://geocoding-api.open-meteo.com/v1/search?count=8&language=vi&format=json&name=" + encodeURIComponent(q.trim());
  const res = await fetch(url);
  if (!res.ok) throw new Error("Không tìm được lúc này");
  const body = (await res.json()) as { results?: { name: string; latitude: number; longitude: number; admin1?: string; country?: string }[] };
  return (body.results ?? []).map((r) => ({
    name: r.name,
    lat: r.latitude,
    lon: r.longitude,
    where: [r.admin1, r.country].filter(Boolean).join(", "),
  }));
}
