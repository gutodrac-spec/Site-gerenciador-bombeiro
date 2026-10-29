"""
Gera as peças do drone solar para impressão 3D (arquivos STL).

    pip install manifold3d numpy
    python gerar_modelo.py

Saída:
  stl/            uma peça por arquivo, já na posição de impressão
  montagem.stl    o avião montado (só para visualizar)
  pecas.txt       lista de peças, quantidades e peso estimado

Eixos da montagem: X = do nariz para a cauda, Y = envergadura, Z = para cima.
Todas as medidas em milímetros. Mude os valores abaixo e rode de novo.
"""

import math
import os

import numpy as np
from manifold3d import CrossSection, Manifold

# ============================ MEDIDAS PRINCIPAIS ============================
# Asa
CORDA = 220.0              # cabe uma célula SunPower de 125 mm com folga
SEGMENTO = 190.0           # altura de impressão de cada pedaço de asa
SEGMENTOS_RETOS = 6        # por lado
PONTA_ENVERG = 160.0       # pedaço da ponta (afinando)
PONTA_CORDA = 140.0
PERFIL_ASA = "4412"        # NACA 4 dígitos
ASA_X = 220.0              # bordo de ataque, medido da parede de fogo
ASA_Z = 52.0               # altura da linha da corda
AILERON_DE = 0.75          # aileron começa em 75% da corda
LONGARINA = (0.25, 10.2)   # posição (fração da corda) e furo (mm): tubo carbono 10 mm
LONGARINA_TRAS = (0.60, 6.2)
TUBO_FIOS = (0.45, 8.0)

# Cauda
PROFUNDOR_CORDA = 130.0
PROFUNDOR_METADE = 240.0
PROFUNDOR_HASTE = 0.70     # profundor (parte móvel) a partir de 70%
LEME_RAIZ, LEME_PONTA, LEME_ALTURA = 170.0, 110.0, 220.0
LEME_HASTE = 0.65
CAUDA_X = 1032.0           # bordo de ataque do estabilizador

# Fuselagem
PAREDE = 1.6               # espessura da casca (4 perímetros de 0,4 mm)
TUBO_CAUDA = 16.4          # furo para tubo de carbono de 16 mm
CORTES = (200.0, 400.0)    # onde a fuselagem é dividida para caber na impressora
# estação x: (largura, altura, centro z)
ESTACOES = [
    (0, 40, 40, 0),
    (15, 58, 60, 2),
    (40, 78, 82, 6),
    (90, 88, 98, 10),
    (170, 88, 100, 14),
    (220, 88, 100, 14),
    (350, 86, 96, 14),
    (440, 72, 84, 14),
    (510, 50, 58, 9),
    (570, 36, 38, 5),
    (610, 34, 36, 4),
]
OCO_INICIO, OCO_FIM = 4.0, 598.0   # parede de fogo de 4 mm; tampa da cauda de 12 mm
# Na barriga, de x a x. A GoPro fica logo atrás da bateria para ajudar no equilíbrio.
JANELA_GOPRO = (120.0, 190.0)
JANELA_LIDAR = (210.0, 240.0)
FURO_TERMICA_X = 255.0

SEG_CIRCULO = 48
# ===========================================================================

AQUI = os.path.dirname(os.path.abspath(__file__))
PASTA_STL = os.path.join(AQUI, "stl")


