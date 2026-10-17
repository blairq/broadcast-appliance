"""Corre un script con la memoria de la placa 0 topeada (para no molestar a otra corrida):

    CUDA_VISIBLE_DEVICES=0 .venv-entrenar/bin/python tools/entrenar/pruebas/tope.py 0.04 \
        tools/entrenar/entrenar-rapido.py --objetivo dpo --base <juguete> --datos <pares> ...
"""
import runpy, sys, torch
torch.cuda.set_per_process_memory_fraction(float(sys.argv[1]), 0)
sys.argv = sys.argv[2:]
runpy.run_path(sys.argv[0], run_name="__main__")
