#include "portal.h"

#include "../trace.h"
#include "store.h"

namespace bot {
namespace portal {
namespace {

std::string htmlEsc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

std::string head(bool refresh) {
  std::string h = "<!doctype html><html><head><meta charset=utf-8>"
                  "<meta name=viewport content=\"width=device-width,initial-scale=1\"><title>EspOS Bot</title>";
  if (refresh) h += "<meta http-equiv=refresh content=\"2;url=/status\">";
  return h + R"(<style>
body{font:17px system-ui,-apple-system,sans-serif;margin:0;background:#0d0f14;color:#e9edf5}
main{max-width:420px;margin:0 auto;padding:22px 18px}h1{font-size:22px;margin:0 0 4px}p{color:#9aa3b2;margin:6px 0 18px}
label{display:block;font-size:14px;color:#9aa3b2;margin:14px 0 6px}select,input{width:100%;box-sizing:border-box;
font:inherit;padding:12px;border-radius:10px;border:1px solid #2a3040;background:#161a23;color:#e9edf5}
button{width:100%;margin-top:20px;padding:14px;font:600 17px system-ui;border:0;border-radius:12px;background:#3d7bff;color:#fff}
.msg{margin:14px 0;padding:14px;border-radius:12px;background:#161a23;line-height:1.45}.err{background:#3a1d22}
.ok{background:#173326}.big{font-size:20px;font-weight:600;color:#fff;display:block;margin-bottom:6px}
.row{display:flex;gap:10px}.row label{flex:1}a{color:#8fb4ff}
</style></head><body><main><h1>EspOS Bot</h1>)";
}

}  // namespace

std::string page(const std::vector<std::string>& networks, const std::string& msg) {
  std::string h = head(false);
  if (!msg.empty()) h += "<div class=\"msg err\">" + msg + "</div>";
  h += "<p>Chọn WiFi cho máy · Choose the WiFi for the bot</p><form method=post action=/save>";
  h += "<label>WiFi</label><select name=s>";
  for (const std::string& n : networks) h += "<option>" + htmlEsc(n) + "</option>";
  h += "</select><label>Hoặc gõ tên · Or type its name</label><input name=t autocomplete=off>";
  h += "<label>Mật khẩu · Password</label><input name=p type=password autocomplete=off>";
  h += "<label>Ngôn ngữ · Language</label><div class=row><label><input type=radio name=l value=vi checked> Tiếng Việt</label>"
       "<label><input type=radio name=l value=en> English</label></div>";
  h += "<button>Kết nối · Connect</button></form>";
  return h + "</main></body></html>";
}

std::string whyText(Why why, const std::string& ssid) {
  const std::string n = "“" + htmlEsc(ssid) + "”";
  if (why == WHY_PASSWORD)
    return "<span class=big>Sai mật khẩu WiFi</span>Mật khẩu của " + n + " chưa đúng. Nhập lại rồi bấm Kết nối.<br>"
           "<i>Wrong password for " + n + ". Type it again and press Connect.</i>";
  if (why == WHY_NOT_FOUND)
    return "<span class=big>Không thấy mạng WiFi</span>Máy không thấy " + n +
           ". Mạng phải là 2,4 GHz và ở gần máy.<br><i>The bot can't see " + n +
           ". It needs a 2.4 GHz network nearby.</i>";
  return "<span class=big>Chưa vào được WiFi</span>Máy chưa vào được " + n + ". Thử lại nhé.<br><i>The bot couldn't join " + n +
         ". Please try again.</i>";
}

std::string status(Join j, const std::string& ssid, Why why, const std::vector<std::string>& networks) {
  const std::string n = "“" + htmlEsc(ssid) + "”";
  if (j == Join::Failed) return page(networks, whyText(why, ssid));
  if (j == Join::Ok)
    return head(false) + "<div class=\"msg ok\"><span class=big>Đã kết nối!</span>Máy đã vào WiFi " + n +
           ". Nhìn màn hình của máy để làm tiếp.<br><i>Connected to " + n +
           ". Look at the bot's screen for the next step.</i></div><p>Bạn có thể đóng trang này. · You can close this page.</p>"
           "</main></body></html>";
  return head(true) + "<div class=msg><span class=big>Đang kết nối WiFi…</span>Máy đang vào " + n +
         ". Chờ vài giây, trang này tự cập nhật.<br><i>Connecting to " + n +
         ". This page updates by itself in a few seconds.</i></div><p><a href=/status>Xem lại · Check again</a></p>"
         "</main></body></html>";
}

void form(const std::string& picked, const std::string& typed, const std::string& pass, const std::string& lang,
          std::string& ssid, std::string& passOut) {
  ssid = typed.empty() ? picked : typed;
  passOut = pass;
  if (lang == "vi" || lang == "en") store::set("sys.lang", lang, true);
  // the lengths as decoded (the form sends a space as "+", other signs as %XX), never the password itself
  trace::add("portal: connect to \"%s\" asked (name %u bytes, password %u bytes)", ssid.c_str(), (unsigned)ssid.size(),
             (unsigned)pass.size());
}

}  // namespace portal
}  // namespace bot
