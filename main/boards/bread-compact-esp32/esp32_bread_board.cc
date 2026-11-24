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
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cJSON.h>
#include <driver/i2c_master.h>
#include <driver/ledc.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cstring>
#include <algorithm>

#define TAG "ESP32-WebPortal"

// ============================================================
// CONFIGURACIÓN
// ============================================================

// CAMBIA ESTA CONTRASEÑA
static const char* BOSS_PASSWORD = "CAMBIA_ESTA_PASSWORD";

// Historial limitado para evitar exceder el tamaño de NVS
static const size_t MAX_CHATS = 4;
static const size_t MAX_QUESTION_LENGTH = 350;
static const size_t MAX_ANSWER_LENGTH = 700;

// ============================================================
// WEB
// ============================================================

static const char* PORTAL_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Xiaozhi AI Portal</title>
<style>
:root {
 --bg:#0f172a;--card:#1e293b;--accent:#38bdf8;
 --text:#f8fafc;--muted:#94a3b8;
}
*{box-sizing:border-box;font-family:system-ui,sans-serif}
body{margin:0;background:var(--bg);color:var(--text);
 display:flex;justify-content:center;align-items:center;min-height:100vh}
.box{background:var(--card);padding:28px;border-radius:18px;
 width:92%;max-width:460px;box-shadow:0 20px 40px #0007}
h2{text-align:center;color:var(--accent);margin-top:0}
label{display:block;font-size:13px;color:var(--muted);margin:12px 0 6px}
input{width:100%;padding:12px;border-radius:10px;border:1px solid #334155;
 background:#0f172a;color:white;font-size:15px;outline:none}
input:focus{border-color:var(--accent)}
button{width:100%;padding:13px;margin-top:18px;border:0;border-radius:10px;
 background:var(--accent);color:#0f172a;font-weight:700;font-size:16px;cursor:pointer}
button.secondary{background:#334155;color:white;margin-top:8px}
.hidden{display:none!important}
#animScreen{position:fixed;inset:0;background:var(--bg);display:flex;
 flex-direction:column;justify-content:center;align-items:center;z-index:99}
.bot-square{width:110px;height:110px;background:linear-gradient(135deg,#38bdf8,#6366f1);
 border-radius:24px;display:flex;justify-content:center;align-items:center;gap:18px;
 box-shadow:0 0 35px #38bdf899;animation:squareSpin 2.4s forwards}
.bot-eye{width:22px;height:32px;background:#0f172a;border-radius:11px;
 transform:scale(0);animation:eyePop .6s .45s forwards}
@keyframes eyePop{0%{transform:scale(0)}70%{transform:scale(1.25)}100%{transform:scale(1)}}
@keyframes squareSpin{0%{transform:scale(.3);opacity:0}
 35%{transform:scale(1);opacity:1}60%{transform:scale(1.05)}
 100%{transform:scale(1) rotate(360deg)}}
.chat-wrap{width:95%;max-width:720px;height:90vh;background:var(--card);
 border-radius:18px;display:flex;flex-direction:column;overflow:hidden}
.topbar{padding:14px 18px;background:#0b1120;display:flex;
 justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap}
.badge{background:#f59e0b;color:#000;padding:3px 9px;border-radius:999px;
 font-size:12px;font-weight:800}
.tabs{display:flex;gap:8px}
.tab-btn{padding:6px 12px;margin:0;width:auto;font-size:13px;border-radius:8px}
.messages{flex:1;padding:18px;overflow-y:auto;display:flex;flex-direction:column;gap:12px}
.msg{max-width:82%;padding:12px 15px;border-radius:14px;line-height:1.45;
 font-size:15px;overflow-wrap:anywhere;white-space:pre-wrap}
.msg.user{align-self:flex-end;background:var(--accent);color:#0f172a}
.msg.ai{align-self:flex-start;background:#0f172a;border:1px solid #334155}
.input-row{padding:12px;background:#0b1120;display:flex;gap:8px}
.input-row input{flex:1}
.input-row button{width:auto;margin:0;padding:0 20px}
.boss-panel{flex:1;padding:18px;overflow-y:auto}
.log-card{background:#0f172a;border:1px solid #334155;border-radius:12px;
 padding:12px;margin-bottom:10px;overflow-wrap:anywhere}
.log-user{color:var(--accent);font-weight:700;font-size:13px;margin-bottom:4px}
</style>
</head>
<body>

<div id="loginScreen" class="box">
<h2>Acceso a Xiaozhi AI</h2>
<label>Correo electrónico</label>
<input type="email" id="emailInput" placeholder="tu.correo@ejemplo.com">
<label>Contraseña (solo Jefe)</label>
<input type="password" id="passInput" placeholder="Dejar en blanco si eres usuario">
<button onclick="doLogin()">Entrar</button>
</div>

<div id="animScreen" class="hidden">
<div class="bot-square"><div class="bot-eye"></div><div class="bot-eye"></div></div>
<p style="margin-top:22px;color:#38bdf8;font-weight:600">Iniciando sistema...</p>
</div>

<div id="appScreen" class="chat-wrap hidden">
<div class="topbar">
<div><span id="userLabel" style="font-weight:600;font-size:14px"></span>
<span id="bossBadge" class="badge hidden">JEFE</span></div>
<div class="tabs">
<button class="tab-btn" onclick="showTab('chat')">Mi Chat</button>
<button id="btnTabBoss" class="tab-btn secondary hidden" onclick="showTab('boss')">Panel del Jefe</button>
<button class="tab-btn secondary" onclick="logout()">Salir</button>
</div>
</div>

<div id="chatView" style="display:flex;flex-direction:column;flex:1;overflow:hidden">
<div id="messages" class="messages"></div>
<div class="input-row">
<input type="text" id="msgInput" placeholder="Escribe tu mensaje..."
 onkeydown="if(event.key==='Enter')sendMsg()">
<button onclick="sendMsg()">Enviar</button>
</div>
</div>

<div id="bossView" class="boss-panel hidden">
<h3 style="margin-top:0;color:#f59e0b">Ajustes del Jefe y Conversaciones</h3>
<div class="log-card">
<label style="margin-top:0">Clave API (Groq gsk_... u OpenAI sk-...)</label>
<input type="password" id="bossApiKey" placeholder="API Key">
<button style="margin-top:10px" onclick="saveBossConfig()">Guardar Clave API</button>
</div>
<button class="secondary" style="margin-bottom:14px" onclick="loadLogs()">Actualizar historial</button>
<div id="allLogsContainer"></div>
</div>
</div>

<script>
let currentUser="";
let sessionToken="";
let isBoss=false;
let busy=false;

async function doLogin(){
 const email=document.getElementById('emailInput').value.trim();
 const pass=document.getElementById('passInput').value;
 if(!email||!email.includes('@')){
  alert("Escribe un correo válido.");return;
 }
 try{
  const res=await fetch('/api/login',{
   method:'POST',headers:{'Content-Type':'application/json'},
   body:JSON.stringify({email,pass})
  });
  if(!res.ok){alert("Error de inicio de sesión.");return;}
  const data=await res.json();
  currentUser=email;sessionToken=data.token;isBoss=data.is_boss;
  document.getElementById('loginScreen').classList.add('hidden');
  document.getElementById('animScreen').classList.remove('hidden');
  setTimeout(()=>{
   document.getElementById('animScreen').classList.add('hidden');
   document.getElementById('appScreen').classList.remove('hidden');
   document.getElementById('userLabel').textContent=currentUser;
   if(isBoss){
    document.getElementById('bossBadge').classList.remove('hidden');
    document.getElementById('btnTabBoss').classList.remove('hidden');
   }
   loadLogs();
  },2500);
 }catch(e){alert("No se pudo conectar con el ESP32.");}
}

function showTab(tab){
 if(tab==='chat'){
  document.getElementById('chatView').classList.remove('hidden');
  document.getElementById('bossView').classList.add('hidden');
 }else{
  if(!isBoss)return;
  document.getElementById('chatView').classList.add('hidden');
  document.getElementById('bossView').classList.remove('hidden');
  loadLogs();
 }
}

function appendBubble(role,text){
 const box=document.getElementById('messages');
 const div=document.createElement('div');
 div.className='msg '+(role==='user'?'user':'ai');
 div.textContent=text;
 box.appendChild(div);
 box.scrollTop=box.scrollHeight;
}

async function loadLogs(){
 try{
  const res=await fetch('/api/logs',{
   headers:{'Authorization':'Bearer '+sessionToken}
  });
  if(!res.ok)return;
  const logs=await res.json();
  const box=document.getElementById('messages');
  box.textContent='';
  logs.filter(x=>x.email===currentUser).forEach(x=>{
   appendBubble('user',x.q);appendBubble('ai',x.a);
  });
  if(isBoss){
   const container=document.getElementById('allLogsContainer');
   container.textContent='';
   if(!logs.length){container.textContent='Aún no hay conversaciones.';return;}
   logs.slice().reverse().forEach(item=>{
    const card=document.createElement('div');card.className='log-card';
    const u=document.createElement('div');u.className='log-user';
    u.textContent=item.email;
    const q=document.createElement('div');q.textContent='Pregunta: '+item.q;
    const a=document.createElement('div');a.style.marginTop='4px';
    a.style.color='#cbd5e1';a.textContent='IA: '+item.a;
    card.appendChild(u);card.appendChild(q);card.appendChild(a);
    container.appendChild(card);
   });
  }
 }catch(e){}
}

async function saveBossConfig(){
 if(!isBoss){alert("No autorizado.");return;}
 const key=document.getElementById('bossApiKey').value.trim();
 if(!key){alert("Escribe una API Key.");return;}
 try{
  const res=await fetch('/api/config',{
   method:'POST',
   headers:{'Content-Type':'application/json','Authorization':'Bearer '+sessionToken},
   body:JSON.stringify({api_key:key})
  });
  if(res.ok){alert("Clave API guardada en el ESP32.");document.getElementById('bossApiKey').value='';}
  else alert("No se pudo guardar la clave.");
 }catch(e){alert("Error de conexión.");}
}

async function sendMsg(){
 if(busy)return;
 const inp=document.getElementById('msgInput');
 const q=inp.value.trim();
 if(!q)return;
 busy=true;inp.value='';
 appendBubble('user',q);
 try{
  const res=await fetch('/api/chat',{
   method:'POST',
   headers:{'Content-Type':'application/json','Authorization':'Bearer '+sessionToken},
   body:JSON.stringify({q})
  });
  if(!res.ok){appendBubble('ai','Error de servidor o sesión.');return;}
  const data=await res.json();
  appendBubble('ai',data.a||'Sin respuesta.');
 }catch(e){appendBubble('ai','Error de red con el ESP32.');}
 finally{busy=false;}
}
function logout(){location.reload();}
</script>
</body>
</html>
)rawliteral";

// ============================================================
// SESIONES E HISTORIAL
// ============================================================

struct UserSession {
    std::string token;
    std::string email;
    bool is_boss;
};

struct ChatItem {
    std::string email;
    std::string q;
    std::string a;
};

using SessionList = std::vector<UserSession>;
using ChatList = std::vector<ChatItem>;

static httpd_handle_t g_httpd = nullptr;
static SessionList g_sessions;
static ChatList g_chats;
static std::string g_stored_api_key;
static std::string g_boss_password = BOSS_PASSWORD;

// ============================================================
// TOKEN
// ============================================================

static std::string GenerateToken() {
    char buf[33] = {0};
    snprintf(buf, sizeof(buf), "%08lx%08lx%08lx%08lx",
        (unsigned long)esp_random(),
        (unsigned long)esp_random(),
        (unsigned long)esp_random(),
        (unsigned long)esp_random());
    return std::string(buf);
}

static bool GetSessionFromReq(httpd_req_t* req, UserSession& out_session) {
    char auth[160] = {0};

    if (httpd_req_get_hdr_value_str(
        req, "Authorization", auth, sizeof(auth)) != ESP_OK) {
        return false;
    }

    std::string hdr(auth);
    if (hdr.rfind("Bearer ", 0) != 0) return false;

    std::string tok = hdr.substr(7);

    for (const auto& s : g_sessions) {
        if (s.token == tok) {
            out_session = s;
            return true;
        }
    }
    return false;
}

// ============================================================
// NVS
// ============================================================

static bool SaveStateToNvs() {
    nvs_handle_t handle;
    if (nvs_open("web_portal", NVS_READWRITE, &handle) != ESP_OK)
        return false;

    bool ok = true;

    if (nvs_set_str(handle, "api_key", g_stored_api_key.c_str()) != ESP_OK)
        ok = false;

    cJSON* arr = cJSON_CreateArray();
    if (!arr) {
        nvs_close(handle);
        return false;
    }

    for (const auto& chat : g_chats) {
        cJSON* obj = cJSON_CreateObject();
        if (!obj) {
            ok = false;
            continue;
        }

        cJSON_AddStringToObject(obj, "email", chat.email.c_str());
        cJSON_AddStringToObject(obj, "q", chat.q.c_str());
        cJSON_AddStringToObject(obj, "a", chat.a.c_str());
        cJSON_AddItemToArray(arr, obj);
    }

    char* printed = cJSON_PrintUnformatted(arr);
    if (printed) {
        size_t len = strlen(printed);

        // NVS string storage must remain within practical limits.
        if (len < 3800) {
            if (nvs_set_str(handle, "chats_json", printed) != ESP_OK)
                ok = false;
        } else {
            ESP_LOGW(TAG, "Historial demasiado grande para NVS");
            ok = false;
        }
        cJSON_free(printed);
    } else {
        ok = false;
    }

    cJSON_Delete(arr);

    if (nvs_commit(handle) != ESP_OK) ok = false;
    nvs_close(handle);
    return ok;
}

static void LoadStateFromNvs() {
    nvs_handle_t handle;
    if (nvs_open("web_portal", NVS_READONLY, &handle) != ESP_OK)
        return;

    size_t key_len = 0;
    if (nvs_get_str(handle, "api_key", nullptr, &key_len) == ESP_OK
        && key_len > 1 && key_len < 512) {
        std::vector<char> buf(key_len, 0);
        if (nvs_get_str(handle, "api_key", buf.data(), &key_len) == ESP_OK)
            g_stored_api_key = buf.data();
    }

    size_t json_len = 0;
    if (nvs_get_str(handle, "chats_json", nullptr, &json_len) == ESP_OK
        && json_len > 1 && json_len < 3900) {
        std::vector<char> buf(json_len, 0);
        if (nvs_get_str(handle, "chats_json", buf.data(), &json_len) == ESP_OK) {
            cJSON* arr = cJSON_Parse(buf.data());
            if (cJSON_IsArray(arr)) {
                g_chats.clear();
                int count = cJSON_GetArraySize(arr);
                for (int i = 0; i < count; i++) {
                    cJSON* item = cJSON_GetArrayItem(arr, i);
                    cJSON* em = cJSON_GetObjectItem(item, "email");
                    cJSON* q = cJSON_GetObjectItem(item, "q");
                    cJSON* a = cJSON_GetObjectItem(item, "a");

                    if (cJSON_IsString(em) && cJSON_IsString(q)
                        && cJSON_IsString(a)) {
                        g_chats.push_back({
                            em->valuestring, q->valuestring, a->valuestring
                        });
                    }
                }
            }
            cJSON_Delete(arr);
        }
    }
    nvs_close(handle);
}

// ============================================================
// IA EXTERNA
// ============================================================

static std::string AskExternalAi(const std::string& prompt) {
    if (g_stored_api_key.empty())
        return "El Jefe aún no ha configurado la API Key.";

    bool is_groq = (g_stored_api_key.rfind("gsk_", 0) == 0);

    std::string url = is_groq
        ? "https://api.groq.com/openai/v1/chat/completions"
        : "https://api.openai.com/v1/chat/completions";

    const char* model = is_groq
        ? "llama-3.1-8b-instant"
        : "gpt-4o-mini";

    cJSON* req_json = cJSON_CreateObject();
    if (!req_json) return "Error creando petición JSON.";

    cJSON_AddStringToObject(req_json, "model", model);

    cJSON* msgs = cJSON_AddArrayToObject(req_json, "messages");
    if (!msgs) {
        cJSON_Delete(req_json);
        return "Error creando mensajes.";
    }

    cJSON* sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content",
        "Eres Xiaozhi, un asistente amable, útil, creativo y claro. "
        "Habla siempre en español salvo que te pidan otro idioma.");
    cJSON_AddItemToArray(msgs, sys_msg);

    cJSON* usr_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(usr_msg, "role", "user");
    cJSON_AddStringToObject(usr_msg, "content", prompt.c_str());
    cJSON_AddItemToArray(msgs, usr_msg);

    char* post_data = cJSON_PrintUnformatted(req_json);
    cJSON_Delete(req_json);

    if (!post_data) return "Error generando petición.";

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 20000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        cJSON_free(post_data);
        return "No se pudo iniciar HTTPS.";
    }

    std::string auth_hdr = "Bearer " + g_stored_api_key;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_hdr.c_str());
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    std::string reply = "No se obtuvo respuesta de la IA.";
    std::string resp_body;

    esp_err_t result = esp_http_client_open(client, strlen(post_data));
    if (result == ESP_OK) {
        int wr = esp_http_client_write(client, post_data, strlen(post_data));

        if (wr >= 0) {
            esp_http_client_fetch_headers(client);
            int status = esp_http_client_get_status_code(client);

            char chunk[512];
            int read_len = 0;

            while ((read_len = esp_http_client_read(
                client, chunk, sizeof(chunk))) > 0) {
                if (resp_body.size() + read_len > 8000) break;
                resp_body.append(chunk, read_len);
            }

            cJSON* resp_json = cJSON_Parse(resp_body.c_str());
            if (resp_json) {
                cJSON* choices = cJSON_GetObjectItem(resp_json, "choices");

                if (status >= 200 && status < 300
                    && cJSON_IsArray(choices)
                    && cJSON_GetArraySize(choices) > 0) {

                    cJSON* first = cJSON_GetArrayItem(choices, 0);
                    cJSON* message = cJSON_GetObjectItem(first, "message");
                    cJSON* content = message
                        ? cJSON_GetObjectItem(message, "content")
                        : nullptr;

                    if (cJSON_IsString(content)
                        && content->valuestring != nullptr) {
                        reply = content->valuestring;
                    }
                } else {
                    cJSON* err_obj = cJSON_GetObjectItem(resp_json, "error");
                    cJSON* err_msg = err_obj
                        ? cJSON_GetObjectItem(err_obj, "message")
                        : nullptr;

                    if (cJSON_IsString(err_msg))
                        reply = std::string("Error API: ") + err_msg->valuestring;
                    else
                        reply = "La API devolvió HTTP " + std::to_string(status);
                }
                cJSON_Delete(resp_json);
            } else {
                reply = "Respuesta de API no válida.";
            }
        } else {
            reply = "Error enviando petición HTTPS.";
        }
    } else {
        reply = "Error conectando con el servidor de IA.";
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    cJSON_free(post_data);
    return reply;
}

// ============================================================
// HTTP HELPERS
// ============================================================

static std::string ReadReqBody(httpd_req_t* req, size_t max_len) {
    if (req->content_len <= 0
        || (size_t)req->content_len > max_len)
        return "";

    std::string body((size_t)req->content_len, '\0');
    int received = 0;

    while (received < req->content_len) {
        int r = httpd_req_recv(
            req, &body[received], req->content_len - received);
        if (r <= 0) return "";
        received += r;
    }
    return body;
}

static esp_err_t SendJson(httpd_req_t* req, cJSON* json) {
    char* out = cJSON_PrintUnformatted(json);
    if (!out) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "JSON error", HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    cJSON_free(out);
    return result;
}

static esp_err_t RootGetHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
}

// ============================================================
// LOGIN
// ============================================================

static esp_err_t LoginPostHandler(httpd_req_t* req) {
    std::string body = ReadReqBody(req, 1024);
    cJSON* root = cJSON_Parse(body.c_str());

    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "JSON inválido", HTTPD_RESP_USE_STRLEN);
    }

    cJSON* em = cJSON_GetObjectItem(root, "email");
    cJSON* pw = cJSON_GetObjectItem(root, "pass");

    std::string email = cJSON_IsString(em) ? em->valuestring : "";
    std::string pass = cJSON_IsString(pw) ? pw->valuestring : "";
    cJSON_Delete(root);

    if (email.find('@') == std::string::npos || email.size() > 80) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "Correo inválido", HTTPD_RESP_USE_STRLEN);
    }

    bool is_boss = (pass == g_boss_password);
    std::string token = GenerateToken();

    g_sessions.push_back({token, email, is_boss});
    while (g_sessions.size() > 25)
        g_sessions.erase(g_sessions.begin());

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "token", token.c_str());
    cJSON_AddBoolToObject(resp, "is_boss", is_boss);

    esp_err_t err = SendJson(req, resp);
    cJSON_Delete(resp);
    return err;
}