# --------------------------------------------------------------- utilidades
def naca4(codigo, corda, n=90):
    """Contorno de um perfil NACA 4 dígitos, sentido anti-horário."""
    m = int(codigo[0]) / 100
    p = int(codigo[1]) / 10
    t = int(codigo[2:]) / 100
    beta = np.linspace(0, math.pi, n)
    x = (1 - np.cos(beta)) / 2  # mais pontos no bordo de ataque e de fuga
    yt = 5 * t * (0.2969 * np.sqrt(x) - 0.1260 * x - 0.3516 * x**2
                  + 0.2843 * x**3 - 0.1036 * x**4)  # bordo de fuga fechado
    if m == 0:
        yc = np.zeros_like(x)
        dyc = np.zeros_like(x)
    else:
        yc = np.where(x < p, m / p**2 * (2 * p * x - x**2),
                      m / (1 - p)**2 * ((1 - 2 * p) + 2 * p * x - x**2))
        dyc = np.where(x < p, 2 * m / p**2 * (p - x), 2 * m / (1 - p)**2 * (p - x))
    th = np.arctan(dyc)
    xu, yu = x - yt * np.sin(th), yc + yt * np.cos(th)
    xl, yl = x + yt * np.sin(th), yc - yt * np.cos(th)
    # bordo de fuga -> extradorso -> bordo de ataque -> intradorso -> bordo de fuga
    xs = np.concatenate([xu[::-1], xl[1:-1]])
    ys = np.concatenate([yu[::-1], yl[1:-1]])
    return np.column_stack([xs * corda, ys * corda])


def linha_media(codigo, frac):
    """Altura da linha média (fração da corda) num ponto do perfil."""
    m = int(codigo[0]) / 100
    p = int(codigo[1]) / 10
    if m == 0:
        return 0.0
    x = frac
    if x < p:
        return m / p**2 * (2 * p * x - x**2)
    return m / (1 - p)**2 * ((1 - 2 * p) + 2 * p * x - x**2)


def extradorso(codigo, frac):
    """Altura do extradorso (fração da corda) num ponto do perfil."""
    t = int(codigo[2:]) / 100
    x = frac
    yt = 5 * t * (0.2969 * math.sqrt(x) - 0.1260 * x - 0.3516 * x**2
                  + 0.2843 * x**3 - 0.1036 * x**4)
    return linha_media(codigo, frac) + yt


def chanfro_movel(xh, z_dobradica, corda):
    """Região da parte móvel: começa em xh no extradorso (onde fica a fita
    que serve de dobradiça) e desce em 45° para trás, para poder mexer
    para cima e para baixo."""
    z = z_dobradica
    return CrossSection([np.array([
        (xh + z + 60, -60), (corda + 10, -60), (corda + 10, z + 30), (xh - 30, z + 30)])])


def secao(pontos):
    return CrossSection([pontos])


def circulo(cx, cy, d):
    return CrossSection.circle(d / 2, SEG_CIRCULO).translate((cx, cy))


def retangulo(x0, y0, x1, y1):
    return CrossSection([np.array([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])])


