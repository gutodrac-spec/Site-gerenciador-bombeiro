/*
 * ESP32 + GoPro HERO7 Black — controle de gravação pelo Wi-Fi
 *
 * O ESP32 se conecta na rede Wi-Fi da própria GoPro e manda comandos HTTP
 * para ela (endereço fixo 10.5.5.9). Dá para:
 *   - apertar o botão BOOT da placa para começar/parar a gravação;
 *   - mandar comandos pelo Monitor Serial (115200 baud);
 *   - ver bateria e tempo de gravação a cada 5 segundos.
 *
 * Placa: qualquer ESP32 (DevKit, TTGO, etc.). Não precisa de bibliotecas
 * extras — só o pacote "esp32" da Espressif na Arduino IDE.
 */

#include <WiFi.h>
#include <HTTPClient.h>

// ======= CONFIGURE AQUI =======
// Na GoPro: Preferências > Conexões > Informações da câmera
const char *GOPRO_SSID = "GP12345678";
const char *GOPRO_SENHA = "senha-da-gopro";

// true = começa a gravar sozinho assim que conectar (útil no drone)
const bool GRAVAR_AO_CONECTAR = false;
// ==============================

const int PINO_BOTAO = 0;  // botão BOOT da maioria das placas ESP32
const int PINO_LED = 2;    // LED azul da placa (aceso = gravando)

const char *GOPRO_URL = "http://10.5.5.9/gp/gpControl";
const unsigned long INTERVALO_STATUS_MS = 5000;

bool gravando = false;
bool jaGravouAoConectar = false;
unsigned long ultimoStatus = 0;

// Envia um comando para a GoPro. Retorna true se ela respondeu 200 OK.
bool comandoGoPro(const String &caminho, String *resposta) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(3000);
  http.begin(String(GOPRO_URL) + caminho);
  int codigo = http.GET();
  if (resposta && codigo == HTTP_CODE_OK) *resposta = http.getString();
  http.end();

  if (codigo != HTTP_CODE_OK) {
    Serial.printf("[GoPro] Falha em %s (código %d)\n", caminho.c_str(), codigo);
    return false;
  }
  return true;
}

bool modoVideo() { return comandoGoPro("/command/mode?p=0", nullptr); }
bool modoFoto() { return comandoGoPro("/command/mode?p=1", nullptr); }

bool iniciarGravacao() {
  modoVideo();
  delay(300);
  return comandoGoPro("/command/shutter?p=1", nullptr);
}

bool pararGravacao() { return comandoGoPro("/command/shutter?p=0", nullptr); }

bool tirarFoto() {
  modoFoto();
  delay(300);
  return comandoGoPro("/command/shutter?p=1", nullptr);
}

// Lê um valor numérico do bloco "status" do JSON da GoPro, ex: "70":85
int lerStatus(const String &json, const char *chave) {
  int fimStatus = json.indexOf("\"settings\"");
  String bloco = fimStatus > 0 ? json.substring(0, fimStatus) : json;

  String busca = String("\"") + chave + "\":";
  int pos = bloco.indexOf(busca);
  if (pos < 0) return -1;
  return bloco.substring(pos + busca.length()).toInt();
}

void atualizarStatus() {
  String json;
  if (!comandoGoPro("/status", &json)) return;

  // Chaves do status da HERO7: 10 = gravando, 13 = segundos de vídeo,
  // 70 = bateria em %, 35 = minutos de vídeo restantes no cartão
  gravando = lerStatus(json, "10") == 1;
  int segundos = lerStatus(json, "13");
  int bateria = lerStatus(json, "70");
  int restante = lerStatus(json, "35");

  digitalWrite(PINO_LED, gravando ? HIGH : LOW);
  Serial.printf("[GoPro] %s | tempo %02d:%02d | bateria %d%% | cartão ~%d min\n",
                gravando ? "GRAVANDO" : "parada",
                segundos / 60, segundos % 60, bateria, restante / 60);
}

void conectarGoPro() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[Wi-Fi] Conectando em %s", GOPRO_SSID);
  WiFi.begin(GOPRO_SSID, GOPRO_SENHA);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(500);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" conectado!");
  } else {
    Serial.println(" falhou. A GoPro está com o Wi-Fi ligado?");
    WiFi.disconnect();
  }
}

void mostrarAjuda() {
  Serial.println();
  Serial.println("Comandos (digite no Monitor Serial):");
  Serial.println("  g = começar a gravar");
  Serial.println("  p = parar de gravar");
  Serial.println("  f = tirar foto");
  Serial.println("  s = mostrar status agora");
  Serial.println("  ? = esta ajuda");
  Serial.println("Ou aperte o botão BOOT para começar/parar a gravação.");
}

void tratarSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case 'g': Serial.println(iniciarGravacao() ? "Gravando." : "Não consegui gravar."); break;
    case 'p': Serial.println(pararGravacao() ? "Gravação parada." : "Não consegui parar."); break;
    case 'f': Serial.println(tirarFoto() ? "Foto tirada." : "Não consegui tirar foto."); break;
    case 's': atualizarStatus(); break;
    case '?': mostrarAjuda(); break;
  }
}

void tratarBotao() {
  static bool anterior = HIGH;
  bool atual = digitalRead(PINO_BOTAO);
  if (anterior == HIGH && atual == LOW) {
    delay(50);  // debounce
    if (gravando) pararGravacao(); else iniciarGravacao();
    delay(500);
    atualizarStatus();
  }
  anterior = atual;
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_LED, OUTPUT);
  WiFi.mode(WIFI_STA);

  Serial.println("\n=== ESP32 + GoPro HERO7 ===");
  mostrarAjuda();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    jaGravouAoConectar = false;
    conectarGoPro();
    return;
  }

  if (GRAVAR_AO_CONECTAR && !jaGravouAoConectar) {
    jaGravouAoConectar = iniciarGravacao();
  }

  tratarSerial();
  tratarBotao();

  if (millis() - ultimoStatus > INTERVALO_STATUS_MS) {
    ultimoStatus = millis();
    atualizarStatus();
  }
}