// ============================================================
// LOGS
// ============================================================

static esp_err_t LogsGetHandler(httpd_req_t* req) {
    UserSession sess;
    if (!GetSessionFromReq(req, sess)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
    }

    cJSON* arr = cJSON_CreateArray();
    if (!arr) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "Memory error", HTTPD_RESP_USE_STRLEN);
    }

    for (const auto& chat : g_chats) {
        if (sess.is_boss || chat.email == sess.email) {
            cJSON* obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "email", chat.email.c_str());
            cJSON_AddStringToObject(obj, "q", chat.q.c_str());
            cJSON_AddStringToObject(obj, "a", chat.a.c_str());
            cJSON_AddItemToArray(arr, obj);
        }
    }

    esp_err_t err = SendJson(req, arr);
    cJSON_Delete(arr);
    return err;
}

// ============================================================
// CHAT
// ============================================================

static esp_err_t ChatPostHandler(httpd_req_t* req) {
    UserSession sess;

    if (!GetSessionFromReq(req, sess)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
    }

    std::string body = ReadReqBody(req, 2048);
    cJSON* root = cJSON_Parse(body.c_str());

    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "JSON inválido", HTTPD_RESP_USE_STRLEN);
    }

    cJSON* q = cJSON_GetObjectItem(root, "q");
    std::string question = cJSON_IsString(q) ? q->valuestring : "";
    cJSON_Delete(root);

    if (question.empty()) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "Mensaje vacío", HTTPD_RESP_USE_STRLEN);
    }

    if (question.size() > MAX_QUESTION_LENGTH)
        question.resize(MAX_QUESTION_LENGTH);

    std::string answer = AskExternalAi(question);
    if (answer.size() > MAX_ANSWER_LENGTH)
        answer.resize(MAX_ANSWER_LENGTH);

    g_chats.push_back({sess.email, question, answer});

    while (g_chats.size() > MAX_CHATS)
        g_chats.erase(g_chats.begin());

    bool saved = SaveStateToNvs();
    if (!saved)
        ESP_LOGW(TAG, "El historial o la clave no se pudieron guardar completamente");

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "a", answer.c_str());

    esp_err_t err = SendJson(req, resp);
    cJSON_Delete(resp);
    return err;
}