def caixa(x0, y0, z0, x1, y1, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def cilindro(eixo, centro, d, comprimento):
    """Cilindro ao longo do eixo 'x', 'y' ou 'z', centrado em 'centro'."""
    c = Manifold.cylinder(comprimento, d / 2, d / 2, SEG_CIRCULO, True)
    if eixo == "x":
        c = c.rotate((0, 90, 0))
    elif eixo == "y":
        c = c.rotate((90, 0, 0))
    return c.translate(centro)


def unir(partes):
    return Manifold.batch_boolean(partes, manifold3d_op_add()) if len(partes) > 1 else partes[0]


def manifold3d_op_add():
    from manifold3d import OpType
    return OpType.Add


def salvar_stl(peca, caminho):
    malha = peca.to_mesh()
    v = np.asarray(malha.vert_properties)[:, :3].astype(np.float32)
    f = np.asarray(malha.tri_verts)
    tri = v[f]
    normais = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    comp = np.linalg.norm(normais, axis=1, keepdims=True)
    normais = (normais / np.where(comp == 0, 1, comp)).astype(np.float32)
    dados = np.zeros(len(f), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    dados["n"] = normais
    dados["v"] = tri
    with open(caminho, "wb") as arq:
        arq.write(b"Drone solar bombeiros".ljust(80, b" "))
        arq.write(np.uint32(len(f)).tobytes())
        arq.write(dados.tobytes())


def no_chao(peca):
    """Coloca a peça encostada em z = 0 e centrada em x/y (posição de impressão)."""
    (x0, y0, z0, x1, y1, z1) = peca.bounding_box()
    return peca.translate((-(x0 + x1) / 2, -(y0 + y1) / 2, -z0))


# ------------------------------------------------------------------ asa
# Cada pedaço é desenhado como é impresso: perfil no plano XY (X = corda,
# Y = espessura) e envergadura no eixo Z.
def perfil_asa(corda=CORDA):
    return secao(naca4(PERFIL_ASA, corda))


def furo_no_perfil(frac, d, corda=CORDA):
    return circulo(frac * corda, linha_media(PERFIL_ASA, frac) * corda, d)


def corte_aileron(corda=CORDA):
    """Região da parte móvel (para tirar da asa)."""
    return retangulo(AILERON_DE * corda, -50, corda + 10, 50)


def aileron_secao(corda=CORDA, folga=1.0):
    xh = AILERON_DE * corda + folga
    z = extradorso(PERFIL_ASA, AILERON_DE) * corda
    return perfil_asa(corda) ^ chanfro_movel(xh, z, corda)


def segmento_asa(tipo):
    s = perfil_asa()
    s = s - furo_no_perfil(*LONGARINA)
    s = s - furo_no_perfil(*TUBO_FIOS)
    if tipo == "A":
        s = s - furo_no_perfil(*LONGARINA_TRAS)
    if tipo in ("B", "C"):
        s = s - corte_aileron()
    peca = Manifold.extrude(s, SEGMENTO)

    if tipo == "B":
        # Alojamento do servo 9 g (por baixo) + canal até o tubo dos fios
        x0 = 0.33 * CORDA
        baixo = linha_media(PERFIL_ASA, 0.36) * CORDA - 14
        meio = SEGMENTO / 2
        peca = peca - caixa(x0, baixo - 10, meio - 11.75, x0 + 12.5, baixo + 18, meio + 11.75)
        xf = TUBO_FIOS[0] * CORDA
        peca = peca - caixa(x0 + 6, baixo + 8, meio - 3, xf, baixo + 14, meio + 3)
    return peca


def ponta_asa():
    """Ponta afinando de 220 para 140 mm, em torno da linha da longarina."""
    ref = LONGARINA[0] * CORDA
    s = perfil_asa().translate((-ref, 0))
    escala = PONTA_CORDA / CORDA
    peca = Manifold.extrude(s, PONTA_ENVERG, 0, 0, (escala, escala))
    return peca.translate((ref, 0, 0))


def aileron():
    return Manifold.extrude(aileron_secao(), SEGMENTO - 1.0)


# ------------------------------------------------------------------ cauda
def estabilizador():
    s = secao(naca4("0009", PROFUNDOR_CORDA))
    s = s - circulo(0.25 * PROFUNDOR_CORDA, 0, 4.2)
    s = s - retangulo(PROFUNDOR_HASTE * PROFUNDOR_CORDA, -20, PROFUNDOR_CORDA + 5, 20)
    return Manifold.extrude(s, PROFUNDOR_METADE)


def profundor():
    c = PROFUNDOR_CORDA
    z = extradorso("0009", PROFUNDOR_HASTE) * c
    s = secao(naca4("0009", c)) ^ chanfro_movel(PROFUNDOR_HASTE * c + 1, z, c)
    s = s - circulo(0.85 * c, 0, 2.2)  # vareta de carbono 2 mm une os dois lados
    return Manifold.extrude(s, PROFUNDOR_METADE - 1)


def deriva(parte):
    """Deriva (fixa) ou leme (móvel), afinando em torno da linha da dobradiça."""
    ref = LEME_HASTE * LEME_RAIZ
    s = secao(naca4("0009", LEME_RAIZ)).translate((-ref, 0))
    if parte == "deriva":
        s = s ^ retangulo(-LEME_RAIZ, -20, 0, 20)
    else:
        z = extradorso("0009", LEME_HASTE) * LEME_RAIZ
        s = s ^ chanfro_movel(1.0, z, LEME_RAIZ)
    escala = LEME_PONTA / LEME_RAIZ
    return Manifold.extrude(s, LEME_ALTURA, 0, 0, (escala, escala)).translate((ref, 0, 0))


def suporte_cauda():
    """Luva do tubo de carbono + centro do estabilizador + base da deriva.
    Coordenadas da montagem com origem no bordo de ataque do estabilizador."""
    c = PROFUNDOR_CORDA
    centro = secao(naca4("0009", c)) - retangulo(PROFUNDOR_HASTE * c, -20, c + 5, 20)
    centro = centro - circulo(0.25 * c, 0, 4.2)
    # perfil no plano XZ, extrudado em Y (largura de 40 mm)
    bloco = Manifold.extrude(centro, 40).translate((0, 0, -20)).rotate((90, 0, 0))
    base_deriva = caixa(-20, -6, 0, PROFUNDOR_HASTE * c, 6, 0.045 * c + 2)
    meia_esp = 0.045 * c
    zt = -meia_esp - 11 + 2
    luva = cilindro("x", (30, 0, zt), 22, 120) - cilindro("x", (30, 0, zt), TUBO_CAUDA, 122)
    return unir([bloco, base_deriva, luva]), zt


# --------------------------------------------------------------- fuselagem
def pchip(xs, ys, xq):
    """Interpolação suave sem 'ondas' (Fritsch-Carlson)."""
    xs, ys = np.asarray(xs, float), np.asarray(ys, float)
    h = np.diff(xs)
    d = np.diff(ys) / h
    m = np.zeros_like(ys)
    for k in range(1, len(xs) - 1):
        if d[k - 1] * d[k] > 0:
            w1, w2 = 2 * h[k] + h[k - 1], h[k] + 2 * h[k - 1]
            m[k] = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k])
    m[0], m[-1] = d[0], d[-1]
    i = np.clip(np.searchsorted(xs, xq) - 1, 0, len(xs) - 2)
    t = (xq - xs[i]) / h[i]
    h00, h10 = 2 * t**3 - 3 * t**2 + 1, t**3 - 2 * t**2 + t
    h01, h11 = -2 * t**3 + 3 * t**2, t**3 - t**2
    return h00 * ys[i] + h10 * h[i] * m[i] + h01 * ys[i + 1] + h11 * h[i] * m[i + 1]


