"""Cérebro do robô: conversa com o Claude e manda comandos ao Arduino pela serial.

Uso:
    python robo_claude.py --porta COM3            # Windows
    python robo_claude.py --porta /dev/ttyACM0    # Linux
    python robo_claude.py --porta COM3 --manual   # testa comandos sem o Claude
    python robo_claude.py --simular               # sem Arduino (para testar o Claude)
    python robo_claude.py --simular --camera 1    # escolhe outra webcam
    python robo_claude.py --porta COM3 --sem-camera
    python robo_claude.py --testar-camera         # tira uma foto e salva em foto_teste.jpg

Requer a variável de ambiente ANTHROPIC_API_KEY (exceto no modo --manual).
"""

import argparse
import base64
import time

MODELO = "claude-sonnet-5-5"

SISTEMA = """Você é o cérebro de um pequeno robô-carrinho.
Hardware: um motor traseiro (só frente/ré, sem controle de velocidade), direção dianteira
por servo (-40 a +40 graus) e um sensor de distância apontado para frente.
A direção só muda o rumo enquanto o robô anda. Para fazer curva: vire, depois mova.
Ande em passos curtos (300 a 1000 ms) e confira a distância após cada movimento.
Se a distância for menor que 30 cm, não vá para frente: dê ré com a direção virada.
Responda em português, de forma breve."""

SISTEMA_CAMERA = """
Você também tem uma webcam (ferramenta olhar_camera). Use-a para entender o ambiente:
antes de se mover quando não souber o que há à frente, para procurar objetos que o
usuário pedir e para confirmar o resultado de uma manobra. Descreva o que vê só quando
for útil. O sensor de distância é mais confiável que a imagem para medir distâncias."""

MAX_IMAGENS_NO_HISTORICO = 3

TOOLS = [
    {
        "name": "mover",
        "description": "Anda para FRENTE ou TRAS por ms milissegundos (máx 3000). "
        "Para sozinho se houver obstáculo a menos de 20 cm à frente. Retorna a distância à frente em cm.",
        "input_schema": {
            "type": "object",
            "properties": {
                "direcao": {"type": "string", "enum": ["FRENTE", "TRAS"]},
                "ms": {"type": "integer", "minimum": 0, "maximum": 3000},
            },
            "required": ["direcao", "ms"],
        },
    },
    {
        "name": "virar",
        "description": "Posiciona a direção: -40 = máximo para um lado, 0 = reto, +40 = máximo para o outro.",
        "input_schema": {
            "type": "object",
            "properties": {"angulo": {"type": "integer", "minimum": -40, "maximum": 40}},
            "required": ["angulo"],
        },
    },
    {
        "name": "medir_distancia",
        "description": "Lê a distância até o obstáculo à frente, em cm.",
        "input_schema": {"type": "object", "properties": {}},
    },
    {
        "name": "luz",
        "description": "Liga (true) ou desliga (false) a luz.",
        "input_schema": {
            "type": "object",
            "properties": {"ligar": {"type": "boolean"}},
            "required": ["ligar"],
        },
    },
]


TOOL_CAMERA = {
    "name": "olhar_camera",
    "description": "Tira uma foto com a webcam e devolve a imagem para você analisar.",
    "input_schema": {"type": "object", "properties": {}},
}


class Camera:
    def __init__(self, indice=0, largura=800):
        import cv2

        self.cv2 = cv2
        self.largura = largura
        self.cap = cv2.VideoCapture(indice)
        if not self.cap.isOpened():
            raise RuntimeError(f"não consegui abrir a webcam {indice}")
        for _ in range(5):  # descarta os primeiros quadros (exposição ainda ajustando)
            self.cap.read()

    def foto_jpeg(self):
        for _ in range(2):  # esvazia o buffer para pegar um quadro atual
            self.cap.grab()
        ok, quadro = self.cap.read()
        if not ok:
            raise RuntimeError("falha ao ler a webcam")
        h, w = quadro.shape[:2]
        if w > self.largura:
            quadro = self.cv2.resize(quadro, (self.largura, int(h * self.largura / w)))
        ok, jpg = self.cv2.imencode(".jpg", quadro, [self.cv2.IMWRITE_JPEG_QUALITY, 80])
        if not ok:
            raise RuntimeError("falha ao codificar a imagem")
        return jpg.tobytes()

    def fechar(self):
        self.cap.release()


class Arduino:
    def __init__(self, porta):
        import serial

        self.ser = serial.Serial(porta, 115200, timeout=5)
        time.sleep(2)  # o Arduino reinicia ao abrir a serial
        self.ser.reset_input_buffer()

    def enviar(self, cmd):
        self.ser.write((cmd + "\n").encode())
        resposta = self.ser.readline().decode(errors="ignore").strip()
        return resposta or "ERRO sem resposta do Arduino"


