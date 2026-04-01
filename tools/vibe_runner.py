#!/usr/bin/env python3
"""Stub mínimo de runner para o ambiente de testes netctl/netmgrd."""
import argparse
import sys


def main():
    parser = argparse.ArgumentParser(description="vibe_runner stub for test_deployment")
    parser.add_argument("--image", required=True, help="Path para boot.img")
    parser.add_argument("--run", required=True, help="App a executar")
    args = parser.parse_args()

    if not args.image or not args.run:
        print("vibe_runner: parâmetros insuficientes", file=sys.stderr)
        return 1

    # Para o fluxo de validação local, apenas confirmamos entrada.
    print(f"vibe_runner: image={args.image} run={args.run}")
    print("vibe_runner: execução simulada com sucesso")
    return 0


if __name__ == "__main__":
    sys.exit(main())