def superelipse(largura, altura, zc, n=3.0, pontos=96):
    a, b = largura / 2, altura / 2
    t = np.linspace(0, 2 * math.pi, pontos, endpoint=False)
    c, s = np.cos(t), np.sin(t)
    y = a * np.sign(c) * np.abs(c) ** (2 / n)
    z = zc + b * np.sign(s) * np.abs(s) ** (2 / n)
    return np.column_stack([y, z])


def loft_fuselagem(encolher, x_ini, x_fim, passo=10.0):
    """Casca externa (encolher=0) ou interna, entre x_ini e x_fim."""
    est = np.array(ESTACOES, float)
    xs = np.arange(x_ini, x_fim, passo).tolist() + [x_fim]
    fatias = []
    for x in xs:
        w = pchip(est[:, 0], est[:, 1], x) - 2 * encolher
        h = pchip(est[:, 0], est[:, 2], x) - 2 * encolher
        zc = pchip(est[:, 0], est[:, 3], x)
        sec = secao(superelipse(w, h, zc))
        fatias.append(Manifold.extrude(sec, 0.01).translate((0, 0, x)))
    pedacos = [Manifold.batch_hull([fatias[i], fatias[i + 1]]) for i in range(len(fatias) - 1)]
    loft = unir(pedacos)
    # local (y, z, x) -> montagem (x, y, z)
    return loft.transform(np.array([[0, 0, 1, 0], [1, 0, 0, 0], [0, 1, 0, 0]], float))


def asa_na_montagem(peca_impressao, y_raiz, lado):
    """Leva um pedaço de asa da posição de impressão para a montagem."""
    p = peca_impressao.rotate((90, 0, 0))  # espessura -> Z, envergadura -> -Y
    if lado == "D":
        p = p.mirror((0, 1, 0))
        return p.translate((ASA_X, y_raiz, ASA_Z))
    return p.translate((ASA_X, -y_raiz, ASA_Z))


def bloco_da_asa():
    """Asa inteira como um bloco (para recortar o berço na fuselagem)."""
    return Manifold.extrude(perfil_asa(), 400).translate((0, 0, -200)).rotate((90, 0, 0)) \
        .translate((ASA_X, 0, ASA_Z))


