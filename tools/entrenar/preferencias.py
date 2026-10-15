"""Objetivos por preferencias para entrenar-rapido (--objetivo dpo|kto).

POR QUE. Con el corpus ya afinado, lo que queda son errores puntuales del
modelo: llamar con un dato inventado en vez de pedirlo («falta dato»), o elegir
la herramienta vecina. El SFT solo empuja hacia la respuesta buena; estos dos
ademas empujan EN CONTRA de la mala, medida contra el modelo de partida (la
referencia), sin alejarse de el mas de lo que pide beta.

  DPO (Rafailov 2023, arXiv:2305.18290): pares (buena, mala) para la misma frase.
      perdida = -log sigma(beta * [(lp_b - ref_b) - (lp_m - ref_m)])
  KTO (Ethayarajh 2024, arXiv:2402.01306): respuestas sueltas marcadas buena o
      mala (lo que junta el autoexamen), sin pares.
      buena: 1 - sigma(beta * (lp - ref - z0))
      mala:  1 - sigma(beta * (z0 - (lp - ref)))
      z0 = max(0, KL estimado), sin gradiente: la misma cuenta sobre respuestas
      CRUZADAS (la frase de un ejemplo con la respuesta de otro).

lp es la suma del log p de los tokens de la respuesta (como TRL), ref lo mismo
con el modelo de partida, calculado una vez antes de entrenar. Las mismas
formulas que DPOTrainer (loss_type «sigmoid») y KTOTrainer de TRL.

El prefijo compartido calza justo: la buena y la mala (y la cruzada) son
sufijos distintos sobre el MISMO prefijo de fichas, que se calcula una vez.
"""
import torch
import torch.nn.functional as F


def dpo(lp_b, lp_m, ref_b, ref_m, beta):
    """(perdida por par, margen implicito por par)."""
    m = beta * ((lp_b - ref_b) - (lp_m - ref_m))
    return -F.logsigmoid(m), m


def kto(lp, ref, buena, z0, beta, peso_buena=1.0, peso_mala=1.0):
    """(perdida por respuesta, recompensa implicita beta*(lp-ref)). buena: bool."""
    r = beta * (lp - ref)
    zb = beta * z0
    perd = torch.where(buena, peso_buena * (1 - torch.sigmoid(r - zb)),
                       peso_mala * (1 - torch.sigmoid(zb - r)))
    return perd, r


def aciertos(objetivo, valor, buena=None, z0=0.0, beta=1.0):
    """Cuantas van para el lado correcto: DPO margen > 0; KTO recompensa del lado de su etiqueta."""
    if objetivo == "dpo":
        return (valor > 0).sum()
    return torch.where(buena, valor > beta * z0, valor < beta * z0).sum()