// ============================================================
// CONFIG API KEY
// ============================================================

static esp_err_t ConfigPostHandler(httpd_req_t* req) {
    UserSession sess;

    if (!GetSessionFromReq(req, sess) || !sess.is_boss) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_send(req, "Forbidden", HTTPD_RESP_USE_STRLEN);
    }

    std::string body = ReadReqBody(req, 512);
    cJSON* root = cJSON_Parse(body.c_str());

    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "JSON inválido", HTTPD_RESP_USE_STRLEN);
    }

    cJSON* k = cJSON_GetObjectItem(root, "api_key");
    if (!cJSON_IsString(k) || strlen(k->valuestring) == 0
        || strlen(k->valuestring) > 250) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "API Key inválida", HTTPD_RESP_USE_STRLEN);
    }

    g_stored_api_key = k->valuestring;
    cJSON_Delete(root);

    if (!SaveStateToNvs()) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "No se pudo guardar", HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, R"json({"status":"ok"})json",
        HTTPD_RESP_USE_STRLEN);
}

// ============================================================
// WEB SERVER
// ============================================================

static void StartWebPortal() {
    if (g_httpd != nullptr) return;

    LoadStateFromNvs();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 10240;
    config.max_uri_handlers = 8;

    if (httpd_start(&g_httpd, &config) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar servidor web");
        g_httpd = nullptr;
        return;
    }

    httpd_uri_t root_uri = {
        .uri="/", .method=HTTP_GET,
        .handler=RootGetHandler, .user_ctx=nullptr
    };
    httpd_uri_t login_uri = {
        .uri="/api/login", .method=HTTP_POST,
        .handler=LoginPostHandler, .user_ctx=nullptr
    };
    httpd_uri_t logs_uri = {
        .uri="/api/logs", .method=HTTP_GET,
        .handler=LogsGetHandler, .user_ctx=nullptr
    };
    httpd_uri_t chat_uri = {
        .uri="/api/chat", .method=HTTP_POST,
        .handler=ChatPostHandler, .user_ctx=nullptr
    };
    httpd_uri_t cfg_uri = {
        .uri="/api/config", .method=HTTP_POST,
        .handler=ConfigPostHandler, .user_ctx=nullptr
    };

    httpd_register_uri_handler(g_httpd, &root_uri);
    httpd_register_uri_handler(g_httpd, &login_uri);
    httpd_register_uri_handler(g_httpd, &logs_uri);
    httpd_register_uri_handler(g_httpd, &chat_uri);
    httpd_register_uri_handler(g_httpd, &cfg_uri);

    ESP_LOGI(TAG, "Servidor web iniciado en puerto 80");
}