def fuselagem():
    externa = loft_fuselagem(0, 0, ESTACOES[-1][0])
    interna = loft_fuselagem(PAREDE, OCO_INICIO, OCO_FIM)
    est = np.array(ESTACOES, float)
    x_fim, z_fim = est[-1, 0], est[-1, 3]
    # luva do tubo da cauda e guias das varetas, por dentro do cone
    luva = cilindro("x", (x_fim - 35, 0, z_fim), TUBO_CAUDA + 4, 70)
    guias = [cilindro("x", (x_fim - 30, 0, z_fim + 12.5), 6, 60),
             cilindro("x", (x_fim - 30, 12.5, z_fim), 6, 60)]
    corpo = (externa - interna) + (unir([luva] + guias) ^ externa)
    furos = [
        # tubo da cauda e saída das varetas (profundor em cima, leme ao lado)
        cilindro("x", (x_fim - 30, 0, z_fim), TUBO_CAUDA, 70),
        cilindro("x", (x_fim - 40, 0, z_fim + 12.5), 3.2, 120),
        cilindro("x", (x_fim - 40, 12.5, z_fim), 3.2, 120),
        # parede de fogo: motor 2216 (furos M3 em 16 e 19 mm) + ventilação
        cilindro("x", (0, 0, 0), 10, 12),
        cilindro("x", (0, 8, 0), 3.3, 12), cilindro("x", (0, -8, 0), 3.3, 12),
        cilindro("x", (0, 0, 9.5), 3.3, 12), cilindro("x", (0, 0, -9.5), 3.3, 12),
        # varetas para os elásticos que prendem a asa
        cilindro("y", (ASA_X - 30, 0, 40), 5.2, 200),
        cilindro("y", (ASA_X + CORDA + 20, 0, 40), 5.2, 200),
        # janelas na barriga: GoPro, LiDAR e câmera térmica
        caixa(JANELA_GOPRO[0], -24, -60, JANELA_GOPRO[1], 24, 0),
        caixa(JANELA_LIDAR[0], -12, -60, JANELA_LIDAR[1], 12, 0),
        cilindro("z", (FURO_TERMICA_X, 0, -30), 14, 40),
    ]
    for ang in (45, 135, 225, 315):
        a = math.radians(ang)
        furos.append(cilindro("x", (0, 13 * math.cos(a), 13 * math.sin(a)), 5, 12))
    corpo = corpo - unir(furos)
    return corpo - bloco_da_asa(), externa, interna


def colar(x, interna_menor):
    """Anel de encaixe colado na peça da frente, entra 10 mm na peça de trás."""
    fatia = caixa(x - 0.01, -100, -100, x + 10, 100, 200)
    folga = loft_fuselagem(PAREDE + 0.15, OCO_INICIO, OCO_FIM)
    anel = (folga - interna_menor) ^ fatia
    return anel - bloco_da_asa()


def pecas_fuselagem():
    corpo, _, _ = fuselagem()
    interna_menor = loft_fuselagem(2 * PAREDE + 0.15, OCO_INICIO, OCO_FIM)
    c1, c2 = CORTES
    nariz = corpo ^ caixa(-50, -100, -100, c1, 100, 200)
    meio = corpo ^ caixa(c1, -100, -100, c2, 100, 200)
    cauda = corpo ^ caixa(c2, -100, -100, 700, 100, 200)
    nariz = nariz + colar(c1, interna_menor)
    meio = meio + colar(c2, interna_menor)
    return nariz, meio, cauda


# --------------------------------------------------------- suportes internos
def suporte_gopro():
    """Base com os 3 dedos padrão GoPro (parafuso M5). Cole na parede interna."""
    base = caixa(-20, -15, 0, 20, 15, 3)
    dedos = []
    for y in (-6.1, 0, 6.1):
        d = caixa(-7.5, y - 1.5, 0, 7.5, y + 1.5, 10.5)
        topo = cilindro("y", (0, y, 10.5), 15, 3)
        dedos.append(d + topo)
    peca = unir([base] + dedos)
    return peca - cilindro("y", (0, 0, 10.5), 5.2, 40)


