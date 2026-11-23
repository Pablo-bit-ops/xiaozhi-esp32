#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "display/oled_display.h"

#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <driver/i2c_master.h>
#include <driver/ledc.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#include <string>
#include <vector>
#include <algorithm>

#define TAG "ESP32-WebPortal"

// Página web completa con Login, Animación del Cuadrado con Ojos que Gira, Chat y Panel del Jefe
static const char* PORTAL_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Xiaozhi AI Portal</title>
<style>
  :root { --bg: #0f172a; --card: #1e293b; --accent: #38bdf8; --text: #f8fafc; --muted: #94a3b8; }
  * { box-sizing: border-box; font-family: system-ui, -apple-system, sans-serif; }
  body { margin: 0; background: var(--bg); color: var(--text); display: flex; justify-content: center; align-items: center; min-height: 100vh; }
  .box { background: var(--card); padding: 28px; border-radius: 18px; width: 92%; max-width: 460px; box-shadow: 0 20px 40px rgba(0,0,0,0.45); }
  h2 { margin-top: 0; text-align: center; color: var(--accent); }
  label { display: block; font-size: 13px; color: var(--muted); margin: 12px 0 6px; }
  input, select { width: 100%; padding: 12px; border-radius: 10px; border: 1px solid #334155; background: #0f172a; color: #fff; font-size: 15px; outline: none; }
  input:focus { border-color: var(--accent); }
  button { width: 100%; padding: 13px; margin-top: 18px; border: none; border-radius: 10px; background: var(--accent); color: #0f172a; font-weight: 700; font-size: 16px; cursor: pointer; }
  button.secondary { background: #334155; color: #fff; margin-top: 8px; }
  .hidden { display: none !important; }

  /* Animación del cuadrado con ojos que salen de dentro y gira */
  #animScreen { position: fixed; inset: 0; background: var(--bg); display: flex; flex-direction: column; justify-content: center; align-items: center; z-index: 99; }
  .bot-square {
    width: 110px; height: 110px; background: linear-gradient(135deg, #38bdf8, #6366f1);
    border-radius: 24px; display: flex; justify-content: center; align-items: center; gap: 18px;
    box-shadow: 0 0 35px rgba(56,189,248,0.6);
    animation: squareSpin 2.4s cubic-bezier(0.34, 1.56, 0.64, 1) forwards;
  }
  .bot-eye {
    width: 22px; height: 32px; background: #0f172a; border-radius: 11px;
    transform: scale(0);
    animation: eyePop 0.6s 0.45s cubic-bezier(0.34, 1.56, 0.64, 1) forwards;
  }
  @keyframes eyePop {
    0% { transform: scale(0); }
    70% { transform: scale(1.25); }
    100% { transform: scale(1); }
  }
  @keyframes squareSpin {
    0% { transform: scale(0.3) rotate(0deg); opacity: 0; }
    35% { transform: scale(1) rotate(0deg); opacity: 1; }
    60% { transform: scale(1.05) rotate(0deg); }
    100% { transform: scale(1) rotate(360deg); }
  }

  /* Chat estilo ChatGPT */
  .chat-wrap { width: 95%; max-width: 720px; height: 90vh; background: var(--card); border-radius: 18px; display: flex; flex-direction: column; overflow: hidden; }
  .topbar { padding: 14px 18px; background: #0b1120; display: flex; justify-content: space-between; align-items: center; gap: 8px; flex-wrap: wrap; }
  .badge { background: #f59e0b; color: #000; padding: 3px 9px; border-radius: 999px; font-size: 12px; font-weight: 800; }
  .tabs { display: flex; gap: 8px; }
  .tab-btn { padding: 6px 12px; margin: 0; width: auto; font-size: 13px; border-radius: 8px; }
  .messages { flex: 1; padding: 18px; overflow-y: auto; display: flex; flex-direction: column; gap: 12px; }
  .msg { max-width: 82%; padding: 12px 15px; border-radius: 14px; line-height: 1.45; font-size: 15px; word-wrap: break-word; }
  .msg.user { align-self: flex-end; background: var(--accent); color: #0f172a; font-weight: 500; }
  .msg.ai { align-self: flex-start; background: #0f172a; border: 1px solid #334155; }
  .input-row { padding: 12px; background: #0b1120; display: flex; gap: 8px; }
  .input-row input { flex: 1; }
  .input-row button { width: auto; margin: 0; padding: 0 20px; }
  .boss-panel { flex: 1; padding: 18px; overflow-y: auto; }
  .log-card { background: #0f172a; border: 1px solid #334155; border-radius: 12px; padding: 12px; margin-bottom: 10px; }
  .log-user { color: var(--accent); font-weight: 700; font-size: 13px; margin-bottom: 4px; }
</style>
</head>
<body>

<!-- PANTALLA 1: LOGIN -->
<div id="loginScreen" class="box">
  <h2>Acceso a Xiaozhi AI</h2>
  <label>Correo electrónico</label>
  <input type="email" id="emailInput" placeholder="tu.correo@ejemplo.com" required>
  <label>Contraseña (opcional, solo si eres Jefe)</label>
  <input type="password" id="passInput" placeholder="Dejar en blanco si eres usuario">
  <label>Clave API IA opcional (Groq / OpenAI para respuestas reales)</label>
  <input type="password" id="apiKeyInput" placeholder="gsk_... o sk-... (opcional)">
  <button onclick="doLogin()">Entrar</button>
</div>

<!-- PANTALLA 2: ANIMACIÓN DEL CUADRADO CON OJOS QUE GIRA -->
<div id="animScreen" class="hidden">
  <div class="bot-square">
    <div class="bot-eye"></div>
    <div class="bot-eye"></div>
  </div>
  <p style="margin-top:22px; color:#38bdf8; font-weight:600;">Iniciando sistema...</p>
</div>

<!-- PANTALLA 3: CHAT Y PANEL DEL JEFE -->
<div id="appScreen" class="chat-wrap hidden">
  <div class="topbar">
    <div>
      <span id="userLabel" style="font-weight:600;font-size:14px;"></span>
      <span id="bossBadge" class="badge hidden">JEFE</span>
    </div>
    <div class="tabs">
      <button id="btnTabChat" class="tab-btn" onclick="showTab('chat')">Mi Chat</button>
      <button id="btnTabBoss" class="tab-btn secondary hidden" onclick="showTab('boss')">Ver Todos los Chats</button>
      <button class="tab-btn secondary" onclick="logout()">Salir</button>
    </div>
  </div>

  <!-- Vista Chat -->
  <div id="chatView" style="display:flex;flex-direction:column;flex:1;overflow:hidden;">
    <div id="messages" class="messages"></div>
    <div class="input-row">
      <input type="text" id="msgInput" placeholder="Escribe tu mensaje aquí..." onkeydown="if(event.key==='Enter') sendMsg()">
      <button onclick="sendMsg()">Enviar</button>
    </div>
  </div>

  <!-- Vista Panel del Jefe -->
  <div id="bossView" class="boss-panel hidden">
    <h3 style="margin-top:0;color:#f59e0b;">Historial de todos los usuarios</h3>
    <button class="secondary" style="margin-bottom:14px;" onclick="loadAllLogs()">Actualizar conversaciones</button>
    <div id="allLogsContainer"></div>
  </div>
</div>

<script>
let currentUser = "";
let isBoss = false;
let apiKey = "";

async function doLogin() {
  const email = document.getElementById('emailInput').value.trim();
  const pass = document.getElementById('passInput').value.trim();
  apiKey = document.getElementById('apiKeyInput').value.trim();
  if (!email) { alert("Por favor escribe un correo electrónico."); return; }

  const res = await fetch('/api/login', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({email, pass})
  });
  const data = await res.json();
  currentUser = email;
  isBoss = data.is_boss;

  document.getElementById('loginScreen').classList.add('hidden');
  document.getElementById('animScreen').classList.remove('hidden');

  setTimeout(() => {
    document.getElementById('animScreen').classList.add('hidden');
    document.getElementById('appScreen').classList.remove('hidden');
    document.getElementById('userLabel').textContent = currentUser;
    if (isBoss) {
      document.getElementById('bossBadge').classList.remove('hidden');
      document.getElementById('btnTabBoss').classList.remove('hidden');
    }
    loadMyHistory();
  }, 2500);
}

function showTab(tab) {
  if (tab === 'chat') {
    document.getElementById('chatView').classList.remove('hidden');
    document.getElementById('bossView').classList.add('hidden');
  } else {
    document.getElementById('chatView').classList.add('hidden');
    document.getElementById('bossView').classList.remove('hidden');
    loadAllLogs();
  }
}

function appendBubble(role, text) {
  const box = document.getElementById('messages');
  const div = document.createElement('div');
  div.className = 'msg ' + (role === 'user' ? 'user' : 'ai');
  div.textContent = text;
  box.appendChild(div);
  box.scrollTop = box.scrollHeight;
}

async function loadMyHistory() {
  const res = await fetch('/api/logs');
  const logs = await res.json();
  const box = document.getElementById('messages');
  box.innerHTML = '';
  logs.filter(x => x.email === currentUser).forEach(x => {
    appendBubble('user', x.q);
    appendBubble('ai', x.a);
  });
}

async function loadAllLogs() {
  const res = await fetch('/api/logs');
  const logs = await res.json();
  const container = document.getElementById('allLogsContainer');
  container.innerHTML = '';
  if (logs.length === 0) { container.innerHTML = '<p>Aún no hay conversaciones guardadas.</p>'; return; }
  logs.slice().reverse().forEach(item => {
    const card = document.createElement('div');
    card.className = 'log-card';
    card.innerHTML = `<div class="log-user">${item.email}</div><div><b>Pregunta:</b> ${item.q}</div><div style="margin-top:4px;color:#cbd5e1;"><b>IA:</b> ${item.a}</div>`;
    container.appendChild(card);
  });
}

async function sendMsg() {
  const inp = document.getElementById('msgInput');
  const q = inp.value.trim();
  if (!q) return;
  inp.value = '';
  appendBubble('user', q);

  let answer = "Respuesta registrada en tu ESP32. (Añade una clave API de Groq/OpenAI al entrar para respuestas completas con IA).";
  if (apiKey) {
    try {
      const isGroq = apiKey.startsWith('gsk_');
Usa el código con precaución.
const url = isGroq ? 'groq.com' : 'openai.com';
const model = isGroq ? 'llama-3.1-8b-instant' : 'gpt-4o-mini';
const aiRes = await fetch(url, {
method: 'POST',
headers: {'Content-Type': 'application/json', 'Authorization': 'Bearer ' + apiKey},
body: JSON.stringify({model, messages: [{role: 'user', content: q}]})
});
const aiJson = await aiRes.json();
if (aiJson.choices && aiJson.choices[0]) {
answer = aiJson.choices[0].message.content;
}
} catch (e) {
answer = "Error conectando con la API de IA.";
}
}
appendBubble('ai', answer);
await fetch('/api/save', {
method: 'POST',
headers: {'Content-Type': 'application/json'},
body: JSON.stringify({email: currentUser, q, a: answer})
});
}
function logout() { location.reload(); }



)rawliteral";
struct ChatEntry {
std::string email;
std::string q;
std::string a;
};
static std::vector g_chat_logs;
static httpd_handle_t g_httpd = nullptr;
static std::string EscapeJson(const std::string& s) {
std::string out;
for (char c : s) {
if (c == '"') out += "\"";
else if (c == '\') out += "\\";
else if (c == '\n') out += " ";
else if (c == '\r') continue;
else out += c;
}
return out;
}
static std::string ExtractJsonField(const std::string& body, const std::string& key) {
std::string pattern = """ + key + """;
size_t pos = body.find(pattern);
if (pos == std::string::npos) return "";
size_t colon = body.find(':', pos + pattern.size());
if (colon == std::string::npos) return "";
size_t q1 = body.find('"', colon + 1);
if (q1 == std::string::npos) return "";
size_t q2 = q1 + 1;
while (q2 < body.size()) {
if (body[q2] == '"' && body[q2 - 1] != '\') break;
q2++;
}
if (q2 >= body.size()) return "";
return body.substr(q1 + 1, q2 - q1 - 1);
}
static void SaveLogsToNvs() {
nvs_handle_t handle;
if (nvs_open("web_chat", NVS_READWRITE, &handle) != ESP_OK) return;
std::string serialized;
for (const auto& e : g_chat_logs) {
serialized += e.email + "\t" + e.q + "\t" + e.a + "\n";
}
nvs_set_str(handle, "logs", serialized.c_str());
nvs_commit(handle);
nvs_close(handle);
}
static void LoadLogsFromNvs() {
nvs_handle_t handle;
if (nvs_open("web_chat", NVS_READONLY, &handle) != ESP_OK) return;
size_t required_size = 0;
if (nvs_get_str(handle, "logs", nullptr, &required_size) == ESP_OK && required_size > 1) {
std::string buf(required_size, '\0');
if (nvs_get_str(handle, "logs", &buf[0], &required_size) == ESP_OK) {
g_chat_logs.clear();
size_t start = 0;
while (start < buf.size()) {
size_t end = buf.find('\n', start);
if (end == std::string::npos) break;
std::string line = buf.substr(start, end - start);
size_t t1 = line.find('\t');
size_t t2 = (t1 != std::string::npos) ? line.find('\t', t1 + 1) : std::string::npos;
if (t1 != std::string::npos && t2 != std::string::npos) {
g_chat_logs.push_back({line.substr(0, t1), line.substr(t1 + 1, t2 - t1 - 1), line.substr(t2 + 1)});
}
start = end + 1;
}
}
}
nvs_close(handle);
}
static esp_err_t RootGetHandler(httpd_req_t* req) {
httpd_resp_set_type(req, "text/html");
return httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t LoginPostHandler(httpd_req_t* req) {
char buf[512];
int len = httpd_req_recv(req, buf, std::min((size_t)req->content_len, sizeof(buf) - 1));
if (len <= 0) return ESP_FAIL;
buf[len] = '\0';
std::string body(buf);
std::string pass = ExtractJsonField(body, "pass");
std::string clean_pass;
for (char c : pass) {
if (c != ' ') clean_pass += c;
}
bool is_boss = (clean_pass == "090807060504030201");
httpd_resp_set_type(req, "application/json");
std::string resp = std::string("{"status":"ok","is_boss":") + (is_boss ? "true}" : "false}");
return httpd_resp_send(req, resp.c_str(), HTTPD_RESP_USE_STRLEN);
}
static esp_err_t SavePostHandler(httpd_req_t* req) {
std::string body;
body.resize(req->content_len);
int received = 0;
while (received < req->content_len) {
int r = httpd_req_recv(req, &body[received], req->content_len - received);
if (r <= 0) return ESP_FAIL;
received += r;
}
ChatEntry entry;
entry.email = ExtractJsonField(body, "email");
entry.q = ExtractJsonField(body, "q");
entry.a = ExtractJsonField(body, "a");
if (!entry.email.empty() && !entry.q.empty()) {
g_chat_logs.push_back(entry);
if (g_chat_logs.size() > 40) {
g_chat_logs.erase(g_chat_logs.begin());
}
SaveLogsToNvs();
}
httpd_resp_set_type(req, "application/json");
return httpd_resp_send(req, "{"status":"saved"}", HTTPD_RESP_USE_STRLEN);
}
static esp_err_t LogsGetHandler(httpd_req_t* req) {
std::string json = "[";
for (size_t i = 0; i < g_chat_logs.size(); i++) {
if (i > 0) json += ",";
json += "{"email":"" + EscapeJson(g_chat_logs[i].email) + "","
""q":"" + EscapeJson(g_chat_logs[i].q) + "","
""a":"" + EscapeJson(g_chat_logs[i].a) + ""}";
}
json += "]";
httpd_resp_set_type(req, "application/json");
return httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
}
static void StartWebPortal() {
if (g_httpd != nullptr) return;
LoadLogsFromNvs();
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.stack_size = 8192;
if (httpd_start(&g_httpd, &config) == ESP_OK) {
httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = RootGetHandler, .user_ctx = nullptr };
httpd_uri_t login_uri = { .uri = "/api/login", .method = HTTP_POST, .handler = LoginPostHandler, .user_ctx = nullptr };
httpd_uri_t save_uri = { .uri = "/api/save", .method = HTTP_POST, .handler = SavePostHandler, .user_ctx = nullptr };
httpd_uri_t logs_uri = { .uri = "/api/logs", .method = HTTP_GET, .handler = LogsGetHandler, .user_ctx = nullptr };
httpd_register_uri_handler(g_httpd, &root_uri);
httpd_register_uri_handler(g_httpd, &login_uri);
httpd_register_uri_handler(g_httpd, &save_uri);
httpd_register_uri_handler(g_httpd, &logs_uri);
ESP_LOGI(TAG, "Portal web iniciado en puerto 80");
}
}
class ServoController {
private:
int current_angle_ = 90;
uint32_t AngleToDuty(int angle) {
uint32_t min_duty = 410;
uint32_t max_duty = 2048;
return min_duty + ((max_duty - min_duty) * angle) / 180;
}
public:
ServoController(gpio_num_t gpio_num) {
ledc_timer_config_t ledc_timer = {
.speed_mode       = LEDC_LOW_SPEED_MODE,
.duty_resolution  = LEDC_TIMER_14_BIT,
.timer_num        = LEDC_TIMER_1,
.freq_hz          = 50,
.clk_cfg          = LEDC_AUTO_CLK
};
ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
ledc_channel_config_t ledc_channel = {
.gpio_num       = gpio_num,
.speed_mode     = LEDC_LOW_SPEED_MODE,
.channel        = LEDC_CHANNEL_1,
.intr_type      = LEDC_INTR_DISABLE,
.timer_sel      = LEDC_TIMER_1,
.duty           = AngleToDuty(90),
.hpoint         = 0
};
ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
auto& mcp_server = McpServer::GetInstance();
mcp_server.AddTool("self.servo.set_angle",
"Mueve el servomotor a un ángulo específico entre 0 y 180 grados.",
PropertyList({
Property("angle", kPropertyTypeInteger, 0, 180)
}),
[this](const PropertyList& properties) -> ReturnValue {
int angle = properties["angle"].value();
SetAngle(angle);
return "{"status": "ok", "angle": " + std::to_string(current_angle_) + "}";
});
mcp_server.AddTool("self.network.get_ip",
"Devuelve la dirección IP actual del ESP32 para abrir su página web desde el móvil o PC.",
PropertyList(),
[](const PropertyList& properties) -> ReturnValue {
esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
esp_netif_ip_info_t ip_info;
if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
char ip_str[16];
esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
return std::string("{"ip": "") + ip_str + ""}";
}
return "{"ip": "desconectado"}";
});
}
void SetAngle(int angle) {
if (angle < 0) angle = 0;
if (angle > 180) angle = 180;
current_angle_ = angle;
ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, AngleToDuty(current_angle_));
ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
ESP_LOGI(TAG, "Servo movido a %d grados", current_angle_);
}
};
class CompactWifiBoard : public WifiBoard {
private:
Button boot_button_;
Button touch_button_;
Button asr_button_;
i2c_master_bus_handle_t display_i2c_bus_;
esp_lcd_panel_io_handle_t panel_io_ = nullptr;
esp_lcd_panel_handle_t panel_ = nullptr;
Display* display_ = nullptr;
void InitializeDisplayI2c() {
i2c_master_bus_config_t bus_config = {
.i2c_port = (i2c_port_t)0,
.sda_io_num = DISPLAY_SDA_PIN,
.scl_io_num = DISPLAY_SCL_PIN,
.clk_source = I2C_CLK_SRC_DEFAULT,
.glitch_ignore_cnt = 7,
.intr_priority = 0,
.trans_queue_depth = 0,
.flags = {
.enable_internal_pullup = 1,
},
};
ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
}
void InitializeSsd1306Display() {
esp_lcd_panel_io_i2c_config_t io_config = {
.dev_addr = 0x3C,
.scl_speed_hz = 400 * 1000,
.control_phase_bytes = 1,
.dc_bit_offset = 6,
.lcd_cmd_bits = 8,
.lcd_param_bits = 8,
.on_color_trans_done = nullptr,
.user_ctx = nullptr,
.flags = {
.dc_low_on_data = 0,
.disable_control_phase = 0,
},
};
ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));
ESP_LOGI(TAG, "Install SSD1306 driver");
esp_lcd_panel_dev_config_t panel_config = {};
panel_config.reset_gpio_num = GPIO_NUM_NC;
panel_config.bits_per_pixel = 1;
esp_lcd_panel_ssd1306_config_t ssd1306_config = {
.height = static_cast<uint8_t>(DISPLAY_HEIGHT),
};
panel_config.vendor_config = &ssd1306_config;
ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
ESP_LOGI(TAG, "SSD1306 driver installed");
ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
if (esp_lcd_panel_init(panel_) != ESP_OK) {
ESP_LOGE(TAG, "Failed to initialize display");
display_ = new NoDisplay();
return;
}
ESP_LOGI(TAG, "Turning display on");
ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));
display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
}
void InitializeButtons() {
gpio_config_t io_conf = {
.pin_bit_mask = 1ULL << BUILTIN_LED_GPIO,
.mode = GPIO_MODE_OUTPUT,
.pull_up_en = GPIO_PULLUP_DISABLE,
.pull_down_en = GPIO_PULLDOWN_DISABLE,
.intr_type = GPIO_INTR_DISABLE
};
gpio_config(&io_conf);
boot_button_.OnClick(this {
auto& app = Application::GetInstance();
if (app.GetDeviceState() == kDeviceStateStarting) {
EnterWifiConfigMode();
return;
}
gpio_set_level(BUILTIN_LED_GPIO, 1);
app.ToggleChatState();
});
asr_button_.OnClick(this {
std::string wake_word="你好小智";
Application::GetInstance().WakeWordInvoke(wake_word);
});
touch_button_.OnPressDown(this {
gpio_set_level(BUILTIN_LED_GPIO, 1);
Application::GetInstance().StartListening();
});
touch_button_.OnPressUp(this {
gpio_set_level(BUILTIN_LED_GPIO, 0);
Application::GetInstance().StopListening();
});
}
void InitializeTools() {
static LampController lamp(LAMP_GPIO);
static ServoController servo(SERVO_GPIO);
StartWebPortal();
}
public:
CompactWifiBoard() : WifiBoard(), boot_button_(BOOT_BUTTON_GPIO), touch_button_(TOUCH_BUTTON_GPIO), asr_button_(ASR_BUTTON_GPIO)
{
InitializeDisplayI2c();
InitializeSsd1306Display();
InitializeButtons();
InitializeTools();
}
virtual AudioCodec* GetAudioCodec() override
{
#ifdef AUDIO_I2S_METHOD_SIMPLEX
static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
return &audio_codec;
}
virtual Display* GetDisplay() override {
return display_;
}
};
DECLARE_BOARD(CompactWifiBoard);