class Simulador:
    def __init__(self):
        self.dist = 120

    def enviar(self, cmd):
        nome, _, valor = cmd.partition(" ")
        if nome == "FRENTE":
            self.dist = max(15, self.dist - int(valor) // 20)
        elif nome == "TRAS":
            self.dist += int(valor) // 20
        print(f"  [simulado] {cmd} -> dist {self.dist}")
        return f"OK DIST {self.dist}"


def executar(robo, camera, nome, a):
    """Retorna o conteúdo do tool_result: texto ou lista de blocos (imagem)."""
    if nome == "olhar_camera":
        if camera is None:
            return "ERRO câmera desativada"
        try:
            jpg = camera.foto_jpeg()
        except RuntimeError as e:
            return f"ERRO {e}"
        with open("ultima_foto.jpg", "wb") as f:  # para você conferir o que o Claude viu
            f.write(jpg)
        return [
            {
                "type": "image",
                "source": {
                    "type": "base64",
                    "media_type": "image/jpeg",
                    "data": base64.b64encode(jpg).decode(),
                },
            },
            {"type": "text", "text": robo.enviar("DIST 0")},
        ]
    if nome == "mover":
        return robo.enviar(f"{a['direcao']} {int(a['ms'])}")
    if nome == "virar":
        return robo.enviar(f"VIRAR {int(a['angulo'])}")
    if nome == "medir_distancia":
        return robo.enviar("DIST 0")
    if nome == "luz":
        return robo.enviar(f"LUZ {1 if a['ligar'] else 0}")
    return f"ERRO ferramenta desconhecida: {nome}"


def modo_manual(robo):
    print("Digite comandos (ex: FRENTE 500, VIRAR -30, LUZ 1). 'sair' encerra.")
    while (cmd := input("> ").strip()) != "sair":
        if cmd:
            print(robo.enviar(cmd))


def podar_imagens(msgs, manter=MAX_IMAGENS_NO_HISTORICO):
    """Troca fotos antigas por texto, para o histórico não crescer sem limite."""
    vistas = 0
    for msg in reversed(msgs):
        if msg["role"] != "user" or not isinstance(msg["content"], list):
            continue
        for bloco in msg["content"]:
            if bloco.get("type") != "tool_result" or not isinstance(bloco["content"], list):
                continue
            if any(c.get("type") == "image" for c in bloco["content"]):
                vistas += 1
                if vistas > manter:
                    bloco["content"] = [
                        {"type": "text", "text": "[foto antiga removida]"}
                    ] + [c for c in bloco["content"] if c.get("type") == "text"]


def modo_claude(robo, camera):
    import anthropic

    client = anthropic.Anthropic()
    tools = TOOLS + ([TOOL_CAMERA] if camera else [])
    sistema = SISTEMA + (SISTEMA_CAMERA if camera else "")
    msgs = []
    print("Converse com o robô. 'sair' encerra.")
    while (texto := input("Você: ").strip()) != "sair":
        if not texto:
            continue
        msgs.append({"role": "user", "content": texto})
        while True:
            r = client.messages.create(
                model=MODELO, max_tokens=1024, system=sistema, tools=tools, messages=msgs
            )
            msgs.append({"role": "assistant", "content": r.content})
            if r.stop_reason != "tool_use":
                print("Robô:", "".join(b.text for b in r.content if b.type == "text"))
                break
            resultados = []
            for b in r.content:
                if b.type == "tool_use":
                    saida = executar(robo, camera, b.name, b.input)
                    resumo = saida if isinstance(saida, str) else "[foto] " + saida[-1]["text"]
                    print(f"  {b.name}({b.input}) -> {resumo}")
                    resultados.append({"type": "tool_result", "tool_use_id": b.id, "content": saida})
            msgs.append({"role": "user", "content": resultados})
            podar_imagens(msgs)


def main():
    p = argparse.ArgumentParser(description="Robô controlado pelo Claude")
    p.add_argument("--porta", help="porta serial do Arduino (ex: COM3, /dev/ttyACM0)")
    p.add_argument("--manual", action="store_true", help="envia comandos digitados direto ao Arduino")
    p.add_argument("--simular", action="store_true", help="roda sem Arduino")
    p.add_argument("--camera", type=int, default=0, help="índice da webcam (padrão 0)")
    p.add_argument("--sem-camera", action="store_true", help="não usa a webcam")
    p.add_argument("--testar-camera", action="store_true", help="salva uma foto em foto_teste.jpg e sai")
    args = p.parse_args()

    if args.testar_camera:
        cam = Camera(args.camera)
        with open("foto_teste.jpg", "wb") as f:
            f.write(cam.foto_jpeg())
        cam.fechar()
        print("Foto salva em foto_teste.jpg")
        return

    if args.simular:
        robo = Simulador()
    elif args.porta:
        robo = Arduino(args.porta)
    else:
        p.error("informe --porta ou use --simular")

    if args.manual:
        try:
            modo_manual(robo)
        finally:
            robo.enviar("PARAR 0")
        return

    camera = None
    if not args.sem_camera:
        try:
            camera = Camera(args.camera)
            print(f"Webcam {args.camera} ativa (última foto em ultima_foto.jpg).")
        except (ImportError, RuntimeError) as e:
            print(f"Aviso: seguindo sem câmera ({e}).")

    try:
        modo_claude(robo, camera)
    finally:
        robo.enviar("PARAR 0")
        if camera:
            camera.fechar()


if __name__ == "__main__":
    main()
