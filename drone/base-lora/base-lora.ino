/*
 * Drone solar dos bombeiros — estação base em terra
 *
 * Placa: LilyGO TTGO LoRa32 V2.1 (a mesma do avião)
 * Biblioteca: "LoRa" de Sandeep Mistry
 *
 * - Recebe a telemetria do avião pelo rádio LoRa
 * - Cria a rede Wi-Fi "Drone-Bombeiro". Conecte o celular nela e abra
 *   http://192.168.4.1 para ver o painel, os focos de incêndio que a IA
 *   achar, comandar a GoPro e mandar o avião voltar à missão
 * - Também imprime cada pacote no Monitor Serial (115200)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <LoRa.h>

// ---------- CONFIGURE AQUI (rádio igual ao config.h do avião) ----------
const char *REDE_NOME = "Drone-Bombeiro";
const char *REDE_SENHA = "bombeiro193";  // mínimo 8 caracteres
#define LORA_FREQ 915E6
#define LORA_SF 9
#define LORA_SYNC 0xB0
#define LORA_POTENCIA_DBM 17
#define LORA_CHAVE "BMB"

#define PINO_LORA_SCK 5
#define PINO_LORA_MISO 19
#define PINO_LORA_MOSI 27
#define PINO_LORA_SS 18
#define PINO_LORA_RST 23
#define PINO_LORA_DIO0 26
// ------------------------------------------------------------------------

WebServer servidor(80);

String ultimaLinha = "";
int ultimoRssi = 0;
float ultimoSnr = 0;
uint32_t ultimaRecepcaoMs = 0;
uint16_t idComando = 0;

const char PAGINA[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Drone Bombeiro</title>
<style>
body{margin:0;font-family:system-ui,sans-serif;background:#111;color:#eee}
header{background:#b71c1c;padding:12px 16px;font-weight:700;font-size:18px}
main{padding:12px;display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.c{background:#1e1e1e;border-radius:10px;padding:10px}
.r{font-size:12px;color:#aaa}.v{font-size:22px;font-weight:700;margin-top:4px}
.alerta{background:#e65100}.ok{color:#66bb6a}.ruim{color:#ef5350}
.fogo{background:#b71c1c;grid-column:1/-1}.fogo .v{font-size:26px}
.botoes{padding:0 12px 16px;display:flex;gap:8px}
button{flex:1;padding:14px;font-size:16px;border:0;border-radius:10px;color:#fff;background:#333}
#g{background:#c62828}#f{background:#1565c0}
a{color:#90caf9}
</style></head><body>
<header>🚒 Drone Bombeiro <span id="sinal" class="r"></span></header>
<main id="cards"></main>
<div class="botoes">
<button id="g" onclick="cmd('G')">● Gravar</button>
<button onclick="cmd('P')">■ Parar</button>
<button id="f" onclick="cmd('F')">📷 Foto</button>
</div>
<div class="botoes">
<button onclick="cmd('M')">↩ Voltar à missão</button>
<button id="ia" onclick="cmd(iaLigada?'D':'A')">IA</button>
</div>
<script>
const GOPRO=['desconectada','parada','GRAVANDO'];
const IA=['desligada','vigiando','CIRCULANDO FOCO'];
let iaLigada=true;
function card(r,v,cls){return `<div class="c ${cls||''}"><div class="r">${r}</div><div class="v">${v}</div></div>`}
function mostrar(d){
  const s=document.getElementById('sinal');
  if(!d.linha){s.textContent=' · aguardando avião...';return}
  const x=d.linha.split(','), n=i=>Number(x[i]);
  const idade=Math.round(d.idade_ms/1000);
  s.textContent=` · sinal ${d.rssi} dBm · há ${idade}s`;
  s.className=idade>10?'ruim':'r';
  const lat=n(2)/1e7, lon=n(3)/1e7;
  const solarW=(n(16)*n(17)/1e6).toFixed(1);
  const lidar=n(18)==-2?'sem sensor':n(18)==-1?'sem retorno':(n(18)/100).toFixed(1)+' m';
  let h='';
  h+=card('Modo',(n(8)?'ARMADO · ':'')+x[9],n(10)?'':'alerta');
  h+=card('Altitude',(n(4)/10).toFixed(0)+' m');
  h+=card('Velocidade',(n(5)/10*3.6).toFixed(0)+' km/h');
  h+=card('Bateria',(n(11)/1000).toFixed(2)+' V'+(n(13)>=0?' · '+n(13)+'%':''));
  h+=card('Consumo',(n(11)*n(12)/1e5).toFixed(0)+' W');
  h+=card('Solar',solarW+' W');
  h+=card(n(19)?'⚠ OBSTÁCULO':'LiDAR',lidar,n(19)?'alerta':'');
  h+=card('GPS',n(14)+' satélites',n(15)<3?'alerta':'');
  h+=card('GoPro',GOPRO[n(20)]+(n(21)>=0?' · '+n(21)+'%':''),n(20)==2?'ok':'');
  h+=card('Posição',`<a href="https://maps.google.com/?q=${lat},${lon}">${lat.toFixed(5)}, ${lon.toFixed(5)}</a>`);
  if(x.length>28){
    iaLigada=n(24)==1;
    h+=card('IA',IA[n(23)]+(iaLigada?' · pode assumir':' · só avisa'),n(23)==2?'alerta':'');
    if(n(25)>-999){
      const fl=n(26)/1e7, fo=n(27)/1e7, ida=n(28);
      const quando=ida<60?ida+' s':Math.round(ida/60)+' min';
      h=card('🔥 FOCO DE INCÊNDIO · '+n(25)+' °C · há '+quando,
        `<a href="https://maps.google.com/?q=${fl},${fo}">${fl.toFixed(5)}, ${fo.toFixed(5)}</a>`,'fogo')+h;
    }
    document.getElementById('ia').textContent=iaLigada?'🤖 IA: só avisar':'🤖 IA: pode assumir';
  }
  document.getElementById('cards').innerHTML=h;
}
function atualizar(){fetch('/dados').then(r=>r.json()).then(mostrar).catch(()=>{})}
function cmd(c){fetch('/cmd?c='+c).then(r=>r.text()).then(t=>alert(t))}
setInterval(atualizar,1000);atualizar();
</script></body></html>)HTML";

void paginaInicial() { servidor.send(200, "text/html", PAGINA); }

void dados() {
  String json = "{\"linha\":\"" + ultimaLinha + "\",\"rssi\":" + String(ultimoRssi) +
                ",\"snr\":" + String(ultimoSnr, 1) +
                ",\"idade_ms\":" + String(millis() - ultimaRecepcaoMs) + "}";
  servidor.send(200, "application/json", json);
}

// Manda o comando 3 vezes (o avião pode estar transmitindo); o id evita repetir a ação
void comando() {
  String c = servidor.arg("c");
  if (c.length() != 1 || String("GPFMAD").indexOf(c) < 0) {
    servidor.send(400, "text/plain", "Comando inválido");
    return;
  }
  String msg = String("CMD,") + LORA_CHAVE + "," + c + "," + String(++idComando);
  for (int i = 0; i < 3; i++) {
    LoRa.beginPacket();
    LoRa.print(msg);
    LoRa.endPacket();
    delay(250);
  }
  Serial.println("Enviado: " + msg);
  servidor.send(200, "text/plain", "Comando enviado ao drone");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Base do drone solar ===");

  SPI.begin(PINO_LORA_SCK, PINO_LORA_MISO, PINO_LORA_MOSI, PINO_LORA_SS);
  LoRa.setPins(PINO_LORA_SS, PINO_LORA_RST, PINO_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("ERRO: rádio LoRa não encontrado. Confira os pinos.");
  }
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSyncWord(LORA_SYNC);
  LoRa.setTxPower(LORA_POTENCIA_DBM);
  LoRa.enableCrc();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(REDE_NOME, REDE_SENHA);
  Serial.printf("Wi-Fi \"%s\" criado. Abra http://%s\n", REDE_NOME, WiFi.softAPIP().toString().c_str());

  servidor.on("/", paginaInicial);
  servidor.on("/dados", dados);
  servidor.on("/cmd", comando);
  servidor.begin();
}

void loop() {
  if (LoRa.parsePacket() > 0) {
    String linha;
    while (LoRa.available()) linha += (char)LoRa.read();
    if (linha.startsWith("BMB,")) {
      ultimaLinha = linha;
      ultimoRssi = LoRa.packetRssi();
      ultimoSnr = LoRa.packetSnr();
      ultimaRecepcaoMs = millis();
      Serial.printf("%s  (RSSI %d dBm, SNR %.1f)\n", linha.c_str(), ultimoRssi, ultimoSnr);
    }
  }
  servidor.handleClient();
}
