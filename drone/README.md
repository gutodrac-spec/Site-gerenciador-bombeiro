# Drone solar — ESP32 + GoPro HERO7 Black

| Pasta / arquivo | O que é |
|---|---|
| [`GUIA-MONTAGEM.md`](GUIA-MONTAGEM.md) | **Comece aqui:** lista de compras, ligações e montagem |
| `esp32-gopro/` | Teste de bancada: ESP32 controlando a GoPro (abaixo) |
| `aviao-esp32/` | Código do ESP32 de bordo: LiDAR, MAVLink, solar, GoPro e LoRa. Configure no `config.h` |
| `base-lora/` | Estação base: recebe a telemetria e mostra o painel no celular |
| `ardupilot/drone-solar.param` | Parâmetros para a controladora de voo (ArduPlane) |
| [`modelo-3d/`](modelo-3d/README.md) | **Avião para imprimir em 3D:** peças STL, como imprimir e montar |

O `aviao-esp32` tem também a **IA que procura focos de incêndio** com a
câmera térmica (`ia_fogo.h`); veja a seção 5 do guia.

Instale pelo Gerenciador de Bibliotecas da Arduino IDE: **LoRa** (Sandeep
Mistry), usada no `aviao-esp32` e no `base-lora`, e **Adafruit MLX90640**,
usada no `aviao-esp32`.

## Teste de bancada: ESP32 + GoPro

O ESP32 controlando a GoPro pelo Wi-Fi. Dá para testar tudo em cima da
mesa, antes de ter o avião.

### 1. Preparar a GoPro

1. Atualize a GoPro para o firmware mais recente (pelo app GoPro Quik).
2. Na câmera: **Preferências > Conexões > Conexões sem fio = Ligado**.
3. Ainda em **Conexões > Informações da câmera**, anote o **nome da rede**
   (ex: `GP12345678`) e a **senha**.

> Se a GoPro nunca foi pareada com um celular, pareie uma vez com o app
> Quik. Isso ativa a rede Wi-Fi dela.

### 2. Preparar a Arduino IDE

1. Instale a [Arduino IDE](https://www.arduino.cc/en/software).
2. Em **Arquivo > Preferências > URLs adicionais**, cole:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Em **Ferramentas > Placa > Gerenciador de placas**, instale **esp32**
   (da Espressif).
4. Escolha a placa **ESP32 Dev Module** (ou o modelo da sua placa).

### 3. Gravar o código

1. Abra `esp32-gopro/esp32-gopro.ino`.
2. No topo do arquivo, troque `GOPRO_SSID` e `GOPRO_SENHA` pelos dados da
   sua câmera.
3. Ligue o ESP32 no USB e clique em **Carregar**.
4. Abra o **Monitor Serial** em **115200**.

### 4. Usar

| Ação | Como |
|---|---|
| Começar/parar gravação | Botão **BOOT** da placa, ou `g` / `p` no Monitor Serial |
| Tirar foto | `f` |
| Ver status | `s` (ou aparece sozinho a cada 5 s) |

O LED azul da placa acende enquanto a GoPro está gravando.

Para o drone, mude `GRAVAR_AO_CONECTAR` para `true`: assim que o ESP32
achar a GoPro, ela começa a gravar sozinha.

### Problemas comuns

- **"falhou. A GoPro está com o Wi-Fi ligado?"**: a GoPro desliga o Wi-Fi
  depois de um tempo parada. Ligue de novo pelo menu de Conexões.
- **Conecta, mas o comando falha**: deixe só o ESP32 conectado. Feche o
  app Quik no celular, porque a GoPro aceita um controle por vez.
- **Placa sem LED no pino 2**: troque `PINO_LED` no código.

## Próximas etapas

- [x] LiDAR no ESP32, repassado para o ArduPilot
- [x] Telemetria por rádio LoRa até a base
- [x] ESP32 ligado na controladora de voo (MAVLink)
- [x] IA de bordo procurando focos de incêndio
- [x] Modelo 3D do avião para imprimir
- [ ] Painel no site mostrando o drone no mapa