def berco_lidar():
    """Berço para o TF-Luna (35 x 21 x 12 mm), aberto na frente das lentes."""
    externo = caixa(0, 0, 0, 39.6, 25.6, 15)
    bolso = caixa(2, 2, 2, 37.6, 23.6, 16)
    fios = caixa(15, 20, 2, 25, 26, 16)
    return externo - bolso - fios


def bandeja():
    """Placa para prender a eletrônica com velcro (fica no meio da fuselagem)."""
    placa = caixa(0, 0, 0, 140, 70, 2)
    furos = [caixa(x, y, -1, x + 22, y + 3.2, 3) for x in (10, 59, 108) for y in (8, 58.8)]
    furos += [cilindro("z", (x, 35, 1), 18, 4) for x in (35, 70, 105)]
    return placa - unir(furos)


# --------------------------------------------------------------- execução
def massa_asa(peca):
    """Asa impressa em LW-PLA: 1 perímetro de ~0,5 mm, 3% de preenchimento."""
    rho = 0.65  # g/cm³ do LW-PLA espumado
    casca = peca.surface_area() * 0.5 / 1000 * rho
    miolo = peca.volume() * 0.03 / 1000 * rho
    return casca + miolo


def massa_solida(peca, rho=1.27):
    """Casca da fuselagem: as paredes do desenho são impressas maciças."""
    return peca.volume() / 1000 * rho


def massa_preenchida(peca, rho=1.27, parede=1.2, preenchimento=0.20):
    """Peça maciça no desenho, impressa com 3 perímetros e 20% de preenchimento."""
    casca = peca.surface_area() * parede
    miolo = max(peca.volume() - casca, 0) * preenchimento
    return (casca + miolo) / 1000 * rho


def centro_x(peca, por_superficie):
    """Posição X do centro de massa (pela superfície ou pelo volume)."""
    malha = peca.to_mesh()
    v = np.asarray(malha.vert_properties)[:, :3].astype(float)
    t = v[np.asarray(malha.tri_verts)]
    if por_superficie:
        area = np.linalg.norm(np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]), axis=1)
        return float((area * t[:, :, 0].mean(axis=1)).sum() / area.sum())
    vol = np.einsum("ij,ij->i", t[:, 0], np.cross(t[:, 1], t[:, 2]))
    return float((vol * t.sum(axis=1)[:, 0] / 4).sum() / vol.sum())