// ============================================================
// MCP: IP
// ============================================================

static ReturnValue HandleGetIpTool(const PropertyList& properties) {
    esp_netif_t* netif =
        esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    esp_netif_ip_info_t ip_info;

    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char ip_buf[IP4ADDR_STRLEN_MAX] = {0};
        esp_ip4addr_ntoa(&ip_info.ip, ip_buf, sizeof(ip_buf));
        return std::string("{\"ip\":\"") + ip_buf + "\"}";
    }

    return R"json({"ip":"desconectado"})json";
}

// ============================================================
// SERVO
// ============================================================

class ServoController {
private:
    int current_angle_ = 90;

    uint32_t AngleToDuty(int angle) {
        uint32_t min_duty = 410;
        uint32_t max_duty = 2048;
        return min_duty + ((max_duty - min_duty) * angle) / 180;
    }

    ReturnValue HandleSetAngleTool(const PropertyList& properties) {
        int angle = 90;

        for (const auto& prop : properties) {
            if (prop.name() == "angle") {
                angle = prop.value<int>();
                break;
            }
        }

        SetAngle(angle);

        return std::string("{\"status\":\"ok\",\"angle\":")
            + std::to_string(current_angle_) + "}";
    }

public:
    ServoController(gpio_num_t gpio_num) {
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_14_BIT,
            .timer_num = LEDC_TIMER_1,
            .freq_hz = 50,
            .clk_cfg = LEDC_AUTO_CLK
        };
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        ledc_channel_config_t ledc_channel = {
            .gpio_num = gpio_num,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_1,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_1,
            .duty = AngleToDuty(90),
            .hpoint = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool(
            "self.servo.set_angle",
            "Mueve el servo a un ángulo entre 0 y 180 grados.",
            PropertyList({
                Property("angle", kPropertyTypeInteger, 0, 180)
            }),
            std::bind(&ServoController::HandleSetAngleTool,
                this, std::placeholders::_1));

        mcp_server.AddTool(
            "self.network.get_ip",
            "Devuelve la dirección IP del ESP32 para abrir su web.",
            PropertyList(),
            HandleGetIpTool);
    }

