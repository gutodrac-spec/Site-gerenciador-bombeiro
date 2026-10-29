# Drone solar — ESP32 + GoPro HERO7 Black

Primeira etapa: o ESP32 controlando a GoPro pelo Wi-Fi. Dá para testar
tudo em cima da mesa, antes de ter o avião.

## 1. Preparar a GoPro

1. Atualize a GoPro para o firmware mais recente (pelo app GoPro Quik).
2. Na câmera: **Preferências > Conexões > Conexões sem fio = Ligado**.
3. Ainda em **Conexões > Informações da câmera**, anote o **nome da rede**
   (ex: `GP12345678`) e a **senha**.

> Se a GoPro nunca foi pareada com um celular, pareie uma vez com o app
> Quik. Isso ativa a rede Wi-Fi dela.

## 2. Preparar a Arduino IDE

1. Instale a [Arduino IDE](https://www.arduino.cc/en/software).
2. Em **Arquivo > Preferências > URLs adicionais**, cole:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Em **Ferramentas > Placa > Gerenciador de placas**, instale **esp32**
   (da Espressif).
4. Escolha a placa **ESP32 Dev Module** (ou o modelo da sua placa).

## 3. Gravar o código

1. Abra `esp32-gopro/esp32-gopro.ino`.
2. No topo do arquivo, troque `GOPRO_SSID` e `GOPRO_SENHA` pelos dados da
   sua câmera.
3. Ligue o ESP32 no USB e clique em **Carregar**.
4. Abra o **Monitor Serial** em **115200**.

## 4. Usar

| Ação | Como |
|---|---|
| Começar/parar gravação | Botão **BOOT** da placa, ou `g` / `p` no Monitor Serial |
| Tirar foto | `f` |
| Ver status | `s` (ou aparece sozinho a cada 5 s) |

O LED azul da placa acende enquanto a GoPro está gravando.

Para o drone, mude `GRAVAR_AO_CONECTAR` para `true`: assim que o ESP32
achar a GoPro, ela começa a gravar sozinha.

## Problemas comuns

- **"falhou. A GoPro está com o Wi-Fi ligado?"**: a GoPro desliga o Wi-Fi
  depois de um tempo parada. Ligue de novo pelo menu de Conexões.
- **Conecta, mas o comando falha**: deixe só o ESP32 conectado. Feche o
  app Quik no celular, porque a GoPro aceita um controle por vez.
- **Placa sem LED no pino 2**: troque `PINO_LED` no código.

## Próximas etapas

- [ ] Ligar o sensor ao ESP32 e mandar a leitura junto com o status
- [ ] Telemetria por rádio LoRa até a base
- [ ] Ligar o ESP32 na controladora de voo (MAVLink)
- [ ] Painel no site mostrando o drone no mapa