def main():
    os.makedirs(PASTA_STL, exist_ok=True)
    lista = []  # (arquivo, quantidade, massa unitária, material, peça impressa)

    seg_a, seg_b, seg_c = segmento_asa("A"), segmento_asa("B"), segmento_asa("C")
    ponta = ponta_asa()
    ail = aileron()
    for nome, peca, qtd in [
        ("asa_segmento_A_raiz.stl", seg_a, 2 * (SEGMENTOS_RETOS - 2)),
        ("asa_segmento_B_servo.stl", seg_b, 2),
        ("asa_segmento_C.stl", seg_c, 2),
        ("asa_ponta_esquerda.stl", ponta, 1),
        ("asa_ponta_direita.stl", ponta.mirror((0, 0, 1)), 1),
        ("aileron.stl", ail, 4),
    ]:
        lista.append((nome, qtd, massa_asa(peca), "LW-PLA", peca))

    est, prof = estabilizador(), profundor()
    der, leme = deriva("deriva"), deriva("leme")
    for nome, peca, qtd in [
        ("cauda_estabilizador.stl", est, 2),
        ("cauda_profundor.stl", prof, 2),
        ("cauda_deriva.stl", der, 1),
        ("cauda_leme.stl", leme, 1),
    ]:
        lista.append((nome, qtd, massa_asa(peca), "LW-PLA", peca))

    sup, zt = suporte_cauda()
    nariz, meio, cauda = pecas_fuselagem()
    # posição de impressão das peças da fuselagem: em pé, com o corte na mesa
    em_pe = np.array([[0, 1, 0, 0], [0, 0, 1, 0], [1, 0, 0, 0]], float)
    for nome, peca, qtd in [
        ("fuselagem_1_nariz.stl", nariz, 1),
        ("fuselagem_2_meio.stl", meio, 1),
        ("fuselagem_3_cauda.stl", cauda, 1),
        ("suporte_cauda.stl", sup, 1),
    ]:
        if nome.startswith("fuselagem"):
            lista.append((nome, qtd, massa_solida(peca), "PETG", peca.transform(em_pe)))
        else:
            lista.append((nome, qtd, massa_preenchida(peca), "PETG", peca))

    for nome, peca in [("suporte_gopro.stl", suporte_gopro()),
                       ("berco_lidar.stl", berco_lidar()),
                       ("bandeja_eletronica.stl", bandeja())]:
        lista.append((nome, 1, massa_preenchida(peca), "PETG", peca))

    for nome, _, _, _, peca in lista:
        salvar_stl(no_chao(peca), os.path.join(PASTA_STL, nome))

    # ---------------- montagem (para visualizar)
    montagem = [nariz, meio, cauda]
    for lado in ("E", "D"):
        y = 0.0
        for i in range(SEGMENTOS_RETOS):
            tipo = "A" if i < SEGMENTOS_RETOS - 2 else ("B" if i == SEGMENTOS_RETOS - 2 else "C")
            montagem.append(asa_na_montagem(segmento_asa(tipo), y, lado))
            if tipo != "A":
                montagem.append(asa_na_montagem(ail.translate((0, 0, 0.5)), y, lado))
            y += SEGMENTO
        montagem.append(asa_na_montagem(ponta, y, lado))

    z_tubo = np.array(ESTACOES, float)[-1, 3]
    z_cauda = z_tubo - zt  # altura do estabilizador para o tubo ficar reto
    cx = CAUDA_X
    montagem.append(sup.translate((cx, 0, z_cauda)))
    for lado in (1, -1):
        e = est.rotate((90, 0, 0)).translate((cx, -20, z_cauda))
        p = prof.rotate((90, 0, 0)).translate((cx, -20.5, z_cauda))
        if lado == 1:
            e, p = e.mirror((0, 1, 0)), p.mirror((0, 1, 0))
        montagem += [e, p]
    topo = z_cauda + 0.045 * PROFUNDOR_CORDA + 2
    for parte in (der, leme):
        montagem.append(parte.translate((cx - 20, 0, topo)))
    x_tubo0 = np.array(ESTACOES, float)[-1, 0] - 60
    x_tubo1 = cx + 30 + 60
    montagem.append(cilindro("x", ((x_tubo0 + x_tubo1) / 2, 0, z_tubo), 16, x_tubo1 - x_tubo0))
    for i, m in enumerate(montagem):
        if m.is_empty() or m.status().name != "NoError":
            print("peça vazia na montagem:", i, m.status())
    salvar_stl(unir(montagem), os.path.join(AQUI, "montagem.stl"))

    # ---------------- equilíbrio (centro de gravidade)
    m = {nome: massa for nome, _, massa, _, _ in lista}
    asa_massa = (m["asa_segmento_A_raiz.stl"] * 2 * (SEGMENTOS_RETOS - 2)
                 + 2 * (m["asa_segmento_B_servo.stl"] + m["asa_segmento_C.stl"])
                 + 2 * m["asa_ponta_esquerda.stl"] + 4 * m["aileron.stl"])
    cauda_massa = (2 * (m["cauda_estabilizador.stl"] + m["cauda_profundor.stl"])
                   + m["cauda_deriva.stl"] + m["cauda_leme.stl"] + m["suporte_cauda.stl"])
    fus = [(nariz, m["fuselagem_1_nariz.stl"]), (meio, m["fuselagem_2_meio.stl"]),
           (cauda, m["fuselagem_3_cauda.stl"])]
    a = ASA_X
    x_tubo = (x_tubo0 + x_tubo1) / 2
    itens = [  # (o quê, gramas, posição x em mm)
        ("asa impressa", asa_massa, a + 0.42 * CORDA),
        ("cauda impressa", cauda_massa, CAUDA_X + 0.35 * PROFUNDOR_CORDA),
        ("suportes e bandeja", m["suporte_gopro.stl"] + m["berco_lidar.stl"]
         + m["bandeja_eletronica.stl"], a + 80),
        ("motor, hélice e cone", 80, -25),
        ("ESC 30 A", 25, 30),
        ("controladora, GPS, receptor", 45, a + 40),
        ("TTGO LoRa32", 15, a - 20),
        ("GoPro HERO7", 116, sum(JANELA_GOPRO) / 2),
        ("LiDAR e câmera térmica", 15, (JANELA_LIDAR[0] + FURO_TERMICA_X) / 2),
        ("MPPT, INA226, BEC", 40, a + 60),
        ("servos da cauda (na fuselagem)", 18, a + 100),
        ("servos dos ailerons", 18, a + 0.36 * CORDA),
        ("longarinas de carbono", 100, a + 0.35 * CORDA),
        ("18 células solares", 220, a + 0.43 * CORDA),
        ("tubo da cauda", 40, x_tubo),
        ("varetas", 15, x_tubo),
        ("fios, cola e fita", 80, a + 30),
    ] + [(f"fuselagem {i + 1}", massa, centro_x(p, False)) for i, (p, massa) in enumerate(fus)]
    bateria, x_cg = 420.0, a + 0.30 * CORDA
    if os.environ.get("DEPURAR"):
        for o, g, x in itens:
            print(f"{o:32s}{g:6.0f} g  x={x:7.0f}  momento={g * (x - x_cg):9.0f}")
    momento = sum(g * (x - x_cg) for _, g, x in itens)
    x_bateria = x_cg - momento / bateria
    total_voo = sum(g for _, g, _ in itens) + bateria
    BATERIA_MIN_X = 15 + 35  # centro da bateria encostada na parede de fogo
    if x_bateria >= BATERIA_MIN_X:
        equilibrio = (f"Centro da bateria a {x_bateria:.0f} mm da parede de fogo "
                      "(sem lastro)")
    else:
        lastro = (momento - bateria * (x_cg - BATERIA_MIN_X)) / (x_cg - 5)
        equilibrio = (f"Bateria encostada na frente e ~{lastro:.0f} g de lastro "
                      "no nariz")

    # ---------------- lista de peças
    total = 0.0
    linhas = ["Peça                          Qtd  Material  Peso un. (g)  Tamanho (mm)"]
    for nome, qtd, massa, mat, peca in lista:
        x0, y0, z0, x1, y1, z1 = peca.bounding_box()
        tam = f"{x1 - x0:.0f} x {y1 - y0:.0f} x {z1 - z0:.0f}"
        linhas.append(f"{nome:30s}{qtd:3d}  {mat:8s}  {massa:10.0f}    {tam}")
        total += qtd * massa
    linhas.append(f"\nPeso estimado das peças impressas: {total:.0f} g")
    envergadura = 2 * (SEGMENTOS_RETOS * SEGMENTO + PONTA_ENVERG)
    area = 2 * (SEGMENTOS_RETOS * SEGMENTO * CORDA + PONTA_ENVERG * (CORDA + PONTA_CORDA) / 2)
    linhas.append(f"Envergadura: {envergadura:.0f} mm   Área da asa: {area / 1e6:.3f} m²")
    linhas.append(f"Centro de gravidade: {ASA_X + 0.30 * CORDA:.0f} mm atrás da parede de fogo "
                  f"(30% da corda)")
    linhas.append(f"Peso estimado em voo (com eletrônica e bateria): {total_voo:.0f} g")
    linhas.append(f"Carga alar: {total_voo / (area / 1e4):.0f} g/dm²")
    linhas.append(f"Para equilibrar: {equilibrio}")
    texto = "\n".join(linhas)
    with open(os.path.join(AQUI, "pecas.txt"), "w", encoding="utf-8") as arq:
        arq.write(texto + "\n")
    print(texto)


if __name__ == "__main__":
    main()