    void SetAngle(int angle) {
        angle = std::max(0, std::min(180, angle));
        current_angle_ = angle;

        ledc_set_duty(LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_1, AngleToDuty(current_angle_));
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

        ESP_LOGI(TAG, "Servo movido a %d grados", current_angle_);
    }
};

// ============================================================
// PLACA
// ============================================================

class CompactWifiBoard : public WifiBoard {
private:
    Button boot_button_;
    Button touch_button_;
    Button asr_button_;

    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
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
                .enable_internal_pullup = 1
            }
        };

        ESP_ERROR_CHECK(i2c_new_master_bus(
            &bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .scl_speed_hz = 400000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0
            }
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(
            display_i2c_bus_, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT)
        };
        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(
            panel_io_, &panel_config, &panel_));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));

        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Error inicializando OLED");
            display_ = new NoDisplay();
            return;
        }

        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(
            panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void OnBootButtonClick() {
        auto& app = Application::GetInstance();

        if (app.GetDeviceState() == kDeviceStateStarting) {
            EnterWifiConfigMode();
            return;
        }

        gpio_set_level(BUILTIN_LED_GPIO, 1);
        app.ToggleChatState();
    }

    void OnAsrButtonClick() {
        std::string wake_word = "你好小智";
        Application::GetInstance().WakeWordInvoke(wake_word);
    }

    void OnTouchPressDown() {
        gpio_set_level(BUILTIN_LED_GPIO, 1);
        Application::GetInstance().StartListening();
    }

    void OnTouchPressUp() {
        gpio_set_level(BUILTIN_LED_GPIO, 0);
        Application::GetInstance().StopListening();
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

        boot_button_.OnClick(std::bind(
            &CompactWifiBoard::OnBootButtonClick, this));

        asr_button_.OnClick(std::bind(
            &CompactWifiBoard::OnAsrButtonClick, this));

        touch_button_.OnPressDown(std::bind(
            &CompactWifiBoard::OnTouchPressDown, this));

        touch_button_.OnPressUp(std::bind(
            &CompactWifiBoard::OnTouchPressUp, this));
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        static ServoController servo(SERVO_GPIO);
        StartWebPortal();
    }

public:
    CompactWifiBoard()
        : WifiBoard(),
          boot_button_(BOOT_BUTTON_GPIO),
          touch_button_(TOUCH_BUTTON_GPIO),
          asr_button_(ASR_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeTools();
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(CompactWifiBoard);
